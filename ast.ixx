// ast.ixx — Framework-Agnostic Abstract Syntax Tree
// This module defines the COMPLETE, TYPED AST for the .winui language.
// It knows NOTHING about WinUI, Win32, Qt, or any platform. It represents
// pure INTENT: layout, style, state, events, expressions, statements.

// Key design decisions:
// 1. Recursive types (BinaryExpr, ClosureExpr, IfStmt) use unique_ptr
// to break circular dependencies while staying in std::variant.
// 2. Expr and Stmt are wrapper structs around variants, giving us
// .as<T>() and .visit() helpers for ergonomic pattern matching.
// 3. The language uses ABSTRACT control names (Column, Row, Label, Button)
// that are platform-independent. Backends map them to real controls.
// 4. The language uses SwiftUI-like syntax: @state, @binding, body {},
// modifier chains, trailing closures, if/else, ForEach.

// Grammar reference (what the parser produces):
// SourceFile    → VersionDirective? ImportStmt* ViewDecl*
// ViewDecl      → 'export' 'view' IDENT '{' Member* '}'
// Member        → PropertyDecl | CallbackDecl | BodyBlock
// PropertyDecl  → Attribute? 'var' IDENT ':' Type ('=' Expr)?
// CallbackDecl  → 'callback' IDENT '(' ParamList ')'
// BodyBlock     → 'body' '{' BodyItem* '}'
// BodyItem      → ViewNode | ConditionalView | ForEachNode
// ViewNode      → IDENT Args? Block? Modifier*
// Modifier      → '.' IDENT Args? Block?
// Expr          → full expression tree (Pratt parsed)
// Stmt          → assignment | expression | if | var | return
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
#include <cctype>
#include <utility>
#include <unordered_map>
#include <span>
#include <algorithm>
#include <ranges>
export module winui.ast;

import winui.diagnostics;

