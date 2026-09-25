// winmd_emitter.ixx — Direct Binary WinMD Metadata Emitter (Bypassing IDL)
// Generates 100% ECMA-335 compliant binary PE .winmd metadata directly.
// Bypasses IDL text generation and midl.exe entirely.
// Copyright (c) 2026 Momin Shaikh. All rights reserved.

module;
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <format>
#include <utility>
#include <cstdint>
#include <variant>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <algorithm>

export module winui.winmd_emitter;

import winui.ir;
import winui.ast;

export namespace winui::winmd {

struct WinMdFile {
    std::string          path;        // e.g. "TestApp.winmd"
    std::vector<uint8_t> binary_data; // ECMA-335 compliant binary PE WinMD payload
    std::string          description;
};

class WinMdEmitter {
public:
    [[nodiscard]] auto emit_module(ir::IrModule const& mod,
                                   std::string const& namespace_name,
                                   std::string const& catalog_json = {}) const
        -> std::vector<WinMdFile> {
        std::vector<WinMdFile> files;
        auto ns = namespace_name.empty() ? mod.name : namespace_name;

        // ECMA-335 PE Binary WinMD Metadata
        WinMdFile file;
        file.path = std::format("{}.winmd", ns);
        file.description = std::format("Direct ECMA-335 binary WinMD metadata for {}", ns);
        file.binary_data = build_binary_winmd(mod, ns, catalog_json);
        files.push_back(std::move(file));

        return files;
    }

    // Serialize an IR view's layout tree to its canonical JSON representation.
    // Shared between the .winmd WinUIAttribute and resources.pri indexer
    // so both artifacts describe literally the same UI for every view.
    [[nodiscard]] static inline auto serialize_view_layout(ir::IrView const& view)
        -> std::string {
        return serialize_view_to_json(view);
    }

private:

    struct BlobHeapBuilder {
        std::vector<uint8_t> data{0}; // Index 0 is null/empty

        auto add_blob(std::vector<uint8_t> const& blob) -> uint32_t {
            uint32_t offset = static_cast<uint32_t>(data.size());
            uint32_t len = static_cast<uint32_t>(blob.size());
            // #Blob heap entries are prefixed with an ECMA-335 compressed
            // integer: 1 byte (< 0x80), 2 bytes (< 0x4000), or 4 bytes.
            if (len <= 0x7F) {
                data.push_back(static_cast<uint8_t>(len));
            } else if (len <= 0x3FFF) {
                data.push_back(static_cast<uint8_t>((len >> 8) | 0x80));
                data.push_back(static_cast<uint8_t>(len & 0xFF));
            } else {
                data.push_back(static_cast<uint8_t>((len >> 24) | 0xC0));
                data.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
                data.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
                data.push_back(static_cast<uint8_t>(len & 0xFF));
            }
            data.insert(data.end(), blob.begin(), blob.end());
            return offset;
        }

        auto add_custom_attr_string(std::string_view str) -> uint32_t {
            std::vector<uint8_t> blob;
            blob.push_back(0x01); // Custom attribute prolog: 0x0001 (little endian)
            blob.push_back(0x00);

            // SerString length: same compressed-integer encoding as the blob
            // heap itself (supports payloads beyond the 16 KB 2-byte limit).
            uint32_t len = static_cast<uint32_t>(str.size());
            if (len <= 0x7F) {
                blob.push_back(static_cast<uint8_t>(len));
            } else if (len <= 0x3FFF) {
                blob.push_back(static_cast<uint8_t>((len >> 8) | 0x80));
                blob.push_back(static_cast<uint8_t>(len & 0xFF));
            } else {
                blob.push_back(static_cast<uint8_t>((len >> 24) | 0xC0));
                blob.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
                blob.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
                blob.push_back(static_cast<uint8_t>(len & 0xFF));
            }
            blob.insert(blob.end(), str.begin(), str.end());
            blob.push_back(0x00); // NumNamedArgs = 0
            blob.push_back(0x00);

            return add_blob(blob);
        }

        auto add_custom_attr_guid(uint32_t seed) -> uint32_t {
            std::vector<uint8_t> blob;
            blob.push_back(0x01); // Custom attribute prolog: 0x0001 (little endian)
            blob.push_back(0x00);
            
            uint32_t g1 = 0x8F4A1200 | (seed & 0xFFF);
            uint16_t g2 = static_cast<uint16_t>(0x4B3C + (seed & 0xFF));
            uint16_t g3 = static_cast<uint16_t>(0x4A1D + (seed & 0x7F));
            uint8_t g4[8] = {0x91, 0x0E, 0x62, 0x18, 0x48, static_cast<uint8_t>(seed & 0xFF), 0x30, 0x44};

            uint8_t const* p1 = reinterpret_cast<uint8_t const*>(&g1);
            blob.insert(blob.end(), p1, p1 + 4);
            uint8_t const* p2 = reinterpret_cast<uint8_t const*>(&g2);
            blob.insert(blob.end(), p2, p2 + 2);
            uint8_t const* p3 = reinterpret_cast<uint8_t const*>(&g3);
            blob.insert(blob.end(), p3, p3 + 2);
            blob.insert(blob.end(), g4, g4 + 8);

            blob.push_back(0x00); // NumNamedArgs = 0
            blob.push_back(0x00);

            return add_blob(blob);
        }
    };

    struct StringHeapBuilder {
        std::vector<char> data{'\0'}; // Index 0 is null

        auto add_string(std::string_view s) -> uint32_t {
            if (s.empty()) return 0;
            uint32_t offset = static_cast<uint32_t>(data.size());
            data.insert(data.end(), s.begin(), s.end());
            data.push_back('\0');
            return offset;
        }
    };

    template <typename T>
    static void write_val(std::vector<uint8_t>& buf, T val) {
        uint8_t const* p = reinterpret_cast<uint8_t const*>(&val);
        buf.insert(buf.end(), p, p + sizeof(T));
    }

    static inline std::string escape_json_string(std::string_view str) {
        std::string out;
        out.reserve(str.size() + 8);
        for (char c : str) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        return out;
    }

    static inline std::string serialize_ir_value(ir::IrValue const& val) {
        return std::visit([](auto const& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ir::IrLiteral>) {
                return std::visit([](auto const& lit) -> std::string {
                    using L = std::decay_t<decltype(lit)>;
                    if constexpr (std::is_same_v<L, std::string>) return std::format("\"{}\"", escape_json_string(lit));
                    if constexpr (std::is_same_v<L, std::int64_t>) return std::to_string(lit);
                    if constexpr (std::is_same_v<L, double>) return std::to_string(lit);
                    if constexpr (std::is_same_v<L, bool>) return lit ? "true" : "false";
                }, v.value);
            } else if constexpr (std::is_same_v<T, ir::IrStateRef>) {
                // One-way state read — no $ prefix, so projections can tell a
                // plain read apart from a two-way binding ref (see below).
                return std::format("\"{}{}\"", v.name,
                    v.member.empty() ? "" : std::format(".{}", v.member));
            } else if constexpr (std::is_same_v<T, ir::IrBindingRef>) {
                // Two-way binding ref — the $ prefix is the mode marker that
                // projections use to wire write-back through the change event.
                return std::format("\"${}{}\"", v.name,
                    v.member.empty() ? "" : std::format(".{}", v.member));
            } else if constexpr (std::is_same_v<T, ir::IrEnumValue>) {
                return std::format("\"{}\"", v.value_name);
            } else if constexpr (std::is_same_v<T, ir::IrRange>) {
                auto fmt = [](double d) {
                    if (d == static_cast<std::int64_t>(d))
                        return std::to_string(static_cast<std::int64_t>(d));
                    return std::to_string(d);
                };
                return std::format("\"{}{}{}\"",
                    fmt(v.lower), v.inclusive ? "..." : "..<", fmt(v.upper));
            } else if constexpr (std::is_same_v<T, ir::IrInterpolatedString>) {
                std::string res;
                for (auto const& seg : v.segments) {
                    if (seg.is_expr) res += "{" + seg.expr_name + "}";
                    else res += seg.text;
                }
                return std::format("\"{}\"", escape_json_string(res));
            } else if constexpr (std::is_same_v<T, ir::IrExpr>) {
                return std::format("\"{}\"", escape_json_string(v.expr_text));
            }
            return std::string("\"\"");
        }, val);
    }

