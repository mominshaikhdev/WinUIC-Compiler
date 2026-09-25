// parser.ixx — Recursive Descent + Pratt Expression Parser
// Fully-featured parser producing a complete AST with:
// - Pratt parser for expressions (proper operator precedence)
// - Full statement parsing for closures (assignment, if, var, return)
// - SwiftUI-like view syntax with modifier chains
// - Deterministic view-vs-closure detection (no heuristics)
// - Version directive (#version 1)
// - Both SwiftUI-style and legacy PDF-style property declarations

// Operator Precedence (Pratt binding powers, low → high):
// 1  ||           logical or
// 2  &&           logical and
// 3  == !=        equality
// 4  < > <= >=    comparison
// 5  + -          additive
// 6  * /          multiplicative
// 7  ! -          unary prefix
// 8  . () []      postfix (member, call, subscript)
// Copyright (c) 2026 Momin Shaikh. All rights reserved.

module;
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include <format>
#include <utility>
#include <cstdint>
#include <stdexcept>
#include <functional>
export module winui.parser;

import winui.diagnostics;
import winui.ast;
import winui.lexer;

export namespace winui::parse {

namespace tk = winui::lex;

class Parser {
public:
    // Predicate: does the named control accept child view nodes?
    // Wired from the ControlRegistry (schema.accepts_children). When null,
    // a PascalCase heuristic is used so ANY control — including future
    // WinUI 3 or third-party controls — works without compiler changes.
    using ContainerPred = std::function<bool(std::string_view)>;

    explicit Parser(std::vector<tk::Token> tokens, std::string file = "<input>",
                    ContainerPred is_container = nullptr)
        : m_toks(std::move(tokens)), m_file(std::move(file)),
          m_is_container(std::move(is_container)) {}

    [[nodiscard]] auto parse() -> ast::SourceFile {
        ast::SourceFile sf;
        sf.file_path = m_file;

        // Optional #version directive
        if (check(tk::TokenKind::Hash)) {
            sf.version = parse_version();
        }
        while (!at_end()) {
            std::size_t start_pos = m_pos;
            if (check(tk::TokenKind::KwImport)) {
                sf.imports.push_back(parse_import());
            } else if (check(tk::TokenKind::KwExport)) {
                sf.views.push_back(parse_view_decl());
            } else if (check(tk::TokenKind::Dot) && peek().kind == tk::TokenKind::Identifier) {
                sf.directives.push_back(parse_directive());
            } else {
                error("expected 'import', '.directive', or 'export view'");
                advance();
            }
            if (m_pos == start_pos) advance();
        }
        return sf;
    }

    [[nodiscard]] auto take_diagnostics() -> diag::DiagnosticBag {
        return std::move(m_diag);
    }

private:
    std::vector<tk::Token> m_toks;
    std::string            m_file;
    std::size_t            m_pos = 0;
    diag::DiagnosticBag    m_diag;
    ContainerPred          m_is_container;

    // Registry-backed container detection
    // Replaces the old hardcoded container-name list. The predicate comes
    // from the ControlRegistry (accepts_children). Fallback heuristic: any
    // PascalCase control name is treated as a potential container — this
    // keeps the compiler fully extensible for new WinUI 3 controls and
    // third-party controls that are not (yet) in the registry.
    [[nodiscard]] auto control_is_container(std::string_view name) const -> bool {
        if (m_is_container) return m_is_container(name);
        if (name.empty()) return false;
        char c0 = name[0];
        return c0 >= 'A' && c0 <= 'Z';
    }

    // Token navigation

    [[nodiscard]] auto at_end() const -> bool {
        return m_pos >= m_toks.size() || cur().kind == tk::TokenKind::EndOfFile;
    }
    [[nodiscard]] auto cur() const -> tk::Token const& {
        return m_pos < m_toks.size() ? m_toks[m_pos] : m_toks.back();
    }
    [[nodiscard]] auto peek(std::size_t off = 1) const -> tk::Token const& {
        auto i = m_pos + off;
        return i < m_toks.size() ? m_toks[i] : m_toks.back();
    }
    [[nodiscard]] auto check(tk::TokenKind k) const -> bool { return cur().kind == k; }

    auto advance() -> tk::Token const& {
        auto const& t = cur();
        if (!at_end()) m_pos++;
        return t;
    }
    auto expect(tk::TokenKind k) -> tk::Token const& {
        if (check(k)) return advance();
        error(std::format("expected {}, got {}", tk::token_name(k), tk::token_name(cur().kind)));
        return cur();
    }
    auto match(tk::TokenKind k) -> bool {
        if (!check(k)) return false;
        advance(); return true;
    }
    auto loc() const -> ast::SourceLocation { return cur().location(m_file); }
    auto error(std::string msg) -> void { m_diag.error(loc(), std::move(msg)); }

    // §1  Version directive: #version 1

    auto parse_version() -> ast::VersionDirective {
        auto l = loc();
        expect(tk::TokenKind::Hash);
        auto const& kw = expect(tk::TokenKind::Identifier); // "version"
        if (kw.lexeme != "version") error("expected 'version' after '#'");
        auto const& maj = expect(tk::TokenKind::NumberLiteral);
        ast::VersionDirective v;
        v.major = static_cast<std::uint32_t>(std::stoul(maj.lexeme));
        v.location = l;
        if (check(tk::TokenKind::Dot) && peek().kind == tk::TokenKind::NumberLiteral) {
            advance();
            auto const& min = expect(tk::TokenKind::NumberLiteral);
            v.minor = static_cast<std::uint32_t>(std::stoul(min.lexeme));
        }
        return v;
    }

    // §2  Import

