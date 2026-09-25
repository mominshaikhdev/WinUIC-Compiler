// ir.ixx — Framework-Agnostic Intermediate Representation
// The IR sits between the AST (parse output) and the backend (code emitter).
// It is FULLY RESOLVED and TYPED — all names are resolved, all types are
// checked, all abstract controls are validated against the registry.

// The IR knows NOTHING about WinUI, Qt, Web, or any platform. It describes:
// - Layout intent (Column, Row, Stack, Label, Button, ...)
// - Resolved property values with types
// - State variables and their initial values
// - Event bindings with resolved closures
// - ABI surface (properties and events exposed to host language)

// The backend reads the IR and maps abstract controls to platform-specific
// implementations via the control registry.
// Copyright (c) 2026 Momin Shaikh. All rights reserved.

module;
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include <format>
#include <cstdint>
#include <utility>
#include <ranges>
#include <algorithm>
export module winui.ir;

import winui.diagnostics;
import winui.ast;

export namespace winui::ir {

using SourceLocation = diag::SourceLocation;

// §1  Resolved Values

// A concrete literal value with a known type
struct IrLiteral {
    std::variant<std::string, std::int64_t, double, bool> value;
    ast::TypeKind type;
};

// Reference to a @state variable
struct IrStateRef {
    std::string   name;
    ast::TypeKind type;
    std::string   member;        // chained access: $name.member ("" if none)
};

// Reference to a @binding variable ($var)
struct IrBindingRef {
    std::string   name;
    ast::TypeKind type;
    std::string   member;        // chained access: $name.member ("" if none)
};

// Dot-enum value: .accent, .bold, .vertical
struct IrEnumValue {
    std::string enum_context;   // e.g. "Orientation", "FontWeight", ""
    std::string value_name;     // e.g. "vertical", "bold", "accent"
};

// Range value: 0...100
struct IrRange {
    double lower;
    double upper;
    bool   inclusive;
};

// Segment of an interpolated string: literal text or expression reference.
// Uses string names (not IrValue) to avoid recursive variant issues.
struct IrInterpolatedSegment {
    bool        is_expr = false;
    std::string text;        // literal text if !is_expr
    std::string expr_name;   // state/binding var name if is_expr
};

// Interpolated string: "Clicked \(count) times"
struct IrInterpolatedString {
    std::vector<IrInterpolatedSegment> segments;
    SourceLocation                      location;
};

// Projection-agnostic expression text (e.g. (self->m_counter + 1))
struct IrExpr {
    std::string   expr_text;
    ast::TypeKind type;
};

// A resolved value in the IR
using IrValue = std::variant<
    IrLiteral,
    IrStateRef,
    IrBindingRef,
    IrEnumValue,
    IrRange,
    IrInterpolatedString,
    IrExpr
>;

// Get the type of an IrValue (if determinable)
[[nodiscard]] inline auto value_type(IrValue const& v) -> std::optional<ast::TypeKind> {
    return std::visit([](auto const& val) -> std::optional<ast::TypeKind> {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, IrLiteral>)             return val.type;
        if constexpr (std::is_same_v<T, IrStateRef>)            return val.type;
        if constexpr (std::is_same_v<T, IrBindingRef>)          return val.type;
        if constexpr (std::is_same_v<T, IrInterpolatedString>)  return ast::TypeKind::String;
        if constexpr (std::is_same_v<T, IrExpr>)                return val.type;
        return std::nullopt;
    }, v);
}

// §2  Resolved Statements (inside closures/event handlers)

// Assign a value to a state variable: count = count + 1
// For element property targets the full chain is kept: obj.member = value
struct IrAssign {
    std::string    target_state;   // member/state name ("Text", "count")
    std::string    target_object;  // owning object for member targets ("Control1Output"), "" for state vars
    ast::AssignOp  op;
    IrValue        value;
    SourceLocation location;
};

// Fire an ABI event: onSubmit("SubmitBtn")
struct IrFireEvent {
    std::string         event_name;
    std::vector<IrValue> arguments;
    SourceLocation      location;
};

// Call a method or function: someHelper()
struct IrCallMethod {
    std::string         method_name;
    std::string         object;          // "" if freestanding
    std::vector<IrValue> arguments;
    SourceLocation      location;
};

// Inner action types used inside IrIfAction (no recursive IrIfAction to break cycles).
// This allows one level of if-else nesting inside closures.
using IrInnerAction = std::variant<IrAssign, IrFireEvent, IrCallMethod>;

// Variable declaration inside a closure: var x: type = expr
struct IrVarDecl {
    std::string             name;
    ast::TypeKind           type;
    IrValue                 initializer;
    SourceLocation          location;
};

// Return statement inside a closure: return expr
struct IrReturn {
    IrValue                 value;
    SourceLocation          location;
};

// Conditional in a closure: if count > 0 { ... }
struct IrIfAction {
    IrValue                       condition_lhs;
    ast::BinaryOp                 condition_op;
    IrValue                       condition_rhs;
    std::vector<IrInnerAction>    then_body;
    std::vector<IrInnerAction>    else_body;
    SourceLocation                location;
};

// Any executable action in a closure
using IrAction = std::variant<IrAssign, IrFireEvent, IrCallMethod, IrIfAction, IrVarDecl, IrReturn>;

// A resolved closure / event handler body
struct IrClosure {
    std::vector<IrAction> actions;
    SourceLocation        location;
};

// §3  Resolved Properties and Modifiers

