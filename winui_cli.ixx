// winui_cli.ixx — CLI Module for the .winui Compiler
// Provides:
// - Argument parsing with -files, -out_dir, -namespace, etc.
// - Parallel compilation across all available cores
// - Direct binary ECMA-335 .winmd metadata emission and RAM PRI resource packaging
// Copyright (c) 2026 Momin Shaikh. All rights reserved.

module;
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <winnls.h>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <format>
#include <print>
#include <fstream>
#include <filesystem>
#include <thread>
#include <functional>
#include <variant>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <span>
#include <algorithm>
#include <ranges>
#include <array>
#include <map>
#include <set>
export module winui.cli;

import winui.diagnostics;
import winui.pipeline;
import winui.registry;
import winui.winmd_emitter;
import winui.ir;

export namespace winui::cli {

// §1  Internal helpers

inline auto error(std::string_view msg) -> void {
    std::println(stderr, "error: {}", msg);
}

inline auto print_help(char const* prog) -> void {
    (void)prog;
    std::println("");
    std::println(".winui Compiler CLI — Language-Agnostic UI Metadata Generator");
    std::println("");
    std::println("USAGE:");
    std::println("  winuic [options] [files...]");
    std::println("  winuic -files file1.winui,file2.winui [options]");
    std::println("  winuic ./winui_dir/ -o ./out -n MyProject");
    std::println("");
    std::println("OPTIONS:");
    std::println("  -f, -files <csv>      Comma-separated .winui files or directories to compile");
    std::println("  -o, -out_dir <dir>    Output directory (default: ./out)");
    std::println("  -n, -namespace <ns>   Project namespace (default: MyGeneratedUI)");
    std::println("  -p, -project <name>   Project name (default: same as namespace)");
    std::println("  -c, -control <file>   Path to a control-registry JSON (ANY name, repeatable,");
    std::println("                         no recompile); every *.json next to winuic.exe is");
    std::println("                         auto-loaded as a control registry too.");
    std::println("  -v, -verbose          Verbose logging output");
    std::println("  -h, -help             Show this help text");
    std::println("");
    std::println("ASSETS & LICENSE:");
    std::println("  Any LICENSE file present in the source input directories is");
    std::println("  automatically copied to the output directory.");
    std::println("");
    std::println("PATHS:");
    std::println("  File paths use single backslashes as separators, e.g.");
    std::println("  ..\\..\\a\\b\\c\\page — forward slashes and double backslashes");
    std::println("  are rejected. Forward slashes are permitted only within a URI");
    std::println("  scheme such as https://..., ms-appx:///... or any custom");
    std::println("  scheme://... reference. Absolute paths accept drive letters in");
    std::println("  either case (C:\\... or c:\\...). All paths — absolute and");
    std::println("  relative — are case insensitive.");
    std::println("");
    std::println("IMPORTS:");
    std::println("  import {{ View }} from \"..\\..\\Samples\\Button\\ButtonPage\"");
    std::println("  Import paths may be relative to the importing file, absolute,");
    std::println("  or use '..' traversal; single backslashes only; the '.winui'");
    std::println("  extension is optional (appended automatically when missing).");
    std::println("");
    std::println("EXAMPLES:");
    std::println("  winuic App.winui");
    std::println("  winuic -f App.winui,Controls.winui -o ./out -v");
    std::println("  winuic -f MainView.winui -n MyCompany.MyModule");
    std::println("  winuic -f App.winui -control third_party_controls.json");
    std::println("");
}

inline auto normalize_win_path(std::string path) -> std::string {
    for (char& c : path) {
        if (c == '/') c = '\\';
    }
    // Resolve relative paths ("..\\out", "foo.winui") against the current
    // directory so the compiler works from any launch directory.
    if (path.length() >= 2 && path[1] != ':' && path[0] != '\\') {
        std::error_code ec;
        auto abs = std::filesystem::absolute(path, ec);
        if (!ec) {
            auto canon = std::filesystem::weakly_canonical(abs, ec);
            path = ec ? abs.string() : canon.string();
        }
    }
    // Automatically enable Windows Long Path prefix (\\?\) for paths >= 240 characters
    if (path.length() >= 240 && !path.starts_with("\\\\?\\") && !path.starts_with("\\\\.\\")) {
        if (path.length() >= 3 && path[1] == ':' && path[2] == '\\') {
            path = "\\\\?\\" + path;
        }
    }
    return path;
}

inline auto is_external_sample_sidecar(std::filesystem::path const& path) -> bool {
    auto is_component = [](std::filesystem::path const& part, std::string_view expected) {
        std::string value = part.string();
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value == expected;
    };
    for (auto part = path.begin(); part != path.end(); ++part) {
        if (!is_component(*part, "samples")) continue;
        auto next = part;
        ++next;
        if (next != path.end() && is_component(*next, "winui")) return true;
    }
    return false;
}

// CLI path validation: accepts both / and \ as path separators.
// Only double backslashes are rejected.
inline auto validate_cli_path(std::string const& path, std::string_view what) -> bool {
    if (path.find("\\\\") != std::string::npos) {
        error(std::format("{} '{}' must not contain double backslashes", what, path));
        return false;
    }
    return true;
}

// .winui file-internal path validation (import/export paths, string literals):
// single backslashes only. Forward slashes are reserved for URI schemes.
inline auto validate_file_path(std::string const& path, std::string_view what) -> bool {
    if (path.find('/') != std::string::npos) {
        error(std::format("{} '{}' must use single backslashes, not forward slashes", what, path));
        return false;
    }
    if (path.find("\\\\") != std::string::npos) {
        error(std::format("{} '{}' must not contain double backslashes", what, path));
        return false;
    }
    return true;
}

// CLI path cleanup: cmd.exe can leave a stray double quote on an argument
// when a quoted path ends with a backslash (e.g.  -f "M:\dir with space\"
// arrives as argv  M:\dir with space" ). Double quotes are illegal in
// Windows file names, so any leading/trailing '"' on a CLI path is always
// an artifact and is stripped.
inline auto clean_cli_path(std::string path) -> std::string {
    while (!path.empty() && path.front() == '"') path.erase(path.begin());
    while (!path.empty() && path.back() == '"') path.pop_back();
    return path;
}

inline auto split_csv(std::string_view s) -> std::vector<std::string> {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < s.size()) {
        auto end = s.find(',', start);
        if (end == std::string_view::npos) end = s.size();
        auto trimmed = s.substr(start, end - start);
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
            trimmed.remove_prefix(1);
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
            trimmed.remove_suffix(1);
        // Strip any stray double quotes left on the item (cmd.exe leaves a
        // dangling '"' when a quoted path ends with a backslash, and fully
        // quoted items arrive with their quotes intact). Quotes are illegal
        // in Windows file names, so this is always an artifact.
        while (!trimmed.empty() && trimmed.front() == '"') trimmed.remove_prefix(1);
        while (!trimmed.empty() && trimmed.back() == '"') trimmed.remove_suffix(1);
        if (!trimmed.empty())
            result.emplace_back(normalize_win_path(std::string(trimmed)));
        start = end + 1;
    }
    return result;
}

