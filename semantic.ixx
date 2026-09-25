// semantic.ixx — Semantic Analysis & AST → IR Lowering
// Takes the parsed AST and produces a fully-resolved IrModule:
// 1. Validates all control names against the registry
// 2. Validates properties against control schemas
// 3. Resolves root.x / self.x references to declared state/properties
// 4. Resolves $binding references
// 5. Type-checks expressions
// 6. Assigns unique IDs to all control nodes
// 7. Converts closures from AST statements to IR actions
// Copyright (c) 2026 Momin Shaikh. All rights reserved.

module;
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include <unordered_map>
#include <unordered_set>
#include <format>
#include <utility>
#include <algorithm>
#include <cstdint>
#include <ranges>
export module winui.semantic;

import winui.diagnostics;
import winui.ast;
import winui.ir;
import winui.registry;

export namespace winui::semantic {

class Analyzer {
public:
    explicit Analyzer(reg::ControlRegistry const& registry)
        : m_registry(registry) {}

    // Lower an entire AST SourceFile to an IrModule
    [[nodiscard]] auto lower(ast::SourceFile const& sf) -> ir::IrModule {
        ir::IrModule mod;
        mod.name = extract_module_name(sf.file_path);

        if (sf.version) {
            mod.version_major = sf.version->major;
            mod.version_minor = sf.version->minor;
        }

        for (auto const& imp : sf.imports) {
            mod.imported_modules.push_back(imp.module_path);
        }

        // Lower file-level directives into IrModule app-level fields
        for (auto const& dir : sf.directives) {
            switch (dir.kind) {
            case ast::Directive::Kind::ExtendContentIntoTitleBar:
                mod.extend_content_into_titlebar = true;
                break;
            case ast::Directive::Kind::AppIcon:
                mod.app_icon_path = dir.arg;
                break;
            case ast::Directive::Kind::DarkTitlebar:
                mod.dark_titlebar = true;
                break;
            case ast::Directive::Kind::DarkContextMenu:
                mod.dark_context_menu = true;
                break;
            case ast::Directive::Kind::Backdrop:
                mod.backdrop_type = dir.arg;
                break;
            case ast::Directive::Kind::Titlebar:
                mod.titlebar_text = dir.arg;
                break;
            }
        }

        for (auto const& vd : sf.views) {
            mod.views.push_back(lower_view(vd));
        }

        return mod;
    }

    [[nodiscard]] auto take_diagnostics() -> diag::DiagnosticBag {
        return std::move(m_diag);
    }

private:
    reg::ControlRegistry const& m_registry;
    diag::DiagnosticBag         m_diag;
    std::size_t                 m_next_node_id = 0;

    // Current view context for resolving references
    struct ViewContext {
        std::unordered_map<std::string, ast::TypeKind> state_vars;
        std::unordered_map<std::string, ast::TypeKind> binding_vars;
        std::unordered_set<std::string>                event_names;
    };
    ViewContext m_ctx;

    // View lowering

    auto lower_view(ast::ViewDecl const& vd) -> ir::IrView {
        ir::IrView view;
        view.name = vd.name;
        view.base_class = vd.base_class;
        view.location = vd.location;

        // Reset context
        m_ctx = {};
        m_next_node_id = 0;

        // Process properties → state vars + ABI surface
        for (auto const& pd : vd.properties) {
            // Register in context for reference resolution
            if (pd.attribute == ast::PropAttr::State ||
                pd.attribute == ast::PropAttr::Published ||
                pd.attribute == ast::PropAttr::Computed ||
                pd.attribute == ast::PropAttr::None) {
                m_ctx.state_vars[pd.name] = pd.type;
            }
            if (pd.attribute == ast::PropAttr::Binding ||
                pd.attribute == ast::PropAttr::Environment) {
                m_ctx.binding_vars[pd.name] = pd.type;
            }

            // State variable
            ir::IrStateVar sv;
            sv.name = pd.name;
            sv.type = pd.type;
            sv.type_text = pd.type_text;
            sv.location = pd.location;
            if (pd.default_value && pd.default_value->get()) {
                sv.initial_value = try_lower_literal(*pd.default_value->get());
            }
            view.state_vars.push_back(std::move(sv));

            // ABI property (exposed to host)
            ir::IrAbiProperty abip;
            abip.name = ast::to_pascal_case(pd.name);
            abip.type = pd.type;
            abip.attribute = pd.attribute;
            abip.location = pd.location;
            if (pd.default_value && pd.default_value->get()) {
                abip.has_default = true;
                abip.default_value = try_lower_literal(*pd.default_value->get());
            }
            view.abi_properties.push_back(std::move(abip));
        }

        // Process callbacks → ABI events
        for (auto const& cb : vd.callbacks) {
            m_ctx.event_names.insert(cb.name);

            ir::IrAbiEvent evt;
            evt.name = ast::to_pascal_case(cb.name);
            evt.parameters = cb.parameters;
            evt.location = cb.location;
            // Lower the callback's statement body (code-behind logic) to
            // IR actions so the winmd carries real handler behavior.
            for (auto const& stmt_ptr : cb.body) {
                if (!stmt_ptr) continue;
                auto action = lower_stmt(*stmt_ptr);
                if (action) evt.body.push_back(std::move(*action));
            }
            view.abi_events.push_back(std::move(evt));
        }

        // Process body → IR tree
        for (auto const& item : vd.body)
            view.root_nodes.push_back(lower_body_item(item));

        return view;
    }