    auto parse_import() -> ast::ImportStmt {
        auto l = loc();
        expect(tk::TokenKind::KwImport);
        std::vector<std::string> syms;
        if (check(tk::TokenKind::LBrace)) {
            advance();
            do { syms.push_back(expect(tk::TokenKind::Identifier).lexeme); }
            while (match(tk::TokenKind::Comma));
            expect(tk::TokenKind::RBrace);
        } else {
            syms.push_back(expect(tk::TokenKind::Identifier).lexeme);
        }
        expect(tk::TokenKind::KwFrom);
        auto path = strip_q(expect(tk::TokenKind::StringLiteral).lexeme);
        // Import paths are file paths: single backslashes only. Forward
        // slashes belong to URIs (https://, scheme://) which are never valid
        // here, and double backslashes are rejected too.
        if (path.find('/') != std::string::npos) {
            error("import path must use single backslashes, not forward slashes: '" + path + "'");
        }
        if (path.find("\\\\") != std::string::npos) {
            error("import path must not contain double backslashes: '" + path + "'");
        }
        match(tk::TokenKind::Semicolon);
        return { .symbols = std::move(syms), .module_path = std::move(path), .location = l };
    }

    // §2.5  File-level directive (.extendContentIntoTitleBar, .appIcon, etc.)

    auto parse_directive() -> ast::Directive {
        auto l = loc();
        expect(tk::TokenKind::Dot);
        auto name = expect(tk::TokenKind::Identifier).lexeme;
        ast::Directive d;
        d.location = l;

        if (name == "extendContentIntoTitleBar") {
            d.kind = ast::Directive::Kind::ExtendContentIntoTitleBar;
        } else if (name == "appIcon") {
            d.kind = ast::Directive::Kind::AppIcon;
            expect(tk::TokenKind::LParen);
            d.arg = strip_q(expect(tk::TokenKind::StringLiteral).lexeme);
            expect(tk::TokenKind::RParen);
        } else if (name == "darkTitlebar") {
            d.kind = ast::Directive::Kind::DarkTitlebar;
        } else if (name == "darkContextMenu") {
            d.kind = ast::Directive::Kind::DarkContextMenu;
        } else if (name == "backdrop") {
            d.kind = ast::Directive::Kind::Backdrop;
            expect(tk::TokenKind::LParen);
            d.arg = expect(tk::TokenKind::Identifier).lexeme; // "mica" or "acrylic"
            expect(tk::TokenKind::RParen);
        } else if (name == "titlebar") {
            d.kind = ast::Directive::Kind::Titlebar;
            expect(tk::TokenKind::LParen);
            d.arg = strip_q(expect(tk::TokenKind::StringLiteral).lexeme);
            expect(tk::TokenKind::RParen);
        } else {
            error("unknown directive: '.' + name");
        }
        match(tk::TokenKind::Semicolon);
        return d;
    }

    // §3  View declaration

    auto parse_view_decl() -> ast::ViewDecl {
        auto l = loc();
        expect(tk::TokenKind::KwExport);
        expect(tk::TokenKind::KwView);
        auto name = expect(tk::TokenKind::Identifier).lexeme;
        ast::ViewDecl vd;
        vd.name = std::move(name);
        vd.location = l;
        // Optional base: export view X : Button / : Microsoft::UI::Xaml::Controls::Button
        // Only the bare framework name is kept - the winmd stays projection-agnostic
        // and every language projection maps the hint to its own base type.
        if (match(tk::TokenKind::Colon)) {
            std::string full;
            while (cur().kind == tk::TokenKind::Identifier || check(tk::TokenKind::Colon)) {
                full += cur().lexeme;
                advance();
            }
            auto cc = full.rfind("::");
            vd.base_class = (cc == std::string::npos) ? full : full.substr(cc + 2);
        }
        expect(tk::TokenKind::LBrace);
        while (!check(tk::TokenKind::RBrace) && !at_end()) {
            std::size_t start_pos = m_pos;
            if (check(tk::TokenKind::KwState) || check(tk::TokenKind::KwBinding)
                || check(tk::TokenKind::KwPublished) || check(tk::TokenKind::KwComputed)
                || check(tk::TokenKind::KwEnvironment) || check(tk::TokenKind::KwVar)) {
                vd.properties.push_back(parse_prop_decl());
            } else if (check(tk::TokenKind::KwProperty)) {
                vd.properties.push_back(parse_prop_decl_legacy());
            } else if (check(tk::TokenKind::KwCallback)) {
                vd.callbacks.push_back(parse_callback_decl());
            } else if (check(tk::TokenKind::KwBody)) {
                parse_body_block(vd.body);
            } else {
                vd.body.push_back(parse_body_item());
            }
            if (m_pos == start_pos) advance();
        }
        expect(tk::TokenKind::RBrace);
        return vd;
    }

    // §4  Property declaration

    auto parse_prop_decl() -> ast::PropertyDecl {
        auto l = loc();
        ast::PropertyDecl p;
        p.location = l;
        p.attribute = ast::PropAttr::None;
        if (match(tk::TokenKind::KwState))            p.attribute = ast::PropAttr::State;
        else if (match(tk::TokenKind::KwBinding))     p.attribute = ast::PropAttr::Binding;
        else if (match(tk::TokenKind::KwPublished))   p.attribute = ast::PropAttr::Published;
        else if (match(tk::TokenKind::KwComputed))    p.attribute = ast::PropAttr::Computed;
        else if (match(tk::TokenKind::KwEnvironment)) p.attribute = ast::PropAttr::Environment;
        expect(tk::TokenKind::KwVar);
        p.name = expect(tk::TokenKind::Identifier).lexeme;
        expect(tk::TokenKind::Colon);
        p.type = parse_type(p.type_text);
        if (match(tk::TokenKind::Equal)) {
            p.default_value = parse_expr_ptr();
        }
        match(tk::TokenKind::Semicolon);
        return p;
    }