    // Serialize an IrValue to code text for use inside serialized statements.
    // Unlike serialize_ir_value (which emits JSON-quoted values for the props
    // object), this emits unquoted C++-style text the projection can splice
    // straight into handler bodies: L"..." literals, bare identifiers and
    // verbatim IrExpr text (expressions already carry L"" via the semantic
    // formatter).
    static inline std::string value_to_text(ir::IrValue const& val) {
        return std::visit([](auto const& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, ir::IrLiteral>) {
                return std::visit([](auto const& lit) -> std::string {
                    using L = std::decay_t<decltype(lit)>;
                    if constexpr (std::is_same_v<L, std::string>) return std::format("L\"{}\"", escape_for_cpp_literal(lit));
                    if constexpr (std::is_same_v<L, std::int64_t>) return std::to_string(lit);
                    if constexpr (std::is_same_v<L, double>)
                        return (lit == static_cast<std::int64_t>(lit))
                            ? std::to_string(static_cast<std::int64_t>(lit))
                            : std::to_string(lit);
                    if constexpr (std::is_same_v<L, bool>) return lit ? "true" : "false";
                }, v.value);
            } else if constexpr (std::is_same_v<T, ir::IrStateRef>) {
                return v.member.empty() ? v.name : v.name + "." + v.member;
            } else if constexpr (std::is_same_v<T, ir::IrBindingRef>) {
                return v.member.empty() ? v.name : v.name + "." + v.member;
            } else if constexpr (std::is_same_v<T, ir::IrEnumValue>) {
                return v.value_name;
            } else if constexpr (std::is_same_v<T, ir::IrInterpolatedString>) {
                std::string out;
                bool first = true;
                for (auto const& seg : v.segments) {
                    if (seg.is_expr) {
                        if (!first) out += " + ";
                        out += seg.expr_name;
                        first = false;
                    } else {
                        if (!first) out += " + ";
                        out += "L\"" + seg.text + "\"";
                        first = false;
                    }
                }
                return out;
            } else if constexpr (std::is_same_v<T, ir::IrExpr>) {
                return v.expr_text;
            }
            return std::string("<value>");
        }, val);
    }

    // Escape a string for embedding inside a C++ L"" literal (no JSON layer).
    static inline std::string escape_for_cpp_literal(std::string_view s) {
        std::string out;
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    }

    // Serialize one resolved closure action to its canonical string form.
    // The projection parses these back into C++ (or any host language):
    // "clicks += 1"          → state assignment
    // "open()" / "open($pop)"→ method call (open/close protocol)
    // "onSubmit(\"x\")"       → abstract event firing
    // Serialize an IrInnerAction (assign / event / call) - the statement
    // subset allowed inside if/else bodies of a closure.
    static inline std::string serialize_inner_action(ir::IrInnerAction const& action) {
        return std::visit([](auto const& a) -> std::string {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, ir::IrAssign>) {
                std::string_view op;
                switch (a.op) {
                    case ast::AssignOp::Assign:    op = "=";  break;
                    case ast::AssignOp::AddAssign: op = "+="; break;
                    case ast::AssignOp::SubAssign: op = "-="; break;
                    case ast::AssignOp::MulAssign: op = "*="; break;
                    case ast::AssignOp::DivAssign: op = "/="; break;
                    default: return std::string();
                }
                std::string target = a.target_object.empty()
                    ? a.target_state
                    : a.target_object + "." + a.target_state;
                return std::format("{} {} {}", target, op, value_to_text(a.value));
            } else if constexpr (std::is_same_v<T, ir::IrCallMethod>) {
                std::string args;
                for (size_t i = 0; i < a.arguments.size(); ++i) {
                    if (i) args += ",";
                    args += value_to_text(a.arguments[i]);
                }
                std::string callee = a.object.empty()
                    ? a.method_name
                    : a.object + "." + a.method_name;
                return std::format("{}({})", callee, args);
            } else if constexpr (std::is_same_v<T, ir::IrFireEvent>) {
                std::string args;
                for (size_t i = 0; i < a.arguments.size(); ++i) {
                    if (i) args += ",";
                    args += value_to_text(a.arguments[i]);
                }
                return std::format("{}({})", a.event_name, args);
            }
            return std::string();
        }, action);
    }

    static inline std::string serialize_action_stmt(ir::IrAction const& action) {
        return std::visit([](auto const& a) -> std::string {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, ir::IrAssign>) {
                std::string_view op;
                switch (a.op) {
                    case ast::AssignOp::Assign:    op = "=";  break;
                    case ast::AssignOp::AddAssign: op = "+="; break;
                    case ast::AssignOp::SubAssign: op = "-="; break;
                    case ast::AssignOp::MulAssign: op = "*="; break;
                    case ast::AssignOp::DivAssign: op = "/="; break;
                    default: return std::string();
                }
                std::string target = a.target_object.empty()
                    ? a.target_state
                    : a.target_object + "." + a.target_state;
                return std::format("{} {} {}", target, op, value_to_text(a.value));
            } else if constexpr (std::is_same_v<T, ir::IrCallMethod>) {
                std::string args;
                for (size_t i = 0; i < a.arguments.size(); ++i) {
                    if (i) args += ",";
                    args += value_to_text(a.arguments[i]);
                }
                std::string callee = a.object.empty()
                    ? a.method_name
                    : a.object + "." + a.method_name;
                return std::format("{}({})", callee, args);
            } else if constexpr (std::is_same_v<T, ir::IrFireEvent>) {
                std::string args;
                for (size_t i = 0; i < a.arguments.size(); ++i) {
                    if (i) args += ",";
                    args += value_to_text(a.arguments[i]);
                }
                return std::format("{}({})", a.event_name, args);
            } else if constexpr (std::is_same_v<T, ir::IrVarDecl>) {
                std::string init = value_to_text(a.initializer);
                return std::format("var {} {} = {}", a.name,
                    std::string(ast::type_name(a.type)), init);
            } else if constexpr (std::is_same_v<T, ir::IrReturn>) {
                return std::format("return {}", value_to_text(a.value));
            } else if constexpr (std::is_same_v<T, ir::IrIfAction>) {
                std::string lhs = value_to_text(a.condition_lhs);
                std::string rhs = value_to_text(a.condition_rhs);
                std::string op;
                switch (a.condition_op) {
                    case ast::BinaryOp::Eq:    op = "=="; break;
                    case ast::BinaryOp::Neq:   op = "!="; break;
                    case ast::BinaryOp::Lt:    op = "<";  break;
                    case ast::BinaryOp::Lte:   op = "<="; break;
                    case ast::BinaryOp::Gt:    op = ">";  break;
                    case ast::BinaryOp::Gte:   op = ">="; break;
                    default: op = "!=";
                }
                // Bare boolean condition: if (x) lowered as x != false - emit as if (x)
                bool is_bare_bool = false;
                if (a.condition_op == ast::BinaryOp::Neq) {
                    if (auto const* lit = std::get_if<ir::IrLiteral>(&a.condition_rhs)) {
                        if (lit->type == ast::TypeKind::Bool)
                            if (auto const* b = std::get_if<bool>(&lit->value))
                                is_bare_bool = (*b == false);
                    }
                }
                std::string out;
                if (is_bare_bool)
                    out = std::format("if ({}) {{", lhs);
                else
                    out = std::format("if ({} {} {}) {{", lhs, op, rhs);
                for (auto const& inner : a.then_body) {
                    auto s = serialize_inner_action(inner);
                    if (!s.empty()) out += " " + s + ";";
                }
                out += " }";
                if (!a.else_body.empty()) {
                    out += " else {";
                    for (auto const& inner : a.else_body) {
                        auto s = serialize_inner_action(inner);
                        if (!s.empty()) out += " " + s + ";";
                    }
                    out += " }";
                }
                return out;
            }
            return std::string();
        }, action);
    }

    // Serialize a closure's action list as a JSON string array.
    static inline std::string serialize_action_array(ir::IrClosure const& closure) {
        std::string json = "[";
        bool first = true;
        for (auto const& action : closure.actions) {
            auto s = serialize_action_stmt(action);
            if (s.empty()) continue;
            if (!first) json += ",";
            first = false;
            json += std::format("\"{}\"", escape_json_string(s));
        }
        json += "]";
        return json;
    }

    static inline std::string serialize_ir_node(ir::IrNode const& node) {
        std::string json = "{";
        json += std::format("\"control\":\"{}\"", node.abstract_control);

        json += ",\"props\":{";
        bool first_prop = true;
        for (auto const& prop : node.properties) {
            if (!first_prop) json += ",";
            first_prop = false;
            json += std::format("\"{}\":{}", prop.name, serialize_ir_value(prop.value));
        }
        for (auto const& mod : node.modifiers) {
            if (!first_prop) json += ",";
            first_prop = false;
            // Map winuic modifier names to WinUI property names
            std::string prop_name = mod.name;
            if (prop_name == "visible") prop_name = "Visibility";
            // Event modifiers (onClick, onChanged, ...) serialize as props so
            // projections can subscribe events[].platform_event (README
            // "Required projection algorithm" step 4).  When a modifier
            // carries a closure its body goes into the "events" array below.
            if (!mod.arguments.empty()) {
                json += std::format("\"{}\":{}", prop_name, serialize_ir_value(mod.arguments[0]));
            } else {
                json += std::format("\"{}\":true", prop_name);
            }
        }
        json += "}";

        // Verbatim XAML attributes / fragments (fidelity pass-through).
        if (!node.raw_attrs.empty()) {
            json += ",\"raw\":[";
            bool first_raw = true;
            for (auto const& [k, v] : node.raw_attrs) {
                if (!first_raw) json += ",";
                first_raw = false;
                json += std::format("[\"{}\",\"{}\"]",
                                    escape_json_string(k), escape_json_string(v));
            }
            json += "]";
        }

        // Event modifiers with closure bodies → events[].action (the marker
        // prop above stays so old projections keep working).
        json += ",\"events\":[";
        bool first_event = true;
        for (auto const& mod : node.modifiers) {
            if (!mod.closure || mod.closure->actions.empty()) continue;
            if (!first_event) json += ",";
            first_event = false;
            json += std::format("{{\"name\":\"{}\",\"action\":{}}}", mod.name,
                                serialize_action_array(*mod.closure));
        }
        json += "]";

        // Trailing action closure: Button("OK") { ... }
        if (node.action_closure) {
            json += ",\"action\":" + serialize_action_array(*node.action_closure);
        }

        json += ",\"children\":[";
        bool first_child = true;
        for (auto const& child : node.children) {
            if (!first_child) json += ",";
            first_child = false;
            json += serialize_body_node(child);
        }
        json += "]}";
        return json;
    }

    static inline std::string serialize_body_node(ir::IrBodyNode const& bn) {
        return std::visit([](auto const& inner) -> std::string {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, ir::IrNode>) {
                return serialize_ir_node(inner);
            } else if constexpr (std::is_same_v<T, ir::IrConditionalNode>) {
                std::string json = "{\"control\":\"If\",\"props\":{\"condition\":" + serialize_ir_value(inner.condition) + "},\"children\":[";
                bool first = true;
                for (auto const& n : inner.then_branch) {
                    if (!first) json += ",";
                    first = false;
                    json += serialize_body_node(n);
                }
                json += "]";
                if (!inner.else_branch.empty()) {
                    json += ",\"else\":[";
                    first = true;
                    for (auto const& n : inner.else_branch) {
                        if (!first) json += ",";
                        first = false;
                        json += serialize_body_node(n);
                    }
                    json += "]";
                }
                json += "}";
                return json;
            } else if constexpr (std::is_same_v<T, ir::IrForEachNode>) {
                std::string json = "{\"control\":\"ForEach\",\"props\":{\"collection\":" + serialize_ir_value(inner.collection) + "},\"children\":[";
                bool first = true;
                for (auto const& n : inner.body) {
                    if (!first) json += ",";
                    first = false;
                    json += serialize_body_node(n);
                }
                json += "]}";
                return json;
            }
            return std::string("{}");
        }, bn);
    }

    static inline std::string serialize_view_to_json(ir::IrView const& view) {
        std::string json = "[";
        bool first = true;
        // Leading "state" node carries the view's @state variables so the
        // projection can emit real members + RefreshState() re-rendering.
        // The view's base class rides along as a bare framework name hint
        // ("base") - never a projection-language construct - so any projection
        // can map it to its own platform base type.
        if (!view.state_vars.empty() || !view.base_class.empty()) {
            json += "{\"control\":\"state\",\"props\":{\"vars\":[";
            bool first_var = true;
            for (auto const& sv : view.state_vars) {
                if (!first_var) json += ",";
                first_var = false;
                json += std::format("{{\"name\":\"{}\",\"type\":\"{}\"",
                                    sv.name,
                                    sv.type_text.empty() ? std::string(ast::type_name(sv.type)) : sv.type_text);
                if (sv.initial_value) {
                    json += ",\"value\":" + serialize_ir_value(ir::IrLiteral{ sv.initial_value->value, sv.initial_value->type });
                }
                json += "}";
            }
            json += "]";
            if (!view.base_class.empty())
                json += std::format(",\"base\":\"{}\"", view.base_class);
            json += "}}";
            first = false;
        }
        // Callback bodies: the code-behind logic for each handler, carried as
        // serialized action strings so projections can emit real handler code.
        bool any_cb_body = false;
        for (auto const& ev : view.abi_events)
            if (!ev.body.empty()) { any_cb_body = true; break; }
        if (any_cb_body) {
            if (!first) json += ",";
            first = false;
            json += "{\"control\":\"callbacks\",\"props\":{\"cbs\":[";
            bool first_cb = true;
            for (auto const& ev : view.abi_events) {
                if (ev.body.empty()) continue;
                if (!first_cb) json += ",";
                first_cb = false;
                json += std::format("{{\"name\":\"{}\",\"body\":[", ev.name);
                bool first_stmt = true;
                for (auto const& action : ev.body) {
                    auto s = serialize_action_stmt(action);
                    if (s.empty()) continue;
                    if (!first_stmt) json += ",";
                    first_stmt = false;
                    json += std::format("\"{}\"", escape_json_string(s));
                }
                json += "]}";
            }
            json += "]}}";
        }
        for (auto const& bn : view.root_nodes) {
            if (!first) json += ",";
            first = false;
            json += serialize_body_node(bn);
        }
        json += "]";
        return json;
    }

    [[nodiscard]] auto build_binary_winmd(ir::IrModule const& mod, std::string const& ns,
                                          std::string const& catalog_json) const -> std::vector<uint8_t> {
        StringHeapBuilder str_heap;
        BlobHeapBuilder blob_heap;

        uint32_t module_name_idx = str_heap.add_string(std::format("{}.winmd", ns));
        uint32_t ns_idx = str_heap.add_string(ns);
        uint32_t winui_attr_name_idx = str_heap.add_string("WinUIAttribute");

        uint32_t xaml_base_ns_idx = str_heap.add_string("Microsoft.UI.Xaml");
        uint32_t xaml_ns_idx = str_heap.add_string("Microsoft.UI.Xaml.Controls");
        uint32_t app_class_idx = str_heap.add_string("Application");
        uint32_t window_class_idx = str_heap.add_string("Window");
        uint32_t user_control_idx = str_heap.add_string("UserControl");
        uint32_t dummy_str_idx = str_heap.add_string("Dummy");
        uint32_t ctor_name_idx = str_heap.add_string(".ctor");
        uint32_t metadata_ns_idx = str_heap.add_string("Windows.Foundation.Metadata");
        uint32_t default_attr_idx = str_heap.add_string("DefaultAttribute");
        uint32_t guid_attr_idx = str_heap.add_string("GuidAttribute");
        uint32_t activatable_attr_idx = str_heap.add_string("ActivatableAttribute");
        uint32_t dark_titlebar_attr_idx = str_heap.add_string("WinUIDarkTitlebarAttribute");
        uint32_t app_icon_attr_idx = str_heap.add_string("WinUIAppIconAttribute");
        uint32_t dark_context_menu_attr_idx = str_heap.add_string("WinUIDarkContextMenuAttribute");
        uint32_t catalog_attr_name_idx = str_heap.add_string("WinUIControlCatalogAttribute");
        uint32_t extend_tb_attr_idx = str_heap.add_string("WinUIExtendContentIntoTitleBarAttribute");
        uint32_t backdrop_attr_idx = str_heap.add_string("WinUIBackdropAttribute");
        uint32_t titlebar_attr_idx = str_heap.add_string("WinUITitlebarAttribute");

        // .ctor(string) MethodDefSignature blob: HASTHIS (0x20), 1 param, VOID (0x01), STRING (0x0E)
        uint32_t ctor_sig_idx = blob_heap.add_blob({ 0x20, 0x01, 0x01, 0x0E });

        // .ctor() MethodDefSignature blob: HASTHIS (0x20), 0 params, VOID (0x01)
        uint32_t ctor_no_args_sig_idx = blob_heap.add_blob({ 0x20, 0x00, 0x01 });

        // .ctor(u4, u2, u2, u1, u1, u1, u1, u1, u1, u1, u1) MethodDefSignature blob
        uint32_t guid_ctor_sig_idx = blob_heap.add_blob({ 0x20, 0x0B, 0x01, 0x09, 0x07, 0x07, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05 });

        // .ctor(u4) MethodDefSignature blob: HASTHIS (0x20), 1 param, VOID (0x01), U4 (0x09)
        uint32_t activatable_ctor_sig_idx = blob_heap.add_blob({ 0x20, 0x01, 0x01, 0x09 });

        // DefaultAttribute empty prolog blob: 0x0001, 0 named args -> 0x01, 0x00, 0x00, 0x00
        uint32_t default_attr_blob_idx = blob_heap.add_blob({ 0x01, 0x00, 0x00, 0x00 });

        // ActivatableAttribute blob: prolog 0x0001, version 1 (u4), 0 named args -> 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
        uint32_t activatable_blob_idx = blob_heap.add_blob({ 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 });

        // Empty attribute prolog blob: 0x0001, 0 named args
        uint32_t empty_attr_blob_idx = blob_heap.add_blob({ 0x01, 0x00, 0x00, 0x00 });

        struct TypeInfo {
            uint32_t interface_name_idx;
            uint32_t class_name_idx;
            uint32_t ns_idx;
            uint32_t blob_idx;
            uint32_t guid_blob_idx;
            uint16_t extends_coded;
            std::string base_class;   // bare framework base name (projection-agnostic)
        };

        std::vector<TypeInfo> types;
        std::vector<std::string> base_names;   // distinct non-default base classes
        uint32_t seed_counter = 1;
        for (auto const& view : mod.views) {
            TypeInfo ti;
            ti.interface_name_idx = str_heap.add_string(std::format("I{}", view.name));
            ti.class_name_idx = str_heap.add_string(view.name);
            ti.ns_idx = ns_idx;
            ti.blob_idx = blob_heap.add_custom_attr_string(serialize_view_to_json(view));
            ti.guid_blob_idx = blob_heap.add_custom_attr_guid(seed_counter++);
            ti.base_class = view.base_class;
            if (!view.base_class.empty() && view.base_class != "Application"
                && view.base_class != "Window" && view.base_class != "UserControl") {
                if (std::find(base_names.begin(), base_names.end(), view.base_class) == base_names.end())
                    base_names.push_back(view.base_class);
                ti.extends_coded = 0; // patched below once TypeRef rows are assigned
            } else if (view.name == "App") {
                ti.extends_coded = (1 << 2) | 1; // TypeRef 1: Application
            } else if (view.name.ends_with("Window")) {
                ti.extends_coded = (2 << 2) | 1; // TypeRef 2: Window
            } else {
                ti.extends_coded = (3 << 2) | 1; // TypeRef 3: UserControl
            }
            types.push_back(ti);
        }

        // WinUIControlCatalogAttribute on <Module> TypeDef row 1 (tag 3) → (1<<5)|3 = 35
        // MemberRef 8 in CustomAttributeType tag 3 → (8<<3)|3 = 67
        // Carries the full registry (abstract controls + winui3 platform mappings)
        // as JSON so projections can discover every registered control.
        struct CustomAttrRow {
            uint16_t parent_coded;
            uint16_t type_coded;
            uint32_t blob_idx;
        };
        std::vector<CustomAttrRow> custom_attrs;
        // MemberRef 8 = WinUIControlCatalogAttribute..ctor → (8<<3)|3 = 67
        if (!catalog_json.empty()) {
            custom_attrs.push_back({ 35, static_cast<uint16_t>((8 << 3) | 3), blob_heap.add_custom_attr_string(catalog_json) });
        }
        // Module-level directives on <Module> TypeDef row 1 (parent_coded = 35)
        // ExtendContentIntoTitleBar: MemberRef 9 → (9<<3)|3 = 75
        if (mod.extend_content_into_titlebar) {
            custom_attrs.push_back({ 35, static_cast<uint16_t>((9 << 3) | 3), empty_attr_blob_idx });
        }
        // DarkTitlebar: MemberRef 5 → (5<<3)|3 = 43
        if (mod.dark_titlebar) {
            custom_attrs.push_back({ 35, static_cast<uint16_t>((5 << 3) | 3), empty_attr_blob_idx });
        }
        // DarkContextMenu: MemberRef 7 → (7<<3)|3 = 59
        if (mod.dark_context_menu) {
            custom_attrs.push_back({ 35, static_cast<uint16_t>((7 << 3) | 3), empty_attr_blob_idx });
        }
        // AppIcon: MemberRef 6 → (6<<3)|3 = 51
        if (!mod.app_icon_path.empty()) {
            custom_attrs.push_back({ 35, static_cast<uint16_t>((6 << 3) | 3), blob_heap.add_custom_attr_string(mod.app_icon_path) });
        }
        // Backdrop: MemberRef 10 → (10<<3)|3 = 83
        if (!mod.backdrop_type.empty()) {
            custom_attrs.push_back({ 35, static_cast<uint16_t>((10 << 3) | 3), blob_heap.add_custom_attr_string(mod.backdrop_type) });
        }
        // Titlebar: MemberRef 11 → (11<<3)|3 = 91
        if (!mod.titlebar_text.empty()) {
            custom_attrs.push_back({ 35, static_cast<uint16_t>((11 << 3) | 3), blob_heap.add_custom_attr_string(mod.titlebar_text) });
        }

        // ECMA-335 Metadata Stream #~ Tables
        // Blob-heap offsets are emitted as 4-byte indices whenever the #Blob heap
        // exceeds 0xFFFF (HeapSizes bit 0x04). The catalog/layout JSON blobs easily
        // exceed 64KB, so this must be computed before locking the table schema.
        // The AssemblyRef name must be in the heap before sizing it too.
        uint32_t asmref_name_idx = str_heap.add_string("Microsoft.WindowsAppSDK");
        uint32_t str_heap_len_full = static_cast<uint32_t>(str_heap.data.size());
        uint32_t blob_heap_len_full = static_cast<uint32_t>(blob_heap.data.size());
        uint8_t heap_sizes_byte = 0;
        if (str_heap_len_full >= 0x10000) heap_sizes_byte |= 0x01;
        if (blob_heap_len_full >= 0x10000) heap_sizes_byte |= 0x04;

        std::vector<uint8_t> table_stream;
        write_val<uint32_t>(table_stream, 0);          // Reserved
        write_val<uint8_t>(table_stream, 2);           // MajorVersion
        write_val<uint8_t>(table_stream, 0);           // MinorVersion
        write_val<uint8_t>(table_stream, heap_sizes_byte);  // HeapSizes
        write_val<uint8_t>(table_stream, 1);           // Reserved

        // Blob-table column width must match the heap-size flag above.
        auto const write_blob_index = [&](uint32_t const value) {
            if (blob_heap_len_full >= 0x10000) write_val<uint32_t>(table_stream, value);
            else write_val<uint16_t>(table_stream, static_cast<uint16_t>(value));
        };

        // String-heap column width must match the HeapSizes 0x01 flag (ECMA-335
        // II.24.2.6): 4-byte indexes once #Strings exceeds 0xFFFF bytes.
        auto const write_str_index = [&](uint32_t const value) {
            if (heap_sizes_byte & 0x01) write_val<uint32_t>(table_stream, value);
            else write_val<uint16_t>(table_stream, static_cast<uint16_t>(value));
        };

// Valid tables: Module (0x00), TypeRef (0x01), TypeDef (0x02), Field (0x04), MethodDef (0x06), Param (0x08), InterfaceImpl (0x09), MemberRef (0x0A), CustomAttribute (0x0C), Assembly (0x20), AssemblyRef (0x23)
uint64_t valid_mask = (1ULL << 0x00) | (1ULL << 0x01) | (1ULL << 0x02) | (1ULL << 0x04) | (1ULL << 0x06) | (1ULL << 0x08) | (1ULL << 0x09) | (1ULL << 0x0A) | (1ULL << 0x0C) | (1ULL << 0x20) | (1ULL << 0x23);
        write_val<uint64_t>(table_stream, valid_mask); // Valid
        uint64_t sorted_mask = (1ULL << 0x0C);          // Bit 0x0C: CustomAttribute table is sorted
        write_val<uint64_t>(table_stream, sorted_mask); // Sorted

        uint32_t num_typerefs = static_cast<uint32_t>(14 + base_names.size()); // 1: Application, 2: Window, 3: UserControl, 4: WinUIAttribute, 5: DefaultAttribute, 6: GuidAttribute, 7: ActivatableAttribute, 8: WinUIDarkTitlebarAttribute, 9: WinUIAppIconAttribute, 10: WinUIDarkContextMenuAttribute, 11: WinUIControlCatalogAttribute, 12: WinUIExtendContentIntoTitleBarAttribute, 13: WinUIBackdropAttribute, 14: WinUITitlebarAttribute, 15+: view base classes

        // Assign each base-class TypeRef its row index (15+) and patch the
        // TypeDef Extends coded index accordingly.
        for (std::size_t bi = 0; bi < base_names.size(); ++bi) {
            uint16_t ref_idx = static_cast<uint16_t>(15 + bi);
            for (auto& ti : types)
                if (ti.base_class == base_names[bi])
                    ti.extends_coded = (ref_idx << 2) | 1; // TypeDefOrRef coded → TypeRef
        }
        uint32_t num_typedefs = static_cast<uint32_t>(1 + 2 * types.size()); // <Module> + (IView + View) per view
        uint32_t num_interface_impls = static_cast<uint32_t>(types.size()); // View->IView per view
        uint32_t num_member_refs = 11; // 1: WinUIAttribute..ctor, 2: DefaultAttribute..ctor, 3: GuidAttribute..ctor, 4: ActivatableAttribute..ctor, 5: WinUIDarkTitlebarAttribute..ctor, 6: WinUIAppIconAttribute..ctor, 7: WinUIDarkContextMenuAttribute..ctor, 8: WinUIControlCatalogAttribute..ctor, 9: WinUIExtendContentIntoTitleBarAttribute..ctor, 10: WinUIBackdropAttribute..ctor

        for (size_t i = 0; i < types.size(); ++i) {
            uint16_t iview_typedef_idx = static_cast<uint16_t>(2 + 2 * i);
            uint16_t view_typedef_idx = static_cast<uint16_t>(3 + 2 * i);

            uint16_t view_parent_coded = static_cast<uint16_t>((view_typedef_idx << 5) | 3); // TypeDef tag = 3
            uint16_t iview_parent_coded = static_cast<uint16_t>((iview_typedef_idx << 5) | 3); // TypeDef tag = 3

            uint16_t layout_attr_type_coded = static_cast<uint16_t>((1 << 3) | 3); // MemberRef 1 in CustomAttributeType tag 3 = 11
            uint16_t default_attr_type_coded = static_cast<uint16_t>((2 << 3) | 3); // MemberRef 2 in CustomAttributeType tag 3 = 19
            uint16_t guid_attr_type_coded = static_cast<uint16_t>((3 << 3) | 3); // MemberRef 3 in CustomAttributeType tag 3 = 27
            uint16_t activatable_attr_type_coded = static_cast<uint16_t>((4 << 3) | 3); // MemberRef 4 in CustomAttributeType tag 3 = 35
            uint16_t dark_titlebar_attr_type_coded = static_cast<uint16_t>((5 << 3) | 3); // MemberRef 5 = 43
            uint16_t app_icon_attr_type_coded = static_cast<uint16_t>((6 << 3) | 3); // MemberRef 6 = 51
            uint16_t dark_context_menu_attr_type_coded = static_cast<uint16_t>((7 << 3) | 3); // MemberRef 7 = 59

            bool is_window = (types[i].extends_coded == static_cast<uint16_t>((2 << 2) | 1)); // TypeRef 2: Window

            // 1. WinUIAttribute on View class
            custom_attrs.push_back({ view_parent_coded, layout_attr_type_coded, types[i].blob_idx });

            // 2. ActivatableAttribute on View class
            custom_attrs.push_back({ view_parent_coded, activatable_attr_type_coded, activatable_blob_idx });

            // Window-level attributes on Window view classes (only if directive set)
            if (is_window) {
                if (mod.dark_titlebar)
                    custom_attrs.push_back({ view_parent_coded, dark_titlebar_attr_type_coded, empty_attr_blob_idx });
                if (!mod.app_icon_path.empty())
                    custom_attrs.push_back({ view_parent_coded, app_icon_attr_type_coded, blob_heap.add_custom_attr_string(mod.app_icon_path) });
                if (mod.dark_context_menu)
                    custom_attrs.push_back({ view_parent_coded, dark_context_menu_attr_type_coded, empty_attr_blob_idx });
            }

            // DefaultAttribute on InterfaceImpl row (View -> IView) which is row i + 1
            uint16_t iface_impl_row_idx = static_cast<uint16_t>(i + 1);
            uint16_t iface_impl_parent_coded = static_cast<uint16_t>((iface_impl_row_idx << 5) | 5); // InterfaceImpl tag = 5
            custom_attrs.push_back({ iface_impl_parent_coded, default_attr_type_coded, default_attr_blob_idx });

            // GuidAttribute on IView interface
            custom_attrs.push_back({ iview_parent_coded, guid_attr_type_coded, types[i].guid_blob_idx });
        }

        uint32_t num_custom_attrs = static_cast<uint32_t>(custom_attrs.size());
        uint32_t num_method_defs = static_cast<uint32_t>(1 + types.size()); // 1 dummy + 1 .ctor per view class

        write_val<uint32_t>(table_stream, 1);                   // Module rows = 1
        write_val<uint32_t>(table_stream, num_typerefs);         // TypeRef rows
        write_val<uint32_t>(table_stream, num_typedefs);         // TypeDef rows
        write_val<uint32_t>(table_stream, 1);                   // Field rows = 1
        write_val<uint32_t>(table_stream, num_method_defs);     // MethodDef rows
        write_val<uint32_t>(table_stream, 1);                   // Param rows = 1
        write_val<uint32_t>(table_stream, num_interface_impls);  // InterfaceImpl rows
        write_val<uint32_t>(table_stream, num_member_refs);     // MemberRef rows
        write_val<uint32_t>(table_stream, num_custom_attrs);    // CustomAttribute rows
        write_val<uint32_t>(table_stream, 1);                   // Assembly rows = 1
        write_val<uint32_t>(table_stream, 1);                   // AssemblyRef rows = 1

        // 1. Module row 1
        write_val<uint16_t>(table_stream, 0);            // Generation
        write_str_index(module_name_idx);                 // Name
        write_val<uint16_t>(table_stream, 1);             // Mvid - row 1 of #GUID
        write_val<uint16_t>(table_stream, 0);             // EncId (null)
        write_val<uint16_t>(table_stream, 0);             // EncBaseId (null)

        // 2. TypeRef row 1: Application
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(app_class_idx);
        write_str_index(xaml_base_ns_idx);

        // 2. TypeRef row 2: Window
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(window_class_idx);
        write_str_index(xaml_base_ns_idx);

        // 2. TypeRef row 3: UserControl
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(user_control_idx);
        write_str_index(xaml_ns_idx);

        // 2. TypeRef row 4: WinUIAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(winui_attr_name_idx);
        write_str_index(ns_idx);

        // 2. TypeRef row 5: DefaultAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(default_attr_idx);
        write_str_index(metadata_ns_idx);

        // 2. TypeRef row 6: GuidAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(guid_attr_idx);
        write_str_index(metadata_ns_idx);

        // 2. TypeRef row 7: ActivatableAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(activatable_attr_idx);
        write_str_index(metadata_ns_idx);

        // 2. TypeRef row 8: WinUIDarkTitlebarAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(dark_titlebar_attr_idx);
        write_str_index(ns_idx);

        // 2. TypeRef row 9: WinUIAppIconAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(app_icon_attr_idx);
        write_str_index(ns_idx);

        // TypeRef 10: WinUIDarkContextMenuAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(dark_context_menu_attr_idx);
        write_str_index(ns_idx);

        // TypeRef 11: WinUIControlCatalogAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(catalog_attr_name_idx);
        write_str_index(ns_idx);

        // TypeRef 12: WinUIExtendContentIntoTitleBarAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(extend_tb_attr_idx);
        write_str_index(ns_idx);

        // TypeRef 13: WinUIBackdropAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(backdrop_attr_idx);
        write_str_index(ns_idx);

        // TypeRef 14: WinUITitlebarAttribute
        write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
        write_str_index(titlebar_attr_idx);
        write_str_index(ns_idx);

        // Extra TypeRef rows for declared view base classes (15+). The base
        // hint is a bare framework name in its platform namespace, so the
        // winmd stays projection-agnostic - each language projection maps the
        // name to its own base type.
        for (auto const& bn : base_names) {
            std::string bns = "Microsoft.UI.Xaml.Controls";
            if (bn == "FrameworkElement" || bn == "UIElement" || bn == "DependencyObject"
                || bn == "ContentPresenter" || bn == "Window" || bn == "Application")
                bns = "Microsoft.UI.Xaml";
            else if (bn == "Storyboard" || bn == "DoubleAnimation" || bn == "EasingFunctionBase")
                bns = "Microsoft.UI.Xaml.Media.Animation";
            else if (bn == "ThemeShadow" || bn == "Brush" || bn == "SolidColorBrush")
                bns = "Microsoft.UI.Xaml.Media";
            write_val<uint16_t>(table_stream, 6); // ResolutionScope: AssemblyRef row 1
            write_str_index(str_heap.add_string(bn));
            write_str_index(str_heap.add_string(bns));
        }

        // 3. TypeDef rows
        // Row 1: <Module> pseudo-type (required by ECMA-335 §22.37)
        write_val<uint32_t>(table_stream, 0);
        write_str_index(str_heap.add_string("<Module>"));
        write_str_index(0);                              // Namespace (null)
        write_val<uint16_t>(table_stream, 0);            // Extends (null)
        write_val<uint16_t>(table_stream, 1);            // FieldList
        write_val<uint16_t>(table_stream, 1);            // MethodList

        // 2 rows per view: IView (interface) + View (runtime class)
        //   • Interfaces never have an Extends entry.
        //   • Runtime class Extends carries a projection-agnostic TypeRef hint
        //     (1 = Application, 2 = Window, 3 = UserControl).  The language
        //     projection (C++/WinRT, C#, Rust, Python, etc.) reads the layout
        //     tree from WinUIAttribute and supplies the correct platform
        //     base class itself.  The hint is a bare framework name, never a
        //     projection-language construct — the .winmd stays
        //     projection-agnostic.
        for (size_t i = 0; i < types.size(); ++i) {
            uint16_t ctor_method_idx = static_cast<uint16_t>(2 + i);

            // Interface IView: tdPublic | tdInterface | tdAbstract | tdImport | tdWindowsRuntime = 0x000050A1
            write_val<uint32_t>(table_stream, 0x000050A1);
            write_str_index(types[i].interface_name_idx);
            write_str_index(types[i].ns_idx);
            write_val<uint16_t>(table_stream, 0); // Extends = 0 (interfaces have no base)
            write_val<uint16_t>(table_stream, 1);
            write_val<uint16_t>(table_stream, ctor_method_idx);

            // Runtime Class View: tdPublic | tdWindowsRuntime = 0x00004001
            write_val<uint32_t>(table_stream, 0x00004001);
            write_str_index(types[i].class_name_idx);
            write_str_index(types[i].ns_idx);
            write_val<uint16_t>(table_stream, types[i].extends_coded); // TypeDefOrRef coded → Window/UserControl/Application TypeRef
            write_val<uint16_t>(table_stream, 1);
            write_val<uint16_t>(table_stream, ctor_method_idx);
        }

        // 4. Field row 1 (dummy — required ECMA-335 §22.15)
        write_val<uint16_t>(table_stream, 0);
        write_str_index(dummy_str_idx);
        write_blob_index(0); // Signature

        // 5. MethodDef row 1 (dummy)
        write_val<uint32_t>(table_stream, 0); // RVA
        write_val<uint16_t>(table_stream, 0); // ImplFlags
        write_val<uint16_t>(table_stream, 0); // Flags
        write_str_index(dummy_str_idx);
        write_blob_index(0); // Signature
        write_val<uint16_t>(table_stream, 1); // ParamList

        // MethodDef rows 2..N+1: .ctor() for each view class
        for (size_t i = 0; i < types.size(); ++i) {
            write_val<uint32_t>(table_stream, 0);      // RVA = 0
            write_val<uint16_t>(table_stream, 0x0003); // ImplFlags = miRuntime
            write_val<uint16_t>(table_stream, 0x1886); // Flags = mdPublic | mdHideBySig | mdSpecialName | mdRTSpecialName
            write_str_index(ctor_name_idx);
            write_blob_index(ctor_no_args_sig_idx);
            write_val<uint16_t>(table_stream, 1); // ParamList = 1
        }

        // 6. Param row 1 (dummy)
        write_val<uint16_t>(table_stream, 0);
        write_val<uint16_t>(table_stream, 1);
        write_str_index(dummy_str_idx);

        // 7. InterfaceImpl rows: 1 per view (View -> IView)
        for (size_t i = 0; i < types.size(); ++i) {
            uint16_t iview_typedef_idx   = static_cast<uint16_t>(2 + 2 * i);
            uint16_t view_typedef_idx    = static_cast<uint16_t>(3 + 2 * i);
            uint16_t iview_typedef_coded = static_cast<uint16_t>((iview_typedef_idx << 2) | 0); // TypeDef tag = 0
            write_val<uint16_t>(table_stream, view_typedef_idx);
            write_val<uint16_t>(table_stream, iview_typedef_coded);
        }

        // 8. MemberRef rows
        // MemberRef parent coded: (TypeRef_1indexed << 3) | tag1

        // MemberRef 1: WinUIAttribute..ctor(string)  — TypeRef 4 → (4<<3)|1 = 33
        write_val<uint16_t>(table_stream, 33);
        write_str_index(ctor_name_idx);
        write_blob_index(ctor_sig_idx);

        // MemberRef 2: DefaultAttribute..ctor()  — TypeRef 5 → (5<<3)|1 = 41
        write_val<uint16_t>(table_stream, 41);
        write_str_index(ctor_name_idx);
        write_blob_index(ctor_no_args_sig_idx);

        // MemberRef 3: GuidAttribute..ctor(...)  — TypeRef 6 → (6<<3)|1 = 49
        write_val<uint16_t>(table_stream, 49);
        write_str_index(ctor_name_idx);
        write_blob_index(guid_ctor_sig_idx);

        // MemberRef 4: ActivatableAttribute..ctor(u4)  — TypeRef 7 → (7<<3)|1 = 57
        write_val<uint16_t>(table_stream, 57);
        write_str_index(ctor_name_idx);
        write_blob_index(activatable_ctor_sig_idx);

        // MemberRef 5: WinUIDarkTitlebarAttribute..ctor()  — TypeRef 8 → (8<<3)|1 = 65
        write_val<uint16_t>(table_stream, 65);
        write_str_index(ctor_name_idx);
        write_blob_index(ctor_no_args_sig_idx);

        // MemberRef 6: WinUIAppIconAttribute..ctor()  — TypeRef 9 → (9<<3)|1 = 73
        write_val<uint16_t>(table_stream, 73);
        write_str_index(ctor_name_idx);
        write_blob_index(ctor_no_args_sig_idx);

        // MemberRef 7: WinUIDarkContextMenuAttribute..ctor()  — TypeRef 10 → (10<<3)|1 = 81
        write_val<uint16_t>(table_stream, 81);
        write_str_index(ctor_name_idx);
        write_blob_index(ctor_no_args_sig_idx);

        // MemberRef 8: WinUIControlCatalogAttribute..ctor(string)  — TypeRef 11 → (11<<3)|1 = 89
        write_val<uint16_t>(table_stream, 89);
        write_str_index(ctor_name_idx);
        write_blob_index(ctor_sig_idx);

        // MemberRef 9: WinUIExtendContentIntoTitleBarAttribute..ctor()  — TypeRef 12 → (12<<3)|1 = 97
        write_val<uint16_t>(table_stream, 97);
        write_str_index(ctor_name_idx);
        write_blob_index(ctor_no_args_sig_idx);

        // MemberRef 10: WinUIBackdropAttribute..ctor(string)  — TypeRef 13 → (13<<3)|1 = 105
        write_val<uint16_t>(table_stream, 105);
        write_str_index(ctor_name_idx);
        write_blob_index(ctor_sig_idx);

        // MemberRef 11: WinUITitlebarAttribute..ctor(string)  — TypeRef 14 → (14<<3)|1 = 113
        write_val<uint16_t>(table_stream, 113);
        write_str_index(ctor_name_idx);
        write_blob_index(ctor_sig_idx);

        // 9. CustomAttribute rows (sorted by parent_coded, ECMA-335 §22.10)
        std::stable_sort(custom_attrs.begin(), custom_attrs.end(), [](auto const& a, auto const& b) {
            return a.parent_coded < b.parent_coded;
        });
        for (auto const& ca : custom_attrs) {
            write_val<uint16_t>(table_stream, ca.parent_coded);
            write_val<uint16_t>(table_stream, ca.type_coded);
            write_blob_index(ca.blob_idx);
        }

        // Table 0x20: Assembly - one row describing this metadata-only
        // Windows Runtime assembly (ECMA-335 II.22.2).
        write_val<uint32_t>(table_stream, 0x8004);      // HashAlgId (SHA1)
        write_val<uint16_t>(table_stream, 1);           // MajorVersion
        write_val<uint16_t>(table_stream, 0);           // MinorVersion
        write_val<uint16_t>(table_stream, 0);           // BuildNumber
        write_val<uint16_t>(table_stream, 0);           // RevisionNumber
        write_val<uint32_t>(table_stream, 1);           // Flags (WindowsRuntime)
        write_blob_index(0);                            // PublicKeyOrToken (null)
        write_str_index(ns_idx);                        // Name
        write_str_index(0);                             // Culture (null)

        // Table 0x23: AssemblyRef — one row naming the platform assembly that
        // owns every TypeRef this winmd references (ECMA-335 II.22.26). The
        // ResolutionScope of each TypeRef codes to this row (tag 2 = AssemblyRef).
        write_val<uint16_t>(table_stream, 1);           // MajorVersion
        write_val<uint16_t>(table_stream, 0);           // MinorVersion
        write_val<uint16_t>(table_stream, 0);           // BuildNumber
        write_val<uint16_t>(table_stream, 0);           // RevisionNumber
        write_val<uint32_t>(table_stream, 0);           // Flags
        write_blob_index(0);                            // PublicKeyOrToken (null)
        write_str_index(asmref_name_idx);               // Name
        write_str_index(0);                             // Culture (null)
        write_blob_index(0);                            // HashValue (null)

        while (table_stream.size() % 4 != 0) table_stream.push_back(0);

        uint32_t table_stream_len = static_cast<uint32_t>(table_stream.size());
        uint32_t str_heap_len     = static_cast<uint32_t>(str_heap.data.size());
        while (str_heap_len % 4 != 0) { str_heap.data.push_back('\0'); ++str_heap_len; }

        uint32_t blob_heap_len = static_cast<uint32_t>(blob_heap.data.size());
        while (blob_heap_len % 4 != 0) { blob_heap.data.push_back(0); ++blob_heap_len; }

        // #US heap: ECMA-335 II.24.2.4 - a single empty (0x00) entry.
        std::vector<uint8_t> us_heap = { 0x00 };
        uint32_t us_heap_len = 1;
        while (us_heap_len % 4 != 0) { us_heap.push_back(0); ++us_heap_len; }

        // #GUID heap: one entry - the Module.Mvid (index 1). Deterministic
        // bytes derived from the namespace so rebuilds are reproducible.
        std::vector<uint8_t> guid_heap(16, 0);
        {
            uint64_t h = 1469598103934665603ULL;
            for (char c : ns) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ULL; }
            for (int k = 0; k < 16; ++k)
            {
                guid_heap[k] = static_cast<uint8_t>((h >> ((k % 8) * 8)) ^ (k * 37));
            }
            guid_heap[7] = (guid_heap[7] & 0x0F) | 0x40; // version 4
            guid_heap[8] = (guid_heap[8] & 0x3F) | 0x80; // variant 10
        }

        // Metadata root (BSJB magic)
        std::vector<uint8_t> meta_root;
        write_val<uint32_t>(meta_root, 0x424A5342); // BSJB
        write_val<uint16_t>(meta_root, 1);           // MajorVersion
        write_val<uint16_t>(meta_root, 1);           // MinorVersion
        write_val<uint32_t>(meta_root, 0);           // Reserved
        write_val<uint32_t>(meta_root, 12);          // VersionLength

        char v_str[12] = "v4.0.30319\0";
        meta_root.insert(meta_root.end(), v_str, v_str + 12);
        write_val<uint16_t>(meta_root, 0); // Flags
        write_val<uint16_t>(meta_root, 5); // Streams count = 5 (#~ #Strings #US #GUID #Blob)

        uint32_t stream_header_size = 5 * 8 + 4 + 12 + 4 + 8 + 8; // headers + padded name bytes
        uint32_t current_offset = static_cast<uint32_t>(meta_root.size()) + stream_header_size;

        // Stream Header 1: #~
        write_val<uint32_t>(meta_root, current_offset);
        write_val<uint32_t>(meta_root, table_stream_len);
        meta_root.insert(meta_root.end(), {'#', '~', '\0', '\0'});
        current_offset += table_stream_len;

        // Stream Header 2: #Strings
        write_val<uint32_t>(meta_root, current_offset);
        write_val<uint32_t>(meta_root, str_heap_len);
        meta_root.insert(meta_root.end(), {'#', 'S', 't', 'r', 'i', 'n', 'g', 's', '\0', '\0', '\0', '\0'});
        current_offset += str_heap_len;

        // Stream Header 3: #US (empty user-string heap)
        write_val<uint32_t>(meta_root, current_offset);
        write_val<uint32_t>(meta_root, us_heap_len);
        meta_root.insert(meta_root.end(), {'#', 'U', 'S', '\0'});
        current_offset += us_heap_len;

        // Stream Header 4: #GUID
        write_val<uint32_t>(meta_root, current_offset);
        write_val<uint32_t>(meta_root, static_cast<uint32_t>(guid_heap.size()));
        meta_root.insert(meta_root.end(), {'#', 'G', 'U', 'I', 'D', '\0', '\0', '\0'});
        current_offset += static_cast<uint32_t>(guid_heap.size());

        // Stream Header 5: #Blob
        write_val<uint32_t>(meta_root, current_offset);
        write_val<uint32_t>(meta_root, blob_heap_len);
        meta_root.insert(meta_root.end(), {'#', 'B', 'l', 'o', 'b', '\0', '\0', '\0'});

        meta_root.insert(meta_root.end(), table_stream.begin(), table_stream.end());
        meta_root.insert(meta_root.end(), str_heap.data.begin(), str_heap.data.end());
        meta_root.insert(meta_root.end(), us_heap.begin(), us_heap.end());
        meta_root.insert(meta_root.end(), guid_heap.begin(), guid_heap.end());
        meta_root.insert(meta_root.end(), blob_heap.data.begin(), blob_heap.data.end());

        // PE wrapper
        //
        // Layout mirrors a minimal managed .winmd:
        //   DOS stub (0x00) + PE header (0x80) + OptionalHeader + 1 section (.text)
        //   Section starts at file offset 0x200 (PointerToRawData), VA 0x1000.
        //   CLI header (IMAGE_COR20_HEADER, 72 bytes) lives at the start of .text.
        //   Metadata root (BSJB) follows the CLI header.
        //
        const uint32_t file_align     = 0x200;
        const uint32_t section_align  = 0x1000;
        const uint32_t text_raw       = 0x200;                       // file offset of .text
        const uint32_t text_rva       = 0x1000;                      // VA of .text
        const uint32_t cli_size       = 72;                          // IMAGE_COR20_HEADER
        const uint32_t cli_rva        = text_rva + 0;                // CLI sits at .text start
        const uint32_t meta_rva       = text_rva + cli_size;         // metadata root follows CLI
        const uint32_t meta_raw       = text_raw + cli_size;         // file offset of metadata

        uint32_t text_size = cli_size + static_cast<uint32_t>(meta_root.size());
        uint32_t text_vsize = (text_size + section_align - 1) & ~(section_align - 1); // virtual size (sec-aligned)
        uint32_t text_rsize = (text_size + file_align - 1) & ~(file_align - 1);       // raw size (file-aligned)
        uint32_t size_of_image = (text_rva + text_vsize + section_align - 1) & ~(section_align - 1);
        uint32_t size_of_headers = file_align;
        uint32_t file_len = text_raw + text_rsize;

        std::vector<uint8_t> pe_file(file_len, 0x00);

        // DOS Header
        pe_file[0] = 'M'; pe_file[1] = 'Z';
        *reinterpret_cast<uint32_t*>(&pe_file[0x3C]) = 0x80;

        // PE Header at 0x80
        pe_file[0x80] = 'P'; pe_file[0x81] = 'E'; pe_file[0x82] = 0; pe_file[0x83] = 0;

        // FileHeader at 0x84
        *reinterpret_cast<uint16_t*>(&pe_file[0x84]) = 0x014C;   // Machine i386
        *reinterpret_cast<uint16_t*>(&pe_file[0x86]) = 1;        // NumberOfSections
        *reinterpret_cast<uint16_t*>(&pe_file[0x94]) = 0xE0;     // SizeOfOptionalHeader
        *reinterpret_cast<uint16_t*>(&pe_file[0x96]) = 0x2102;   // Characteristics: EXEC@32BIT_LARGE

        // OptionalHeader (PE32) at 0x98
        *reinterpret_cast<uint16_t*>(&pe_file[0x98]) = 0x010B;   // Magic PE32
        *reinterpret_cast<uint16_t*>(&pe_file[0x9A]) = 0;        // MajorLinkerVersion
        *reinterpret_cast<uint16_t*>(&pe_file[0x9C]) = 0;        // SizeOfCode
        *reinterpret_cast<uint32_t*>(&pe_file[0xA0]) = 0;        // SizeOfInitializedData
        *reinterpret_cast<uint32_t*>(&pe_file[0xA4]) = 0;        // SizeOfUninitializedData
        *reinterpret_cast<uint32_t*>(&pe_file[0xA8]) = 0;        // AddressOfEntryPoint
        *reinterpret_cast<uint32_t*>(&pe_file[0xAC]) = 0;        // BaseOfCode
        *reinterpret_cast<uint32_t*>(&pe_file[0xB0]) = 0;        // BaseOfData
        *reinterpret_cast<uint32_t*>(&pe_file[0xB4]) = 0x00400000; // ImageBase
        *reinterpret_cast<uint32_t*>(&pe_file[0xB8]) = 0x1000;   // SectionAlignment
        *reinterpret_cast<uint32_t*>(&pe_file[0xBC]) = 0x200;    // FileAlignment
        *reinterpret_cast<uint32_t*>(&pe_file[0xC0]) = 0x00000000; // Win32VersionValue (0.0 sub-major)
        *reinterpret_cast<uint32_t*>(&pe_file[0xC4]) = 0;        // ImageVersion
        *reinterpret_cast<uint32_t*>(&pe_file[0xC8]) = 0;        // MajorSubsystemVersion
        *reinterpret_cast<uint32_t*>(&pe_file[0xCC]) = 0;        // Win32VersionValue
        *reinterpret_cast<uint32_t*>(&pe_file[0xD0]) = size_of_image;   // SizeOfImage
        *reinterpret_cast<uint32_t*>(&pe_file[0xD4]) = size_of_headers; // SizeOfHeaders
        *reinterpret_cast<uint32_t*>(&pe_file[0xD8]) = 0;        // CheckSum
        *reinterpret_cast<uint16_t*>(&pe_file[0xDC]) = 3;   // Subsystem (CONSOLE) — WinRT ignores
        *reinterpret_cast<uint16_t*>(&pe_file[0xDE]) = 0;   // DllCharacteristics
        *reinterpret_cast<uint32_t*>(&pe_file[0xE0]) = 0;   // SizeOfStackReserve
        *reinterpret_cast<uint32_t*>(&pe_file[0xE4]) = 0;   // SizeOfStackCommit
        *reinterpret_cast<uint32_t*>(&pe_file[0xE8]) = 0;   // SizeOfHeapReserve
        *reinterpret_cast<uint32_t*>(&pe_file[0xEC]) = 0;   // SizeOfHeapCommit
        *reinterpret_cast<uint32_t*>(&pe_file[0xF0]) = 0;   // LoaderFlags
        *reinterpret_cast<uint32_t*>(&pe_file[0xF4]) = 16;   // NumberOfRvaAndSizes

        // DataDirectory 14: COM Descriptor (CLI Header)
        {
            uint32_t dd = 0xF8 + 14 * 8;                    // data directory array start = 0xF8
            *reinterpret_cast<uint32_t*>(&pe_file[dd])     = cli_rva;   // RVA
            *reinterpret_cast<uint32_t*>(&pe_file[dd + 4]) = cli_size;  // Size
        }

        // Section Header .text at 0x178
        std::memcpy(&pe_file[0x178], ".text\0\0\0", 8);
        *reinterpret_cast<uint32_t*>(&pe_file[0x180]) = text_vsize;  // VirtualSize
        *reinterpret_cast<uint32_t*>(&pe_file[0x184]) = text_rva;    // VirtualAddress
        *reinterpret_cast<uint32_t*>(&pe_file[0x188]) = text_rsize;  // SizeOfRawData
        *reinterpret_cast<uint32_t*>(&pe_file[0x18C]) = text_raw;    // PointerToRawData
        *reinterpret_cast<uint32_t*>(&pe_file[0x19C]) = 0x60000020;  // Characteristics (CODE|EXECUTE|READ)

        // CLI Header (IMAGE_COR20_HEADER) at file offset 0x200
        *reinterpret_cast<uint32_t*>(&pe_file[text_raw])       = 72;        // cb
        *reinterpret_cast<uint16_t*>(&pe_file[text_raw +  4])  = 2;         // MajorRuntimeVersion
        *reinterpret_cast<uint16_t*>(&pe_file[text_raw +  6])  = 5;         // MinorRuntimeVersion
        *reinterpret_cast<uint32_t*>(&pe_file[text_raw +  8])  = meta_rva;  // MetaData RVA
        *reinterpret_cast<uint32_t*>(&pe_file[text_raw + 12])  = static_cast<uint32_t>(meta_root.size()); // MetaData Size
        *reinterpret_cast<uint32_t*>(&pe_file[text_raw + 16])  = 0x00000001; // Flags IL_ONLY

        std::memcpy(&pe_file[meta_raw], meta_root.data(), meta_root.size());

        return pe_file;
    }
};

} // namespace winui::winmd