export namespace winui::ast {

using SourceLocation = diag::SourceLocation;

// §1  Enumerations

// Primitive types

enum class TypeKind : std::uint8_t {
    String, Int, Bool, Float, Double, DateTime, Collection, Custom, Void,
};

[[nodiscard]] constexpr auto type_name(TypeKind k) -> std::string_view {
    switch (k) {
        case TypeKind::String:     return "string";
        case TypeKind::Int:        return "int";
        case TypeKind::Bool:       return "bool";
        case TypeKind::Float:      return "float";
        case TypeKind::Double:     return "double";
        case TypeKind::DateTime:   return "datetime";
        case TypeKind::Collection: return "collection";
        case TypeKind::Custom:     return "custom";
        case TypeKind::Void:       return "void";
    }
    std::unreachable();
}

// Property attribute (SwiftUI-like decorators)

enum class PropAttr : std::uint8_t {
    None,         // plain ABI property
    State,        // @state      — local mutable state
    Binding,      // @binding    — two-way binding from parent
    Published,    // @published  — observable, triggers update
    Computed,     // @computed   — derived read-only state (getter only)
    Environment,  // @environment — DI / theme injection
};


// Operators

enum class UnaryOp : std::uint8_t {
    Negate,  // -x
    Not,     // !x
};

enum class BinaryOp : std::uint8_t {
    Add, Sub, Mul, Div,               // arithmetic
    Eq, Neq, Lt, Gt, Lte, Gte,       // comparison
    And, Or,                           // logical
    NullCoalesce,                      // ?? (return lhs if non-null, else rhs)
    Is, As,                            // type test / cast (sender is Button)
};

enum class AssignOp : std::uint8_t {
    Assign,      // =
    AddAssign,   // +=
    SubAssign,   // -=
    MulAssign,   // *=
    DivAssign,   // /=
};


[[nodiscard]] constexpr auto binary_op_symbol(BinaryOp op) -> std::string_view {
    switch (op) {
        case BinaryOp::Add:          return "+";  case BinaryOp::Sub: return "-";
        case BinaryOp::Mul:          return "*";  case BinaryOp::Div: return "/";
        case BinaryOp::Eq:           return "=="; case BinaryOp::Neq: return "!=";
        case BinaryOp::Lt:           return "<";  case BinaryOp::Gt:  return ">";
        case BinaryOp::Lte:          return "<="; case BinaryOp::Gte: return ">=";
        case BinaryOp::And:          return "&&"; case BinaryOp::Or:  return "||";
        case BinaryOp::NullCoalesce: return "??";
        case BinaryOp::Is:           return "is";  case BinaryOp::As: return "as";
    }
    std::unreachable();
}

[[nodiscard]] constexpr auto assign_op_symbol(AssignOp op) -> std::string_view {
    switch (op) {
        case AssignOp::Assign:    return "=";
        case AssignOp::AddAssign: return "+=";
        case AssignOp::SubAssign: return "-=";
        case AssignOp::MulAssign: return "*=";
        case AssignOp::DivAssign: return "/=";
    }
    std::unreachable();
}


// §2  Expression Nodes
//
// Recursive types reference `Expr` through unique_ptr<Expr>.
// `Expr` is forward-declared here and defined after all node types.

struct Expr;   // forward declaration
struct Stmt;   // forward declaration

// Parameter (for callbacks and closures)

struct Param {
    std::string    name;
    TypeKind       type;
    SourceLocation location;
};

// Named or positional argument

struct Argument {
    std::optional<std::string>   label;    // nullopt = positional
    std::unique_ptr<Expr>        value;
    SourceLocation               location;
};

// Leaf expression types (no recursion)

struct StringLit {
    std::string    value;       // unquoted content
    SourceLocation location;
};

struct NumberLit {
    double         value;
    bool           is_integer;
    SourceLocation location;
};

struct BoolLit {
    bool           value;
    SourceLocation location;
};

struct IdentExpr {
    std::string    name;
    SourceLocation location;
};

// root.headerText, self.count, a.b.c — chained member access.
// `object` is the left-hand side (e.g. IdentExpr("root") for root.x,
// or another MemberExpr for a.b.c). nullptr if this is the start of
// a chain (e.g. in `root.x`, the parser creates IdentExpr("root") as object).
struct MemberExpr {
    std::unique_ptr<Expr> object;   // left-hand expression (may be nullptr for partial chains)
    std::string           member;   // the accessed member name
    SourceLocation        location;
};

// $variableName — SwiftUI binding reference
struct BindingRef {
    std::string    name;
    SourceLocation location;
};

// .accent, .bold, .vertical — SwiftUI dot-enum shorthand
struct DotEnumExpr {
    std::string    value;
    SourceLocation location;
};

// 0...100 or 0..<100 — SwiftUI range
struct RangeExpr {
    std::unique_ptr<Expr> lower;
    std::unique_ptr<Expr> upper;
    bool                  inclusive;   // true = ..., false = ..<
    SourceLocation        location;
};

// Recursive expression types (reference Expr via unique_ptr)

struct UnaryExpr {
    UnaryOp               op;
    std::unique_ptr<Expr> operand;
    SourceLocation        location;
};

struct BinaryExpr {
    BinaryOp              op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    SourceLocation        location;
};

// Function or method call: format("x={}", val), root.onSubmit("Btn")
struct CallExpr {
    std::unique_ptr<Expr>    callee;     // the function/method expression
    std::vector<Argument>    arguments;
    SourceLocation           location;
};

// Closure / lambda: { param in stmts... } or { stmts... }
struct ClosureExpr {
    std::vector<Param>                params;
    std::vector<std::unique_ptr<Stmt>> body;
    SourceLocation                    location;
};

// Interpolated string: "Clicked \(count) times"
struct InterpolatedStringExpr {
    struct Segment {
        bool                       is_expr;   // false=literal, true=expression
        std::string                text;      // literal text if !is_expr
        std::unique_ptr<Expr>      expr;      // expression if is_expr
    };
    std::vector<Segment> segments;
    SourceLocation       location;
};

// New: Ternary expression

// condition ? then_expr : else_expr
struct TernaryExpr {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> then_expr;
    std::unique_ptr<Expr> else_expr;
    SourceLocation        location;
};

// New: Array/list literal

// [item0, item1, item2, ...]
struct ArrayLitExpr {
    std::vector<std::unique_ptr<Expr>> elements;
    SourceLocation                     location;
};

// New: Subscript access

// object[index]
struct SubscriptExpr {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
    SourceLocation        location;
};


// §3  Expr Wrapper (the main Expression type)

struct Expr {
    using Variant = std::variant<
        StringLit,
        NumberLit,
        BoolLit,
        IdentExpr,
        MemberExpr,
        BindingRef,
        DotEnumExpr,
        RangeExpr,
        UnaryExpr,
        BinaryExpr,
        CallExpr,
        ClosureExpr,
        InterpolatedStringExpr,
        TernaryExpr,       // cond ? a : b
        ArrayLitExpr,      // [a, b, c]
        SubscriptExpr      // obj[i]
    >;


    Variant        data;
    SourceLocation location;

    // Check if this expression holds a specific type
    template<typename T>
    [[nodiscard]] auto is() const noexcept -> bool {
        return std::holds_alternative<T>(data);
    }

    // Get a pointer to the held type, or nullptr
    template<typename T>
    [[nodiscard]] auto as() const noexcept -> T const* {
        return std::get_if<T>(&data);
    }

    template<typename T>
    [[nodiscard]] auto as() noexcept -> T* {
        return std::get_if<T>(&data);
    }