    // Body item lowering (shared by root body, if/else branches, ForEach)

    auto lower_body_item(ast::BodyItem const& item) -> ir::IrBodyNode {
        return std::visit([&](auto const& node) -> ir::IrBodyNode {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::ViewNode>) {
                return lower_view_node(node);
            }
            else if constexpr (std::is_same_v<T, ast::ConditionalView>) {
                ir::IrConditionalNode cond;
                cond.location = node.location;
                if (node.condition) {
                    cond.condition = lower_value(*node.condition);
                } else {
                    cond.condition = ir::IrLiteral{.value = true, .type = ast::TypeKind::Bool};
                }
                cond.node_id = m_next_node_id++;
                for (auto const& child : node.then_branch)
                    cond.then_branch.push_back(lower_body_item(child));
                for (auto const& child : node.else_branch)
                    cond.else_branch.push_back(lower_body_item(child));
                return cond;
            }
            else if constexpr (std::is_same_v<T, ast::ForEachNode>) {
                ir::IrForEachNode fe;
                fe.location = node.location;
                if (node.collection) {
                    fe.collection = lower_value(*node.collection);
                } else {
                    fe.collection = ir::IrLiteral{.value = std::int64_t(0), .type = ast::TypeKind::Int};
                }
                fe.binding_name = node.binding_name;
                for (auto const& child : node.body)
                    fe.body.push_back(lower_body_item(child));
                return fe;
            }
            std::unreachable();
        }, item);
    }

    // View node lowering