    auto parse_prop_decl_legacy() -> ast::PropertyDecl {
        auto l = loc();
        expect(tk::TokenKind::KwProperty);
        ast::PropertyDecl p;
        p.location = l;
        p.attribute = ast::PropAttr::State;
        p.type = parse_type(p.type_text);
        p.name = expect(tk::TokenKind::Identifier).lexeme;
        expect(tk::TokenKind::Colon);
        p.default_value = parse_expr_ptr();
        match(tk::TokenKind::Semicolon);
        return p;
    }

    // §5  Callback declaration

    auto parse_callback_decl() -> ast::CallbackDecl {
        auto l = loc();
        expect(tk::TokenKind::KwCallback);
        ast::CallbackDecl cb;
        cb.name = expect(tk::TokenKind::Identifier).lexeme;
        cb.location = l;
        expect(tk::TokenKind::LParen);
        if (!check(tk::TokenKind::RParen)) {
            do {
                ast::Param pm;
                pm.location = loc();
                // Both "name: type" and "type name" styles
                if (peek().kind == tk::TokenKind::Colon) {
                    pm.name = expect(tk::TokenKind::Identifier).lexeme;
                    expect(tk::TokenKind::Colon);
                    std::string pt;
                    pm.type = parse_type(pt);
                } else {
                    std::string pt;
                    pm.type = parse_type(pt);
                    pm.name = expect(tk::TokenKind::Identifier).lexeme;
                }
                cb.parameters.push_back(std::move(pm));
            } while (match(tk::TokenKind::Comma));
        }
        expect(tk::TokenKind::RParen);
        // Optional statement body: callback Name() { ... } - carries the
        // code-behind logic for this handler (assignment, calls, if/switch).
        if (match(tk::TokenKind::LBrace)) {
            while (!check(tk::TokenKind::RBrace) && !at_end()) {
                std::size_t start_pos = m_pos;
                cb.body.push_back(parse_stmt());
                if (m_pos == start_pos) advance();
            }
            expect(tk::TokenKind::RBrace);
        }
        match(tk::TokenKind::Semicolon);
        return cb;
    }

    // §6  Body block and body items

    auto parse_body_block(std::vector<ast::BodyItem>& items) -> void {
        expect(tk::TokenKind::KwBody);
        expect(tk::TokenKind::LBrace);
        while (!check(tk::TokenKind::RBrace) && !at_end()) {
            std::size_t start_pos = m_pos;
            items.push_back(parse_body_item());
            if (m_pos == start_pos) advance();
        }
        expect(tk::TokenKind::RBrace);
    }

    auto parse_body_item() -> ast::BodyItem {
        if (check(tk::TokenKind::KwIf))      return parse_conditional_view();
        if (check(tk::TokenKind::KwForEach)) return parse_foreach();
        return parse_view_node();
    }

    // §7  View node

    auto parse_view_node() -> ast::ViewNode {
        auto l = loc();
        ast::ViewNode node;
        node.control_name = expect(tk::TokenKind::Identifier).lexeme;
        node.location = l;

        // Constructor args: Label("Hello"), Column(spacing: 15)
        if (match(tk::TokenKind::LParen)) {
            node.arguments = parse_arg_list();
            expect(tk::TokenKind::RParen);
        }

        // Block: { children... } or { closure... }
        // DETERMINISTIC detection: we look at context, not heuristics.
        // If the control accepts children (registry predicate / PascalCase
        // fallback), a block is treated as children here. The semantic pass
        // will validate. If the first token is a known statement keyword
        // (identifier followed by =, +=, -=) it's a closure.
        auto parse_node_block = [&] {
            bool is_container = control_is_container(node.control_name);
            if (!is_container || block_is_closure()) {
                node.trailing_closure = parse_closure_expr();
                return;
            }
            expect(tk::TokenKind::LBrace);
            while (!check(tk::TokenKind::RBrace) && !at_end()) {
                std::size_t start_pos = m_pos;
                if (check(tk::TokenKind::Identifier) && peek().kind == tk::TokenKind::Colon) {
                    auto prop_l = loc();
                    auto prop_name = advance().lexeme;
                    expect(tk::TokenKind::Colon);
                    if (check(tk::TokenKind::Arrow) || (check(tk::TokenKind::LBrace) && block_is_closure())) {
                        if (match(tk::TokenKind::Arrow)) {}
                        auto closure = parse_closure_expr();
                        node.modifiers.push_back(ast::Modifier{
                            .name = std::move(prop_name),
                            .arguments = {},
                            .trailing_closure = std::move(closure),
                            .location = prop_l
                        });
                    } else {
                        auto val = parse_expr_ptr();
                        match(tk::TokenKind::Semicolon);
                        std::vector<ast::Argument> args;
                        args.push_back(ast::Argument{
                            .label = std::nullopt,
                            .value = std::move(val),
                            .location = prop_l
                        });
                        node.modifiers.push_back(ast::Modifier{
                            .name = std::move(prop_name),
                            .arguments = std::move(args),
                            .trailing_closure = std::nullopt,
                            .location = prop_l
                        });
                    }
                } else {
                    node.children.push_back(parse_body_item());
                }
                if (m_pos == start_pos) advance();
            }
            expect(tk::TokenKind::RBrace);
        };
        if (check(tk::TokenKind::LBrace)) parse_node_block();

        // Modifier chain: .fontSize(24).bold().padding(20)
        while (check(tk::TokenKind::Dot)
               && peek().kind == tk::TokenKind::Identifier) {
            node.modifiers.push_back(parse_modifier());
        }

        // Children block after the modifier chain:
        //   Grid.width(300).padding(8) { ... }  — emitted by xaml2winui
        if (check(tk::TokenKind::LBrace)) parse_node_block();

        match(tk::TokenKind::Semicolon);

        return node;
    }