    // Visit with a callable
    template<typename F>
    decltype(auto) visit(F&& f) const {
        return std::visit(std::forward<F>(f), data);
    }

    template<typename F>
    decltype(auto) visit(F&& f) {
        return std::visit(std::forward<F>(f), data);
    }
};

// Expr factory helpers

template<typename T, typename... Args>
[[nodiscard]] auto make_expr(SourceLocation loc, Args&&... args)
    -> std::unique_ptr<Expr> {
    auto e = std::make_unique<Expr>();
    e->data     = T{std::forward<Args>(args)...};
    e->location = std::move(loc);
    return e;
}

// §4  Statement Nodes

// x = expr, x += expr
struct AssignStmt {
    std::unique_ptr<Expr> target;
    AssignOp              op;
    std::unique_ptr<Expr> value;
    SourceLocation        location;
};

// Standalone expression (usually a function call)
struct ExprStmt {
    std::unique_ptr<Expr> expr;
    SourceLocation        location;
};

// if condition { ... } else { ... }
struct IfStmt {
    std::unique_ptr<Expr>               condition;
    std::vector<std::unique_ptr<Stmt>>  then_body;
    std::vector<std::unique_ptr<Stmt>>  else_body;
    SourceLocation                      location;
};

// var x: type = initializer
struct VarStmt {
    std::string             name;
    std::optional<TypeKind> type;       // may be inferred
    std::unique_ptr<Expr>   initializer;
    SourceLocation          location;
};

// return expr
struct ReturnStmt {
    std::unique_ptr<Expr> value;       // may be null
    SourceLocation        location;
};

// New: while loop

// while condition { body... }
struct WhileStmt {
    std::unique_ptr<Expr>               condition;
    std::vector<std::unique_ptr<Stmt>>  body;
    SourceLocation                      location;
};

// New: switch / case

// One arm of a switch statement: case pattern: body...
struct SwitchCase {
    std::unique_ptr<Expr>               pattern;   // the matched value
    std::vector<std::unique_ptr<Stmt>>  body;
    SourceLocation                      location;
};

// switch subject { case p1: ... case p2: ... default: ... }
struct SwitchStmt {
    std::unique_ptr<Expr>               subject;
    std::vector<SwitchCase>             cases;
    std::vector<std::unique_ptr<Stmt>>  default_body;  // empty if no default
    SourceLocation                      location;
};


// §5  Stmt Wrapper

struct Stmt {
    using Variant = std::variant<
        AssignStmt,
        ExprStmt,
        IfStmt,
        VarStmt,
        ReturnStmt,
        WhileStmt,   // while loop
        SwitchStmt   // switch/case
    >;


    Variant        data;
    SourceLocation location;

    template<typename T>
    [[nodiscard]] auto is() const noexcept -> bool {
        return std::holds_alternative<T>(data);
    }

    template<typename T>
    [[nodiscard]] auto as() const noexcept -> T const* {
        return std::get_if<T>(&data);
    }

    template<typename T>
    [[nodiscard]] auto as() noexcept -> T* {
        return std::get_if<T>(&data);
    }