// A resolved property on a control: spacing = 15, text = stateRef
struct IrProperty {
    std::string    name;     // abstract property name: "spacing", "text"
    IrValue        value;
    SourceLocation location;
};

// A resolved modifier: .fontSize(24), .padding(20), .onClick { closure }
struct IrModifier {
    std::string              name;       // "fontSize", "padding", "onClick"
    std::vector<IrValue>     arguments;
    std::optional<IrClosure> closure;    // for event modifiers
    SourceLocation           location;
};

// §4  Control Node (the core view tree, framework-agnostic)

// A single control in the resolved view tree.
// Uses ABSTRACT names only: "Column", "Label", "Button", etc.
struct IrNode;

// A conditional view: if condition { then } else if ... { else }
struct IrConditionalNode;

// A ForEach view: for item in collection { body }
struct IrForEachNode;

// Any node in the view tree
using IrBodyNode = std::variant<IrNode, IrConditionalNode, IrForEachNode>;

// A single control in the resolved view tree.
// Uses ABSTRACT names only: "Column", "Label", "Button", etc.
struct IrNode {
    std::string              abstract_control;   // "Column", "Label", "Button"
    std::vector<IrProperty>  properties;         // resolved constructor args
    std::vector<IrModifier>  modifiers;          // resolved modifier chain
    std::vector<IrBodyNode>  children;           // child body nodes (controls, if, ForEach)
    std::optional<IrClosure> action_closure;     // trailing action (Button)
    std::vector<std::pair<std::string, std::string>> raw_attrs; // verbatim XAML attrs/fragments
    SourceLocation           location;

    // Unique ID for code generation (assigned during lowering)
    std::size_t              node_id = 0;

    // Find a property by name
    [[nodiscard]] auto find_property(std::string_view name) const
        -> IrProperty const* {
        for (auto const& p : properties) {
            if (p.name == name) return &p;
        }
        return nullptr;
    }

    // Find a modifier by name
    [[nodiscard]] auto find_modifier(std::string_view name) const
        -> IrModifier const* {
        for (auto const& m : modifiers) {
            if (m.name == name) return &m;
        }
        return nullptr;
    }
};

// A conditional view: if condition { then } else if ... { else }
struct IrConditionalNode {
    IrValue              condition;
    std::vector<IrBodyNode> then_branch;
    std::vector<IrBodyNode> else_branch;
    SourceLocation   location;
    std::size_t      node_id = 0;
};

// A ForEach view: for item in collection { body }
struct IrForEachNode {
    IrValue          collection;
    std::string      binding_name;
    std::vector<IrBodyNode> body;
    SourceLocation   location;
    std::size_t      node_id = 0;
};

// §5  ABI Surface (what the host language sees)

// An ABI-exposed property (accessible from Python, Rust, C#, etc.)
struct IrAbiProperty {
    std::string               name;          // PascalCase for ABI
    ast::TypeKind             type;
    bool                      has_default;
    std::optional<IrLiteral>  default_value;
    ast::PropAttr             attribute;     // state, binding, etc.
    SourceLocation            location;
};

// An ABI-exposed event
struct IrAbiEvent {
    std::string              name;           // PascalCase for ABI
    std::vector<ast::Param>  parameters;
    std::vector<IrAction>    body;           // code-behind logic (may be empty)
    SourceLocation           location;
};

// Internal state variable (not directly ABI-exposed unless @state)
struct IrStateVar {
    std::string               name;
    ast::TypeKind             type;
    std::string               type_text;   // raw type text for projections
    std::optional<IrLiteral>  initial_value;
    SourceLocation            location;
};

// §6  View (a complete resolved view)

struct IrView {
    std::string                name;
    std::string                base_class;     // bare framework base name (projection-agnostic)
    std::vector<IrAbiProperty> abi_properties;
    std::vector<IrAbiEvent>    abi_events;
    std::vector<IrStateVar>    state_vars;
    std::vector<IrBodyNode>    root_nodes;     // the view tree
    SourceLocation             location;

    [[nodiscard]] auto total_node_count() const -> std::size_t {
        std::size_t count = 0;
        auto recurse = [&](auto& self, std::vector<IrBodyNode> const& nodes) -> void {
            for (auto const& bn : nodes) {
                std::visit([&](auto const& inner) {
                    using T = std::decay_t<decltype(inner)>;
                    if constexpr (std::is_same_v<T, IrNode>) {
                        count++;
                        self(self, inner.children);
                    } else if constexpr (std::is_same_v<T, IrConditionalNode>) {
                        self(self, inner.then_branch);
                        self(self, inner.else_branch);
                    } else if constexpr (std::is_same_v<T, IrForEachNode>) {
                        self(self, inner.body);
                    }
                }, bn);
            }
        };
        recurse(recurse, root_nodes);
        return count;
    }
};

// §7  Module (top-level IR output)

struct IrModule {
    std::string           name;          // project/module name
    std::uint32_t         version_major = 1;
    std::uint32_t         version_minor = 0;
    std::vector<IrView>   views;    std::vector<std::string> imported_modules;
    // File-level directives (app-level settings)
    bool extend_content_into_titlebar = false;
    bool dark_titlebar = false;
    bool dark_context_menu = false;
    std::string app_icon_path;
    std::string backdrop_type;
    std::string titlebar_text;

    // Get a view by name
    [[nodiscard]] auto find_view(std::string_view name) const
        -> IrView const* {
        for (auto const& v : views) {
            if (v.name == name) return &v;
        }
        return nullptr;
    }
};

} // namespace winui::ir