    // Deterministic: is the upcoming { } block a closure (statements)
    // or a children block (view nodes)?

    // Rules (checked in order):
    // 1. { }  → empty children block
    // 2. { IDENT = | += | -=  → closure (assignment)
    // 3. { return  → closure
    // 4. { IDENT ( ... )  where IDENT is lowercase → closure (call)
    // 5. Otherwise → children block (view nodes start with PascalCase)
    [[nodiscard]] auto block_is_closure() const -> bool {
        if (!check(tk::TokenKind::LBrace)) return false;
        auto const& first = peek(1);
        if (first.kind == tk::TokenKind::RBrace) return false; // empty = children
        if (first.kind == tk::TokenKind::KwReturn) return true;

        if (first.kind == tk::TokenKind::Identifier) {
            auto const& second = peek(2);
            // Assignment: x = ..., x += ..., x -= ...
            if (second.kind == tk::TokenKind::Equal
                || second.kind == tk::TokenKind::PlusEqual
                || second.kind == tk::TokenKind::MinusEqual) return true;

            // Lowercase identifier followed by ( → likely a function call
            if (!first.lexeme.empty()) {
                char fc = first.lexeme[0];
                if (fc >= 'a' && fc <= 'z' && second.kind == tk::TokenKind::LParen)
                    return true;
            }
        }

        // Keyword-based starts that indicate closures
        if (first.kind == tk::TokenKind::KwVar) return true;
        if (first.kind == tk::TokenKind::KwSelf
            || first.kind == tk::TokenKind::KwRoot) {
            auto const& second = peek(2);
            if (second.kind == tk::TokenKind::Dot) return true;
        }

        return false; // default: children block
    }

    // §8  Modifier

    auto parse_modifier() -> ast::Modifier {
        auto l = loc();
        expect(tk::TokenKind::Dot);
        ast::Modifier mod;
        mod.name = expect(tk::TokenKind::Identifier).lexeme;
        mod.location = l;

        if (match(tk::TokenKind::Colon)) {
            if (match(tk::TokenKind::Arrow) || check(tk::TokenKind::LBrace)) {
                if (match(tk::TokenKind::Arrow)) {}
                mod.trailing_closure = parse_closure_expr();
            } else {
                auto val = parse_expr_ptr();
                mod.arguments.push_back(ast::Argument{
                    .label = std::nullopt,
                    .value = std::move(val),
                    .location = l
                });
            }
            match(tk::TokenKind::Semicolon);
        } else if (match(tk::TokenKind::LParen)) {
            mod.arguments = parse_arg_list();
            expect(tk::TokenKind::RParen);
            match(tk::TokenKind::Semicolon);
        } else if (check(tk::TokenKind::LBrace)) {
            mod.trailing_closure = parse_closure_expr();
            match(tk::TokenKind::Semicolon);
        } else {
            match(tk::TokenKind::Semicolon);
        }

        return mod;
    }

    // §9  Conditional view & ForEach

    auto parse_conditional_view() -> ast::ConditionalView {
        auto l = loc();
        expect(tk::TokenKind::KwIf);
        ast::ConditionalView cv;
        cv.location = l;
        cv.condition = parse_expr_ptr();
        expect(tk::TokenKind::LBrace);
        while (!check(tk::TokenKind::RBrace) && !at_end()) {
            std::size_t start_pos = m_pos;
            cv.then_branch.push_back(parse_body_item());
            if (m_pos == start_pos) advance();
        }
        expect(tk::TokenKind::RBrace);
        if (match(tk::TokenKind::KwElse)) {
            // else if { ... } chain: the nested ConditionalView is stored directly
            // in the else branch, preserving its own condition and trailing else.
            if (check(tk::TokenKind::KwIf)) {
                cv.else_branch.push_back(parse_conditional_view());
            } else {
                expect(tk::TokenKind::LBrace);
                while (!check(tk::TokenKind::RBrace) && !at_end()) {
                    std::size_t start_pos = m_pos;
                    cv.else_branch.push_back(parse_body_item());
                    if (m_pos == start_pos) advance();
                }
                expect(tk::TokenKind::RBrace);
            }
        }
        return cv;
    }

    auto parse_foreach() -> ast::ForEachNode {
        auto l = loc();
        expect(tk::TokenKind::KwForEach);
        expect(tk::TokenKind::LParen);
        ast::ForEachNode fe;
        fe.location = l;
        fe.collection = parse_expr_ptr();
        expect(tk::TokenKind::RParen);
        expect(tk::TokenKind::LBrace);
        if (check(tk::TokenKind::Identifier) && peek().kind == tk::TokenKind::KwIn) {
            fe.binding_name = cur().lexeme;
            advance(); advance(); // ident, 'in'
        }
        while (!check(tk::TokenKind::RBrace) && !at_end()) {
            std::size_t start_pos = m_pos;
            fe.body.push_back(parse_body_item());
            if (m_pos == start_pos) advance();
        }
        expect(tk::TokenKind::RBrace);
        return fe;
    }

    // §10  Argument list

    auto parse_arg_list() -> std::vector<ast::Argument> {
        std::vector<ast::Argument> args;
        if (check(tk::TokenKind::RParen)) return args;
        do {
            ast::Argument a;
            a.location = loc();
            if (cur().kind == tk::TokenKind::Identifier
                && peek().kind == tk::TokenKind::Colon) {
                a.label = cur().lexeme;
                advance(); advance();
            }
            a.value = parse_expr_ptr();
            args.push_back(std::move(a));
        } while (match(tk::TokenKind::Comma));
        return args;
    }

    // §11  Closure expression