    template<typename F>
    decltype(auto) visit(F&& f) const {
        return std::visit(std::forward<F>(f), data);
    }
};

template<typename T, typename... Args>
[[nodiscard]] auto make_stmt(SourceLocation loc, Args&&... args)
    -> std::unique_ptr<Stmt> {
    auto s = std::make_unique<Stmt>();
    s->data     = T{std::forward<Args>(args)...};
    s->location = std::move(loc);
    return s;
}

// §6  Modifier (SwiftUI-style chained modifiers)

// .fontSize(24), .padding(20), .onClick { action }
struct Modifier {
    std::string                         name;
    std::vector<Argument>               arguments;
    std::optional<ClosureExpr>          trailing_closure;
    SourceLocation                      location;
};

// §7  View Tree Nodes (framework-agnostic)

// A single element in the view tree.
// control_name uses ABSTRACT names: Column, Row, Label, Button, etc.
struct ViewNode;

// if expr { views... } else if expr { views... } else { views... }
struct ConditionalView;

// ForEach(collection) { item in views... }
struct ForEachNode;

// Anything that can appear inside a body {} block
using BodyItem = std::variant<ViewNode, ConditionalView, ForEachNode>;

// A single element in the view tree.
// control_name uses ABSTRACT names: Column, Row, Label, Button, etc.
struct ViewNode {
    std::string              control_name;   // "Column", "Label", "Button"
    std::vector<Argument>    arguments;      // constructor args
    std::vector<BodyItem>    children;       // child body items (views, if, ForEach)
    std::vector<Modifier>    modifiers;      // .fontSize(24) etc.
    std::optional<ClosureExpr> trailing_closure;  // action closure
    SourceLocation           location;
};

// if expr { views... } else if expr { views... } else { views... }
struct ConditionalView {
    std::unique_ptr<Expr>    condition;
    std::vector<BodyItem>    then_branch;
    std::vector<BodyItem>    else_branch;
    SourceLocation           location;
};

// ForEach(collection) { item in views... }
struct ForEachNode {
    std::unique_ptr<Expr>    collection;
    std::string              binding_name;
    std::vector<BodyItem>    body;
    SourceLocation           location;
};

// §8  Top-Level Declarations

// @state var headerText: string = "Welcome"
struct PropertyDecl {
    PropAttr                       attribute;
    TypeKind                       type;
    std::string                    type_text;   // raw type text ("string[]", "datetime", custom names)
    std::string                    name;
    std::optional<std::unique_ptr<Expr>> default_value;
    SourceLocation                 location;
};

// callback onSubmit(elementId: string)
// callback onClick() { ... }   - optional statement body (code-behind logic)
struct CallbackDecl {
    std::string                   name;
    std::vector<Param>            parameters;
    std::vector<std::unique_ptr<Stmt>> body;   // may be empty (pure declaration)
    SourceLocation                location;
};

// import { Column, Label, Button } from "core.controls"
struct ImportStmt {
    std::vector<std::string>  symbols;
    std::string               module_path;
    SourceLocation            location;
};

// export view MainView { ... }  /  export view CopyButton : Button { ... }
struct ViewDecl {
    std::string                name;
    std::string                base_class;   // bare framework base name (projection-agnostic)
    std::vector<PropertyDecl>  properties;
    std::vector<CallbackDecl>  callbacks;
    std::vector<BodyItem>      body;
    SourceLocation             location;
};

// #version 1
struct VersionDirective {
    std::uint32_t   major = 1;
    std::uint32_t   minor = 0;
    SourceLocation  location;
};

// §8.5  File-level directives (app-level settings)

// A file-level directive: .extendContentIntoTitleBar, .appIcon(path),
// .darkTitlebar, .darkContextMenu, .backdrop(mica|acrylic)
struct Directive {
    enum class Kind : std::uint8_t {
        ExtendContentIntoTitleBar,  // .extendContentIntoTitleBar
        AppIcon,                    // .appIcon("path\\icon.ico")
        DarkTitlebar,               // .darkTitlebar
        DarkContextMenu,            // .darkContextMenu
        Backdrop,                   // .backdrop(mica) or .backdrop(acrylic)
        Titlebar,                   // .titlebar("text with \xF0\x9F\x98\x80 emojis")
    };
    Kind        kind;
    std::string arg;            // for AppIcon: the path; for Backdrop: "mica" or "acrylic"; for Titlebar: title text
    SourceLocation location;
};

// §9  Source File (root AST node)

struct SourceFile {
    std::string                     file_path;
    std::optional<VersionDirective> version;
    std::vector<ImportStmt>         imports;
    std::vector<Directive>          directives;  // app-level settings
    std::vector<ViewDecl>           views;
};

// §10  Name Conversion Utilities

// camelCase or snake_case → PascalCase
// "headerText" → "HeaderText", "on_submit" → "OnSubmit"
[[nodiscard]] inline auto to_pascal_case(std::string_view s) -> std::string {
    if (s.empty()) return {};
    std::string r;
    r.reserve(s.size());
    bool cap = true;
    for (char c : s) {
        if (c == '_') { cap = true; continue; }
        r += cap ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        if (cap) cap = false;
    }
    return r;
}

// PascalCase or snake_case → camelCase
// "HeaderText" → "headerText"
[[nodiscard]] inline auto to_camel_case(std::string_view s) -> std::string {
    if (s.empty()) return {};
    std::string r;
    r.reserve(s.size());
    bool first = true;
    bool cap = false;
    for (char c : s) {
        if (c == '_') { cap = true; continue; }
        if (first) {
            r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            first = false;
        } else if (cap) {
            r += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            cap = false;
        } else {
            r += c;
        }
    }
    return r;
}

// Generate C++ member variable name
[[nodiscard]] inline auto to_member_name(std::string_view base, std::size_t idx = 0)
    -> std::string {
    if (idx == 0) return std::format("m_{}", to_camel_case(base));
    return std::format("m_{}{}", to_camel_case(base), idx);
}

// Convert TypeKind to C++ type string (for emitted code)
[[nodiscard]] inline auto type_kind_to_cpp(TypeKind k) -> std::string_view {
    switch (k) {
        case TypeKind::String: return "hstring";
        case TypeKind::Int:    return "int";
        case TypeKind::Bool:   return "bool";
        case TypeKind::Float:  return "float";
        case TypeKind::Double: return "double";
        case TypeKind::Void:   return "void";
    }
    std::unreachable();
}

} // namespace winui::ast