// §2  CLI Options

struct CliOptions {
    std::vector<std::string>      input_files;        // .winui source files
    std::vector<std::string>      control_files;      // runtime control-registry JSON files (-c/-control)
    std::string                   output_dir = ".\\out";
    std::string                   ns         = "MyGeneratedUI";
    std::string                   project    = "";
    bool                          verbose    = false;
};

// §3  Parse arguments

[[nodiscard]] inline auto parse_args(int argc, char** argv) -> std::optional<CliOptions> {
    CliOptions opts;


    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        if (arg == "-help" || arg == "--help" || arg == "-h" || arg == "/?") {
            print_help(argv[0]);
            return std::nullopt;
        }
        else if (arg == "-files" || arg == "-f") {
            if (++i >= argc) { error("-files requires a value"); return std::nullopt; }
            if (!validate_cli_path(argv[i], "-files")) return std::nullopt;
            auto val = split_csv(argv[i]);
            opts.input_files.insert(opts.input_files.end(), val.begin(), val.end());
        }
        else if (arg == "-out_dir" || arg == "-o") {
            if (++i >= argc) { error("-out_dir requires a value"); return std::nullopt; }
            if (!validate_cli_path(argv[i], "-out_dir")) return std::nullopt;
            opts.output_dir = normalize_win_path(clean_cli_path(std::string(argv[i])));
        }
        else if (arg == "-namespace" || arg == "-n") {
            if (++i >= argc) { error("-namespace requires a value"); return std::nullopt; }
            // Default naming rule: spaces are hyphen-joined once ("WinUI
            // Gallery" -> "WinUI-Gallery") - this value names the emitted
            // .winmd, the metadata namespace and the project, and cppwinc
            // applies the identical rule to exe/pri names downstream.
            // The switch itself is the override: any other spelling the user
            // types (-n WinUI_Gallery) is kept exactly as written.
            opts.ns = argv[i];
            for (auto& c : opts.ns)
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') c = '-';
        }
        else if (arg == "-project" || arg == "-p") {
            if (++i >= argc) { error("-project requires a value"); return std::nullopt; }
            opts.project = argv[i];
        }
        else if (arg == "-control" || arg == "-c") {
            if (++i >= argc) { error("-control requires a value"); return std::nullopt; }
            if (!validate_cli_path(argv[i], "-control")) return std::nullopt;
            opts.control_files.push_back(normalize_win_path(clean_cli_path(std::string(argv[i]))));
        }
        else if (arg == "-verbose" || arg == "-v") {
            opts.verbose = true;
        }
        else if (arg.starts_with("-")) {
            error(std::format("unknown option '{}'", arg));
            return std::nullopt;
        }
        else {
            if (!validate_cli_path(argv[i], "input file")) return std::nullopt;
            opts.input_files.push_back(normalize_win_path(clean_cli_path(std::string(arg))));
        }
    }

    // Expand directory inputs, glob patterns, and individual files (recursive)
    std::vector<std::string> expanded;
    for (auto const& raw : opts.input_files) {
        // Check if this is a glob pattern (*.winui, **/*.winui, etc.)
        bool has_glob = raw.find('*') != std::string::npos || raw.find('?') != std::string::npos;
        if (has_glob) {
            // Find the base directory (everything before the first wildcard)
            std::filesystem::path p(raw);
            std::string parent_str = p.parent_path().string();
            if (parent_str.empty()) parent_str = ".";
            std::filesystem::path parent(parent_str);
            std::error_code ec;
            if (std::filesystem::is_directory(parent, ec)) {
                // Extract the glob pattern (filename part)
                std::string pattern = p.filename().string();
                // Use recursive_directory_iterator and match the pattern
                for (auto const& entry : std::filesystem::recursive_directory_iterator(parent, ec)) {
                    if (!entry.is_regular_file() || is_external_sample_sidecar(entry.path())) continue;
                    std::string fname = entry.path().filename().string();
                    // Simple glob match: * matches any, ? matches one
                    auto glob_match = [](std::string_view pat, std::string_view str) -> bool {
                        size_t pi = 0, si = 0;
                        size_t star_pi = std::string_view::npos, star_si = 0;
                        while (si < str.size()) {
                            if (pi < pat.size() && (pat[pi] == '?' || pat[pi] == str[si])) { ++pi; ++si; continue; }
                            if (pi < pat.size() && pat[pi] == '*') { star_pi = pi++; star_si = si; continue; }
                            if (star_pi != std::string_view::npos) { pi = star_pi + 1; si = ++star_si; continue; }
                            return false;
                        }
                        while (pi < pat.size() && pat[pi] == '*') ++pi;
                        return pi == pat.size();
                    };
                    if (glob_match(pattern, fname))
                        expanded.push_back(normalize_win_path(entry.path().string()));
                }
            } else {
                // Parent doesn't exist — treat as literal path
                std::error_code ec2;
                if (std::filesystem::is_regular_file(p, ec2))
                    expanded.push_back(normalize_win_path(raw));
            }
        } else {
            std::filesystem::path p(raw);
            std::error_code ec;
            if (std::filesystem::is_directory(p, ec)) {
                // Recursively scan directory for .winui files
                for (auto const& entry : std::filesystem::recursive_directory_iterator(p, ec)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".winui"
                        && !is_external_sample_sidecar(entry.path())) {
                        expanded.push_back(normalize_win_path(entry.path().string()));
                    }
                }
            } else {
                expanded.push_back(raw);
            }
        }
    }
    opts.input_files = std::move(expanded);

    if (opts.input_files.empty()) {
        error("no input files specified. Use -files file1.winui,file2.winui or pass files/directories directly.");
        return std::nullopt;
    }

    if (opts.project.empty()) opts.project = opts.ns;

    return opts;
}