    auto parse_closure_expr() -> ast::ClosureExpr {
        auto l = loc();
        expect(tk::TokenKind::LBrace);
        ast::ClosureExpr cl;
        cl.location = l;

        // Parse closure params: { param in ... }
        if (check(tk::TokenKind::Identifier) && peek().kind == tk::TokenKind::KwIn) {
            ast::Param pm;
            pm.location = loc();
            pm.name = cur().lexeme;
            pm.type = ast::TypeKind::Void; // inferred
            cl.params.push_back(std::move(pm));
            advance(); advance(); // ident, 'in'
        }

        while (!check(tk::TokenKind::RBrace) && !at_end()) {
            std::size_t start_pos = m_pos;
            cl.body.push_back(parse_stmt());
            if (m_pos == start_pos) advance();
        }
        expect(tk::TokenKind::RBrace);
        return cl;
    }

    // §12  Statements (inside closures)

    auto parse_stmt() -> std::unique_ptr<ast::Stmt> {
        auto l = loc();

        // var / let  x: type = expr
        if (check(tk::TokenKind::KwVar) || check(tk::TokenKind::KwLet)) {
            return parse_var_stmt();
        }

        // while condition { body... }
        if (check(tk::TokenKind::KwWhile)) {
            return parse_while_stmt();
        }

        // switch expr { case p: ... default: ... }
        if (check(tk::TokenKind::KwSwitch)) {
            return parse_switch_stmt();
        }

        // return expr
        if (check(tk::TokenKind::KwReturn)) {
            advance();
            std::unique_ptr<ast::Expr> val;
            if (!check(tk::TokenKind::RBrace) && !check(tk::TokenKind::Semicolon)) {
                val = parse_expr_ptr();
            }
            match(tk::TokenKind::Semicolon);
            auto s = std::make_unique<ast::Stmt>();
            s->data = ast::ReturnStmt{.value = std::move(val), .location = l};
            s->location = l;
            return s;
        }

        // if condition { ... }
        if (check(tk::TokenKind::KwIf)) {
            return parse_if_stmt();
        }

        // Expression or assignment
        auto expr = parse_expr_ptr();

        // Check for assignment: expr = val, expr += val, expr -= val, expr *= val, expr /= val
        ast::AssignOp aop;
        bool is_assign = false;
        if (check(tk::TokenKind::Equal))           { aop = ast::AssignOp::Assign;    is_assign = true; advance(); }
        else if (check(tk::TokenKind::PlusEqual))  { aop = ast::AssignOp::AddAssign; is_assign = true; advance(); }
        else if (check(tk::TokenKind::MinusEqual)) { aop = ast::AssignOp::SubAssign; is_assign = true; advance(); }
        else if (check(tk::TokenKind::StarEqual))  { aop = ast::AssignOp::MulAssign; is_assign = true; advance(); }
        else if (check(tk::TokenKind::SlashEqual)) { aop = ast::AssignOp::DivAssign; is_assign = true; advance(); }

        if (is_assign) {
            auto val = parse_expr_ptr();
            match(tk::TokenKind::Semicolon);
            auto s = std::make_unique<ast::Stmt>();
            s->data = ast::AssignStmt{
                .target = std::move(expr), .op = aop,
                .value = std::move(val), .location = l};
            s->location = l;
            return s;
        }

        // Standalone expression statement
        match(tk::TokenKind::Semicolon);
        auto s = std::make_unique<ast::Stmt>();
        s->data = ast::ExprStmt{.expr = std::move(expr), .location = l};
        s->location = l;
        return s;
    }

    auto parse_while_stmt() -> std::unique_ptr<ast::Stmt> {
        auto l = loc();
        advance(); // consume 'while'
        auto cond = parse_expr_ptr();
        expect(tk::TokenKind::LBrace);
        std::vector<std::unique_ptr<ast::Stmt>> body;
        while (!check(tk::TokenKind::RBrace) && !at_end()) {
            std::size_t start_pos = m_pos;
            body.push_back(parse_stmt());
            if (m_pos == start_pos) advance();
        }
        expect(tk::TokenKind::RBrace);
        auto s = std::make_unique<ast::Stmt>();
        s->data = ast::WhileStmt{
            .condition = std::move(cond),
            .body      = std::move(body),
            .location  = l};
        s->location = l;
        return s;
    }

    auto parse_switch_stmt() -> std::unique_ptr<ast::Stmt> {
        auto l = loc();
        advance(); // consume 'switch'
        auto subject = parse_expr_ptr();
        expect(tk::TokenKind::LBrace);
        std::vector<ast::SwitchCase> cases;
        std::vector<std::unique_ptr<ast::Stmt>> default_body;
        while (!check(tk::TokenKind::RBrace) && !at_end()) {
            if (check(tk::TokenKind::KwCase)) {
                auto cl = loc();
                advance(); // 'case'
                auto pattern = parse_expr_ptr();
                expect(tk::TokenKind::Colon);
                std::vector<std::unique_ptr<ast::Stmt>> case_body;
                while (!check(tk::TokenKind::KwCase) && !check(tk::TokenKind::KwDefault)
                       && !check(tk::TokenKind::RBrace) && !at_end()) {
                    std::size_t start_pos = m_pos;
                    case_body.push_back(parse_stmt());
                    if (m_pos == start_pos) advance();
                }
                cases.push_back(ast::SwitchCase{
                    .pattern  = std::move(pattern),
                    .body     = std::move(case_body),
                    .location = cl});
            } else if (check(tk::TokenKind::KwDefault)) {
                advance(); // 'default'
                expect(tk::TokenKind::Colon);
                while (!check(tk::TokenKind::RBrace) && !at_end()) {
                    std::size_t start_pos = m_pos;
                    default_body.push_back(parse_stmt());
                    if (m_pos == start_pos) advance();
                }
            } else {
                advance();
            }
        }
        expect(tk::TokenKind::RBrace);
        auto s = std::make_unique<ast::Stmt>();
        s->data = ast::SwitchStmt{
            .subject      = std::move(subject),
            .cases        = std::move(cases),
            .default_body = std::move(default_body),
            .location     = l};
        s->location = l;
        return s;
    }

