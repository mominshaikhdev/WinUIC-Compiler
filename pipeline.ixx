// pipeline.ixx — Compiler Pipeline Orchestrator
// Ties together the entire compilation flow:
// source text → lex → parse → semantic → IR → IDL Emitter → .idl Output
// Copyright (c) 2026 Momin Shaikh. All rights reserved.

module;
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <format>
#include <print>
#include <utility>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <variant>
export module winui.pipeline;

import winui.diagnostics;
import winui.ast;
import winui.lexer;
import winui.parser;
import winui.ir;
import winui.registry;
import winui.semantic;
import winui.winmd_emitter;

export namespace winui::pipeline {

// §1  Compile Options & Output File

inline auto normalize_win_path(std::string path) -> std::string {
    for (char& c : path) {
        if (c == '/') c = '\\';
    }
    return path;
}

struct OutputFile {
    std::string path;
    std::string content;
    std::string description;
};

struct CompileOptions {
    std::string input_path;         // .winui source file path
    std::string output_dir;         // where to write generated files
    std::string namespace_name;     // project namespace
    std::string project_name;       // project name
    std::string sdk_version;        // target SDK version
    bool        verbose = false;    // verbose diagnostics
};

// §2  Compile Result

struct CompileResult {
    bool                   success;
    std::vector<OutputFile> output_files;
    diag::DiagnosticBag    diagnostics;
    ir::IrModule           ir_module;     // for inspection

    // Write all output files to disk
    auto write_to_disk() const -> bool {
        for (auto const& f : output_files) {
            std::string clean_path = normalize_win_path(f.path);
            auto dir = clean_path.substr(0, clean_path.find_last_of('\\'));
            if (!dir.empty()) {
                std::filesystem::create_directories(dir);
            }
            std::ofstream out(clean_path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!out) return false;
            out << f.content;
        }
        return true;
    }
};

// §2.5  Import path resolution

// Resolves an import module path against the importing file's directory.
// Accepted forms:
//   "views\MainView"             relative to the importing file (auto .winui)
//   "..\..\shared\Controls"      parent traversal
//   "C:\src\proj\Views.winui"    absolute
//   "views/MainView"             forward slashes are accepted too
// Returns true and rewrites `path` to the absolute normalized location.
inline auto resolve_import_path(std::string& path, std::string const& importing_file) -> bool {
    for (char& c : path) if (c == '/') c = '\\';

    std::filesystem::path candidate(path);
    if (!candidate.is_absolute()) {
        std::filesystem::path base = importing_file.empty()
            ? std::filesystem::current_path()
            : std::filesystem::path(importing_file).parent_path();
        candidate = base / candidate;
    }
    candidate = candidate.lexically_normal();

    auto exists = [](std::filesystem::path const& p) {
        std::error_code ec;
        return std::filesystem::exists(p, ec);
    };

    std::filesystem::path resolved;
    if (exists(candidate)) {
        resolved = candidate;
    } else {
        std::string ext = candidate.extension().string();   // e.g. ".winui"
        bool already_winui = true;
        if (ext.size() != 6) {
            already_winui = false;
        } else {
            for (std::size_t i = 0; i < 6; ++i) {
                char a = ext[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                if (a != ".winui"[i]) { already_winui = false; break; }
            }
        }
        if (!already_winui) {
            auto with_ext = std::filesystem::path(candidate.string() + ".winui");
            if (exists(with_ext)) resolved = with_ext;
        }
    }
    if (resolved.empty()) return false;

    path = resolved.string();
    for (char& c : path) if (c == '/') c = '\\';
    return true;
}

// §3  Compiler

class Compiler {
public:
    explicit Compiler(reg::ControlRegistry registry)
        : m_registry(std::move(registry))
    {}

    // Compile a .winui source string
    [[nodiscard]] auto compile(std::string source,
                               CompileOptions const& opts) -> CompileResult {
        CompileResult result;
        result.success = false;

        std::string clean_input = normalize_win_path(opts.input_path);

        // Phase 1: Lexing
        auto [tokens, lex_diag] = lex::tokenize(
            std::move(source), clean_input);
        result.diagnostics.merge(lex_diag);

        if (result.diagnostics.has_errors()) {
            return result;
        }

        // Phase 2: Parsing
        auto [ast, parse_diag] = parse::parse_tokens(
            std::move(tokens), clean_input,
            [&](std::string_view name) -> bool {
                auto const* s = m_registry.find_control(name);
                if (s && s->accepts_children) return true;
                // PascalCase fallback: in the .winui format,
                // any PascalCase control CAN have a { children } block.
                return !name.empty() && name[0] >= 'A' && name[0] <= 'Z';
            });
        result.diagnostics.merge(parse_diag);

        if (result.diagnostics.has_errors()) {
            return result;
        }

        // Phase 2.5: Import path resolution
        // Imports accept absolute paths, relative paths (including "..\..\"
        // traversal), forward or back slashes, and extension-less module
        // names — ".winui" is appended automatically. Each import is
        // resolved against the importing file's directory and recorded as
        // its absolute, normalized location.
        for (auto& imp : ast.imports) {
            std::string orig = imp.module_path;
            if (resolve_import_path(imp.module_path, clean_input)) {
                if (opts.verbose) {
                    result.diagnostics.note(imp.location,
                        std::format("import '{}' resolved to '{}'", orig, imp.module_path));
                }
            } else {
                result.diagnostics.error(imp.location,
                    std::format("import '{}' could not be resolved — file not found "
                                "(relative, absolute, '..' traversal and automatic "
                                "'.winui' extension are supported)", orig));
            }
        }

        // Phase 3: Semantic Analysis (AST → IR)
        auto [ir_module, sem_diag] = semantic::analyze(ast, m_registry);
        result.diagnostics.merge(sem_diag);

        if (result.diagnostics.has_errors()) {
            return result;
        }

        result.ir_module = std::move(ir_module);

        // Phase 4: Direct Binary WinMD Metadata Emission
        winmd::WinMdEmitter emitter;
        auto winmd_files = emitter.emit_module(result.ir_module, opts.namespace_name,
                                               m_registry.serialize_catalog());

        for (auto const& f : winmd_files) {
            std::string content_str(f.binary_data.begin(), f.binary_data.end());
            result.output_files.push_back(OutputFile{
                .path = normalize_win_path(f.path),
                .content = std::move(content_str),
                .description = f.description
            });
        }

        result.success = true;

        return result;
    }

    // Compile from a file path
    [[nodiscard]] auto compile_file(CompileOptions const& opts) -> CompileResult {
        std::ifstream file(opts.input_path);
        if (!file) {
            CompileResult r;
            r.success = false;
            r.diagnostics.error({},
                std::format("cannot open file '{}'", opts.input_path));
            return r;
        }
        std::string source(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        return compile(std::move(source), opts);
    }

    // Access the registry (for inspection/modification)
    [[nodiscard]] auto registry() const -> reg::ControlRegistry const& {
        return m_registry;
    }

    [[nodiscard]] auto registry() -> reg::ControlRegistry& {
        return m_registry;
    }

private:
    reg::ControlRegistry     m_registry;
};

} // namespace winui::pipeline