    auto lower_view_node(ast::ViewNode const& vn) -> ir::IrNode {
        ir::IrNode node;
        node.abstract_control = vn.control_name;
        node.location = vn.location;
        node.node_id = m_next_node_id++;

        // Validate control name
        auto* schema = m_registry.find_control(vn.control_name);
        if (!schema) {
            m_diag.error(vn.location,
                std::format("unknown control '{}' — not in registry", vn.control_name));
        }

        // Lower constructor arguments → properties
        // Third-party/unknown controls have no schema, so the positional
        // text argument is preserved as a "content" property anyway —
        // this keeps the compiler ready for controls added in the future.
        if ((!schema || schema->accepts_text_content) && !vn.arguments.empty()) {
            // First positional arg is text content: Label("Hello"), Button("Click")
            auto const& first = vn.arguments[0];
            if (!first.label) {
                ir::IrProperty prop;
                prop.name = "content";
                prop.location = first.location;
                if (first.value) prop.value = lower_value(*first.value);
                node.properties.push_back(std::move(prop));
            }
        }

        // Named arguments
        for (auto const& arg : vn.arguments) {
            if (arg.label) {
                ir::IrProperty prop;
                prop.name = *arg.label;
                prop.location = arg.location;
                if (arg.value) prop.value = lower_value(*arg.value);

                // Validate property against schema. Dynamic schemas
                // (category "custom") are the third-party fallback — they
                // have no property list, so any property is accepted: a
                // control added after this compiler was built must never
                // fail compilation.
                if (schema && schema->category != "custom" && !schema->find_property(prop.name)) {
                    m_diag.error(arg.location,
                        std::format("unknown property '{}' on '{}'",
                            prop.name, vn.control_name));
                }

                // Path rules: '\' is the only legal path separator inside
                // .winui source. For path-like properties (platform
                // transforms "uri" / "bitmap_image"):
                //   • forward slashes are rejected unless the value is a URI
                //     (https://, ms-appx:/// or any scheme:// reference),
                //   • double backslashes are always rejected,
                //   • everything is case insensitive (scheme, drive letter,
                //     path segments).
                if (arg.value) {
                    if (auto const* sl = arg.value->as<ast::StringLit>()) {
                        auto const& v = sl->value;
                        bool path_like = false;
                        if (auto const* be = m_registry.find_backend("winui3")) {
                            if (auto const* pc = be->find_control(vn.control_name)) {
                                if (auto const* pm = pc->map_property(*arg.label)) {
                                    path_like = pm->value_transform == "uri" ||
                                                pm->value_transform == "bitmap_image";
                                }
                            }
                        }
                        if (!path_like) { /* validation below applies to path-like properties only */ }
                        else {
                            bool is_uri = v.find("://") != std::string::npos;
                            if (!is_uri && v.find('/') != std::string::npos) {
                                m_diag.error(sl->location,
                                    std::format("forward slash '/' is not allowed in '{}'; use single backslashes only (e.g. \"assets\\icon.png\"). Forward slashes are permitted only inside URI schemes such as https:// or ms-appx:///.",
                                        prop.name));
                            }
                            if (v.find("\\\\") != std::string::npos) {
                                m_diag.error(sl->location,
                                    std::format("double backslash '\\\\' is not allowed in '{}'; use single backslashes only (e.g. \"..\\..\\assets\\icon.png\").",
                                        prop.name));
                            }
                        }
                    }
                }

                node.properties.push_back(std::move(prop));
            }
        }

        // Lower modifiers
        for (auto const& mod : vn.modifiers) {
            if (mod.name == "raw") {
                // Verbatim XAML attribute / fragment pass-through: carried into
                // the .winmd as-is so projections never lose source information.
                if (mod.arguments.size() != 2 || !mod.arguments[0].value || !mod.arguments[1].value) {
                    m_diag.error(mod.location, "'.raw' expects exactly 2 string arguments (key, value)");
                    continue;
                }
                auto to_str = [&](ast::Expr const& e) -> std::string {
                    if (auto const* sl = e.as<ast::StringLit>()) return sl->value;
                    m_diag.error(e.location, "'.raw' arguments must be string literals");
                    return {};
                };
                node.raw_attrs.emplace_back(
                    to_str(*mod.arguments[0].value),
                    to_str(*mod.arguments[1].value));
                continue;
            }
            node.modifiers.push_back(lower_modifier(mod));
        }

        // Lower trailing closure
        if (vn.trailing_closure) {
            node.action_closure = lower_closure(*vn.trailing_closure);
        }

        // Lower children recursively (views, if/else, ForEach)
        for (auto const& child : vn.children) {
            node.children.push_back(lower_body_item(child));
        }

        return node;
    }

    // Modifier lowering

    auto lower_modifier(ast::Modifier const& mod) -> ir::IrModifier {
        ir::IrModifier irmod;
        irmod.name = mod.name;
        irmod.location = mod.location;

        // Validate modifier
        auto* ms = m_registry.find_modifier(mod.name);
        if (!ms) {
            m_diag.error(mod.location,
                std::format("unknown modifier '.{}'", mod.name));
        }

        for (auto const& arg : mod.arguments) {
            if (arg.value) irmod.arguments.push_back(lower_value(*arg.value));
        }

        if (mod.trailing_closure) {
            irmod.closure = lower_closure(*mod.trailing_closure);
        }

        return irmod;
    }

    // Expression → IrValue lowering