    auto parse_var_stmt() -> std::unique_ptr<ast::Stmt> {
        auto l = loc();
        // Accept both 'var' and 'let' (immutable binding alias)
        if (check(tk::TokenKind::KwLet)) advance();
        else expect(tk::TokenKind::KwVar);
        auto name = expect(tk::TokenKind::Identifier).lexeme;
        std::optional<ast::TypeKind> typ;
        if (match(tk::TokenKind::Colon)) { std::string vt; typ = parse_type(vt); }
        std::unique_ptr<ast::Expr> init;
        if (match(tk::TokenKind::Equal)) init = parse_expr_ptr();
        match(tk::TokenKind::Semicolon);
        auto s = std::make_unique<ast::Stmt>();
        s->data = ast::VarStmt{.name = std::move(name), .type = typ,
                                .initializer = std::move(init), .location = l};
        s->location = l;
        return s;
    }

    auto parse_if_stmt() -> std::unique_ptr<ast::Stmt> {
        auto l = loc();
        expect(tk::TokenKind::KwIf);
        auto cond = parse_expr_ptr();
        expect(tk::TokenKind::LBrace);
        std::vector<std::unique_ptr<ast::Stmt>> then_b;
        while (!check(tk::TokenKind::RBrace) && !at_end()) {
            std::size_t start_pos = m_pos;
            then_b.push_back(parse_stmt());
            if (m_pos == start_pos) advance();
        }
        expect(tk::TokenKind::RBrace);

        std::vector<std::unique_ptr<ast::Stmt>> else_b;
        if (match(tk::TokenKind::KwElse)) {
            if (check(tk::TokenKind::KwIf)) {
                // else if: recursively parse as a nested if-stmt in the else branch
                else_b.push_back(parse_if_stmt());
            } else {
                expect(tk::TokenKind::LBrace);
                while (!check(tk::TokenKind::RBrace) && !at_end()) {
                    std::size_t start_pos = m_pos;
                    else_b.push_back(parse_stmt());
                    if (m_pos == start_pos) advance();
                }
                expect(tk::TokenKind::RBrace);
            }
        }

        auto s = std::make_unique<ast::Stmt>();
        s->data = ast::IfStmt{.condition = std::move(cond),
                               .then_body = std::move(then_b),
                               .else_body = std::move(else_b), .location = l};
        s->location = l;
        return s;
    }

    // §13  Pratt Expression Parser

    // Binding power of infix operators (higher = tighter)
    static constexpr auto infix_bp(tk::TokenKind k) -> std::pair<int,int> {
        switch (k) {
            case tk::TokenKind::PipePipe:         return {1, 2};
            case tk::TokenKind::QuestionQuestion: return {2, 3};  // ?? between || and &&
            case tk::TokenKind::AmpAmp:           return {3, 4};
            case tk::TokenKind::EqualEqual:
            case tk::TokenKind::BangEqual:        return {5, 6};
            case tk::TokenKind::Less:
            case tk::TokenKind::LessEqual:
            case tk::TokenKind::Greater:
            case tk::TokenKind::GreaterEqual:     return {7, 8};
            case tk::TokenKind::KwIs:
            case tk::TokenKind::KwAs:             return {7, 8};  // type test/cast
            case tk::TokenKind::Plus:
            case tk::TokenKind::Minus:            return {9, 10};
            case tk::TokenKind::Star:
            case tk::TokenKind::Slash:            return {11, 12};
            case tk::TokenKind::DotDotDot:
            case tk::TokenKind::DotDotLess:       return {0, 1}; // lowest
            default:                              return {-1, -1}; // not infix
        }
    }

    static constexpr auto to_binary_op(tk::TokenKind k) -> ast::BinaryOp {
        switch (k) {
            case tk::TokenKind::Plus:             return ast::BinaryOp::Add;
            case tk::TokenKind::Minus:            return ast::BinaryOp::Sub;
            case tk::TokenKind::Star:             return ast::BinaryOp::Mul;
            case tk::TokenKind::Slash:            return ast::BinaryOp::Div;
            case tk::TokenKind::EqualEqual:       return ast::BinaryOp::Eq;
            case tk::TokenKind::BangEqual:        return ast::BinaryOp::Neq;
            case tk::TokenKind::Less:             return ast::BinaryOp::Lt;
            case tk::TokenKind::Greater:          return ast::BinaryOp::Gt;
            case tk::TokenKind::LessEqual:        return ast::BinaryOp::Lte;
            case tk::TokenKind::GreaterEqual:     return ast::BinaryOp::Gte;
            case tk::TokenKind::AmpAmp:           return ast::BinaryOp::And;
            case tk::TokenKind::PipePipe:         return ast::BinaryOp::Or;
            case tk::TokenKind::QuestionQuestion: return ast::BinaryOp::NullCoalesce;
            case tk::TokenKind::KwIs:             return ast::BinaryOp::Is;
            case tk::TokenKind::KwAs:             return ast::BinaryOp::As;
            default: std::unreachable();
        }
    }