// §4  Compile a single file

struct FileResult {
    std::string                      file_path;
    bool                             success = false;
    diag::DiagnosticBag              diagnostics;
    std::vector<pipeline::OutputFile> output_files;
    ir::IrModule                     ir_module;
};

[[nodiscard]] inline auto compile_single(
    std::string const& file,
    CliOptions const& opts,
    reg::ControlRegistry const& registry) -> FileResult {

    FileResult result;
    result.file_path = file;

    std::ifstream ifs(file);
    if (!ifs) {
        result.diagnostics.error({}, std::format("cannot open file '{}'", file));
        return result;
    }
    std::string source((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());

    pipeline::Compiler compiler(registry);

    pipeline::CompileOptions c_opts;
    c_opts.input_path     = file;
    c_opts.namespace_name = opts.ns;
    c_opts.project_name   = opts.project;
    c_opts.output_dir     = opts.output_dir;

    auto cres = compiler.compile(source, c_opts);
    result.success = cres.success;
    result.diagnostics = std::move(cres.diagnostics);
    result.output_files = std::move(cres.output_files);
    result.ir_module = std::move(cres.ir_module);

    return result;
}

// §5  Parallel Multi-File Driver

struct BatchSummary {
    std::size_t             total = 0;
    std::size_t             succeeded = 0;
    std::size_t             failed = 0;
    std::vector<FileResult> results;
};

[[nodiscard]] inline auto compile_batch(
    CliOptions const& opts,
    reg::ControlRegistry const& registry) -> BatchSummary {

    BatchSummary summary;
    summary.total = opts.input_files.size();
    summary.results.resize(summary.total);

    std::size_t num_threads = std::min(static_cast<std::size_t>(std::thread::hardware_concurrency()), opts.input_files.size());

    if (opts.verbose) {
        std::println("[winui] Compiling {} file(s) across {} worker thread(s)...",
                     opts.input_files.size(), num_threads);
    }

    std::mutex result_mutex;
    std::atomic<std::size_t> next_index{0};
    std::vector<std::jthread> workers;
    workers.reserve(num_threads);

    for (std::size_t t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            while (true) {
                std::size_t idx = next_index.fetch_add(1);
                if (idx >= opts.input_files.size()) break;

                auto const& file = opts.input_files[idx];
                auto res = compile_single(file, opts, registry);

                {
                    std::lock_guard lock(result_mutex);
                    summary.results[idx] = std::move(res);
                }
            }
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    for (auto const& r : summary.results) {
        if (r.success) ++summary.succeeded;
        else           ++summary.failed;
    }

    return summary;
}

// §6  Write outputs + report

inline auto write_outputs(std::vector<FileResult> const& results,
                          std::string const& output_dir) -> std::size_t {
    std::size_t written = 0;
    for (auto const& fr : results) {
        for (auto const& of : fr.output_files) {
            std::filesystem::path full_path = std::filesystem::path(output_dir) / of.path;
            if (full_path.has_parent_path()) {
                std::filesystem::create_directories(full_path.parent_path());
            }
            std::ofstream out(full_path, std::ios::binary | std::ios::out | std::ios::trunc);
            if (out) {
                out.write(of.content.data(), of.content.size());
                ++written;
            }
        }
    }
    return written;
}

inline auto print_summary(BatchSummary const& summary) -> void {
    std::println("");
    std::println("  .winui Compiler - Build Summary");
    std::println("");
    std::println("  Total files:  {}", summary.total);
    std::println("  Succeeded:    {}", summary.succeeded);
    std::println("  Failed:       {}", summary.failed);
    std::println("");

    for (auto const& fr : summary.results) {
        if (!fr.success) {
            std::println("  ❌ {} — FAILED", fr.file_path);
            std::println("{}", fr.diagnostics.format_all());
        } else {
            std::size_t files = 0;
            for (auto const& of : fr.output_files) {
                if (!of.content.empty()) ++files;
            }
            std::println("  ✅ {} — {} binary .winmd file(s) generated", fr.file_path, files);
        }
    }
    std::println("");
}

// §7  Programmatic C++ MrmResourceIndexer API Generator

inline auto utf8_to_utf16(std::string_view s) -> std::wstring {
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

struct MrmResourceIndexerHandle { PVOID handle; };

typedef HRESULT (WINAPI *pfnMrmCreateResourceIndexer)(
    PCWSTR packageFamilyName, PCWSTR projectRoot, int platformVersion,
    PCWSTR defaultQualifiers, MrmResourceIndexerHandle* indexer);

typedef HRESULT (WINAPI *pfnMrmIndexString)(
    MrmResourceIndexerHandle indexer, PCWSTR resourceUri, PCWSTR resourceString, PCWSTR qualifiers);

typedef HRESULT (WINAPI *pfnMrmIndexFileAutoQualifiers)(
    MrmResourceIndexerHandle indexer, PCWSTR filePath);

typedef HRESULT (WINAPI *pfnMrmCreateResourceFile)(
    MrmResourceIndexerHandle indexer, int packagingMode, int packagingOptions, PCWSTR outputDirectory);

typedef HRESULT (WINAPI *pfnMrmDestroyIndexerAndMessages)(
    MrmResourceIndexerHandle indexer);

inline auto generate_resources_pri(std::string const& out_dir, std::string const& project_name,
                                   std::vector<std::pair<std::string, std::string>> const& view_resources,
                                   bool verbose = false) -> void {
    std::filesystem::path out_path = std::filesystem::absolute(out_dir);
    std::filesystem::create_directories(out_path);

    // 1. Load Windows SDK native MrmResourceIndexer C++ APIs in RAM
    HMODULE hMrm = ::LoadLibraryW(L"mrmsupport.dll");
    if (!hMrm) hMrm = ::LoadLibraryW(L"Microsoft.Windows.Mrm.dll");
    if (!hMrm) {
        char* pf86_ptr = nullptr; size_t pf86_len = 0;
        _dupenv_s(&pf86_ptr, &pf86_len, "ProgramFiles(x86)");
        if (pf86_len > 0 && pf86_ptr) {
            std::filesystem::path sdk_bin = std::filesystem::path(pf86_ptr) / "Windows Kits" / "10" / "bin";
            if (std::filesystem::exists(sdk_bin)) {
                for (auto const& entry : std::filesystem::directory_iterator(sdk_bin)) {
                    if (entry.is_directory()) {
                        auto dll_cand = entry.path() / "x64" / "mrmsupport.dll";
                        if (std::filesystem::exists(dll_cand)) {
                            hMrm = ::LoadLibraryW(dll_cand.c_str());
                            if (hMrm) { if (verbose) std::println("[winui-pri] loaded MRM engine '{}'", dll_cand.string()); break; }
                        }
                    }
                }
            }
        }
        free(pf86_ptr);
    }
    if (!hMrm && verbose) std::println("[winui-pri] warning: mrmsupport.dll not found; falling back to in-memory PRI encoder");

    if (hMrm) {
        auto fnCreate = (pfnMrmCreateResourceIndexer)::GetProcAddress(hMrm, "MrmCreateResourceIndexer");
        auto fnIndexFileAuto = (pfnMrmIndexFileAutoQualifiers)::GetProcAddress(hMrm, "MrmIndexFileAutoQualifiers");
        auto fnIndexString = (pfnMrmIndexString)::GetProcAddress(hMrm, "MrmIndexString");
        auto fnCreateFile = (pfnMrmCreateResourceFile)::GetProcAddress(hMrm, "MrmCreateResourceFile");
        auto fnDestroy = (pfnMrmDestroyIndexerAndMessages)::GetProcAddress(hMrm, "MrmDestroyIndexerAndMessages");

        if (fnCreate && fnCreateFile && fnDestroy) {
            MrmResourceIndexerHandle indexer{};
            // Remove any existing broken PRI before regenerating
            { std::error_code dc; std::filesystem::remove(out_path / "resources.pri", dc); }
            // The Windows MrmResourceIndexer engine requires COM to be
            // initialized on this thread (CO_E_NOTINITIALIZED otherwise).
            ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            // MrmPlatformVersion_Windows10_0_0_5 = 0x010A0005. Passing defaultQualifiers=nullptr lets Windows MRM decide system defaults.
            HRESULT hr = fnCreate(L"Application", out_path.c_str(), 0x010A0005, nullptr, &indexer);
            if (SUCCEEDED(hr) && indexer.handle) {
                if (fnIndexString) {
                    std::wstring appName;
                    for (char c : project_name) appName += static_cast<wchar_t>(c);
                    fnIndexString(indexer, L"ms-resource:///Files/App/Title", appName.c_str(), nullptr);

                    // Index every view's full UI layout as a named string resource so the single
                    // per-project resources.pri literally contains all UI information.
                    for (auto const& [view_name, layout_json] : view_resources) {
                        std::wstring uri = L"ms-resource:///Files/Views/" + utf8_to_utf16(view_name) + L"/Layout";
                        std::wstring utf16_json = utf8_to_utf16(layout_json);
                        HRESULT h = fnIndexString(indexer, uri.c_str(), utf16_json.c_str(), nullptr);
                        if (verbose && FAILED(h)) {
                            std::println("[winui-pri] MrmIndexString('{}') failed hr=0x{:08X}", view_name, (unsigned)h);
                        }
                    }
                }

                if (fnIndexFileAuto) {
                    // Index only project resource assets and metadata (.winmd, .png, .jpg, .ico, .json, .resw, .xbf, etc.)
                    // Exclude non-resource build artifacts (.exe, .dll, .obj, .pdb, .log, .lib, .ifc, etc.)
                    for (auto const& entry : std::filesystem::recursive_directory_iterator(out_path)) {
                        if (entry.is_regular_file()) {
                            auto ext = entry.path().extension().string();
                            for (char& c : ext) c = static_cast<char>(::tolower(c));
                            if (ext == ".exe" || ext == ".dll" || ext == ".obj" || ext == ".pdb" ||
                                ext == ".log" || ext == ".exp" || ext == ".lib" || ext == ".ifc" ||
                                ext == ".iobj" || ext == ".ipdb" || ext == ".pri" || ext == ".winmd" ||
                                ext == ".xaml" || ext == ".cs" || ext == ".h" || ext == ".cpp" ||
                                ext == ".ixx" || ext == ".txt" || ext == ".bat" || ext == ".cmd" ||
                                ext == ".ps1" || ext == ".py" || ext == ".md" || ext == ".sln" ||
                                ext == ".csproj" || ext == ".vcxproj" || ext == ".props" || ext == ".targets" ||
                                ext == ".gitignore" || ext == ".gitattributes" || ext == ".config") {
                                continue;
                            }
                            if (entry.path().filename() == "resources.pri" || entry.path().filename() == "priconfig.xml") continue;
                            auto rel = std::filesystem::relative(entry.path(), out_path);
                            fnIndexFileAuto(indexer, rel.c_str());
                        }
                    }
                }

                // Compile PRI file directly in memory -> disk (MrmPackagingModeStandaloneFile = 0)
                HRESULT hrPri = fnCreateFile(indexer, 0, 0, out_path.c_str());
                fnDestroy(indexer);
                if (SUCCEEDED(hrPri) && std::filesystem::exists(out_path / "resources.pri")) {
                    if (verbose) std::println("[winui] Programmatically generated resources.pri via Windows MRM Engine");
                    ::FreeLibrary(hMrm);
                    return;
                }
                if (verbose) std::println("[winui] MRM createPriFile hr=0x{:08X}, falling back to in-memory encoder", (unsigned)hrPri);
                ::FreeLibrary(hMrm);
                // Fall through to in-memory PRI writer
            }
        }
        ::FreeLibrary(hMrm);
    }

        // 2. Fallback: use makepri.exe from Windows SDK
    {
        auto pri_file = out_path / "resources.pri";
        // Find makepri.exe in Windows SDK
        std::filesystem::path makepri;
        char* pf86_ptr2 = nullptr; size_t pf86_len2 = 0;
        _dupenv_s(&pf86_ptr2, &pf86_len2, "ProgramFiles(x86)");
        if (pf86_len2 > 0 && pf86_ptr2) {
            std::filesystem::path sdk_bin = std::filesystem::path(pf86_ptr2) / "Windows Kits" / "10" / "bin";
            if (std::filesystem::exists(sdk_bin)) {
                for (auto const& entry : std::filesystem::directory_iterator(sdk_bin)) {
                    if (entry.is_directory()) {
                        auto cand = entry.path() / "x64" / "makepri.exe";
                        if (std::filesystem::exists(cand)) {
                            makepri = cand;
                            break;
                        }
                    }
                }
            }
        }
        free(pf86_ptr2);
        if (!makepri.empty()) {
            // Write a minimal priconfig.xml for makepri.exe
            auto config = out_path / "priconfig.xml";
            {
                std::ofstream cfg(config);
                std::string xml =
                    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                    "<resources targetOsVersion=\"10.0.0\" majorVersion=\"1\">\n"
                    "\t<packaging>\n"
                    "\t\t<autoResourcePackage qualifier=\"Language\"/>\n"
                    "\t\t<autoResourcePackage qualifier=\"Scale\"/>\n"
                    "\t\t<autoResourcePackage qualifier=\"DXFeatureLevel\"/>\n"
                    "\t</packaging>\n"
                    "\t<index root=\"\\\" startIndexAt=\"\\\">\n"
                    "\t\t<default>\n"
                    "\t\t\t<qualifier name=\"Language\" value=\"en-US\"/>\n"
                    "\t\t\t<qualifier name=\"Contrast\" value=\"standard\"/>\n"
                    "\t\t\t<qualifier name=\"Scale\" value=\"100\"/>\n"
                    "\t\t\t<qualifier name=\"HomeRegion\" value=\"001\"/>\n"
                    "\t\t\t<qualifier name=\"TargetSize\" value=\"256\"/>\n"
                    "\t\t\t<qualifier name=\"LayoutDirection\" value=\"LTR\"/>\n"
                    "\t\t\t<qualifier name=\"Theme\" value=\"dark\"/>\n"
                    "\t\t\t<qualifier name=\"AlternateForm\" value=\"\"/>\n"
                    "\t\t\t<qualifier name=\"DXFeatureLevel\" value=\"DX9\"/>\n"
                    "\t\t\t<qualifier name=\"Configuration\" value=\"\"/>\n"
                    "\t\t\t<qualifier name=\"DeviceFamily\" value=\"Universal\"/>\n"
                    "\t\t\t<qualifier name=\"Custom\" value=\"\"/>\n"
                    "\t\t</default>\n"
                    "\t\t<indexer-config type=\"folder\" foldernameAsQualifier=\"true\" filenameAsQualifier=\"true\" qualifierDelimiter=\".\"/>\n"
                    "\t\t<indexer-config type=\"resw\" convertDotsToSlashes=\"true\" initialPath=\"\"/>\n"
                    "\t\t<indexer-config type=\"resjson\" initialPath=\"\"/>\n"
                    "\t\t<indexer-config type=\"PRI\"/>\n"
                    "\t</index>\n"
                    "</resources>\n";
                cfg << xml;
            }
            // Remove any existing broken PRI before regenerating
            std::error_code delfec;
            std::filesystem::remove(pri_file, delfec);
            // Build wide command line for CreateProcessW (avoids cmd.exe quoting issues)
            std::wstring wcmd = L"\"" + makepri.wstring() + L"\" new /pr \"" + out_path.wstring()
                + L"\" /cf \"" + config.wstring() + L"\" /of \"" + out_path.wstring() + L"\" /o";
            if (verbose) {
                std::string narrow;
                narrow.reserve(wcmd.size());
                for (wchar_t wc : wcmd) narrow += static_cast<char>(wc & 0x7F);
                std::println("[winui] Running makepri: {}", narrow);
            }
            STARTUPINFOW si{.cb = sizeof(si)};
            PROCESS_INFORMATION pi{};
            std::vector<wchar_t> cmdline(wcmd.begin(), wcmd.end());
            cmdline.push_back(L'\0');
            auto ec = ::CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr,
                                       FALSE, 0, nullptr, nullptr, &si, &pi);
            if (ec && pi.hProcess) {
                ::WaitForSingleObject(pi.hProcess, 60000);
                DWORD exit_code = 0;
                ::GetExitCodeProcess(pi.hProcess, &exit_code);
                ::CloseHandle(pi.hProcess);
                ::CloseHandle(pi.hThread);
                ec = (exit_code == 0) ? 1 : 0;  // Invert: 0 means PRI was created
                if (exit_code != 0 && verbose)
                    std::println("[winui] makepri.exe exit code: {}", exit_code);
            } else {
                if (verbose) std::println("[winui] CreateProcessW failed for makepri.exe");
                ec = 0;
            }
            if (std::filesystem::exists(pri_file) && std::filesystem::file_size(pri_file) > 100) {
                auto sz = std::filesystem::file_size(pri_file);
                if (verbose) std::println("[winui] Generated resources.pri via makepri.exe ({} bytes)", sz);
                std::filesystem::remove(config);
                return;
            }
            if (verbose) std::println("[winui] makepri.exe did not produce valid PRI, falling back to stub");
            std::filesystem::remove(config);
        }
    }

    // 3. Last resort: binary stub PRI
    {
        auto pri_file = out_path / "resources.pri";
        std::ofstream pri_out(pri_file, std::ios::binary);
        if (pri_out) {
            #pragma pack(push, 1)
            struct PriHeader {
                char magic[8] = {'m', 'r', 'm', '_', 'p', 'r', 'i', '2'};
                uint16_t version = 1;
                uint16_t target_os = 10;
                uint32_t total_size = 64;
                uint32_t toc_offset = 32;
                uint32_t section_count = 1;
            } header;
            #pragma pack(pop)
            pri_out.write(reinterpret_cast<char const*>(&header), sizeof(header));
            char section_data[32] = "ResourceMapSection\0\0\0\0\0\0\0\0\0\0";
            pri_out.write(section_data, sizeof(section_data));
            if (verbose) std::println("[winui] Wrote stub resources.pri (binary header only)");
        }
    }
}

// §8  Main entry point

inline auto run(int argc, char** argv) -> int {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "-help" || arg == "--help" || arg == "-h" || arg == "/?") {
            print_help(argv[0]);
            return 0;
        }
    }

    auto opts = parse_args(argc, argv);
    if (!opts) return 1;

    auto registry = reg::ControlRegistry::create_default();

    // Auto-load control registries (ANY name, no recompile)
    // Every *.json sitting next to winuic.exe is tried as a runtime control
    // registry — add future or third-party WinUI 3 controls there and they
    // become legal in .winui source and appear in the emitted catalog, with
    // zero recompilation. Files that do not parse as a control registry
    // (e.g. unrelated JSON) are skipped silently. Per-project
    // -c / -control <file.json> overlays below are applied on top and win
    // for duplicate abstract names.
    {
        // Long-path aware: grow buffer dynamically instead of MAX_PATH
        std::wstring exe_path_buf;
        DWORD cap = 256;
        DWORD len = 0;
        for (;;) {
            exe_path_buf.resize(cap);
            len = ::GetModuleFileNameW(nullptr, exe_path_buf.data(), cap);
            if (len == 0) { exe_path_buf.clear(); break; }
            if (len < cap) { exe_path_buf.resize(len); break; }
            cap *= 2;
        }
        if (!exe_path_buf.empty()) {
            std::wstring dir = std::filesystem::path(exe_path_buf).parent_path().wstring();
            std::wstring pattern = dir + L"\\*.json";
            WIN32_FIND_DATAW fd;
            HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    std::wstring fname = fd.cFileName;
                    std::wstring full = dir + L"\\" + fname;
                    std::ifstream in(full, std::ios::binary);
                    if (!in) continue;
                    std::string json((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
                    if (reg::ControlRegistry::load_backend_from_json(registry, json)) {
                        if (opts->verbose) {
                            std::string narrow;
                            narrow.reserve(full.size());
                            for (wchar_t wc : full) narrow += static_cast<char>(wc & 0x7F);
                            std::println("[winui] Loaded runtime control registry '{}'", narrow);
                        }
                    }
                } while (::FindNextFileW(h, &fd));
                ::FindClose(h);
            }
        }
    }

    // Runtime control-registry extension (NO recompile required)
    // -c / -control <file.json> loads third-party / future WinUI 3
    // controls at runtime (any path, including one pointing at a
    // winui.json): platform mappings merge into the "winui3" backend and
    // new abstract schemas are synthesized, feeding the parser, semantic
    // analyzer, and the embedded WinUIControlCatalogAttribute.
    for (auto const& cf : opts->control_files) {
        std::ifstream in(cf, std::ios::binary);
        if (!in) {
            error(std::format("cannot open control-registry JSON file '{}'", cf));
            return 1;
        }
        std::string json((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        if (!reg::ControlRegistry::load_backend_from_json(registry, json)) {
            error(std::format("failed to parse control-registry JSON file '{}'", cf));
            return 1;
        }
        if (opts->verbose) {
            std::println("[winui] Loaded runtime control registry from '{}'", cf);
        }
    }

    if (opts->verbose) {
        std::println("[winui] Starting compilation of {} file(s)...", opts->input_files.size());
    }

    BatchSummary summary;
    try {
        summary = compile_batch(*opts, registry);
    } catch (std::exception const& e) {
        error(std::format("fatal error: {}", e.what()));
        return 1;
    } catch (...) {
        error("fatal error: unknown exception during compilation");
        return 1;
    }

    if (summary.succeeded > 0) {
        // Merge all IR modules into a single unified module for the project namespace
        ir::IrModule merged_module;
        merged_module.name = opts->ns;
        for (auto const& fr : summary.results) {
            if (fr.success) {
                // Merge module-level directives from each compiled file
                if (fr.ir_module.extend_content_into_titlebar)
                    merged_module.extend_content_into_titlebar = true;
                if (fr.ir_module.dark_titlebar)
                    merged_module.dark_titlebar = true;
                if (fr.ir_module.dark_context_menu)
                    merged_module.dark_context_menu = true;
                if (!fr.ir_module.app_icon_path.empty() && merged_module.app_icon_path.empty())
                    merged_module.app_icon_path = fr.ir_module.app_icon_path;
                if (!fr.ir_module.backdrop_type.empty() && merged_module.backdrop_type.empty())
                    merged_module.backdrop_type = fr.ir_module.backdrop_type;
                if (!fr.ir_module.titlebar_text.empty() && merged_module.titlebar_text.empty())
                    merged_module.titlebar_text = fr.ir_module.titlebar_text;
                for (auto const& view : fr.ir_module.views) {
                    merged_module.views.push_back(view);
                }
            }
        }
        winmd::WinMdEmitter emitter;
        auto unified_winmd_files = emitter.emit_module(merged_module, opts->ns,
                                                      registry.serialize_catalog());
        // resources.pri carries ONLY genuine runtime resources: the app title,
        // extracted resource dictionaries and referenced assets. View layout
        // trees are NOT duplicated into the PRI — they already live exactly
        // once in the .winmd WinUIAttribute blob, and projections consume them
        // from metadata at compile time (zero-overhead native visual trees).
        // Duplicating every view's layout JSON here made the PRI carry the
        // whole UI surface of the project (and bloat MRM indexing) for data no
        // runtime ever reads back.
        std::vector<std::pair<std::string, std::string>> view_resources;
        for (auto const& f : unified_winmd_files) {
            std::filesystem::path full_path = std::filesystem::path(opts->output_dir) / f.path;
            if (full_path.has_parent_path()) {
                std::filesystem::create_directories(full_path.parent_path());
            }
            std::ofstream out(full_path, std::ios::binary | std::ios::out | std::ios::trunc);
            if (out) {
                out.write(reinterpret_cast<char const*>(f.binary_data.data()), f.binary_data.size());
            }
        }

        // Extract resource data from App view and embed into resources.pri as binary
        // (Zero XAML — no intermediate .xaml files written to disk)
        {
            std::string resource_payload;
            // Recursively scan IrBodyNode tree for Raw attributes
            std::function<void(std::vector<ir::IrBodyNode> const&)> scan_nodes = [&](std::vector<ir::IrBodyNode> const& nodes) {
                for (auto const& bn : nodes) {
                    std::vector<std::pair<std::string, std::string>> const* raw = nullptr;
                    if (auto const* node = std::get_if<ir::IrNode>(&bn)) {
                        raw = &node->raw_attrs;
                        scan_nodes(node->children);
                    } else if (auto const* cond = std::get_if<ir::IrConditionalNode>(&bn)) {
                        scan_nodes(cond->then_branch);
                        scan_nodes(cond->else_branch);
                    } else if (auto const* fe = std::get_if<ir::IrForEachNode>(&bn)) {
                        scan_nodes(fe->body);
                    }
                    if (raw) {
                        for (auto const& [rk, rv] : *raw) {
                            if (rk == "Resource" || (rk == "Resources" && rv.find("Style") != std::string::npos)) {
                                if (!resource_payload.empty()) resource_payload += "\n";
                                resource_payload += rv;
                            }
                        }
                    }
                }
            };
            for (auto const& view : merged_module.views) {
                scan_nodes(view.root_nodes);
            }
            if (!resource_payload.empty()) {
                // Embed resource payload directly into resources.pri as a named string resource
                view_resources.emplace_back("AppResources", resource_payload);
                if (opts->verbose) {
                    std::println("[winui] Embedded {} bytes of resource data into resources.pri", resource_payload.size());
                }
            }
        }

    // Copy only resource files explicitly referenced by the .winui sources.
    std::size_t assets_copied = 0;
    {
        std::map<std::string, std::string> referenced_assets;
        auto is_resource_ext = [](std::string_view ext) -> bool {
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" ||
                   ext == ".svg" || ext == ".ico" || ext == ".bmp" || ext == ".webp" ||
                   ext == ".ttf" || ext == ".otf" || ext == ".woff" || ext == ".woff2" ||
                   ext == ".mp4" || ext == ".wmv" || ext == ".avi" || ext == ".mkv" ||
                   ext == ".mp3" || ext == ".wav" || ext == ".m4a" ||
                   ext == ".json" || ext == ".resw" || ext == ".xbf" ||
                   ext == ".bin" || ext == ".eot";
        };
        auto lower_ascii = [](std::string value) {
            for (auto& c : value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return value;
        };
        auto add_asset_reference = [&](std::string_view raw) {
            auto end = raw.find_first_of("?#");
            if (end != std::string_view::npos) raw = raw.substr(0, end);
            if (raw.empty()) return;

            std::string candidate(raw);
            std::string lowered = lower_ascii(candidate);
            auto assets_pos = lowered.find("assets");
            if (assets_pos != std::string::npos &&
                (assets_pos == 0 || candidate[assets_pos - 1] == '/' ||
                 candidate[assets_pos - 1] == '\\') &&
                assets_pos + 6 < candidate.size() &&
                (candidate[assets_pos + 6] == '/' || candidate[assets_pos + 6] == '\\')) {
                candidate.erase(0, assets_pos);
            } else {
                auto scheme = lowered.find("://");
                if (scheme != std::string::npos) {
                    if (!lowered.starts_with("ms-appx://")) return;
                    candidate.erase(0, scheme + 3);
                }
            }

            for (auto& c : candidate)
                if (c == '/') c = '\\';
            while (!candidate.empty() && candidate.front() == '\\')
                candidate.erase(candidate.begin());
            while (candidate.starts_with(".\\"))
                candidate.erase(0, 2);

            auto slash = candidate.find_last_of('\\');
            auto dot = candidate.find_last_of('.');
            if (slash == std::string::npos || dot == std::string::npos || dot <= slash)
                return;
            auto ext = lower_ascii(candidate.substr(dot));
            if (!is_resource_ext(ext)) return;
            auto key = lower_ascii(candidate);
            referenced_assets.try_emplace(std::move(key), std::move(candidate));
        };

        for (auto const& file : opts->input_files) {
            std::ifstream input(file, std::ios::binary);
            if (!input) {
                std::println(stderr, "[winui] warning: could not read source for asset scanning: '{}'", file);
                continue;
            }
            std::string source((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());

            for (std::size_t i = 0; i < source.size();) {
                if (source[i] != '"' && source[i] != '\'') {
                    ++i;
                    continue;
                }
                char quote = source[i++];
                auto end = source.find(quote, i);
                if (end == std::string::npos) break;
                add_asset_reference(std::string_view(source).substr(i, end - i));
                i = end + 1;
            }

            std::string lowered = lower_ascii(source);
            for (std::size_t pos = 0; (pos = lowered.find("assets", pos)) != std::string::npos;) {
                auto path_start = pos + 6;
                if (path_start >= source.size() ||
                    (source[path_start] != '/' && source[path_start] != '\\')) {
                    pos = path_start;
                    continue;
                }
                auto end = source.find_first_of("\"'<> \t\r\n)", path_start);
                if (end == std::string::npos) end = source.size();
                std::string asset_path = "Assets\\";
                asset_path.append(source, path_start + 1, end - path_start - 1);
                add_asset_reference(asset_path);
                pos = end;
            }
        }
        if (opts->verbose && !referenced_assets.empty()) {
            std::println("[winui] Found {} referenced asset path(s) in .winui sources",
                         referenced_assets.size());
        }

        std::vector<std::filesystem::path> source_roots;
        auto has_ui_sources = [&](std::filesystem::path const& root) {
            std::error_code ec;
            for (std::filesystem::directory_iterator entry(root, ec), end;
                 !ec && entry != end; entry.increment(ec)) {
                if (entry->is_regular_file(ec)) {
                    auto ext = lower_ascii(entry->path().extension().string());
                    if (ext == ".winui" || ext == ".xaml") return true;
                } else if (entry->is_directory(ec)) {
                    std::error_code child_ec;
                    for (std::filesystem::directory_iterator child(entry->path(), child_ec), child_end;
                         !child_ec && child != child_end; child.increment(child_ec)) {
                        if (!child->is_regular_file(child_ec)) continue;
                        auto ext = lower_ascii(child->path().extension().string());
                        if (ext == ".winui" || ext == ".xaml") return true;
                    }
                }
            }
            return false;
        };
        auto add_root = [&](std::filesystem::path root) {
            std::error_code ec;
            root = std::filesystem::weakly_canonical(root, ec);
            if (ec || !std::filesystem::is_directory(root, ec) || !has_ui_sources(root))
                return;
            if (std::find(source_roots.begin(), source_roots.end(), root) == source_roots.end())
                source_roots.push_back(std::move(root));
        };
        for (auto const& file : opts->input_files) {
            std::error_code ec;
            auto path = std::filesystem::absolute(file, ec);
            if (ec) continue;
            auto dir = std::filesystem::is_directory(path, ec) ? path : path.parent_path();
            add_root(dir);
            for (int depth = 0; depth < 10 && !dir.empty(); ++depth) {
                std::error_code asset_ec;
                if (std::filesystem::is_directory(dir / "Assets", asset_ec))
                    add_root(dir);

                auto parent = dir.parent_path();
                if (parent.empty() || parent == dir) break;
                std::error_code siblings_ec;
                for (std::filesystem::directory_iterator sibling(parent, siblings_ec), end;
                     !siblings_ec && sibling != end; sibling.increment(siblings_ec)) {
                    std::error_code sibling_ec;
                    if (!sibling->is_directory(sibling_ec)) continue;
                    if (std::filesystem::is_directory(sibling->path() / "Assets", sibling_ec))
                        add_root(sibling->path());

                    std::error_code children_ec;
                    for (std::filesystem::directory_iterator child(sibling->path(), children_ec), child_end;
                         !children_ec && child != child_end; child.increment(children_ec)) {
                        std::error_code child_ec;
                        if (child->is_directory(child_ec) &&
                            std::filesystem::is_directory(child->path() / "Assets", child_ec))
                            add_root(child->path());
                    }
                }

                dir = std::move(parent);
            }
        }

        std::size_t missing_assets = 0;
        std::size_t copy_failures = 0;
        for (auto const& [ref_key, ref] : referenced_assets) {
            (void)ref_key;
            bool copied = false;
            for (auto const& root : source_roots) {
                auto source = root / std::filesystem::path(ref);
                std::error_code ec;
                if (!std::filesystem::is_regular_file(source, ec))
                    continue;
                auto dest = std::filesystem::path(opts->output_dir) /
                            std::filesystem::path(ref);
                std::filesystem::create_directories(dest.parent_path(), ec);
                if (ec) {
                    ++copy_failures;
                    std::println(stderr, "[winui] warning: could not create asset directory for '{}': {}",
                                 ref, ec.message());
                    break;
                }
                std::filesystem::copy_file(source, dest,
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    ++copy_failures;
                    std::println(stderr, "[winui] warning: could not copy asset '{}': {}",
                                 ref, ec.message());
                    break;
                }
                ++assets_copied;
                copied = true;
                break;
            }
            if (!copied) {
                ++missing_assets;
                if (opts->verbose)
                    std::println(stderr, "[winui] warning: referenced asset '{}' was not found in source roots",
                                 ref);
            }
        }

        std::set<std::string, std::less<>> referenced_samples;
        for (auto const& file : opts->input_files) {
            std::ifstream input(file, std::ios::binary);
            if (!input) continue;
            std::string source((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
            std::size_t pos = 0;
            while ((pos = source.find("SampleDefinition", pos)) != std::string::npos) {
                auto comma = source.find(',', pos + 16);
                if (comma == std::string::npos) {
                    pos += 16;
                    continue;
                }
                auto quote = source.find('"', comma + 1);
                if (quote == std::string::npos) break;
                auto end = source.find('"', quote + 1);
                if (end == std::string::npos) break;
                std::string sample = source.substr(quote + 1, end - quote - 1);
                for (auto& ch : sample) if (ch == '/') ch = '\\';
                bool unsafe = sample.empty() || sample.front() == '\\'
                    || (sample.size() > 1 && sample[1] == ':');
                for (std::size_t part = 0; !unsafe && part < sample.size();) {
                    auto end_part = sample.find('\\', part);
                    if (end_part == std::string::npos) end_part = sample.size();
                    if (sample.substr(part, end_part - part) == "..") unsafe = true;
                    part = end_part + 1;
                }
                if (!unsafe) referenced_samples.insert(sample);
                pos = end + 1;
            }
        }
        std::size_t samples_copied = 0;
        for (auto const& sample : referenced_samples) {
            std::string relative = sample;
            for (auto& ch : relative) if (ch == '/') ch = '\\';
            bool copied = false;
            for (auto const& root : source_roots) {
                auto source = root / "Samples" / std::filesystem::path(relative);
                std::error_code ec;
                if (!std::filesystem::is_regular_file(source, ec)) continue;
                auto destination = std::filesystem::path(opts->output_dir) / "Samples" / std::filesystem::path(relative);
                std::filesystem::create_directories(destination.parent_path(), ec);
                if (!ec) std::filesystem::copy_file(source, destination,
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    ++copy_failures;
                    std::println(stderr, "[winui] warning: could not copy sample '{}': {}",
                                 source.string(), ec.message());
                } else {
                    ++samples_copied;
                    auto translated_source = root / "Samples" / "WinUI" / std::filesystem::path(relative);
                    translated_source.replace_extension(".winui");
                    std::error_code translated_ec;
                    if (std::filesystem::is_regular_file(translated_source, translated_ec)) {
                        auto translated_destination = std::filesystem::path(opts->output_dir)
                            / "Samples" / "WinUI" / std::filesystem::path(relative);
                        translated_destination.replace_extension(".winui");
                        std::filesystem::create_directories(translated_destination.parent_path(), translated_ec);
                        if (!translated_ec) std::filesystem::copy_file(translated_source, translated_destination,
                            std::filesystem::copy_options::overwrite_existing, translated_ec);
                        if (translated_ec)
                            std::println(stderr, "[winui] warning: could not copy translated sample '{}': {}",
                                         translated_source.string(), translated_ec.message());
                    }
                }
                copied = true;
                break;
            }
            if (!copied) {
                ++missing_assets;
                std::println(stderr, "[winui] warning: referenced sample '{}' was not found in source roots",
                             relative);
            }
        }
        if (samples_copied > 0)
            std::println("[winui] External samples copied: {}", samples_copied);

        if (assets_copied > 0 || missing_assets > 0 || copy_failures > 0) {
            std::println("[winui] Asset copy: {} copied, {} missing, {} copy failure(s)",
                         assets_copied, missing_assets, copy_failures);
        }
        for (auto const& root : source_roots) {
            auto license = root / "LICENSE";
            if (std::filesystem::exists(license)) {
                std::error_code ec;
                std::filesystem::copy_file(license,
                    std::filesystem::path(opts->output_dir) / "LICENSE",
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (ec)
                    std::println(stderr, "[winui] warning: could not copy LICENSE from '{}': {}",
                                 root.string(), ec.message());
                break;
            }
        }
    }


        generate_resources_pri(opts->output_dir, opts->project, view_resources, opts->verbose);
        if (opts->verbose) {
            std::println("[winui] Wrote unified binary .winmd metadata file and resources.pri directly to '{}'", opts->output_dir);
        }
    }


    print_summary(summary);

    return summary.failed > 0 ? 1 : 0;
}

} // namespace winui::cli