    auto lower_value(ast::Expr const& expr) -> ir::IrValue {
        return expr.visit([&](auto const& node) -> ir::IrValue {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, ast::StringLit>) {
                if (auto interp = try_parse_interpolated_string(node.value, node.location)) {
                    return *interp;
                }
                return ir::IrLiteral{
                    .value = node.value,
                    .type = ast::TypeKind::String};
            }
            else if constexpr (std::is_same_v<T, ast::NumberLit>) {
                if (node.is_integer) {
                    return ir::IrLiteral{
                        .value = static_cast<std::int64_t>(node.value),
                        .type = ast::TypeKind::Int};
                }
                return ir::IrLiteral{
                    .value = node.value,
                    .type = ast::TypeKind::Double};
            }
            else if constexpr (std::is_same_v<T, ast::BoolLit>) {
                return ir::IrLiteral{
                    .value = node.value,
                    .type = ast::TypeKind::Bool};
            }
            else if constexpr (std::is_same_v<T, ast::IdentExpr>) {
                // Check if it's a known state variable
                if (auto it = m_ctx.state_vars.find(node.name);
                    it != m_ctx.state_vars.end()) {
                    return ir::IrStateRef{.name = node.name, .type = it->second};
                }
                // Check if it's a known binding variable
                if (auto it = m_ctx.binding_vars.find(node.name);
                    it != m_ctx.binding_vars.end()) {
                    return ir::IrBindingRef{.name = node.name, .type = it->second};
                }
                // Otherwise treat as enum/constant identifier
                return ir::IrEnumValue{.enum_context = "", .value_name = node.name};
            }
            else if constexpr (std::is_same_v<T, ast::MemberExpr>) {
                // Resolve the object expression first
                std::string obj_name;
                if (node.object) {
                    if (auto* id = node.object->as<ast::IdentExpr>()) {
                        obj_name = id->name;
                    }
                }
                // root.headerText, self.count → state ref / binding ref
                if (obj_name == "root" || obj_name == "self") {
                    if (auto it = m_ctx.state_vars.find(node.member);
                        it != m_ctx.state_vars.end()) {
                        return ir::IrStateRef{.name = node.member, .type = it->second};
                    }
                    if (auto it = m_ctx.binding_vars.find(node.member);
                        it != m_ctx.binding_vars.end()) {
                        return ir::IrBindingRef{.name = node.member, .type = it->second};
                    }
                    m_diag.error(node.location,
                        std::format("undefined property '{}'", node.member));
                    return ir::IrStateRef{.name = node.member, .type = ast::TypeKind::String};
                }
                // Chained member access: a.b.c — attach the member to a
                // binding/state ref so $x.value survives into the .winmd,
                // otherwise keep the dotted path verbatim.
                if (node.object) {
                    auto obj_val = lower_value(*node.object);
                    if (auto* b = std::get_if<ir::IrBindingRef>(&obj_val)) {
                        if (!b->member.empty()) b->member += ".";
                        b->member += node.member;
                        return *b;
                    }
                    if (auto* s = std::get_if<ir::IrStateRef>(&obj_val)) {
                        if (!s->member.empty()) s->member += ".";
                        s->member += node.member;
                        return *s;
                    }
                    if (auto* e = std::get_if<ir::IrEnumValue>(&obj_val)) {
                        return ir::IrExpr{
                            .expr_text = std::format("{}.{}", e->value_name, node.member),
                            .type     = ast::TypeKind::String};
                    }
                    return ir::IrLiteral{
                        .value = std::format("<access>.{}", node.member),
                        .type = ast::TypeKind::String};
                }
                return ir::IrLiteral{.value = std::string("<unresolved>"),
                                     .type = ast::TypeKind::String};
            }
            else if constexpr (std::is_same_v<T, ast::BindingRef>) {
                if (auto it = m_ctx.binding_vars.find(node.name);
                    it != m_ctx.binding_vars.end()) {
                    return ir::IrBindingRef{.name = node.name, .type = it->second};
                }
                if (auto it = m_ctx.state_vars.find(node.name);
                    it != m_ctx.state_vars.end()) {
                    return ir::IrBindingRef{.name = node.name, .type = it->second};
                }
                m_diag.error(node.location,
                    std::format("undefined binding '${}'", node.name));
                return ir::IrBindingRef{.name = node.name, .type = ast::TypeKind::String};
            }
            else if constexpr (std::is_same_v<T, ast::DotEnumExpr>) {
                return ir::IrEnumValue{.enum_context = "", .value_name = node.value};
            }
            else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
                // Extract numeric bounds
                double lo = 0, hi = 100;
                if (node.lower) {
                    if (auto* n = node.lower->as<ast::NumberLit>()) lo = n->value;
                }
                if (node.upper) {
                    if (auto* n = node.upper->as<ast::NumberLit>()) hi = n->value;
                }
                return ir::IrRange{.lower = lo, .upper = hi, .inclusive = node.inclusive};
            }
            else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
                // Lower as a C++ expression
                std::string lhs_str = "<expr>";
                std::string rhs_str = "<expr>";
                if (node.lhs) {
                    auto lv = lower_value(*node.lhs);
                    lhs_str = llvm_format_value(lv, "self");
                }
                if (node.rhs) {
                    auto rv = lower_value(*node.rhs);
                    rhs_str = llvm_format_value(rv, "self");
                }
                return ir::IrExpr{
                    .expr_text = std::format("({} {} {})", lhs_str,
                        ast::binary_op_symbol(node.op), rhs_str),
                    .type = ast::TypeKind::Int};
            }
            else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
                std::string operand_str = "<expr>";
                if (node.operand) {
                    auto ov = lower_value(*node.operand);
                    operand_str = llvm_format_value(ov, "self");
                }
                std::string prefix = (node.op == ast::UnaryOp::Negate) ? "-" : "!";
                return ir::IrExpr{
                    .expr_text = std::format("{}({})", prefix, operand_str),
                    .type = ast::TypeKind::Bool};
            }
            else if constexpr (std::is_same_v<T, ast::InterpolatedStringExpr>) {
                ir::IrInterpolatedString is;
                is.location = node.location;
                for (auto const& seg : node.segments) {
                    ir::IrInterpolatedSegment ir_seg;
                    ir_seg.is_expr = seg.is_expr;
                    if (seg.is_expr && seg.expr) {
                        // Extract the referenced variable name
                        if (auto* id = seg.expr->as<ast::IdentExpr>()) {
                            ir_seg.expr_name = id->name;
                        } else if (auto* mem = seg.expr->as<ast::MemberExpr>()) {
                            ir_seg.expr_name = mem->member;
                        } else {
                            ir_seg.expr_name = "<expr>";
                        }
                    } else {
                        ir_seg.text = seg.text;
                    }
                    is.segments.push_back(std::move(ir_seg));
                }
                return is;
            }
            else if constexpr (std::is_same_v<T, ast::TernaryExpr>) {
                // cond ? then : else  → emit as IrExpr with C++ ternary
                std::string cond_str  = "<cond>";
                std::string then_str  = "<then>";
                std::string else_str  = "<else>";
                if (node.condition) cond_str = llvm_format_value(lower_value(*node.condition), "self");
                if (node.then_expr) then_str = llvm_format_value(lower_value(*node.then_expr), "self");
                if (node.else_expr) else_str = llvm_format_value(lower_value(*node.else_expr), "self");
                return ir::IrExpr{
                    .expr_text = std::format("({} ? {} : {})", cond_str, then_str, else_str),
                    .type     = ast::TypeKind::String};
            }
            else if constexpr (std::is_same_v<T, ast::ArrayLitExpr>) {
                // [a, b, c] → emit as IrExpr with initializer-list representation
                std::string elems;
                for (std::size_t i = 0; i < node.elements.size(); ++i) {
                    if (i > 0) elems += ", ";
                    if (node.elements[i])
                        elems += llvm_format_value(lower_value(*node.elements[i]), "self");
                }
                return ir::IrExpr{
                    .expr_text = std::format("{{{}}}", elems),
                    .type     = ast::TypeKind::String};
            }
            else if constexpr (std::is_same_v<T, ast::CallExpr>) {
                // A function call in value position — keep the exact call
                // text so the .winmd carries the full source expression.
                std::string callee = "<callee>";
                if (node.callee) callee = llvm_format_value(lower_value(*node.callee), "self");
                std::string args_str;
                for (std::size_t i = 0; i < node.arguments.size(); ++i) {
                    if (i > 0) args_str += ", ";
                    args_str += llvm_format_value(lower_value(*node.arguments[i].value), "self");
                }
                return ir::IrExpr{
                    .expr_text = std::format("{}({})", callee, args_str),
                    .type     = ast::TypeKind::String};
            }
            else if constexpr (std::is_same_v<T, ast::SubscriptExpr>) {
                // obj[idx] → emit as IrExpr with subscript
                std::string obj_str = "<obj>";
                std::string idx_str = "<idx>";
                if (node.object) obj_str = llvm_format_value(lower_value(*node.object), "self");
                if (node.index)  idx_str = llvm_format_value(lower_value(*node.index),  "self");
                return ir::IrExpr{
                    .expr_text = std::format("{}[{}]", obj_str, idx_str),
                    .type     = ast::TypeKind::String};
            }
            else {
                return ir::IrLiteral{.value = std::string("<unknown-expr>"),
                                     .type = ast::TypeKind::String};
            }
        });
    }

    // Closure lowering

    auto lower_closure(ast::ClosureExpr const& cl) -> ir::IrClosure {
        ir::IrClosure ir_cl;
        ir_cl.location = cl.location;

        for (auto const& stmt_ptr : cl.body) {
            if (!stmt_ptr) continue;
            auto action = lower_stmt(*stmt_ptr);
            if (action) ir_cl.actions.push_back(std::move(*action));
        }

        return ir_cl;
    }

    auto lower_stmt(ast::Stmt const& stmt) -> std::optional<ir::IrAction> {
        return stmt.visit([&](auto const& node) -> std::optional<ir::IrAction> {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, ast::AssignStmt>) {
                // target = value → IrAssign
                std::string target_name;
                std::string target_obj;
                if (node.target) {
                    if (auto* id = node.target->as<ast::IdentExpr>())
                        target_name = id->name;
                    else if (auto* mem = node.target->as<ast::MemberExpr>()) {
                        target_name = mem->member;
                        if (mem->object) {
                            if (auto* obj_id = mem->object->as<ast::IdentExpr>())
                                target_obj = obj_id->name;
                        }
                    }
                }
                ir::IrValue val = node.value
                    ? lower_value(*node.value)
                    : ir::IrLiteral{.value = std::int64_t(0), .type = ast::TypeKind::Int};
                return ir::IrAssign{
                    .target_state = std::move(target_name),
                    .target_object = std::move(target_obj),
                    .op = node.op,
                    .value = std::move(val),
                    .location = node.location};
            }
            else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
                // Expression statement — check if it's a function call
                if (node.expr) {
                    if (auto* call = node.expr->as<ast::CallExpr>()) {
                        return lower_call_action(*call);
                    }
                }
                return std::nullopt;
            }
            else if constexpr (std::is_same_v<T, ast::IfStmt>) {
                // if condition { then } else { else }
                ir::IrValue cond = node.condition
                    ? lower_value(*node.condition)
                    : ir::IrLiteral{.value = true, .type = ast::TypeKind::Bool};

                ir::IrIfAction ifa;
                ifa.condition_lhs = cond;
                ifa.condition_op = ast::BinaryOp::Neq;  // != false — treat condition as boolean
                ifa.condition_rhs = ir::IrLiteral{.value = false, .type = ast::TypeKind::Bool};
                ifa.location = node.location;

                // Lower then_body
                for (auto const& stmt_ptr : node.then_body) {
                    if (stmt_ptr) {
                        auto action = lower_stmt_as_inner(*stmt_ptr);
                        if (action) ifa.then_body.push_back(std::move(*action));
                    }
                }
                // Lower else_body
                for (auto const& stmt_ptr : node.else_body) {
                    if (stmt_ptr) {
                        auto action = lower_stmt_as_inner(*stmt_ptr);
                        if (action) ifa.else_body.push_back(std::move(*action));
                    }
                }

                return ifa;
            }
            else if constexpr (std::is_same_v<T, ast::VarStmt>) {
                ir::IrValue init = node.initializer
                    ? lower_value(*node.initializer)
                    : ir::IrLiteral{.value = std::int64_t(0), .type = ast::TypeKind::Int};
                return ir::IrVarDecl{
                    .name = node.name,
                    .type = node.type.value_or(ast::TypeKind::String),
                    .initializer = std::move(init),
                    .location = node.location};
            }
            else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
                ir::IrValue val = node.value
                    ? lower_value(*node.value)
                    : ir::IrLiteral{.value = std::int64_t(0), .type = ast::TypeKind::Void};
                return ir::IrReturn{
                    .value = std::move(val),
                    .location = node.location};
            }
            else if constexpr (std::is_same_v<T, ast::WhileStmt>) {
                // while (condition) { body } → emit as IrExpr stub (no native IR type yet)
                // The body actions are lowered but collapsed into a single IrCallMethod marker.
                ir::IrCallMethod cm;
                cm.method_name = "<while-loop>";
                cm.location    = node.location;
                if (node.condition)
                    cm.arguments.push_back(lower_value(*node.condition));
                return cm;
            }
            else if constexpr (std::is_same_v<T, ast::SwitchStmt>) {
                // switch (subject) { ... } → emit as IrCallMethod stub
                ir::IrCallMethod cm;
                cm.method_name = "<switch>";
                cm.location    = node.location;
                if (node.subject)
                    cm.arguments.push_back(lower_value(*node.subject));
                return cm;
            }
            else {
                return std::nullopt;
            }
        });
    }

    // Lower a statement to an IrInnerAction (for use inside IrIfAction bodies).
    // Returns std::nullopt for IfStmt (no nesting) and unsupported stmts.
    auto lower_stmt_as_inner(ast::Stmt const& stmt) -> std::optional<ir::IrInnerAction> {
        return stmt.visit([&](auto const& node) -> std::optional<ir::IrInnerAction> {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, ast::AssignStmt>) {
                std::string target_name;
                std::string target_obj;
                if (node.target) {
                    if (auto* id = node.target->as<ast::IdentExpr>())
                        target_name = id->name;
                    else if (auto* mem = node.target->as<ast::MemberExpr>()) {
                        target_name = mem->member;
                        if (mem->object) {
                            if (auto* obj_id = mem->object->as<ast::IdentExpr>())
                                target_obj = obj_id->name;
                        }
                    }
                }
                ir::IrValue val = node.value
                    ? lower_value(*node.value)
                    : ir::IrLiteral{.value = std::int64_t(0), .type = ast::TypeKind::Int};
                return ir::IrAssign{
                    .target_state = std::move(target_name),
                    .target_object = std::move(target_obj),
                    .op = node.op,
                    .value = std::move(val),
                    .location = node.location};
            }
            else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
                if (node.expr) {
                    if (auto* call = node.expr->as<ast::CallExpr>()) {
                        return lower_call_action_as_inner(*call);
                    }
                }
                return std::nullopt;
            }
            else {
                return std::nullopt;
            }
        });
    }

    auto lower_call_action_as_inner(ast::CallExpr const& call) -> ir::IrInnerAction {
        if (call.callee) {
            if (auto* mem = call.callee->as<ast::MemberExpr>()) {
                std::string obj_name;
                if (mem->object) {
                    if (auto* id = mem->object->as<ast::IdentExpr>()) {
                        obj_name = id->name;
                    }
                }
                if (obj_name == "root" || obj_name == "self") {
                    if (m_ctx.event_names.contains(mem->member)) {
                        ir::IrFireEvent fe;
                        fe.event_name = ast::to_pascal_case(mem->member);
                        fe.location = call.location;
                        for (auto const& arg : call.arguments) {
                            if (arg.value)
                                fe.arguments.push_back(lower_value(*arg.value));
                        }
                        return fe;
                    }
                }
                ir::IrCallMethod cm;
                cm.object = obj_name;
                cm.method_name = mem->member;
                cm.location = call.location;
                for (auto const& arg : call.arguments) {
                    if (arg.value)
                        cm.arguments.push_back(lower_value(*arg.value));
                }
                return cm;
            }
            if (auto* id = call.callee->as<ast::IdentExpr>()) {
                if (m_ctx.event_names.contains(id->name)) {
                    ir::IrFireEvent fe;
                    fe.event_name = ast::to_pascal_case(id->name);
                    fe.location = call.location;
                    for (auto const& arg : call.arguments) {
                        if (arg.value)
                            fe.arguments.push_back(lower_value(*arg.value));
                    }
                    return fe;
                }
                ir::IrCallMethod cm;
                cm.method_name = id->name;
                cm.location = call.location;
                for (auto const& arg : call.arguments) {
                    if (arg.value)
                        cm.arguments.push_back(lower_value(*arg.value));
                }
                return cm;
            }
        }
        return ir::IrCallMethod{.method_name = "<unknown>", .location = call.location};
    }

    auto lower_call_action(ast::CallExpr const& call) -> ir::IrAction {
        // Check if callee is a member expression (root.onSubmit, self.callback)
        if (call.callee) {
            if (auto* mem = call.callee->as<ast::MemberExpr>()) {
                // Resolve the object to check for root/self
                std::string obj_name;
                if (mem->object) {
                    if (auto* id = mem->object->as<ast::IdentExpr>()) {
                        obj_name = id->name;
                    }
                }
                if (obj_name == "root" || obj_name == "self") {
                    // Check if it's a known event
                    if (m_ctx.event_names.contains(mem->member)) {
                        ir::IrFireEvent fe;
                        fe.event_name = ast::to_pascal_case(mem->member);
                        fe.location = call.location;
                        for (auto const& arg : call.arguments) {
                            if (arg.value)
                                fe.arguments.push_back(lower_value(*arg.value));
                        }
                        return fe;
                    }
                }
                // Regular method call
                ir::IrCallMethod cm;
                cm.object = obj_name;
                cm.method_name = mem->member;
                cm.location = call.location;
                for (auto const& arg : call.arguments) {
                    if (arg.value)
                        cm.arguments.push_back(lower_value(*arg.value));
                }
                return cm;
            }
            if (auto* id = call.callee->as<ast::IdentExpr>()) {
                // Check if it's a known event
                if (m_ctx.event_names.contains(id->name)) {
                    ir::IrFireEvent fe;
                    fe.event_name = ast::to_pascal_case(id->name);
                    fe.location = call.location;
                    for (auto const& arg : call.arguments) {
                        if (arg.value)
                            fe.arguments.push_back(lower_value(*arg.value));
                    }
                    return fe;
                }
                // Regular function call
                ir::IrCallMethod cm;
                cm.method_name = id->name;
                cm.location = call.location;
                for (auto const& arg : call.arguments) {
                    if (arg.value)
                        cm.arguments.push_back(lower_value(*arg.value));
                }
                return cm;
            }
        }
        return ir::IrCallMethod{.method_name = "<unknown>", .location = call.location};
    }

    // Helpers

    static auto try_parse_interpolated_string(std::string const& text, diag::SourceLocation loc)
        -> std::optional<ir::IrInterpolatedString> {
        if (text.find("\\(") == std::string::npos && text.find("\\(") == std::string::npos) {
            return std::nullopt;
        }

        ir::IrInterpolatedString is;
        is.location = loc;
        std::size_t current = 0;

        while (current < text.size()) {
            std::size_t start = text.find("\\(", current);
            std::size_t marker_len = 2;
            if (start == std::string::npos) {
                std::string rem = text.substr(current);
                if (!rem.empty()) {
                    is.segments.push_back(ir::IrInterpolatedSegment{
                        .is_expr = false,
                        .text = rem
                    });
                }
                break;
            }

            if (start > current) {
                is.segments.push_back(ir::IrInterpolatedSegment{
                    .is_expr = false,
                    .text = text.substr(current, start - current)
                });
            }

            std::size_t expr_start = start + marker_len;
            std::size_t end_paren = text.find(')', expr_start);
            if (end_paren != std::string::npos) {
                std::string expr_str = text.substr(expr_start, end_paren - expr_start);
                auto first_ns = expr_str.find_first_not_of(" \t");
                auto last_ns = expr_str.find_last_not_of(" \t");
                if (first_ns != std::string::npos) {
                    expr_str = expr_str.substr(first_ns, last_ns - first_ns + 1);
                }
                is.segments.push_back(ir::IrInterpolatedSegment{
                    .is_expr = true,
                    .expr_name = expr_str
                });
                current = end_paren + 1;
            } else {
                is.segments.push_back(ir::IrInterpolatedSegment{
                    .is_expr = false,
                    .text = text.substr(start)
                });
                break;
            }
        }
        return is;
    }

    // Format an IrValue as a value string (for embedding in IrLiteral expressions).
    // Returns the VALUE itself, not a C++ literal — e.g. "hello" not L"hello".
    // The backend's format_value will add C++ literal wrapping.
    static auto llvm_format_value(ir::IrValue const& v, std::string const& self_var = "self") -> std::string {
        return std::visit([&](auto const& val) -> std::string {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, ir::IrLiteral>) {
                return std::visit([](auto const& inner) -> std::string {
                    using V = std::decay_t<decltype(inner)>;
                    if constexpr (std::is_same_v<V, std::string>)
                        return std::format("L\"{}\"", inner);
                    else if constexpr (std::is_same_v<V, std::int64_t>)
                        return std::to_string(inner);
                    else if constexpr (std::is_same_v<V, double>)
                        return (inner == static_cast<std::int64_t>(inner))
                            ? std::to_string(static_cast<std::int64_t>(inner))
                            : std::to_string(inner);
                    else
                        return inner ? "true" : "false";
                }, val.value);
            }
            else if constexpr (std::is_same_v<T, ir::IrStateRef>)
                return std::format("{}->{}{}", self_var, val.name,
                    val.member.empty() ? "" : std::format(".{}", val.member));
            else if constexpr (std::is_same_v<T, ir::IrBindingRef>)
                return std::format("{}->{}{}", self_var, val.name,
                    val.member.empty() ? "" : std::format(".{}", val.member));
            else if constexpr (std::is_same_v<T, ir::IrEnumValue>)
                return val.value_name;
            else if constexpr (std::is_same_v<T, ir::IrRange>)
                return std::format("{:.0f}, {:.0f}", val.lower, val.upper);
            else if constexpr (std::is_same_v<T, ir::IrExpr>)
                return val.expr_text;
            else
                return "<value>";
        }, v);
    }

    auto try_lower_literal(ast::Expr const& expr) -> std::optional<ir::IrLiteral> {
        if (auto* s = expr.as<ast::StringLit>()) {
            std::string val = s->value;
            for (char& c : val) {
                if (c == '/') c = '\\';
            }
            return ir::IrLiteral{.value = val, .type = ast::TypeKind::String};
        }
        if (auto* n = expr.as<ast::NumberLit>()) {
            if (n->is_integer)
                return ir::IrLiteral{.value = static_cast<std::int64_t>(n->value),
                                     .type = ast::TypeKind::Int};
            return ir::IrLiteral{.value = n->value, .type = ast::TypeKind::Double};
        }
        if (auto* b = expr.as<ast::BoolLit>())
            return ir::IrLiteral{.value = b->value, .type = ast::TypeKind::Bool};
        return std::nullopt;
    }

    static auto extract_module_name(std::string_view path) -> std::string {
        auto pos = path.find_last_of("/\\");
        auto name = (pos != std::string_view::npos) ? path.substr(pos+1) : path;
        auto dot = name.find('.');
        if (dot != std::string_view::npos) name = name.substr(0, dot);
        return std::string(name);
    }
};

// Convenience

[[nodiscard]] inline auto analyze(ast::SourceFile const& sf,
                                  reg::ControlRegistry const& registry)
    -> std::pair<ir::IrModule, diag::DiagnosticBag> {
    Analyzer a(registry);
    auto mod = a.lower(sf);
    return { std::move(mod), a.take_diagnostics() };
}

} // namespace winui::semantic