    // Pratt parser entry: parse expression with minimum binding power
    auto parse_expr(int min_bp = 0) -> std::unique_ptr<ast::Expr> {
        auto lhs = parse_prefix();

        while (true) {
            // Ternary operator: cond ? then : else  (handled outside normal infix_bp)
            // Lowest-precedence construct: only binds when nothing
            // higher-binding is pending, so `count > 50 ? a : b` parses as
            // `(count > 50) ? a : b` rather than `count > (50 ? a : b)`.
            if (cur().kind == tk::TokenKind::Question && min_bp <= 1) {
                auto l = loc();
                advance(); // consume '?'
                auto then_expr = parse_expr(0);
                expect(tk::TokenKind::Colon);
                auto else_expr = parse_expr(0);
                auto tern = std::make_unique<ast::Expr>();
                tern->data = ast::TernaryExpr{
                    .condition = std::move(lhs),
                    .then_expr = std::move(then_expr),
                    .else_expr = std::move(else_expr),
                    .location  = l};
                tern->location = l;
                lhs = std::move(tern);
                continue;
            }

            auto [l_bp, r_bp] = infix_bp(cur().kind);
            if (l_bp < min_bp) break;

            auto op_tok = cur().kind;
            auto l = loc();
            advance();

            // Range operators
            if (op_tok == tk::TokenKind::DotDotDot || op_tok == tk::TokenKind::DotDotLess) {
                auto rhs = parse_expr(r_bp);
                auto range = std::make_unique<ast::Expr>();
                range->data = ast::RangeExpr{
                    .lower = std::move(lhs),
                    .upper = std::move(rhs),
                    .inclusive = (op_tok == tk::TokenKind::DotDotDot),
                    .location = l};
                range->location = l;
                lhs = std::move(range);
                continue;
            }

            auto rhs = parse_expr(r_bp);
            auto bin = std::make_unique<ast::Expr>();
            bin->data = ast::BinaryExpr{
                .op = to_binary_op(op_tok),
                .lhs = std::move(lhs),
                .rhs = std::move(rhs),
                .location = l};
            bin->location = l;
            lhs = std::move(bin);
        }

        return lhs;
    }

    auto parse_expr_ptr() -> std::unique_ptr<ast::Expr> {
        return parse_expr(0);
    }

    // Parse a prefix (unary or primary) expression
    auto parse_prefix() -> std::unique_ptr<ast::Expr> {
        auto l = loc();

        // Unary operators: !, -
        if (check(tk::TokenKind::Bang)) {
            advance();
            auto operand = parse_expr(13); // high binding power
            auto e = std::make_unique<ast::Expr>();
            e->data = ast::UnaryExpr{.op = ast::UnaryOp::Not, .operand = std::move(operand), .location = l};
            e->location = l;
            return e;
        }
        if (check(tk::TokenKind::Minus)) {
            advance();
            auto operand = parse_expr(13);
            auto e = std::make_unique<ast::Expr>();
            e->data = ast::UnaryExpr{.op = ast::UnaryOp::Negate, .operand = std::move(operand), .location = l};
            e->location = l;
            return e;
        }

        auto primary = parse_primary();

        // Postfix: member access, call, and subscript
        while (true) {
            // Member access: expr.member. The member may be a keyword — members
            // are lower-camel projections of PascalCase XAML properties, so
            // collisions like `.view` (CollectionViewSource.View) happen.
            if (check(tk::TokenKind::Dot)
                && (peek().kind == tk::TokenKind::Identifier
                    || (peek().kind >= tk::TokenKind::KwImport && peek().kind <= tk::TokenKind::KwEnvironment))) {
                advance(); // .
                auto mem = cur().lexeme;
                advance();
                auto ml = loc();
                auto e = std::make_unique<ast::Expr>();
                e->data = ast::MemberExpr{
                    .object = std::move(primary),
                    .member = std::move(mem),
                    .location = ml};
                e->location = ml;
                primary = std::move(e);
                continue;
            }
            if (check(tk::TokenKind::LParen)) {
                // Function call: expr(args...)
                advance();
                auto args = parse_arg_list();
                expect(tk::TokenKind::RParen);
                auto call = std::make_unique<ast::Expr>();
                call->data = ast::CallExpr{
                    .callee = std::move(primary),
                    .arguments = std::move(args),
                    .location = l};
                call->location = l;
                primary = std::move(call);
                continue;
            }
            if (check(tk::TokenKind::LBracket)) {
                // Subscript access: expr[index]
                auto sl = loc();
                advance(); // [
                auto idx = parse_expr_ptr();
                expect(tk::TokenKind::RBracket);
                auto sub = std::make_unique<ast::Expr>();
                sub->data = ast::SubscriptExpr{
                    .object   = std::move(primary),
                    .index    = std::move(idx),
                    .location = sl};
                sub->location = sl;
                primary = std::move(sub);
                continue;
            }
            break;
        }

        return primary;
    }

    // Parse a primary (atom) expression
    auto parse_primary() -> std::unique_ptr<ast::Expr> {
        auto l = loc();
        auto e = std::make_unique<ast::Expr>();
        e->location = l;

        // String literal
        if (check(tk::TokenKind::StringLiteral)) {
            e->data = ast::StringLit{.value = strip_q(advance().lexeme), .location = l};
            return e;
        }
        // CSS color literal (hsl, hsla, oklch, color(display-p3))
        if (check(tk::TokenKind::CssColor)) {
            e->data = ast::StringLit{.value = advance().lexeme, .location = l};
            return e;
        }
        // Number literal
        if (check(tk::TokenKind::NumberLiteral)) {
            auto const& tok = advance();
            double v = 0;
            std::from_chars(tok.lexeme.data(), tok.lexeme.data()+tok.lexeme.size(), v);
            e->data = ast::NumberLit{.value = v,
                .is_integer = tok.lexeme.find('.') == std::string::npos, .location = l};
            return e;
        }
        // Bool
        if (check(tk::TokenKind::KwTrue))  { advance(); e->data = ast::BoolLit{.value=true, .location=l}; return e; }
        if (check(tk::TokenKind::KwFalse)) { advance(); e->data = ast::BoolLit{.value=false,.location=l}; return e; }

        // Dot-enum: .accent, .bold
        if (check(tk::TokenKind::Dot) && peek().kind == tk::TokenKind::Identifier) {
            advance();
            e->data = ast::DotEnumExpr{.value = advance().lexeme, .location = l};
            return e;
        }
        // Binding ref: $varName
        if (check(tk::TokenKind::Dollar)) {
            auto name = advance().lexeme.substr(1); // strip $
            e->data = ast::BindingRef{.name = std::move(name), .location = l};
            return e;
        }
        // Member: root.x, self.y
        if ((check(tk::TokenKind::KwRoot) || check(tk::TokenKind::KwSelf))
            && peek().kind == tk::TokenKind::Dot) {            auto obj_name = advance().lexeme;
            auto obj = std::make_unique<ast::Expr>();
            obj->data = ast::IdentExpr{.name = std::move(obj_name), .location = l};
            obj->location = l;
            advance(); // .
            auto mem = expect(tk::TokenKind::Identifier).lexeme;
            // Check for method call: root.onSubmit(...)
            if (check(tk::TokenKind::LParen)) {
                auto callee = std::make_unique<ast::Expr>();
                callee->data = ast::MemberExpr{.object=std::move(obj), .member=std::move(mem), .location=l};
                callee->location = l;
                advance(); // (
                auto args = parse_arg_list();
                expect(tk::TokenKind::RParen);
                e->data = ast::CallExpr{.callee=std::move(callee), .arguments=std::move(args), .location=l};
                return e;
            }
            e->data = ast::MemberExpr{.object=std::move(obj), .member=std::move(mem), .location=l};
            return e;
        }
        // Bare root / self: DataContext binding ("{x:Bind}" empty path)
        if (check(tk::TokenKind::KwRoot) || check(tk::TokenKind::KwSelf)) {
            e->data = ast::IdentExpr{.name = advance().lexeme, .location = l};
            return e;
        }
        // Identifier
        if (check(tk::TokenKind::Identifier)) {
            e->data = ast::IdentExpr{.name = advance().lexeme, .location = l};
            return e;
        }
        // Parenthesized expression
        if (match(tk::TokenKind::LParen)) {
            auto inner = parse_expr_ptr();
            expect(tk::TokenKind::RParen);
            return inner;
        }
        // Array literal: [a, b, c]
        if (check(tk::TokenKind::LBracket)) {
            advance(); // [
            ast::ArrayLitExpr arr;
            arr.location = l;
            if (!check(tk::TokenKind::RBracket)) {
                do {
                    arr.elements.push_back(parse_expr_ptr());
                } while (match(tk::TokenKind::Comma));
            }
            expect(tk::TokenKind::RBracket);
            e->data = std::move(arr);
            return e;
        }
        // Closure
        if (check(tk::TokenKind::LBrace)) {
            e->data = parse_closure_expr();
            return e;
        }

        error(std::format("expected expression, got {}", tk::token_name(cur().kind)));
        advance();
        e->data = ast::StringLit{.value = "<error>", .location = l};
        return e;
    }

    // §14  Type parsing

    // Types are a small vocabulary: primitives (string/int/bool/double), a
    // trailing [] making a collection of the primitive, datetime, any, and
    // custom identifiers. The raw text is captured for projections ("string[]",
    // "datetime", custom class names) — the TypeKind alone cannot carry it.
    auto parse_type(std::string& raw_text) -> ast::TypeKind {
        auto finish = [&](ast::TypeKind k, std::string const& base) -> ast::TypeKind {
            if (match(tk::TokenKind::LBracket)) {
                match(tk::TokenKind::RBracket);
                raw_text = base + "[]";
                match(tk::TokenKind::Question); // nullable collection: T[]? ≡ T[]
                return ast::TypeKind::Collection;
            }
            raw_text = base;
            match(tk::TokenKind::Question); // C#-style nullable annotation: T? ≡ T
            return k;
        };
        switch (cur().kind) {
            case tk::TokenKind::KwString: advance(); return finish(ast::TypeKind::String, "string");
            case tk::TokenKind::KwInt:    advance(); return finish(ast::TypeKind::Int, "int");
            case tk::TokenKind::KwBool:   advance(); return finish(ast::TypeKind::Bool, "bool");
            case tk::TokenKind::KwFloat:  advance(); return finish(ast::TypeKind::Float, "double");
            case tk::TokenKind::KwDouble: advance(); return finish(ast::TypeKind::Double, "double");
            default:
                if (cur().kind == tk::TokenKind::Identifier) {
                    std::string id = expect(tk::TokenKind::Identifier).lexeme;
                    std::string lc = id;
                    for (auto& ch : lc) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (lc == "datetime" || lc == "date") return finish(ast::TypeKind::DateTime, "datetime");
                    if (lc == "any" || lc == "object" || lc == "inspectable") return finish(ast::TypeKind::Custom, "any");
                    // custom type name — carried verbatim for projections
                    return finish(ast::TypeKind::Custom, id);
                }
                error(std::format("expected type, got {}", tk::token_name(cur().kind)));
                advance();
                raw_text = "string";
                return ast::TypeKind::String;
        }
    }

    // Utility

    static auto strip_q(std::string_view s) -> std::string {
        if (s.size()>=2 && s.front()=='"' && s.back()=='"')
            return std::string(s.substr(1, s.size()-2));
        return std::string(s);
    }
};

// Convenience

[[nodiscard]] inline auto parse_tokens(std::vector<tk::Token> tokens,
                                       std::string file = "<input>",
                                       Parser::ContainerPred is_container = nullptr)
    -> std::pair<ast::SourceFile, diag::DiagnosticBag> {
    Parser p(std::move(tokens), std::move(file), std::move(is_container));
    auto sf = p.parse();
    return { std::move(sf), p.take_diagnostics() };
}

} // namespace winui::parse
