// registry.ixx — Data-Driven Control Registry

// Maps abstract control names (Column, Label, Button, ...) to their schemas

// (properties, events, capabilities) and platform-specific mappings.



// The registry is the KEY to future-proofing:

// - Abstract controls are defined ONCE, shared by all backends

// - Platform mappings are LOADED PER-BACKEND, not hardcoded

// - New controls can be added by editing JSON, not recompiling



// Built-in abstract controls (framework-agnostic):

// Legacy:   Column, Row, Stack, Grid, Scroll, Wrap, Spacer, Divider, Card,

// Label, Icon, Image, Badge, Button, Input, TextArea, Toggle,

// Slider, Picker, Checkbox, TabView, NavView, Link, Progress,

// Dialog, InfoBar, Tooltip

// Real WinUI 3 (matches WinUI Gallery's ControlInfoData.json):

// AppBarButton, CommandBar, MenuBar, TreeView, FlipView,

// ItemsRepeater, ListView, CalendarDatePicker, CalendarView,

// DatePicker, TimePicker, DropDownButton, SplitButton, ComboBox,

// ColorPicker, RatingControl, ToggleSwitch, InfoBadge, ProgressBar,

// ProgressRing, ToolTip, ContentDialog, Flyout, Popup, TeachingTip,

// AnnotatedScrollBar, ScrollView, ScrollViewer, SemanticZoom,

// Border, Canvas, Expander, RelativePanel, SplitView, Viewbox,

// NavigationView, BreadcrumbBar, Pivot, SelectorBar, AnimatedVisualPlayer,

// CaptureElement, MapControl, MediaPlayerElement, PersonPicture,

// WebView2, AutoSuggestBox, NumberBox, RichTextBlock, TextBlock,

// TextBox, Line, Rectangle, Ellipse, Path, SymbolIcon, BitmapIcon,

// PathIcon, AnimatedIcon, ParallaxView, ... (see register_builtin_controls)



// serialize_catalog() emits the full registry + winui3 backend as JSON into

// the WinMD (WinUIControlCatalogAttribute) so projections can discover every

// registered control without hardcoding anything.
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

#include <cstdint>

#include <functional>

#include <algorithm>

export module winui.registry;



import winui.ast;

import winui.diagnostics;



export namespace winui::reg {



// §1  Property and Event Schemas



// Schema for a single property on an abstract control

struct PropertySchema {

    std::string     name;       // "spacing", "text", "fontSize"

    ast::TypeKind   type;       // expected type

    bool            required = false;

    std::string     default_value;  // string representation of default

    std::string     description;

};



// Schema for an event on an abstract control

struct EventSchema {

    std::string                name;      // "onClick", "onChanged"

    std::vector<ast::TypeKind> param_types;

    std::string                description;

};



// Complete schema for an abstract control

struct ControlSchema {

    std::string                  abstract_name;      // "Column", "Label"

    std::vector<PropertySchema>  properties;

    std::vector<EventSchema>     events;

    bool                         accepts_children = false;

    bool                         accepts_text_content = false; // Label("text")

    std::string                  composition_kind;   // "panel", "content", "items", "items_panel"

    std::string                  description;

    std::string                  category;           // "layout", "input", etc.



    [[nodiscard]] auto find_property(std::string_view name) const

        -> PropertySchema const* {

        for (auto const& p : properties)

            if (p.name == name) return &p;

        return nullptr;

    }



    [[nodiscard]] auto find_event(std::string_view name) const

        -> EventSchema const* {

        for (auto const& e : events)

            if (e.name == name) return &e;

        return nullptr;

    }

};



// §2  Platform Mapping (backend-specific)



// Maps an abstract property to a platform-specific API call

struct PropertyMapping {

    std::string abstract_name;     // "spacing"

    std::string platform_setter;   // "Spacing" (for method call)

    std::string value_transform;   // optional transform: "double", "box", ""

};



// Maps an abstract event to a platform-specific event

struct EventMapping {

    std::string abstract_name;     // "onClick"

    std::string platform_event;    // "Click"

};



// Maps an abstract control to a platform-specific type

struct PlatformControl {

    std::string                    abstract_name;       // "Column"

    std::string                    platform_type;       // "StackPanel"

    std::string                    platform_namespace;   // "Microsoft.UI.Xaml.Controls"

    std::vector<PropertyMapping>   property_mappings;

    std::vector<EventMapping>      event_mappings;

    std::vector<std::pair<std::string,std::string>> fixed_properties; // auto-set props

    // Control type: "xaml" = XAML TemplatedControl (ResourceDictionary),
    //               "cpp"  = pure C++/WinRT component (direct instantiation).
    // The .winmd is projection-agnostic; downstream projections decide.
    std::string                    control_type;        // "xaml" | "cpp"
    std::string                    template_resource_key;// x:Key in ResourceDictionary
    std::string                    resource_dictionary;  // relative path to .xaml



    [[nodiscard]] auto map_property(std::string_view abstract) const

        -> PropertyMapping const* {

        for (auto const& m : property_mappings)

            if (m.abstract_name == abstract) return &m;

        return nullptr;

    }



    [[nodiscard]] auto map_event(std::string_view abstract) const

        -> EventMapping const* {

        for (auto const& m : event_mappings)

            if (m.abstract_name == abstract) return &m;

        return nullptr;

    }

};



// A set of platform mappings for one backend

struct PlatformBackend {

    std::string                    name;            // "winui", "qt", "web"

    std::string                    description;

    std::vector<PlatformControl>   controls;



    [[nodiscard]] auto find_control(std::string_view abstract) const

        -> PlatformControl const* {

        for (auto const& c : controls)

            if (c.abstract_name == abstract) return &c;

        return nullptr;

    }

};



// §3  Modifier Schema (abstract modifiers, shared by all backends)



struct ModifierSchema {

    std::string              name;         // "fontSize", "padding", "bold"

    std::vector<ast::TypeKind> param_types; // expected argument types

    bool                     is_event = false; // .onClick { } vs .fontSize(24)

    std::string              description;

};



// §4  Control Registry (the central database)



class ControlRegistry {

public:

    // Abstract controls



    auto register_control(ControlSchema schema) -> void {

        m_controls[schema.abstract_name] = std::move(schema);

    }



    [[nodiscard]] auto find_control(std::string_view name) const

        -> ControlSchema const* {

        auto it = m_controls.find(std::string(name));

        if (it != m_controls.end()) return &it->second;



        // Dynamic fallback for any custom WinUI 3 control provided by third parties / users

        static thread_local std::unordered_map<std::string, ControlSchema> s_dynamic_custom_controls;

        std::string sname(name);

        auto cit = s_dynamic_custom_controls.find(sname);

        if (cit != s_dynamic_custom_controls.end()) {

            return &cit->second;

        }



        ControlSchema schema;

        schema.abstract_name = sname;

        schema.accepts_children = true;

        schema.accepts_text_content = true;

        schema.category = "custom";

        schema.description = "Custom user-provided WinUI 3 control";



        auto [inserted_it, _] = s_dynamic_custom_controls.emplace(sname, std::move(schema));

        return &inserted_it->second;

    }



    // Catalog serialization

    // Produces a compact JSON document describing every registered

    // abstract control AND its compiled-in winui3 platform mapping.

    // Emitted into the WinMD as WinUIControlCatalogAttribute so language

    // projections (C++/WinRT, C#, Rust, Python, ...) can discover the full

    // control surface — including which platform type each abstract name

    // maps to — without any hardcoded knowledge of winuic internals.

    [[nodiscard]] auto serialize_catalog() const -> std::string {

        auto escape = [](std::string_view s) -> std::string {

            std::string out;

            out.reserve(s.size());

            for (char c : s) {

                switch (c) {

                    case '"':  out += "\\\""; break;

                    case '\\': out += "\\\\"; break;

                    case '\n': out += "\\n";  break;

                    case '\r': out += "\\r";  break;

                    case '\t': out += "\\t";  break;

                    default:   out += c;      break;

                }

            }

            return out;

        };



        auto const* be = find_backend("winui3");



        std::vector<std::string> names;

        names.reserve(m_controls.size());

        for (auto const& [name, _] : m_controls) names.push_back(name);

        std::sort(names.begin(), names.end());



        std::string json;

        json.reserve(64 * 1024);

        json += "{\"version\":1,\"backend\":\"winui3\",\"controls\":[";

        for (size_t i = 0; i < names.size(); ++i) {

            auto const& cs = m_controls.at(names[i]);

            auto const* pc = be ? be->find_control(names[i]) : nullptr;

            if (i > 0) json += ",";

            json += "{\"abstract_name\":\"" + escape(names[i]) + "\"";

            json += ",\"category\":\"" + escape(cs.category) + "\"";

            json += ",\"accepts_children\":" + std::string(cs.accepts_children ? "true" : "false");

            json += ",\"accepts_text_content\":" + std::string(cs.accepts_text_content ? "true" : "false");

            // composition_kind: explicit or inferred from accepts_children

            std::string ck = cs.composition_kind;

            if (ck.empty() && cs.accepts_children) ck = "content";

            if (!ck.empty())

                json += ",\"composition_kind\":\"" + escape(ck) + "\"";

            if (!cs.description.empty())

                json += ",\"description\":\"" + escape(cs.description) + "\"";

            if (pc) {

                json += ",\"platform_type\":\"" + escape(pc->platform_type) + "\"";

                json += ",\"platform_namespace\":\"" + escape(pc->platform_namespace) + "\"";

                if (!pc->fixed_properties.empty()) {

                    json += ",\"fixed_properties\":[";

                    for (size_t j = 0; j < pc->fixed_properties.size(); ++j) {

                        auto const& [fn, fv] = pc->fixed_properties[j];

                        if (j > 0) json += ",";

                        json += "{\"name\":\"" + escape(fn) + "\",\"value\":\"" + escape(fv) + "\"}";

                    }

                    json += "]";

                }

            }

            {

                // Synthesize the "content" property for text-capable schemas

                // (Label("x"), MenuFlyoutItem("x"), ...): the DSL parser lowers

                // positional text to prop "content", so the projection needs a

                // catalog entry to map it onto the platform setter.

                std::vector<PropertySchema> props = cs.properties;

                if (cs.accepts_text_content) {

                    bool has_content = false;

                    for (auto const& p : props) if (p.name == "content") { has_content = true; break; }

                    if (!has_content) props.push_back({ .name = "content", .type = ast::TypeKind::String });

                }

                json += ",\"properties\":[";

                for (size_t j = 0; j < props.size(); ++j) {

                    auto const& p = props[j];

                    if (j > 0) json += ",";

                    json += "{\"name\":\"" + escape(p.name) + "\",\"type\":\"" + std::string(ast::type_name(p.type)) + "\"";

                    if (pc) {

                        if (auto const* pm = pc->map_property(p.name); pm)

                            json += ",\"platform_setter\":\"" + escape(pm->platform_setter)

                                  + "\",\"transform\":\"" + escape(pm->value_transform) + "\"";

                    }

                    json += "}";

                }

                json += "]";

            }

            if (!cs.events.empty()) {

                json += ",\"events\":[";

                for (size_t j = 0; j < cs.events.size(); ++j) {

                    auto const& ev = cs.events[j];

                    if (j > 0) json += ",";

                    json += "{\"name\":\"" + escape(ev.name) + "\"";

                    if (pc) {

                        if (auto const* em = pc->map_event(ev.name); em)

                            json += ",\"platform_event\":\"" + escape(em->platform_event) + "\"";

                    }

                    json += "}";

                }

                json += "]";

            }

            // Emit control type fields for projection-agnostic XAML/C++ distinction
            if (pc) {
                if (!pc->control_type.empty() && pc->control_type != "cpp")
                    json += ",\"type\":\"" + escape(pc->control_type) + "\"";
                if (!pc->template_resource_key.empty())
                    json += ",\"template_resource_key\":\"" + escape(pc->template_resource_key) + "\"";
                if (!pc->resource_dictionary.empty())
                    json += ",\"resource_dictionary\":\"" + escape(pc->resource_dictionary) + "\"";
            }

            json += "}";

        }

        json += "]}";

        return json;

    }



    // Modifiers



    auto register_modifier(ModifierSchema schema) -> void {

        m_modifiers[schema.name] = std::move(schema);

    }



    [[nodiscard]] auto find_modifier(std::string_view name) const

        -> ModifierSchema const* {

        auto it = m_modifiers.find(std::string(name));

        if (it != m_modifiers.end()) return &it->second;



        // Dynamic fallback for any custom modifier

        static thread_local std::unordered_map<std::string, ModifierSchema> s_dynamic_custom_modifiers;

        std::string sname(name);

        auto mit = s_dynamic_custom_modifiers.find(sname);

        if (mit != s_dynamic_custom_modifiers.end()) {

            return &mit->second;

        }



        ModifierSchema schema;

        schema.name = sname;

        schema.description = "Custom user-provided modifier";



        auto [inserted_it, _] = s_dynamic_custom_modifiers.emplace(sname, std::move(schema));

        return &inserted_it->second;

    }



    // Platform backends



    auto register_backend(PlatformBackend backend) -> void {

        m_backends[backend.name] = std::move(backend);

    }



    [[nodiscard]] auto find_backend(std::string_view name) const

        -> PlatformBackend const* {

        auto it = m_backends.find(std::string(name));

        return it != m_backends.end() ? &it->second : nullptr;

    }



    [[nodiscard]] auto find_backend_mut(std::string_view name)

        -> PlatformBackend* {

        auto it = m_backends.find(std::string(name));

        return it != m_backends.end() ? &it->second : nullptr;

    }



    // Factory: create registry with all built-in controls



    [[nodiscard]] static auto create_default() -> ControlRegistry {

        ControlRegistry reg;

        register_builtin_controls(reg);

        register_builtin_modifiers(reg);

        register_winui3_backend(reg);

        return reg;

    }



    // JSON backend loader (runtime extensibility)

    // Loads a backend descriptor from a minimal JSON string. This is the

    // NO-RECOMPILE extension path: third-party and future WinUI 3 controls

    // are registered at runtime via `-c / --control <file>.json`, without

    // touching winuic.exe sources.

    //

    // JSON schema (fully language-projection agnostic — platform types and

    // namespaces are WinRT API names, never language-specific syntax):

    //   { "backend": "winui3", "controls": [

    //       { "abstract_name": "Column",

    //         "platform_type": "StackPanel",

    //         "platform_namespace": "Microsoft.UI.Xaml.Controls",

    //         "accepts_children": true,        // optional (default false)

    //         "accepts_text_content": false,   // optional (default false)

    //         "category": "custom",            // optional (default "custom")

    //         "fixed_properties": [{"name":"Orientation","value":"..."}],

    //         "properties": [{"abstract_name":"spacing","platform_setter":"Spacing","transform":"double","type":"double"}],

    //         "events":     [{"abstract_name":"onClick","platform_event":"Click"}]

    //       }, ...

    //   ]}

    //

    // When "backend" matches an already-registered backend (e.g. "winui3"),

    // the new controls are MERGED in (replace-or-append per abstract_name),

    // so third-party extensions never wipe the built-in mappings. Every new

    // abstract name is also registered as a ControlSchema, which makes it

    // visible to the parser container predicate and to the embedded

    // WinUIControlCatalogAttribute.

    //

    // Returns false if the JSON is malformed or missing required fields.

    [[nodiscard]] static auto load_backend_from_json(

        ControlRegistry& reg,

        std::string_view json) -> bool

    {

        // Tiny hand-rolled JSON reader

        // We intentionally avoid external JSON libraries to keep the

        // compiler self-contained and zero-dependency.  The format is

        // predictable (generated by humans or tooling following the schema

        // above), so a straightforward state-machine parser is sufficient.



        auto skip_ws = [](std::string_view s, std::size_t p) -> std::size_t {

            while (p < s.size() && (s[p]==' '||s[p]=='\t'||s[p]=='\n'||s[p]=='\r')) ++p;

            return p;

        };



        // Read a JSON string value at position p (p should point at opening '"').

        // Returns {value, next_pos} or {"", npos} on error.

        auto read_string = [](std::string_view s, std::size_t p)

            -> std::pair<std::string, std::size_t>

        {

            if (p >= s.size() || s[p] != '"') return {"", std::string_view::npos};

            ++p; // skip opening '"'

            std::string out;

            while (p < s.size() && s[p] != '"') {

                if (s[p] == '\\' && p+1 < s.size()) {

                    ++p;

                    switch (s[p]) {

                        case '"':  out += '"';  break;

                        case '\\': out += '\\'; break;

                        case '/':  out += '/';  break;

                        case 'n':  out += '\n'; break;

                        case 'r':  out += '\r'; break;

                        case 't':  out += '\t'; break;

                        default:   out += s[p]; break;

                    }

                } else {

                    out += s[p];

                }

                ++p;

            }

            if (p < s.size()) ++p; // skip closing '"'

            return {out, p};

        };



        // Find value for a key inside the current JSON object scope.

        // Returns {raw_value_token, pos_after_value} or {"", npos}.

        // Only works for string values and nested { } / [ ] blocks.

        auto find_key = [&](std::string_view s, std::size_t obj_start, std::string_view key)

            -> std::pair<std::string, std::size_t>

        {

            std::size_t p = obj_start;

            int depth = 0;

            while (p < s.size()) {

                p = skip_ws(s, p);

                if (p >= s.size()) break;

                char c = s[p];

                if (c == '{' || c == '[') { ++depth; ++p; continue; }

                if (c == '}' || c == ']') {

                    if (depth == 0) break;

                    --depth; ++p; continue;

                }

                if (c == '"' && depth == 0) {

                    auto [kname, kend] = read_string(s, p);

                    if (kend == std::string_view::npos) break;

                    p = skip_ws(s, kend);

                    if (p < s.size() && s[p] == ':') ++p;

                    p = skip_ws(s, p);

                    if (kname == key) {

                        // Return position of value

                        if (p < s.size() && s[p] == '"') {

                            auto [val, vend] = read_string(s, p);

                            return {val, vend};

                        }

                        // Return the bracket/brace block as raw

                        std::size_t start = p;

                        return {std::string(s.substr(start, 1)), start};

                    }

                    // Skip past value

                    if (p < s.size()) {

                        if (s[p] == '"') { read_string(s, p); auto [_,vend]=read_string(s,p); p=vend; }

                        else if (s[p]=='{' || s[p]=='[') {

                            int d2=0;

                            while(p<s.size()){

                                if(s[p]=='{'||s[p]=='[')++d2;

                                else if(s[p]=='}'||s[p]==']'){--d2;if(d2==0){++p;break;}}

                                ++p;

                            }

                        } else {

                            while(p<s.size()&&s[p]!=','&&s[p]!='}'&&s[p]!=']')++p;

                        }

                    }

                } else {

                    ++p;

                }

            }

            return {"", std::string_view::npos};

        };



        // Iterate over JSON array elements starting at '[' position.

        // Calls callback(element_start_pos, element_end_pos) for each object element.

        auto for_each_array_obj = [&](std::string_view s, std::size_t arr_pos,

            std::function<void(std::size_t, std::size_t)> cb) -> void

        {

            std::size_t p = arr_pos;

            p = skip_ws(s, p);

            if (p >= s.size() || s[p] != '[') return;

            ++p; // skip '['

            while (p < s.size()) {

                p = skip_ws(s, p);

                if (p >= s.size() || s[p] == ']') break;

                if (s[p] != '{') { ++p; continue; }

                // Find matching '}'

                std::size_t obj_start = p;

                int d = 0;

                std::size_t obj_end = p;

                while (obj_end < s.size()) {

                    if (s[obj_end]=='{') ++d;

                    else if (s[obj_end]=='}') { --d; if(d==0){++obj_end;break;} }

                    ++obj_end;

                }

                cb(obj_start, obj_end);

                p = obj_end;

                p = skip_ws(s, p);

                if (p < s.size() && s[p] == ',') ++p;

            }

        };



        // Parse top-level backend name

        std::size_t root = skip_ws(json, 0);

        if (root >= json.size() || json[root] != '{') return false;



        auto [backend_name, _be] = find_key(json, root+1, "backend");

        if (backend_name.empty()) return false;



        // Find "controls" array position

        std::size_t ctrl_arr_pos = std::string_view::npos;

        {

            std::size_t p = root + 1;

            int depth = 0;

            while (p < json.size()) {

                p = skip_ws(json, p);

                if (p >= json.size()) break;

                char c = json[p];

                if (c == '{' || c == '[') { ++depth; ++p; continue; }

                if (c == '}' || c == ']') {

                    if (depth == 0) break; --depth; ++p; continue;

                }

                if (c == '"' && depth == 0) {

                    auto [kname, kend] = read_string(json, p);

                    if (kend == std::string_view::npos) break;

                    p = skip_ws(json, kend);

                    if (p < json.size() && json[p] == ':') ++p;

                    p = skip_ws(json, p);

                    if (kname == "controls") {

                        ctrl_arr_pos = p;

                        break;

                    }

                    // skip value

                    if (p < json.size()) {

                        if (json[p]=='"') { auto [_s,vend]=read_string(json,p); p=vend; }

                        else if (json[p]=='{'||json[p]=='[') {

                            int d2=0;

                            while(p<json.size()){

                                if(json[p]=='{'||json[p]=='[')++d2;

                                else if(json[p]=='}'||json[p]==']'){--d2;if(d2==0){++p;break;}}

                                ++p;

                            }

                        } else { while(p<json.size()&&json[p]!=','&&json[p]!='}'&&json[p]!=']')++p; }

                    }

                } else { ++p; }

            }

        }

        if (ctrl_arr_pos == std::string_view::npos) return false;



        PlatformBackend backend;

        backend.name = backend_name;



        for_each_array_obj(json, ctrl_arr_pos, [&](std::size_t obj_start, std::size_t /*obj_end*/) {

            std::size_t inner = obj_start + 1;

            auto [abstract_name, _an] = find_key(json, inner, "abstract_name");

            auto [platform_type, _pt] = find_key(json, inner, "platform_type");

            auto [platform_ns, _pn]   = find_key(json, inner, "platform_namespace");

            auto [category, _ct]      = find_key(json, inner, "category");

            auto [accepts_children_raw, children_pos] = find_key(json, inner, "accepts_children");

            auto [accepts_text_raw, text_pos]         = find_key(json, inner, "accepts_text_content");



            if (abstract_name.empty() || platform_type.empty()) return;



            // find_key returns a single raw token char for non-string values,

            // so re-read the boolean token starting at the returned position.

            auto read_bool = [&](std::size_t pos) -> std::optional<bool> {

                if (pos == std::string_view::npos || pos >= json.size()) return std::nullopt;

                std::size_t e = pos;

                while (e < json.size() && json[e] != ',' && json[e] != '}' && json[e] != ']') ++e;

                std::string_view tok = json.substr(pos, e - pos);

                while (!tok.empty() && (tok.front()==' '||tok.front()=='\t')) tok.remove_prefix(1);

                while (!tok.empty() && (tok.back()==' '||tok.back()=='\t')) tok.remove_suffix(1);

                if (tok == "true")  return true;

                if (tok == "false") return false;

                return std::nullopt;

            };

            (void)accepts_children_raw; (void)accepts_text_raw; // raw token is single char



            bool accepts_children = read_bool(children_pos).value_or(false);

            bool accepts_text     = read_bool(text_pos).value_or(false);

            auto children_opt     = read_bool(children_pos);

            auto text_opt         = read_bool(text_pos);

            if (category.empty()) category = "custom";



            PlatformControl ctrl;

            ctrl.abstract_name       = abstract_name;

            ctrl.platform_type       = platform_type;

            ctrl.platform_namespace  = platform_ns;

            // Parse control type fields for3rd-party XAML/C++ distinction
            {
                auto [ct_raw, _ct2] = find_key(json, inner, "type");
                ctrl.control_type = ct_raw.empty() ? "cpp" : ct_raw;
            }
            {
                auto [trk_raw, _trk2] = find_key(json, inner, "template_resource_key");
                ctrl.template_resource_key = trk_raw;
            }
            {
                auto [rd_raw, _rd2] = find_key(json, inner, "resource_dictionary");
                ctrl.resource_dictionary = rd_raw;
            }




            // Properties and events parsed below are collected here so they

            // can be used both for the platform mapping AND for synthesizing

            // the abstract ControlSchema of new third-party controls.

            std::vector<PropertyMapping>  parsed_props;

            std::vector<EventMapping>     parsed_events;



            // Find fixed_properties array

            {

                std::size_t p2 = inner;

                int d=0;

                while(p2<json.size()){

                    p2=skip_ws(json,p2);

                    if(p2>=json.size())break;

                    char c=json[p2];

                    if(c=='{'||c=='['){++d;++p2;continue;}

                    if(c=='}'||c==']'){if(d==0)break;--d;++p2;continue;}

                    if(c=='"'&&d==0){

                        auto[kn,ke]=read_string(json,p2);

                        if(ke==std::string_view::npos)break;

                        p2=skip_ws(json,ke);

                        if(p2<json.size()&&json[p2]==':')++p2;

                        p2=skip_ws(json,p2);

                        if(kn=="fixed_properties"){

                            for_each_array_obj(json,p2,[&](std::size_t fs,std::size_t){

                                auto[fn,_fn2]=find_key(json,fs+1,"name");

                                auto[fv,_fv2]=find_key(json,fs+1,"value");

                                if(!fn.empty()) ctrl.fixed_properties.push_back({fn,fv});

                            });

                        } else if(kn=="properties"){

                            for_each_array_obj(json,p2,[&](std::size_t ps,std::size_t){

                                auto[pan,_pan2]=find_key(json,ps+1,"abstract_name");

                                auto[pset,_pset]=find_key(json,ps+1,"platform_setter");

                                auto[ptx,_ptx]=find_key(json,ps+1,"transform");

                                if(!pan.empty()&&!pset.empty()) {

                                    parsed_props.push_back({pan,pset,ptx});

                                    ctrl.property_mappings.push_back({pan,pset,ptx});

                                }

                            });

                        } else if(kn=="events"){

                            for_each_array_obj(json,p2,[&](std::size_t es,std::size_t){

                                auto[ean,_ean]=find_key(json,es+1,"abstract_name");

                                auto[epev,_epev]=find_key(json,es+1,"platform_event");

                                if(!ean.empty()&&!epev.empty()) {

                                    parsed_events.push_back({ean,epev});

                                    ctrl.event_mappings.push_back({ean,epev});

                                }

                            });

                        }

                        // skip value

                        if(p2<json.size()){

                            if(json[p2]=='"'){auto[_s,vend]=read_string(json,p2);p2=vend;}

                            else if(json[p2]=='{'||json[p2]=='['){

                                int d2=0;

                                while(p2<json.size()){

                                    if(json[p2]=='{'||json[p2]=='[')++d2;

                                    else if(json[p2]=='}'||json[p2]==']'){--d2;if(d2==0){++p2;break;}}

                                    ++p2;

                                }

                            } else {while(p2<json.size()&&json[p2]!=','&&json[p2]!='}'&&json[p2]!=']')++p2;}

                        }

                    } else {++p2;}

                }

            }



            backend.controls.push_back(std::move(ctrl));            // Synthesize or extend an abstract ControlSchema.
            // For NEW controls: register a fresh schema so the parser,
            // semantic analysis, and WinUIControlCatalogAttribute all see it.
            // For EXISTING controls (built-in + previously JSON-registered):
            // merge additional properties/events from JSON so third-party
            // overlays can extend built-in controls (e.g. add
            // isMarkdownEnabled to the built-in RichTextBlock).
            if (!reg.m_controls.contains(abstract_name)) {
                ControlSchema schema;
                schema.abstract_name = abstract_name;
                schema.category = category;
                schema.accepts_children = accepts_children;
                schema.accepts_text_content = accepts_text;
                schema.description = "Runtime-registered control (backend JSON)";
                for (auto const& pm : parsed_props) {
                    ast::TypeKind tk = ast::TypeKind::String;
                    if (pm.value_transform == "double") tk = ast::TypeKind::Double;
                    else if (pm.value_transform == "int") tk = ast::TypeKind::Int;
                    else if (pm.value_transform == "bool" || pm.value_transform == "nullable_bool") tk = ast::TypeKind::Bool;
                    schema.properties.push_back({
                        .name = pm.abstract_name,
                        .type = tk,
                        .description = "Runtime-registered property"
                    });
                }
                for (auto const& em : parsed_events) {
                    schema.events.push_back({
                        .name = em.abstract_name,
                        .param_types = { ast::TypeKind::Void },
                        .description = "Runtime-registered event"
                    });
                }
                reg.register_control(std::move(schema));
            } else {
                // Merge additional properties/events from JSON into the
                // existing schema (skip properties that already exist).
                auto& existing = reg.m_controls.at(abstract_name);
                for (auto const& pm : parsed_props) {
                    if (!existing.find_property(pm.abstract_name)) {
                        ast::TypeKind tk = ast::TypeKind::String;
                        if (pm.value_transform == "double") tk = ast::TypeKind::Double;
                        else if (pm.value_transform == "int") tk = ast::TypeKind::Int;
                        else if (pm.value_transform == "nullable_bool" || pm.value_transform == "bool") tk = ast::TypeKind::Bool;
                        existing.properties.push_back({
                            .name = pm.abstract_name,
                            .type = tk,
                            .description = "Runtime-registered property (JSON merge)"
                        });
                    }
                }
                for (auto const& em : parsed_events) {
                    if (!existing.find_event(em.abstract_name)) {
                        existing.events.push_back({
                            .name = em.abstract_name,
                            .param_types = { ast::TypeKind::Void },
                            .description = "Runtime-registered event (JSON merge)"
                        });
                    }
                }
            }



            // Last-writer-wins for capability flags: when a JSON overlay

            // explicitly sets accepts_children / accepts_text_content, update

            // the (possibly pre-existing) abstract schema so the catalog,

            // parser and semantic analysis agree with the newest overlay.

            if (children_opt) reg.m_controls[abstract_name].accepts_children = *children_opt;

            if (text_opt)     reg.m_controls[abstract_name].accepts_text_content = *text_opt;

        });



        // Merge into an existing backend of the same name

        // Merge-per-abstract_name so a third-party JSON file can extend the

        // built-in "winui3" backend WITHOUT recompiling and without losing

        // the compiled-in controls. The built-in platform type/namespace

        // always wins: JSON only adds property/event/fixed mappings that are

        // missing, so a registry can extend (never hijack) a built-in.

        // (E.g. winui.json adds isMarkdownEnabled to built-in RichTextBlock

        // without re-pointing it to the CommunityToolkit namespace.)

        if (auto* existing = reg.find_backend_mut(backend_name); existing) {

            for (auto&& c : backend.controls) {

                auto it = std::find_if(existing->controls.begin(), existing->controls.end(),

                    [&](PlatformControl const& pc) { return pc.abstract_name == c.abstract_name; });

                if (it != existing->controls.end()) {

                    // Built-in mapping wins: merge only the JSON-only

                    // property/event/fixed mappings into the existing

                    // control, preserving its platform type + namespace.

                    for (auto&& pm : c.property_mappings)

                        if (!it->map_property(pm.abstract_name))

                            it->property_mappings.push_back(pm);

                    for (auto&& em : c.event_mappings)

                        if (!it->map_event(em.abstract_name))

                            it->event_mappings.push_back(em);

                    for (auto&& fp : c.fixed_properties) {

                        bool have = false;

                        for (auto const& f : it->fixed_properties)

                            if (f.first == fp.first) { have = true; break; }

                        if (!have) it->fixed_properties.push_back(fp);

                    }

                }

                else existing->controls.push_back(std::move(c));

            }

        } else {

            reg.register_backend(std::move(backend));

        }



        // Structural validation

        // The per-key scans above stop at EOF, so a truncated file could

        // otherwise pass silently. Require balanced { } [ ] and a closing

        // root brace before accepting the document.

        {

            int b_depth = 0, s_depth = 0;

            std::size_t p = 0;

            while (p < json.size()) {

                char c = json[p];

                if (c == '"') {

                    auto [_, vend] = read_string(json, p);

                    if (vend == std::string_view::npos) return false;

                    p = vend;

                    continue;

                }

                if (c == '{') ++b_depth;

                else if (c == '}') { if (--b_depth < 0) return false; }

                else if (c == '[') ++s_depth;

                else if (c == ']') { if (--s_depth < 0) return false; }

                ++p;

            }

            if (b_depth != 0 || s_depth != 0) return false;

        }

        return true;

    }



private:

    std::unordered_map<std::string, ControlSchema>  m_controls;

    std::unordered_map<std::string, ModifierSchema>  m_modifiers;

    std::unordered_map<std::string, PlatformBackend> m_backends;



    // Built-in abstract control definitions



    static auto register_builtin_controls(ControlRegistry& r) -> void {

        using TK = ast::TypeKind;



        // Layout

        r.register_control({

            .abstract_name = "Column",

            .properties = {

                {.name="spacing", .type=TK::Double},

                {.name="alignment", .type=TK::String},

                {.name="padding", .type=TK::Double},

            },

            .accepts_children = true,
            .composition_kind = "content",

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "Row",

            .properties = {

                {.name="spacing", .type=TK::Double},

                {.name="alignment", .type=TK::String},

                {.name="padding", .type=TK::Double},

            },

            .accepts_children = true,
            .composition_kind = "content",

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "Stack",

            .properties = { {.name="alignment",.type=TK::String} },

            .accepts_children = true,

            .composition_kind = "panel",

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "Grid",

            .properties = {

                {.name="columns", .type=TK::Int},

                {.name="rows", .type=TK::Int},

                {.name="spacing", .type=TK::Double},

            },

            .accepts_children = true,

            .composition_kind = "panel",

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "Scroll",

            .properties = {

                {.name="direction", .type=TK::String},

            },

            .accepts_children = true,
            .composition_kind = "content",

            .category = "layout",

        });



        r.register_control({.abstract_name="Spacer", .category="layout"});

        r.register_control({.abstract_name="Divider", .category="layout"});



        r.register_control({

            .abstract_name = "Card",

            .properties = {

                {.name="elevation", .type=TK::Double},

                {.name="cornerRadius", .type=TK::Double},

            },

            .accepts_children = true,
            .composition_kind = "content",

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "Wrap",

            .properties = {

                {.name="spacing",     .type=TK::Double},

                {.name="itemWidth",   .type=TK::Double},

                {.name="itemHeight",  .type=TK::Double},

                {.name="maximumRowsOrColumns", .type=TK::Int},

                {.name="orientation", .type=TK::String},

            },

            .accepts_children = true,

            .description = "Wrapping layout panel — children wrap to next row/column when space runs out.",

            .category = "layout",

        });



        // Content

        r.register_control({

            .abstract_name = "Label",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

                {.name="fontWeight", .type=TK::String},

                {.name="color", .type=TK::String},

                {.name="maxLines", .type=TK::Int},

                {.name="wrapping", .type=TK::String},

            },

            .accepts_text_content = true, // Label("Hello")

            .category = "content",

        });



        r.register_control({

            .abstract_name = "Icon",

            .properties = {

                {.name="glyph", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

                {.name="color", .type=TK::String},

            },

            .category = "content",

        });



        r.register_control({

            .abstract_name = "Image",

            .properties = {

                {.name="source", .type=TK::String},

                {.name="width", .type=TK::Double},

                {.name="height", .type=TK::Double},

                {.name="fit", .type=TK::String},

            },

            .category = "content",

        });



        // Input

        r.register_control({

            .abstract_name = "Button",

            .properties = {

                {.name="content", .type=TK::String},

                {.name="style", .type=TK::String},

                {.name="width", .type=TK::Double},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = { {.name="onClick"} },

            .accepts_text_content = true, // Button("Click Me")

            .category = "input",

        });



        r.register_control({

            .abstract_name = "Input",

            .properties = {

                {.name="placeholder", .type=TK::String},

                {.name="text", .type=TK::String},

                {.name="isReadOnly", .type=TK::Bool},

            },

            .events = { {.name="onChanged", .param_types={TK::String}} },

            .category = "input",

        });



        r.register_control({

            .abstract_name = "TextArea",

            .properties = {

                {.name="placeholder", .type=TK::String},

                {.name="text", .type=TK::String},

                {.name="maxLength", .type=TK::Int},

            },

            .events = { {.name="onChanged", .param_types={TK::String}} },

            .category = "input",

        });



        r.register_control({

            .abstract_name = "Toggle",

            .properties = {

                {.name="label", .type=TK::String},

                {.name="isOn", .type=TK::Bool},

            },

            .events = { {.name="onToggled", .param_types={TK::Bool}} },

            .accepts_text_content = true, // Toggle("Enable feature")

            .category = "input",

        });



        r.register_control({

            .abstract_name = "Slider",

            .properties = {

                {.name="value", .type=TK::Double},

                {.name="minimum", .type=TK::Double},

                {.name="maximum", .type=TK::Double},

                {.name="step", .type=TK::Double},

            },

            .events = { {.name="onChanged", .param_types={TK::Double}} },

            .category = "input",

        });



        r.register_control({

            .abstract_name = "Checkbox",

            .properties = {

                {.name="label", .type=TK::String},

                {.name="isChecked", .type=TK::Bool},

            },

            .events = { {.name="onChanged", .param_types={TK::Bool}} },

            .accepts_text_content = true,

            .category = "input",

        });



        r.register_control({

            .abstract_name = "Picker",

            .properties = {

                {.name="selectedIndex", .type=TK::Int},

                {.name="header", .type=TK::String},

            },

            .events = { {.name="onSelectionChanged", .param_types={TK::Int}} },

            .accepts_children = true,
            .composition_kind = "items_panel",

            .category = "input",

        });



        // Navigation

        r.register_control({

            .abstract_name = "TabView",

            .properties = { {.name="selectedIndex", .type=TK::Int} },

            .events = { {.name="onTabChanged", .param_types={TK::Int}} },

            .accepts_children = true,


            .composition_kind = "items",
            .category = "navigation",

        });



        r.register_control({            .abstract_name = "NavView",

            .properties = {

                {.name="header", .type=TK::String},

                {.name="isBackVisible", .type=TK::Bool},

                {.name="isBackButtonVisible", .type=TK::String},

                {.name="isSettingsVisible", .type=TK::Bool},

                {.name="background", .type=TK::String},

            },

            .events = { {.name="onNavigated", .param_types={TK::String}}, {.name="onSelectionChanged"} },

            .accepts_children = true,

            .composition_kind = "navigation",

            .category = "navigation",

        });



        r.register_control({

            .abstract_name = "Link",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="url", .type=TK::String},

            },

            .accepts_text_content = true,

            .category = "navigation",

        });



        // Feedback

        r.register_control({

            .abstract_name = "Progress",

            .properties = {

                {.name="value", .type=TK::Double},

                {.name="isIndeterminate", .type=TK::Bool},

                {.name="style", .type=TK::String}, // ring, bar

            },

            .category = "feedback",

        });



        r.register_control({

            .abstract_name = "InfoBar",

            .properties = {

                {.name="title", .type=TK::String},

                {.name="message", .type=TK::String},

                {.name="severity", .type=TK::String}, // info, warning, error, success

                {.name="isOpen", .type=TK::Bool},

            },

            .events = { {.name="onClosed"} },

            .category = "feedback",

        });



        // Real WinUI 3 Controls — WinUI Gallery catalog

        // Every concrete Microsoft.UI.Xaml.Controls type shown in the

        // WinUI Gallery app (microsoft/WinUI-Gallery, ControlInfoData.json).

        // abstract_name == real XAML type name, so language projections can

        // resolve the platform type directly with no translation table.

        //

        // Categories mirror the Gallery's own grouping:

        //   menus, collections, datetime, input, status, dialogs,

        //   scrolling, layout, navigation, media, text, shapes, icons



        // Menus & toolbars

        r.register_control({

            .abstract_name = "AppBarButton",

            .properties = {

                {.name="label", .type=TK::String},

                {.name="icon", .type=TK::String},

                {.name="isCompact", .type=TK::Bool},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = { {.name="onClick"} },

            .accepts_text_content = true,

            .category = "menus",

        });



        r.register_control({

            .abstract_name = "AppBarSeparator",

            .category = "menus",

        });



        r.register_control({

            .abstract_name = "AppBarToggleButton",

            .properties = {

                {.name="label", .type=TK::String},

                {.name="icon", .type=TK::String},

                {.name="isChecked", .type=TK::Bool},

                {.name="isCompact", .type=TK::Bool},

            },

            .events = {

                {.name="onClick"},

                {.name="onChecked", .param_types={TK::Bool}},

                {.name="onUnchecked", .param_types={TK::Bool}},

            },

            .accepts_text_content = true,

            .category = "menus",

        });



        r.register_control({

            .abstract_name = "CommandBar",

            .properties = {

                {.name="title", .type=TK::String},

                {.name="isOpen", .type=TK::Bool},

                {.name="isSticky", .type=TK::Bool},

                {.name="defaultLabelPosition", .type=TK::String},

            },

            .events = {

                {.name="onOpening"},

                {.name="onClosing"},

            },

            .accepts_children = true,

            .category = "menus",

        });



        r.register_control({

            .abstract_name = "CommandBarFlyout",

            .properties = { {.name="placement", .type=TK::String} },

            .accepts_children = true,


            .composition_kind = "items_panel",
            .category = "menus",

        });



        r.register_control({

            .abstract_name = "MenuBar",

            .accepts_children = true,


            .composition_kind = "items",
            .category = "menus",

        });



        r.register_control({

            .abstract_name = "MenuBarItem",

            .accepts_children = true,

            .accepts_text_content = true,

            .composition_kind = "content",

            .category = "menus",

        });



        r.register_control({

            .abstract_name = "MenuFlyoutItem",

            .accepts_text_content = true,

            .category = "menus",

        });



        r.register_control({

            .abstract_name = "MenuItem",

            .accepts_text_content = true,

            .category = "menus",

        });



        r.register_control({

            .abstract_name = "MenuFlyout",

            .properties = { {.name="placement", .type=TK::String} },

            .accepts_children = true,


            .composition_kind = "items_panel",
            .category = "menus",

        });



        r.register_control({

            .abstract_name = "SwipeControl",

            .properties = {

                {.name="mode", .type=TK::String}, // reveal, execute

                {.name="isEnabled", .type=TK::Bool},

            },

            .accepts_children = true,
            .composition_kind = "items_panel",

            .category = "menus",

        });



        r.register_control({

            .abstract_name = "SwipeItem",

            .accepts_text_content = true,

            .category = "menus",

        });



        // Collections

        r.register_control({

            .abstract_name = "FlipView",

            .properties = { {.name="selectedIndex", .type=TK::Int} },

            .events = { {.name="onSelectionChanged", .param_types={TK::Int}} },

            .accepts_children = true,


            .composition_kind = "content",
            .category = "collections",

        });



        r.register_control({

            .abstract_name = "GridView",

            .properties = {

                {.name="selectedIndex", .type=TK::Int},

                {.name="itemsSource", .type=TK::Collection},

                {.name="itemWidth", .type=TK::Double},

                {.name="itemHeight", .type=TK::Double},

                {.name="isMultiSelect", .type=TK::Bool},

            },

            .events = {

                {.name="onSelectionChanged", .param_types={TK::Int}},

                {.name="onItemClick", .param_types={TK::Int}},

            },

            .accepts_children = true,

            .category = "collections",

        });



        r.register_control({

            .abstract_name = "ItemsRepeater",

            .properties = {

                {.name="itemsSource", .type=TK::String},

                {.name="layout", .type=TK::String}, // stack, grid, uniform

                {.name="orientation", .type=TK::String},

                {.name="spacing", .type=TK::Double},

            },

            .accepts_children = true,
            .composition_kind = "items",

            .category = "collections",

        });



        r.register_control({

            .abstract_name = "ItemsView",

            .properties = {

                {.name="selectedIndex", .type=TK::Int},

                {.name="itemsSource", .type=TK::String},

            },

            .events = { {.name="onSelectionChanged", .param_types={TK::Int}} },

            .accepts_children = true,
            .composition_kind = "items",

            .category = "collections",

        });



        r.register_control({

            .abstract_name = "ListView",

            .properties = {

                {.name="selectedIndex", .type=TK::Int},

                {.name="itemsSource", .type=TK::Collection},

                {.name="isMultiSelect", .type=TK::Bool},

                {.name="isItemClickEnabled", .type=TK::Bool},

            },

            .events = {

                {.name="onSelectionChanged", .param_types={TK::Int}},

                {.name="onItemClick", .param_types={TK::Int}},

            },

            .accepts_children = true,

            .category = "collections",

        });



        r.register_control({

            .abstract_name = "TreeView",

            .properties = { {.name="itemsSource", .type=TK::String} },

            .events = { {.name="onItemInvoked", .param_types={TK::Int}} },

            .accepts_children = true,


            .composition_kind = "items",
            .category = "collections",

        });



        r.register_control({

            .abstract_name = "RefreshContainer",

            .properties = {

                {.name="pullDirection", .type=TK::String}, // top, bottom, left, right

                {.name="visualizer", .type=TK::String},

            },

            .events = { {.name="onRefreshRequested"} },

            .accepts_children = true,
            .composition_kind = "items_panel",

            .category = "collections",

        });



        r.register_control({

            .abstract_name = "RefreshVisualizer",

            .properties = {

                {.name="orientation", .type=TK::String},

                {.name="state", .type=TK::String},

            },

            .category = "collections",

        });



        // Date & time

        r.register_control({

            .abstract_name = "CalendarDatePicker",

            .properties = {

                {.name="date", .type=TK::String},

                {.name="placeholder", .type=TK::String},

                {.name="isCalendarOpen", .type=TK::Bool},

                {.name="isTodayHighlighted", .type=TK::Bool},

            },

            .events = { {.name="onDateChanged", .param_types={TK::String}} },

            .category = "datetime",

        });



        r.register_control({

            .abstract_name = "CalendarView",

            .properties = {

                {.name="selectedDate", .type=TK::String},

                {.name="displayMode", .type=TK::String}, // month, year, decade

                {.name="isTodayHighlighted", .type=TK::Bool},

            },

            .events = { {.name="onSelectedDatesChanged", .param_types={TK::String}} },

            .category = "datetime",

        });



        r.register_control({

            .abstract_name = "DatePicker",

            .properties = {

                {.name="date", .type=TK::String},

                {.name="header", .type=TK::String},

                {.name="dayVisible", .type=TK::Bool},

                {.name="monthVisible", .type=TK::Bool},

                {.name="yearVisible", .type=TK::Bool},

            },

            .events = { {.name="onDateChanged", .param_types={TK::String}} },

            .category = "datetime",

        });



        r.register_control({

            .abstract_name = "TimePicker",

            .properties = {

                {.name="time", .type=TK::String},

                {.name="header", .type=TK::String},

                {.name="minuteIncrement", .type=TK::Int},

                {.name="clockIdentifier", .type=TK::String},

            },

            .events = { {.name="onTimeChanged", .param_types={TK::String}} },

            .category = "datetime",

        });



        // Basic input (real control names)

        r.register_control({

            .abstract_name = "DropDownButton",

            .properties = {

                {.name="content", .type=TK::String},

                {.name="flyout", .type=TK::String},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = { {.name="onClick"} },

            .accepts_children = true,


            .accepts_text_content = true,



            .composition_kind = "items_panel",

            .category = "input",

        });



        r.register_control({

            .abstract_name = "RepeatButton",

            .properties = {

                {.name="content", .type=TK::String},

                {.name="delay", .type=TK::Int},

                {.name="interval", .type=TK::Int},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = { {.name="onClick"} },

            .accepts_text_content = true,

            .category = "input",

        });



        r.register_control({

            .abstract_name = "ToggleButton",

            .properties = {

                {.name="content", .type=TK::String},

                {.name="isChecked", .type=TK::Bool},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = {

                {.name="onClick"},

                {.name="onChecked", .param_types={TK::Bool}},

                {.name="onUnchecked", .param_types={TK::Bool}},

            },

            .accepts_text_content = true,

            .category = "input",

        });



        r.register_control({

            .abstract_name = "SplitButton",

            .properties = {

                {.name="content", .type=TK::String},

                {.name="flyout", .type=TK::String},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = { {.name="onClick"} },

            .accepts_children = true,


            .accepts_text_content = true,



            .composition_kind = "items_panel",

            .category = "input",

        });



        r.register_control({

            .abstract_name = "ToggleSplitButton",

            .properties = {

                {.name="content", .type=TK::String},

                {.name="isChecked", .type=TK::Bool},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = {

                {.name="onClick"},

                {.name="onChecked", .param_types={TK::Bool}},

                {.name="onUnchecked", .param_types={TK::Bool}},

            },

            .accepts_text_content = true,

            .category = "input",

        });



        r.register_control({

            .abstract_name = "CheckBox",

            .properties = {

                {.name="content", .type=TK::String},

                {.name="isChecked", .type=TK::Bool},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = {

                {.name="onChecked", .param_types={TK::Bool}},

                {.name="onUnchecked", .param_types={TK::Bool}},

            },

            .accepts_text_content = true,

            .category = "input",

        });



        r.register_control({

            .abstract_name = "ColorPicker",

            .properties = {

                {.name="color", .type=TK::String},

                {.name="isAlphaEnabled", .type=TK::Bool},

                {.name="isColorSpectrumVisible", .type=TK::Bool},

            },

            .events = { {.name="onColorChanged", .param_types={TK::String}} },

            .category = "input",

        });



        r.register_control({

            .abstract_name = "ComboBox",

            .properties = {

                {.name="selectedIndex", .type=TK::Int},

                {.name="header", .type=TK::String},

                {.name="placeholder", .type=TK::String},

                {.name="isEditable", .type=TK::Bool},

            },

            .events = { {.name="onSelectionChanged", .param_types={TK::Int}} },

            .accepts_children = true,

            .category = "input",

        });



        r.register_control({

            .abstract_name = "RadioButton",

            .properties = {

                {.name="content", .type=TK::String},

                {.name="groupName", .type=TK::String},

                {.name="isChecked", .type=TK::Bool},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = {

                {.name="onChecked", .param_types={TK::Bool}},

                {.name="onUnchecked", .param_types={TK::Bool}},

            },

            .accepts_text_content = true,

            .category = "input",

        });



        r.register_control({

            .abstract_name = "RatingControl",

            .properties = {

                {.name="value", .type=TK::Double},

                {.name="maxRating", .type=TK::Int},

                {.name="isReadOnly", .type=TK::Bool},

                {.name="isClearEnabled", .type=TK::Bool},

            },

            .events = { {.name="onValueChanged", .param_types={TK::Double}} },

            .category = "input",

        });



        r.register_control({

            .abstract_name = "ToggleSwitch",

            .properties = {

                {.name="header", .type=TK::String},

                {.name="isOn", .type=TK::Bool},

                {.name="onContent", .type=TK::String},

                {.name="offContent", .type=TK::String},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = { {.name="onToggled", .param_types={TK::Bool}} },

            .accepts_text_content = true,

            .category = "input",

        });



        // Status & info

        r.register_control({

            .abstract_name = "InfoBadge",

            .properties = {

                {.name="value", .type=TK::Int},

                {.name="isIndeterminate", .type=TK::Bool},

                {.name="isEnabled", .type=TK::Bool},

            },

            .category = "status",

        });



        r.register_control({

            .abstract_name = "ProgressBar",

            .properties = {

                {.name="value", .type=TK::Double},

                {.name="isIndeterminate", .type=TK::Bool},

                {.name="showPaused", .type=TK::Bool},

                {.name="showError", .type=TK::Bool},

            },

            .category = "status",

        });



        r.register_control({

            .abstract_name = "ProgressRing",

            .properties = {

                {.name="value", .type=TK::Double},

                {.name="isIndeterminate", .type=TK::Bool},

                {.name="isActive", .type=TK::Bool},

            },

            .category = "status",

        });



        r.register_control({

            .abstract_name = "ToolTip",

            .properties = {

                {.name="content", .type=TK::String},

                {.name="placement", .type=TK::String},

                {.name="isOpen", .type=TK::Bool},

            },

            .events = {

                {.name="onOpened"},

                {.name="onClosed"},

            },

            .accepts_text_content = true,

            .category = "status",

        });



        // Dialogs & flyouts

        r.register_control({

            .abstract_name = "ContentDialog",

            .properties = {

                {.name="title", .type=TK::String},

                {.name="content", .type=TK::String},

                {.name="primaryButtonText", .type=TK::String},

                {.name="secondaryButtonText", .type=TK::String},

                {.name="closeButtonText", .type=TK::String},

                {.name="isPrimaryButtonEnabled", .type=TK::Bool},

            },

            .events = {

                {.name="onPrimaryButtonClick"},

                {.name="onSecondaryButtonClick"},

                {.name="onCloseButtonClick"},

                {.name="onOpened"},

                {.name="onClosed"},

            },

            .accepts_children = true,

            .category = "dialogs",

        });



        r.register_control({

            .abstract_name = "Flyout",

            .properties = { {.name="placement", .type=TK::String} },

            .accepts_children = true,


            .composition_kind = "items_panel",
            .category = "dialogs",

        });



        r.register_control({

            .abstract_name = "Popup",

            .properties = {

                {.name="isOpen", .type=TK::Bool},

                {.name="isLightDismissEnabled", .type=TK::Bool},

            },

            .events = { {.name="onClosed"} },

            .accepts_children = true,
            .composition_kind = "content",

            .category = "dialogs",

        });



        r.register_control({

            .abstract_name = "TeachingTip",

            .properties = {

                {.name="title", .type=TK::String},

                {.name="subtitle", .type=TK::String},

                {.name="content", .type=TK::String},

                {.name="isOpen", .type=TK::Bool},

                {.name="preferredPlacement", .type=TK::String},

            },

            .events = { {.name="onClosed"} },

            .accepts_children = true,

            .category = "dialogs",

        });



        // Scrolling

        r.register_control({

            .abstract_name = "AnnotatedScrollBar",

            .properties = {

                {.name="headerKind", .type=TK::String},

                {.name="previewKind", .type=TK::String},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = { {.name="onValueChanged", .param_types={TK::Double}} },

            .category = "scrolling",

        });



        r.register_control({

            .abstract_name = "PipsPager",

            .properties = {

                {.name="numberOfPages", .type=TK::Int},

                {.name="selectedPageIndex", .type=TK::Int},

                {.name="previousButtonVisibility", .type=TK::String},

                {.name="nextButtonVisibility", .type=TK::String},

                {.name="orientation", .type=TK::String},

            },

            .events = { {.name="onSelectedIndexChanged", .param_types={TK::Int}} },

            .category = "scrolling",

        });



        r.register_control({

            .abstract_name = "ScrollView",

            .properties = {

                {.name="contentOrientation", .type=TK::String},

                {.name="horizontalScrollBarVisibility", .type=TK::String},

                {.name="verticalScrollBarVisibility", .type=TK::String},

                {.name="zoomMode", .type=TK::String},

                {.name="minZoomFactor", .type=TK::Double},

                {.name="maxZoomFactor", .type=TK::Double},

            },

            .accepts_children = true,

            .category = "scrolling",

        });



        r.register_control({

            .abstract_name = "ScrollViewer",

            .properties = {

                {.name="horizontalScrollBarVisibility", .type=TK::String},

                {.name="verticalScrollBarVisibility", .type=TK::String},

                {.name="horizontalScrollMode", .type=TK::String},

                {.name="verticalScrollMode", .type=TK::String},

                {.name="zoomMode", .type=TK::String},

                {.name="isZoomChainingEnabled", .type=TK::Bool},

            },

            .accepts_children = true,

            .category = "scrolling",

        });



        r.register_control({

            .abstract_name = "SemanticZoom",

            .properties = { {.name="isZoomedInViewActive", .type=TK::Bool} },

            .events = { {.name="onViewChangeStarted"}, {.name="onViewChangeCompleted"} },

            .category = "scrolling",

        });



        // Layout (real control names)

        r.register_control({

            .abstract_name = "Border",

            .properties = {

                {.name="background", .type=TK::String},

                {.name="cornerRadius", .type=TK::Double},

                {.name="borderBrush", .type=TK::String},

                {.name="borderThickness", .type=TK::Double},

            },

            .accepts_children = true,
            .composition_kind = "content",

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "Canvas",

            .properties = {

                {.name="left", .type=TK::Double},

                {.name="top", .type=TK::Double},

            },

            .accepts_children = true,
            .composition_kind = "panel",

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "Expander",

            .properties = {

                {.name="header", .type=TK::String},

                {.name="isExpanded", .type=TK::Bool},

                {.name="horizontalHeaderAlignment", .type=TK::String},

            },

            .events = {

                {.name="onExpanding"},

                {.name="onExpanded"},

                {.name="onCollapsing"},

                {.name="onCollapsed"},

            },

            .accepts_children = true,

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "RelativePanel",

            .properties = {

                {.name="alignLeftWith", .type=TK::String},

                {.name="alignTopWith", .type=TK::String},

                {.name="alignRightWith", .type=TK::String},

                {.name="alignBottomWith", .type=TK::String},

            },

            .accepts_children = true,
            .composition_kind = "panel",

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "SplitView",

            .properties = {

                {.name="isPaneOpen", .type=TK::Bool},

                {.name="panePlacement", .type=TK::String}, // left, right

                {.name="displayMode", .type=TK::String},   // overlay, inline, compactOverlay, compactInline

                {.name="openPaneLength", .type=TK::Double},

                {.name="compactPaneLength", .type=TK::Double},

            },

            .events = {

                {.name="onPaneOpening"},

                {.name="onPaneOpened"},

                {.name="onPaneClosing"},

                {.name="onPaneClosed"},

            },

            .accepts_children = true,

            .category = "layout",

        });



        r.register_control({            .abstract_name = "StackPanel",

            .properties = {

                {.name="orientation", .type=TK::String}, // vertical, horizontal

                {.name="spacing", .type=TK::Double},

                {.name="alignment", .type=TK::String},

            },

            .accepts_children = true,

            .composition_kind = "panel",

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "VariableSizedWrapGrid",

            .properties = {

                {.name="itemWidth", .type=TK::Double},

                {.name="itemHeight", .type=TK::Double},

                {.name="maximumRowsOrColumns", .type=TK::Int},

                {.name="orientation", .type=TK::String},

            },

            .accepts_children = true,
            .composition_kind = "panel",

            .category = "layout",

        });



        r.register_control({

            .abstract_name = "Viewbox",

            .properties = {

                {.name="stretch", .type=TK::String},

                {.name="stretchDirection", .type=TK::String},

            },

            .accepts_children = true,
            .composition_kind = "content",

            .category = "layout",

        });



        // Navigation (real control names)

        r.register_control({

            .abstract_name = "BreadcrumbBar",

            .properties = { {.name="itemsSource", .type=TK::String} },

            .events = { {.name="onItemClicked", .param_types={TK::Int}} },

            .category = "navigation",

        });



        r.register_control({

            .abstract_name = "Frame",

            .accepts_children = true,

            .category = "navigation",

        });



        r.register_control({

            .abstract_name = "NavigationView",

            .properties = {

                {.name="header", .type=TK::String},

                {.name="isBackButtonVisible", .type=TK::Bool},

                {.name="isSettingsVisible", .type=TK::Bool},

                {.name="isPaneOpen", .type=TK::Bool},

                {.name="paneDisplayMode", .type=TK::String},

                {.name="background", .type=TK::String},

            },

            .events = {

                {.name="onItemInvoked", .param_types={TK::Int}},

                {.name="onBackRequested"},

                {.name="onSelectionChanged", .param_types={TK::Int}},

            },            .accepts_children = true,
            .composition_kind = "navigation",
            .category = "navigation",
        });



        r.register_control({

            .abstract_name = "NavViewItem",

            .properties = {

                {.name="content", .type=TK::String},

                {.name="tag", .type=TK::String},

            },

            .accepts_children = true,

            .accepts_text_content = true,

            .composition_kind = "navigation_item",

            .category = "navigation",

        });



        r.register_control({

            .abstract_name = "NavigationViewItem",

            .accepts_children = true,

            .accepts_text_content = true,

            .composition_kind = "navigation_item",

            .category = "navigation",

        });

        r.register_control({

            .abstract_name = "KeyboardAccelerator",

            .properties = {
                {.name="key", .type=TK::String},
                {.name="modifiers", .type=TK::String},
                {.name="invoked", .type=TK::String},
            },

            .composition_kind = "keyboard",

            .category = "input",

        });

        r.register_control({

            .abstract_name = "ImageIconSource",

            .properties = {
                {.name="imageSource", .type=TK::String},
            },

            .composition_kind = "icon_source",

            .category = "media",

        });



        r.register_control({

            .abstract_name = "Tab",

            .accepts_text_content = true,

            .category = "navigation",

        });



        r.register_control({

            .abstract_name = "TabViewItem",

            .accepts_text_content = true,

            .category = "navigation",

        });



        r.register_control({

            .abstract_name = "Pivot",

            .properties = {

                {.name="title", .type=TK::String},

                {.name="selectedIndex", .type=TK::Int},

                {.name="isHeaderItemsCarouselEnabled", .type=TK::Bool},

            },

            .events = { {.name="onSelectionChanged", .param_types={TK::Int}} },

            .accepts_children = true,
            .composition_kind = "panel",

            .category = "navigation",

        });



        r.register_control({

            .abstract_name = "SelectorBar",

            .properties = {

                {.name="itemsSource", .type=TK::String},

                {.name="selectedIndex", .type=TK::Int},

            },

            .events = { {.name="onSelectionChanged", .param_types={TK::Int}} },

            .category = "navigation",

        });



        // Media

        r.register_control({

            .abstract_name = "AnimatedVisualPlayer",

            .properties = {

                {.name="source", .type=TK::String},

                {.name="autoPlay", .type=TK::Bool},

                {.name="isPlaying", .type=TK::Bool},

                {.name="playbackRate", .type=TK::Double},

            },

            .events = {

                {.name="onLoaded"},

                {.name="onUnloaded"},

            },

            .accepts_children = true,

            .category = "media",

        });



        r.register_control({

            .abstract_name = "CaptureElement",

            .properties = {

                {.name="source", .type=TK::String},

                {.name="stretch", .type=TK::String},

                {.name="isEnabled", .type=TK::Bool},

            },

            .category = "media",

        });



        r.register_control({

            .abstract_name = "MapControl",

            .properties = {

                {.name="mapServiceToken", .type=TK::String},

                {.name="center", .type=TK::String},

                {.name="zoomLevel", .type=TK::Double},

                {.name="mapStyle", .type=TK::String},

                {.name="isTrafficEnabled", .type=TK::Bool},

            },

            .events = {

                {.name="onMapLoaded"},

                {.name="onMapTapped", .param_types={TK::String}},

            },

            .accepts_children = true,

            .category = "media",

        });



        r.register_control({

            .abstract_name = "MediaPlayerElement",

            .properties = {

                {.name="source", .type=TK::String},

                {.name="autoPlay", .type=TK::Bool},

                {.name="isFullWindow", .type=TK::Bool},

                {.name="isMuted", .type=TK::Bool},

            },

            .events = {

                {.name="onMediaOpened"},

                {.name="onMediaEnded"},

                {.name="onMediaFailed"},

            },

            .category = "media",

        });



        r.register_control({

            .abstract_name = "PersonPicture",

            .properties = {

                {.name="profilePicture", .type=TK::String},

                {.name="displayName", .type=TK::String},

                {.name="initials", .type=TK::String},

                {.name="isEnabled", .type=TK::Bool},

            },

            .events = { {.name="onClick"} },

            .category = "media",

        });



        r.register_control({

            .abstract_name = "WebView2",

            .properties = {

                {.name="source", .type=TK::String},

                {.name="defaultBackgroundColor", .type=TK::String},

                {.name="isVisible", .type=TK::Bool},

            },

            .events = {

                {.name="onNavigationCompleted"},

                {.name="onNavigationStarting"},

                {.name="onWebMessageReceived", .param_types={TK::String}},

            },

            .category = "media",

        });



        // Text (real control names)

        r.register_control({

            .abstract_name = "AutoSuggestBox",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="placeholder", .type=TK::String},

                {.name="itemsSource", .type=TK::String},

                {.name="isSuggestionListOpen", .type=TK::Bool},

                {.name="maxSuggestionListHeight", .type=TK::Double},

            },

            .events = {

                {.name="onTextChanged", .param_types={TK::String}},

                {.name="onQuerySubmitted", .param_types={TK::String}},

                {.name="onSuggestionChosen", .param_types={TK::String}},

            },

            .category = "text",

        });



        r.register_control({

            .abstract_name = "NumberBox",

            .properties = {

                {.name="value", .type=TK::Double},

                {.name="header", .type=TK::String},

                {.name="placeholder", .type=TK::String},

                {.name="minimum", .type=TK::Double},

                {.name="maximum", .type=TK::Double},

                {.name="step", .type=TK::Double},

                {.name="spinButtonPlacementMode", .type=TK::String},

            },

            .events = { {.name="onValueChanged", .param_types={TK::Double}} },

            .category = "text",

        });



        r.register_control({

            .abstract_name = "PasswordBox",

            .properties = {

                {.name="password", .type=TK::String},

                {.name="placeholder", .type=TK::String},

                {.name="isPasswordRevealButtonEnabled", .type=TK::Bool},

                {.name="maxLength", .type=TK::Int},

            },

            .events = { {.name="onPasswordChanged", .param_types={TK::String}} },

            .category = "text",

        });



        r.register_control({

            .abstract_name = "RichEditBox",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="placeholder", .type=TK::String},

                {.name="isReadOnly", .type=TK::Bool},

                {.name="maxLength", .type=TK::Int},

                {.name="acceptsReturn", .type=TK::Bool},

            },

            .events = { {.name="onTextChanged", .param_types={TK::String}} },

            .category = "text",

        });



        r.register_control({

            .abstract_name = "RichTextBlock",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

                {.name="fontWeight", .type=TK::String},

                {.name="color", .type=TK::String},

                {.name="maxLines", .type=TK::Int},

                {.name="wrapping", .type=TK::String},

            },

            .accepts_children = true,

            .accepts_text_content = true,

            .category = "text",

        });



        r.register_control({

            .abstract_name = "TextBlock",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

                {.name="fontWeight", .type=TK::String},

                {.name="color", .type=TK::String},

                {.name="maxLines", .type=TK::Int},

                {.name="wrapping", .type=TK::String},

                {.name="isTextSelectionEnabled", .type=TK::Bool},

            },

            .accepts_text_content = true,

            .category = "text",

        });



        r.register_control({

            .abstract_name = "Paragraph",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

                {.name="fontWeight", .type=TK::String},

                {.name="color", .type=TK::String},

                {.name="wrapping", .type=TK::String},

            },

            .accepts_children = true,

            .accepts_text_content = true,

            .category = "text",

        });



        r.register_control({

            .abstract_name = "Hyperlink",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="navigateUri", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

                {.name="color", .type=TK::String},

            },

            .events = { {.name="onClick"} },

            .accepts_text_content = true,

            .category = "text",

        });



        r.register_control({

            .abstract_name = "Bold",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

            },

            .accepts_text_content = true,

            .category = "text",

        });



        r.register_control({

            .abstract_name = "Italic",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

            },

            .accepts_text_content = true,

            .category = "text",

        });



        r.register_control({

            .abstract_name = "Run",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

            },

            .accepts_text_content = true,

            .category = "text",

        });



        r.register_control({

            .abstract_name = "Span",

            .properties = {

                {.name="text", .type=TK::String},

            },

            .accepts_text_content = true,

            .category = "text",

        });



        r.register_control({

            .abstract_name = "Underline",

            .properties = {

                {.name="text", .type=TK::String},

            },

            .accepts_text_content = true,

            .category = "text",

        });



        r.register_control({

            .abstract_name = "TextBox",

            .properties = {

                {.name="text", .type=TK::String},

                {.name="placeholder", .type=TK::String},

                {.name="isReadOnly", .type=TK::Bool},

                {.name="maxLength", .type=TK::Int},

                {.name="acceptsReturn", .type=TK::Bool},

                {.name="textWrapping", .type=TK::String},

            },

            .events = { {.name="onTextChanged", .param_types={TK::String}} },

            .category = "text",

        });



        // Shapes

        r.register_control({

            .abstract_name = "Line",

            .properties = {

                {.name="x1", .type=TK::Double},

                {.name="y1", .type=TK::Double},

                {.name="x2", .type=TK::Double},

                {.name="y2", .type=TK::Double},

                {.name="stroke", .type=TK::String},

                {.name="strokeThickness", .type=TK::Double},

            },

            .category = "shapes",

        });



        r.register_control({

            .abstract_name = "Rectangle",

            .properties = {

                {.name="fill", .type=TK::String},

                {.name="stroke", .type=TK::String},

                {.name="strokeThickness", .type=TK::Double},

                {.name="radiusX", .type=TK::Double},

                {.name="radiusY", .type=TK::Double},

            },

            .category = "shapes",

        });



        r.register_control({

            .abstract_name = "Ellipse",

            .properties = {

                {.name="fill", .type=TK::String},

                {.name="stroke", .type=TK::String},

                {.name="strokeThickness", .type=TK::Double},

            },

            .category = "shapes",

        });



        r.register_control({

            .abstract_name = "Path",

            .properties = {

                {.name="data", .type=TK::String},

                {.name="fill", .type=TK::String},

                {.name="stroke", .type=TK::String},

                {.name="strokeThickness", .type=TK::Double},

            },

            .category = "shapes",

        });



        r.register_control({

            .abstract_name = "Polygon",

            .properties = {

                {.name="points", .type=TK::String},

                {.name="fill", .type=TK::String},

                {.name="stroke", .type=TK::String},

                {.name="strokeThickness", .type=TK::Double},

            },

            .category = "shapes",

        });



        r.register_control({

            .abstract_name = "Polyline",

            .properties = {

                {.name="points", .type=TK::String},

                {.name="fill", .type=TK::String},

                {.name="stroke", .type=TK::String},

                {.name="strokeThickness", .type=TK::Double},

            },

            .category = "shapes",

        });



        // Icons

        r.register_control({

            .abstract_name = "SymbolIcon",

            .properties = {

                {.name="symbol", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

                {.name="color", .type=TK::String},

            },

            .category = "icons",

        });



        r.register_control({

            .abstract_name = "FontIcon",

            .properties = {

                {.name="glyph", .type=TK::String},

                {.name="fontSize", .type=TK::Double},

                {.name="fontFamily", .type=TK::String},

            },

            .category = "icons",

        });



        r.register_control({

            .abstract_name = "BitmapIcon",

            .properties = {

                {.name="source", .type=TK::String},

                {.name="showAsMonochrome", .type=TK::Bool},

                {.name="color", .type=TK::String},

            },

            .category = "icons",

        });



        r.register_control({

            .abstract_name = "PathIcon",

            .properties = {

                {.name="data", .type=TK::String},

                {.name="color", .type=TK::String},

            },

            .category = "icons",

        });



        // Motion & special elements

        r.register_control({

            .abstract_name = "AnimatedIcon",

            .properties = {

                {.name="source", .type=TK::String},

                {.name="autoPlay", .type=TK::Bool},

                {.name="isPlaying", .type=TK::Bool},

            },

            .events = {

                {.name="onLoaded"},

                {.name="onUnloaded"},

            },

            .accepts_children = true,

            .category = "motion",

        });



        r.register_control({

            .abstract_name = "ParallaxView",

            .properties = {

                {.name="source", .type=TK::String},

                {.name="horizontalShift", .type=TK::Double},

                {.name="verticalShift", .type=TK::Double},

                {.name="isHorizontalShiftClamped", .type=TK::Bool},

                {.name="isVerticalShiftClamped", .type=TK::Bool},

            },

            .accepts_children = true,

            .category = "motion",

        });

    }



    static auto register_builtin_modifiers(ControlRegistry& r) -> void {

        using TK = ast::TypeKind;



        // Text styling

        r.register_modifier({.name="fontSize",   .param_types={TK::Double}});

        r.register_modifier({.name="fontWeight",  .param_types={TK::String}});

        r.register_modifier({.name="fontFamily",  .param_types={TK::String}});

        r.register_modifier({.name="italic"});

        r.register_modifier({.name="bold"});



        // Colors

        r.register_modifier({.name="foregroundColor", .param_types={TK::String}});

        r.register_modifier({.name="backgroundColor", .param_types={TK::String}});
        r.register_modifier({.name="background", .param_types={TK::String}});



        // Layout

        r.register_modifier({.name="padding",      .param_types={TK::Double}});
        r.register_modifier({.name="margin",       .param_types={TK::Double}});

        r.register_modifier({.name="width",         .param_types={TK::Double}});

        r.register_modifier({.name="height",        .param_types={TK::Double}});

        r.register_modifier({.name="minWidth",      .param_types={TK::Double}});

        r.register_modifier({.name="minHeight",     .param_types={TK::Double}});

        r.register_modifier({.name="maxWidth",      .param_types={TK::Double}});

        r.register_modifier({.name="maxHeight",     .param_types={TK::Double}});

        r.register_modifier({.name="alignment",     .param_types={TK::String}});



        // Appearance

        r.register_modifier({.name="cornerRadius",  .param_types={TK::Double}});

        r.register_modifier({.name="opacity",       .param_types={TK::Double}});

        r.register_modifier({.name="border",        .param_types={TK::Double, TK::String}});

        r.register_modifier({.name="shadow",        .param_types={TK::Double}});

        r.register_modifier({.name="style",         .param_types={TK::String}});



        // Visibility & State

        r.register_modifier({.name="visible",       .param_types={TK::Bool}});

        r.register_modifier({.name="disabled",      .param_types={TK::Bool}});

        r.register_modifier({.name="tooltip",       .param_types={TK::String}});



        // Events (closures)

        r.register_modifier({.name="onClick",       .is_event=true});

        r.register_modifier({.name="onChanged",     .is_event=true});

        r.register_modifier({.name="onAppear",      .is_event=true});

        r.register_modifier({.name="onDisappear",   .is_event=true});

    }



    // WinUI3 Platform Backend (compiled-in, zero runtime cost)

    // Maps every abstract control to the real Microsoft.UI.Xaml type.

    // The compiler never calls this during compilation — it is data for

    // downstream consumers (C++/WinRT codegen, C# projection, Rust bindgen,

    // Python wrapper, etc.) that read the WinUIAttribute JSON blob

    // from the emitted WinMD and resolve abstract names via this backend.

    //

    // Adding a new backend (e.g. "qt", "web", "maui"):

    //   Call ControlRegistry::load_backend_from_json(reg, json_string) at

    //   startup, or write a parallel register_<platform>_backend().

    //   The compiler remains completely untouched.



    static auto register_winui3_backend(ControlRegistry& r) -> void {

        PlatformBackend be;

        be.name        = "winui3";

        be.description = "WinUI 3 / C++/WinRT — Microsoft.UI.Xaml mapping (Windows App SDK)";



        // Convenience lambda so we do not repeat the namespace string 23 times

        static constexpr std::string_view NS  = "Microsoft.UI.Xaml.Controls";



        // Layout

        be.controls.push_back({

            .abstract_name      = "Column",

            .platform_type      = "StackPanel",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"spacing",   "Spacing",   "double"},

                {"padding",   "Padding",   "thickness"},

                {"alignment", "HorizontalAlignment", "enum"},

            },

            .fixed_properties = {

                {"Orientation", "Vertical"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Row",

            .platform_type      = "StackPanel",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"spacing",   "Spacing",   "double"},

                {"padding",   "Padding",   "thickness"},

                {"alignment", "VerticalAlignment", "enum"},

            },

            .fixed_properties = {

                {"Orientation", "Horizontal"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Stack",

            .platform_type      = "StackPanel",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"alignment", "HorizontalAlignment", "enum"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Grid",

            .platform_type      = "Grid",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                // columns/rows handled by codegen via ColumnDefinitions/RowDefinitions

                {"spacing",   "ColumnSpacing", "double"},            },
        });



        be.controls.push_back({

            .abstract_name      = "Frame",

            .platform_type      = "Frame",

            .platform_namespace = std::string(NS),

        });



        be.controls.push_back({

            .abstract_name      = "Scroll",

            .platform_type      = "ScrollViewer",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                // "direction" → codegen sets HorizontalScrollBarVisibility / VerticalScrollBarVisibility

                {"direction", "HorizontalScrollBarVisibility", "scroll_direction"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Spacer",

            .platform_type      = "Rectangle",

            .platform_namespace = "Microsoft.UI.Xaml.Shapes",

            .fixed_properties   = {

                {"HorizontalAlignment", "Stretch"},

                {"VerticalAlignment",   "Stretch"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Divider",

            .platform_type      = "NavigationViewItemSeparator",

            .platform_namespace = std::string(NS),

        });



        be.controls.push_back({

            .abstract_name      = "Card",

            .platform_type      = "Border",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"cornerRadius", "CornerRadius", "corner_radius"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Wrap",

            .platform_type      = "VariableSizedWrapGrid",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"itemWidth",             "ItemWidth",             "double"},

                {"itemHeight",            "ItemHeight",            "double"},

                {"maximumRowsOrColumns",  "MaximumRowsOrColumns",  "int"},

                {"orientation",           "Orientation",           "enum"},

            },

        });



        // Content

        be.controls.push_back({

            .abstract_name      = "Label",

            .platform_type      = "TextBlock",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"text",        "Text",           ""},

                {"content",     "Text",           ""},

                {"fontSize",    "FontSize",       "double"},

                {"fontWeight",  "FontWeight",     "font_weight"},

                {"color",       "Foreground",     "brush"},

                {"maxLines",    "MaxLines",       "int"},

                {"wrapping",    "TextWrapping",   "enum"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Icon",

            .platform_type      = "FontIcon",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"glyph",       "Glyph",          ""},

                {"fontSize",    "FontSize",       "double"},

                {"color",       "Foreground",     "brush"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Image",

            .platform_type      = "Image",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"source",  "Source",  "bitmap_image"},

                {"width",   "Width",   "double"},

                {"height",  "Height",  "double"},

                {"fit",     "Stretch", "enum"},   // "fill"→Fill, "fit"→Uniform, "crop"→UniformToFill

            },

        });



        // Input

        be.controls.push_back({

            .abstract_name      = "Button",

            .platform_type      = "Button",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",     "Content",    "box"},

                {"style",       "Style",      "resource"},

                {"width",       "Width",      "double"},

                {"isEnabled",   "IsEnabled",  ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Input",

            .platform_type      = "TextBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"placeholder", "PlaceholderText", ""},

                {"text",        "Text",            ""},

                {"isReadOnly",  "IsReadOnly",      ""},

            },

            .event_mappings = {

                {"onChanged", "TextChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "TextArea",

            .platform_type      = "TextBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"placeholder", "PlaceholderText",   ""},

                {"text",        "Text",              ""},

                {"maxLength",   "MaxLength",         "int"},

            },

            .event_mappings = {

                {"onChanged", "TextChanged"},

            },

            .fixed_properties = {

                {"AcceptsReturn", "true"},

                {"TextWrapping",  "Wrap"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Toggle",

            .platform_type      = "ToggleSwitch",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"label",   "Header",  "box"},

                {"content", "Header",  "box"},

                {"isOn",    "IsOn",    ""},

            },

            .event_mappings = {

                {"onToggled", "Toggled"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Slider",

            .platform_type      = "Slider",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"value",   "Value",          "double"},

                {"minimum", "Minimum",        "double"},

                {"maximum", "Maximum",        "double"},

                {"step",    "StepFrequency",  "double"},

            },

            .event_mappings = {

                {"onChanged", "ValueChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Checkbox",

            .platform_type      = "CheckBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"label",       "Content",     "box"},

                {"content",     "Content",     "box"},

                {"isChecked",   "IsChecked",   "nullable_bool"},

            },

            .event_mappings = {

                {"onChanged", "Checked"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Picker",

            .platform_type      = "ComboBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"selectedIndex", "SelectedIndex", "int"},

                {"header",        "Header",        "box"},

            },

            .event_mappings = {

                {"onSelectionChanged", "SelectionChanged"},

            },

        });



        // Navigation

        be.controls.push_back({

            .abstract_name      = "TabView",

            .platform_type      = "TabView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"selectedIndex", "SelectedIndex", "int"},

            },

            .event_mappings = {

                {"onTabChanged", "SelectionChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Tab",

            .platform_type      = "TabViewItem",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content", "Header", "box"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "TabViewItem",

            .platform_type      = "TabViewItem",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content", "Header", "box"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "NavView",

            .platform_type      = "NavigationView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"header",          "Header",              "box"},

                {"isBackVisible",   "IsBackButtonVisible", "nav_back_enum"},

            },

            .event_mappings = {

                {"onNavigated", "ItemInvoked"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Link",

            .platform_type      = "HyperlinkButton",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"text",    "Content",     "box"},

                {"content", "Content",     "box"},

                {"url",     "NavigateUri", "uri"},

            },

        });



        // Feedback

        be.controls.push_back({

            .abstract_name      = "Progress",

            .platform_type      = "ProgressRing",   // default; codegen switches to ProgressBar when style="bar"

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"value",             "Value",           "double"},

                {"isIndeterminate",   "IsIndeterminate", ""},

                // "style" → "bar" emits ProgressBar instead of ProgressRing (handled by codegen)

                {"style",             "__progress_style", "progress_style"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "InfoBar",

            .platform_type      = "InfoBar",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"title",    "Title",    ""},

                {"message",  "Message",  ""},

                {"severity", "Severity", "info_bar_severity"},

                {"isOpen",   "IsOpen",   ""},

            },

            .event_mappings = {

                {"onClosed", "Closed"},

            },

        });



        // Real WinUI 3 Controls — WinUI Gallery catalog

        // abstract_name == platform_type == real XAML type name, so any

        // projection consumes these with zero translation.



        // Menus & toolbars

        be.controls.push_back({

            .abstract_name      = "AppBarButton",

            .platform_type      = "AppBarButton",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"label",      "Label",       ""},

                {"icon",       "Icon",        "box"},

                {"isCompact",  "IsCompact",   ""},

                {"isEnabled",  "IsEnabled",   ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "AppBarSeparator",

            .platform_type      = "AppBarSeparator",

            .platform_namespace = std::string(NS),

        });



        be.controls.push_back({

            .abstract_name      = "AppBarToggleButton",

            .platform_type      = "AppBarToggleButton",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"label",      "Label",       "box"},

                {"icon",       "Icon",        "box"},

                {"isChecked",  "IsChecked",   ""},

                {"isCompact",  "IsCompact",   ""},

            },

            .event_mappings = {

                {"onClick",     "Click"},

                {"onChecked",   "Checked"},

                {"onUnchecked", "Unchecked"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "CommandBar",

            .platform_type      = "CommandBar",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"isOpen",                "IsOpen",                ""},

                {"isSticky",              "IsSticky",              ""},

                {"defaultLabelPosition",  "DefaultLabelPosition",  "command_bar_label_position"},

            },

            .event_mappings = {

                {"onOpening", "Opening"},

                {"onClosing", "Closing"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "CommandBarFlyout",

            .platform_type      = "CommandBarFlyout",

            .platform_namespace = std::string(NS),

        });



        be.controls.push_back({

            .abstract_name      = "MenuBar",

            .platform_type      = "MenuBar",

            .platform_namespace = std::string(NS),

        });



        be.controls.push_back({

            .abstract_name      = "MenuBarItem",

            .platform_type      = "MenuBarItem",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",   "Title",   ""},

                {"isEnabled", "IsEnabled", ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "MenuFlyoutItem",

            .platform_type      = "MenuFlyoutItem",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",   "Text",   ""},

                {"isEnabled", "IsEnabled", ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "MenuItem",

            .platform_type      = "MenuBarItem",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",   "Title",   ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "MenuFlyout",

            .platform_type      = "MenuFlyout",

            .platform_namespace = std::string(NS),

        });



        be.controls.push_back({

            .abstract_name      = "SwipeControl",

            .platform_type      = "SwipeControl",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"mode",      "Mode",      "swipe_mode"},

                {"isEnabled", "IsEnabled", ""},

            },

        });



        be.controls.push_back({

            .abstract_name      = "SwipeItem",

            .platform_type      = "SwipeItem",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content", "Text", ""},

            },

            .event_mappings = {

                {"onInvoked", "Invoked"},

            },

        });



        // Collections

        be.controls.push_back({

            .abstract_name      = "FlipView",

            .platform_type      = "FlipView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"selectedIndex", "SelectedIndex", "int"},

            },

            .event_mappings = {

                {"onSelectionChanged", "SelectionChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "GridView",

            .platform_type      = "GridView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"selectedIndex", "SelectedIndex", "int"},

                {"itemsSource",   "ItemsSource",    ""},

                {"itemWidth",     "ItemWidth",      "double"},

                {"itemHeight",    "ItemHeight",     "double"},

            },

            .event_mappings = {

                {"onSelectionChanged", "SelectionChanged"},

                {"onItemClick",        "ItemClick"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ItemsRepeater",

            .platform_type      = "ItemsRepeater",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"itemsSource", "ItemsSource", "box"},

                {"layout",      "Layout",      ""},

                {"orientation", "Orientation", "enum"},

                {"spacing",     "Spacing",     "double"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ItemsView",

            .platform_type      = "ItemsView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"itemsSource",   "ItemsSource",   ""},

            },

            .event_mappings = {

                {"onSelectionChanged", "SelectionChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ListView",

            .platform_type      = "ListView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"selectedIndex",      "SelectedIndex",      "int"},

                {"itemsSource",        "ItemsSource",        ""},

                {"isMultiSelect",      "IsMultiSelect",      ""},

                {"isItemClickEnabled", "IsItemClickEnabled", ""},

            },

            .event_mappings = {

                {"onSelectionChanged", "SelectionChanged"},

                {"onItemClick",        "ItemClick"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "TreeView",

            .platform_type      = "TreeView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"itemsSource", "ItemsSource", "box"},

            },

            .event_mappings = {

                {"onItemInvoked", "ItemInvoked"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "RefreshContainer",

            .platform_type      = "RefreshContainer",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"pullDirection", "PullDirection", "refresh_pull_direction"},

                {"visualizer",    "Visualizer",    ""},

            },

            .event_mappings = {

                {"onRefreshRequested", "RefreshRequested"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "RefreshVisualizer",

            .platform_type      = "RefreshVisualizer",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"orientation", "Orientation", "refresh_visualizer_orientation"},

                {"state",       "State",       "refresh_visualizer_state"},

            },

        });



        // Date & time

        be.controls.push_back({

            .abstract_name      = "CalendarDatePicker",

            .platform_type      = "CalendarDatePicker",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"date",               "Date",               "datetime"},

                {"placeholder",        "PlaceholderText",    ""},

                {"isCalendarOpen",     "IsCalendarOpen",     ""},

                {"isTodayHighlighted", "IsTodayHighlighted", ""},

            },

            .event_mappings = {

                {"onDateChanged", "DateChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "CalendarView",

            .platform_type      = "CalendarView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"selectedDate",       "SelectedDates",      ""},

                {"displayMode",        "DisplayMode",        "calendar_view_display_mode"},

                {"isTodayHighlighted", "IsTodayHighlighted", ""},

            },

            .event_mappings = {

                {"onSelectedDatesChanged", "SelectedDatesChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "DatePicker",

            .platform_type      = "DatePicker",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"date",          "Date",          "datetime"},

                {"header",        "Header",        "box"},

                {"dayVisible",    "DayVisible",    ""},

                {"monthVisible",  "MonthVisible",  ""},

                {"yearVisible",   "YearVisible",   ""},

            },

            .event_mappings = {

                {"onDateChanged", "DateChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "TimePicker",

            .platform_type      = "TimePicker",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"time",             "Time",             "datetime"},

                {"header",           "Header",           "box"},

                {"minuteIncrement",  "MinuteIncrement",  "int"},

                {"clockIdentifier",  "ClockIdentifier",  ""},

            },

            .event_mappings = {

                {"onTimeChanged", "TimeChanged"},

            },

        });



        // Basic input (real names)

        be.controls.push_back({

            .abstract_name      = "DropDownButton",

            .platform_type      = "DropDownButton",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",   "Content",   "box"},

                {"flyout",    "Flyout",    ""},

                {"isEnabled", "IsEnabled", ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "RepeatButton",

            .platform_type      = "RepeatButton",

            .platform_namespace = "Microsoft.UI.Xaml.Controls.Primitives",

            .property_mappings  = {

                {"content",   "Content",   "box"},

                {"delay",     "Delay",     "int"},

                {"interval",  "Interval",  "int"},

                {"isEnabled", "IsEnabled", ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ToggleButton",

            .platform_type      = "ToggleButton",

            .platform_namespace = "Microsoft.UI.Xaml.Controls.Primitives",

            .property_mappings  = {

                {"content",   "Content",   "box"},

                {"isChecked", "IsChecked", "nullable_bool"},

                {"isEnabled", "IsEnabled", ""},

            },

            .event_mappings = {

                {"onClick",     "Click"},

                {"onChecked",   "Checked"},

                {"onUnchecked", "Unchecked"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "SplitButton",

            .platform_type      = "SplitButton",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",   "Content",   "box"},

                {"flyout",    "Flyout",    ""},

                {"isEnabled", "IsEnabled", ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ToggleSplitButton",

            .platform_type      = "ToggleSplitButton",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",   "Content",   "box"},

                {"isChecked", "IsChecked", "nullable_bool"},

                {"isEnabled", "IsEnabled", ""},

            },

            .event_mappings = {

                {"onClick",     "Click"},

                {"onChecked",   "Checked"},

                {"onUnchecked", "Unchecked"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "CheckBox",

            .platform_type      = "CheckBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",   "Content",   "box"},

                {"isChecked", "IsChecked", "nullable_bool"},

                {"isEnabled", "IsEnabled", ""},

            },

            .event_mappings = {

                {"onChecked",   "Checked"},

                {"onUnchecked", "Unchecked"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ColorPicker",

            .platform_type      = "ColorPicker",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"color",                    "Color",                    "brush"},

                {"isAlphaEnabled",           "IsAlphaEnabled",           ""},

                {"isColorSpectrumVisible",   "IsColorSpectrumVisible",   ""},

            },

            .event_mappings = {

                {"onColorChanged", "ColorChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ComboBox",

            .platform_type      = "ComboBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"selectedIndex", "SelectedIndex", "int"},

                {"header",        "Header",        "box"},

                {"placeholder",   "PlaceholderText",""},

                {"isEditable",    "IsEditable",    ""},

            },

            .event_mappings = {

                {"onSelectionChanged", "SelectionChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "RadioButton",

            .platform_type      = "RadioButton",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",   "Content",   "box"},

                {"groupName", "GroupName", ""},

                {"isChecked", "IsChecked", "nullable_bool"},

                {"isEnabled", "IsEnabled", ""},

            },

            .event_mappings = {

                {"onChecked",   "Checked"},

                {"onUnchecked", "Unchecked"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "RatingControl",

            .platform_type      = "RatingControl",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"value",            "Value",            "double"},

                {"maxRating",        "MaxRating",        "int"},

                {"isReadOnly",       "IsReadOnly",       ""},

                {"isClearEnabled",   "IsClearEnabled",   ""},

            },

            .event_mappings = {

                {"onValueChanged", "ValueChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ToggleSwitch",

            .platform_type      = "ToggleSwitch",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"header",     "Header",     "box"},

                {"isOn",       "IsOn",       ""},

                {"onContent",  "OnContent",  "box"},

                {"offContent", "OffContent", "box"},

                {"isEnabled",  "IsEnabled",  ""},

            },

            .event_mappings = {

                {"onToggled", "Toggled"},

            },

        });



        // Status & info

        be.controls.push_back({

            .abstract_name      = "InfoBadge",

            .platform_type      = "InfoBadge",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"value",            "Value",            "int"},

                {"isEnabled",        "IsEnabled",        ""},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ProgressBar",

            .platform_type      = "ProgressBar",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"value",            "Value",            "double"},

                {"isIndeterminate",  "IsIndeterminate",  ""},

                {"showPaused",       "ShowPaused",       ""},

                {"showError",        "ShowError",        ""},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ProgressRing",

            .platform_type      = "ProgressRing",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"value",            "Value",            "double"},

                {"isIndeterminate",  "IsIndeterminate",  ""},

                {"isActive",         "IsActive",         ""},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ToolTip",

            .platform_type      = "ToolTip",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",   "Content",   "box"},

                {"placement", "Placement", "placement_mode"},

                {"isOpen",    "IsOpen",    ""},

            },

            .event_mappings = {

                {"onOpened", "Opened"},

                {"onClosed", "Closed"},

            },

        });



        // Dialogs & flyouts

        be.controls.push_back({

            .abstract_name      = "ContentDialog",

            .platform_type      = "ContentDialog",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"title",                   "Title",                   "box"},

                {"content",                 "Content",                 "box"},

                {"primaryButtonText",       "PrimaryButtonText",       ""},

                {"secondaryButtonText",     "SecondaryButtonText",     ""},

                {"closeButtonText",         "CloseButtonText",         ""},

                {"isPrimaryButtonEnabled",  "IsPrimaryButtonEnabled",  ""},

            },

            .event_mappings = {

                {"onPrimaryButtonClick",   "PrimaryButtonClick"},

                {"onSecondaryButtonClick", "SecondaryButtonClick"},

                {"onCloseButtonClick",     "CloseButtonClick"},

                {"onOpened",               "Opened"},

                {"onClosed",               "Closed"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Flyout",

            .platform_type      = "Flyout",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"placement", "Placement", "placement_mode"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Popup",

            .platform_type      = "Popup",

            .platform_namespace = "Microsoft.UI.Xaml.Controls.Primitives",

            .property_mappings  = {

                {"isOpen",                 "IsOpen",                 ""},

                {"isLightDismissEnabled",  "IsLightDismissEnabled",  ""},

            },

            .event_mappings = {

                {"onClosed", "Closed"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "TeachingTip",

            .platform_type      = "TeachingTip",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"title",               "Title",               ""},

                {"subtitle",            "Subtitle",            ""},

                {"content",             "Content",             "box"},

                {"isOpen",              "IsOpen",              ""},

                {"preferredPlacement",  "PreferredPlacement",  "teaching_tip_placement"},

            },

            .event_mappings = {

                {"onClosed", "Closed"},

            },

        });



        // Scrolling

        be.controls.push_back({

            .abstract_name      = "AnnotatedScrollBar",

            .platform_type      = "AnnotatedScrollBar",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"isEnabled",   "IsEnabled",   ""},

            },

            .event_mappings = {

                {"onValueChanged", "ValueChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "PipsPager",

            .platform_type      = "PipsPager",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"numberOfPages",          "NumberOfPages",          "int"},

                {"selectedPageIndex",      "SelectedPageIndex",      "int"},

                {"previousButtonVisibility","PreviousButtonVisibility","pips_pager_buttons"},

                {"nextButtonVisibility",   "NextButtonVisibility",   "pips_pager_buttons"},

                {"orientation",            "Orientation",            "enum"},

            },

            .event_mappings = {

                {"onSelectedIndexChanged", "SelectedIndexChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ScrollView",

            .platform_type      = "ScrollView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"contentOrientation",        "ContentOrientation",        "scroll_content_orientation"},

                {"horizontalScrollBarVisibility", "HorizontalScrollBarVisibility", "scroll_bar_visibility"},

                {"verticalScrollBarVisibility",   "VerticalScrollBarVisibility",   "scroll_bar_visibility"},

                {"zoomMode",                  "ZoomMode",                  "zoom_mode"},

                {"minZoomFactor",             "MinZoomFactor",             "double"},

                {"maxZoomFactor",             "MaxZoomFactor",             "double"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ScrollViewer",

            .platform_type      = "ScrollViewer",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"horizontalScrollBarVisibility", "HorizontalScrollBarVisibility", "scroll_bar_visibility"},

                {"verticalScrollBarVisibility",   "VerticalScrollBarVisibility",   "scroll_bar_visibility"},

                {"horizontalScrollMode",          "HorizontalScrollMode",          "scroll_mode"},

                {"verticalScrollMode",            "VerticalScrollMode",            "scroll_mode"},

                {"zoomMode",                      "ZoomMode",                      "zoom_mode"},

                {"isZoomChainingEnabled",         "IsZoomChainingEnabled",         ""},

            },

        });



        be.controls.push_back({

            .abstract_name      = "SemanticZoom",

            .platform_type      = "SemanticZoom",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"isZoomedInViewActive", "IsZoomedInViewActive", ""},

            },

            .event_mappings = {

                {"onViewChangeStarted",   "ViewChangeStarted"},

                {"onViewChangeCompleted", "ViewChangeCompleted"},

            },

        });



        // Layout (real names)

        be.controls.push_back({

            .abstract_name      = "Border",

            .platform_type      = "Border",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"background",      "Background",      "brush"},

                {"cornerRadius",    "CornerRadius",    "corner_radius"},

                {"borderBrush",     "BorderBrush",     "brush"},

                {"borderThickness", "BorderThickness", "thickness"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Canvas",

            .platform_type      = "Canvas",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"left", "Left", "double"},

                {"top",  "Top",  "double"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Expander",

            .platform_type      = "Expander",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"header",                    "Header",                    "box"},

                {"isExpanded",                "IsExpanded",                ""},

                {"horizontalHeaderAlignment", "HorizontalHeaderAlignment", "enum"},

            },

            .event_mappings = {

                {"onExpanding",  "Expanding"},

                {"onExpanded",   "Expanded"},

                {"onCollapsing", "Collapsing"},

                {"onCollapsed",  "Collapsed"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "RelativePanel",

            .platform_type      = "RelativePanel",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"alignLeftWith",   "AlignLeftWith",   ""},

                {"alignTopWith",    "AlignTopWith",    ""},

                {"alignRightWith",  "AlignRightWith",  ""},

                {"alignBottomWith", "AlignBottomWith", ""},

            },

        });



        be.controls.push_back({

            .abstract_name      = "SplitView",

            .platform_type      = "SplitView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"isPaneOpen",         "IsPaneOpen",         ""},

                {"panePlacement",      "PanePlacement",      "split_view_placement"},

                {"displayMode",        "DisplayMode",        "split_view_display_mode"},

                {"openPaneLength",     "OpenPaneLength",     "double"},

                {"compactPaneLength",  "CompactPaneLength",  "double"},

            },

            .event_mappings = {

                {"onPaneOpening", "PaneOpening"},

                {"onPaneOpened",  "PaneOpened"},

                {"onPaneClosing", "PaneClosing"},

                {"onPaneClosed",  "PaneClosed"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "StackPanel",

            .platform_type      = "StackPanel",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"orientation", "Orientation", "enum"},

                {"spacing",     "Spacing",     "double"},

                {"alignment",   "HorizontalAlignment", "enum"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "VariableSizedWrapGrid",

            .platform_type      = "VariableSizedWrapGrid",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"itemWidth",            "ItemWidth",            "double"},

                {"itemHeight",           "ItemHeight",           "double"},

                {"maximumRowsOrColumns", "MaximumRowsOrColumns", "int"},

                {"orientation",          "Orientation",          "enum"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Viewbox",

            .platform_type      = "Viewbox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"stretch",           "Stretch",           "stretch"},

                {"stretchDirection",  "StretchDirection",  "stretch_direction"},

            },

        });



        // Navigation (real names)

        be.controls.push_back({

            .abstract_name      = "BreadcrumbBar",

            .platform_type      = "BreadcrumbBar",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"itemsSource", "ItemsSource", "box"},

            },

            .event_mappings = {

                {"onItemClicked", "ItemClicked"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "NavigationView",

            .platform_type      = "NavigationView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {                {"header",               "Header",               "box"},
                {"isBackButtonVisible",  "IsBackButtonVisible",  "nav_back_enum"},
                {"isSettingsVisible",    "IsSettingsVisible",    ""},
                {"isPaneOpen",           "IsPaneOpen",           ""},
                {"paneDisplayMode",      "PaneDisplayMode",      "nav_pane_display_mode"},
                {"background",           "Background",           "brush"},

            },

            .event_mappings = {

                {"onItemInvoked",       "ItemInvoked"},

                {"onBackRequested",     "BackRequested"},

                {"onSelectionChanged",  "SelectionChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "NavViewItem",

            .platform_type      = "NavigationViewItem",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",    "Content",    "box"},

                {"isSelected", "IsSelected", ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "NavigationViewItem",

            .platform_type      = "NavigationViewItem",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"content",    "Content",    "box"},

                {"isSelected", "IsSelected", ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Pivot",

            .platform_type      = "Pivot",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"title",                           "Title",                           "box"},

                {"selectedIndex",                   "SelectedIndex",                   "int"},

                {"isHeaderItemsCarouselEnabled",    "IsHeaderItemsCarouselEnabled",    ""},

            },

            .event_mappings = {

                {"onSelectionChanged", "SelectionChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "SelectorBar",

            .platform_type      = "SelectorBar",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

            },

            .event_mappings = {

                {"onSelectionChanged", "SelectionChanged"},

            },

        });



        // Media

        be.controls.push_back({

            .abstract_name      = "AnimatedVisualPlayer",

            .platform_type      = "AnimatedVisualPlayer",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"source",       "Source",       "skip"},

                {"autoPlay",     "AutoPlay",     ""},

                {"isPlaying",    "IsPlaying",    ""},

                {"playbackRate", "PlaybackRate", "double"},

            },

            .event_mappings = {

                {"onLoaded",   "Loaded"},

                {"onUnloaded", "Unloaded"},

            },

        });

        be.controls.push_back({

            .abstract_name      = "MediaPlayerElement",

            .platform_type      = "MediaPlayerElement",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"source",       "Source",       "skip"},

                {"autoPlay",     "AutoPlay",     ""},

                {"isFullWindow", "IsFullWindow", ""},

                {"isMuted",      "IsMuted",      ""},

            },

            .event_mappings = {

                {"onMediaOpened", "MediaOpened"},

                {"onMediaEnded",  "MediaEnded"},

                {"onMediaFailed", "MediaFailed"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "PersonPicture",

            .platform_type      = "PersonPicture",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"profilePicture", "ProfilePicture", "bitmap_image"},

                {"displayName",    "DisplayName",    ""},

                {"initials",       "Initials",       ""},

                {"isEnabled",      "IsEnabled",      ""},

            },

            .event_mappings = {

                {"onClick", "Click"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "WebView2",

            .platform_type      = "WebView2",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"source",                  "Source",                  "uri"},

                {"defaultBackgroundColor",  "DefaultBackgroundColor",  "brush"},

                {"isVisible",               "Visibility",              "visibility"},

            },

            .event_mappings = {

                {"onNavigationCompleted",  "NavigationCompleted"},

                {"onNavigationStarting",   "NavigationStarting"},

                {"onWebMessageReceived",   "WebMessageReceived"},

            },

        });



        // Text (real names)

        be.controls.push_back({

            .abstract_name      = "AutoSuggestBox",

            .platform_type      = "AutoSuggestBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"text",                    "Text",                    ""},

                {"placeholder",             "PlaceholderText",         ""},

                {"itemsSource",             "ItemsSource",             ""},

                {"isSuggestionListOpen",    "IsSuggestionListOpen",    ""},

                {"maxSuggestionListHeight", "MaxSuggestionListHeight", "double"},

            },

            .event_mappings = {

                {"onTextChanged",     "TextChanged"},

                {"onQuerySubmitted",  "QuerySubmitted"},

                {"onSuggestionChosen","SuggestionChosen"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "NumberBox",

            .platform_type      = "NumberBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"value",                     "Value",                     "double"},

                {"header",                    "Header",                    "box"},

                {"placeholder",               "PlaceholderText",           ""},

                {"minimum",                   "Minimum",                   "double"},

                {"maximum",                   "Maximum",                   "double"},

                {"step",                      "SmallChange",               "double"},

                {"spinButtonPlacementMode",   "SpinButtonPlacementMode",   "number_box_spin_placement"},

            },

            .event_mappings = {

                {"onValueChanged", "ValueChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "PasswordBox",

            .platform_type      = "PasswordBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"password",                          "Password",                          ""},

                {"placeholder",                       "PlaceholderText",                   ""},

                {"isPasswordRevealButtonEnabled",     "IsPasswordRevealButtonEnabled",     ""},

                {"maxLength",                         "MaxLength",                         "int"},

            },

            .event_mappings = {

                {"onPasswordChanged", "PasswordChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "RichEditBox",

            .platform_type      = "RichEditBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"placeholder",  "PlaceholderText",""},

                {"isReadOnly",   "IsReadOnly",    ""},

                {"maxLength",    "MaxLength",     "int"},

                {"acceptsReturn","AcceptsReturn", ""},

            },

            .event_mappings = {

                {"onTextChanged", "TextChanged"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "RichTextBlock",

            .platform_type      = "RichTextBlock",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"fontSize",    "FontSize",     "double"},

                {"fontWeight",  "FontWeight",   "font_weight"},

                {"color",       "Foreground",   "brush"},

                {"maxLines",    "MaxLines",     "int"},

                {"wrapping",    "TextWrapping", "enum"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "TextBlock",

            .platform_type      = "TextBlock",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"text",                       "Text",                       ""},

                {"fontSize",                   "FontSize",                   "double"},

                {"fontWeight",                 "FontWeight",                 "font_weight"},

                {"color",                      "Foreground",                 "brush"},

                {"maxLines",                   "MaxLines",                   "int"},

                {"wrapping",                   "TextWrapping",               "enum"},

                {"isTextSelectionEnabled",     "IsTextSelectionEnabled",     ""},

            },

        });



        be.controls.push_back({

            .abstract_name      = "TextBox",

            .platform_type      = "TextBox",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"text",         "Text",          ""},

                {"placeholder",  "PlaceholderText",""},

                {"isReadOnly",   "IsReadOnly",    ""},

                {"maxLength",    "MaxLength",     "int"},

                {"acceptsReturn","AcceptsReturn", ""},

                {"textWrapping", "TextWrapping",  "enum"},

            },

            .event_mappings = {

                {"onTextChanged", "TextChanged"},

            },

        });



        // Shapes

        static constexpr std::string_view SHP_NS  = "Microsoft.UI.Xaml.Shapes";



        be.controls.push_back({

            .abstract_name      = "Line",

            .platform_type      = "Line",

            .platform_namespace = std::string(SHP_NS),

            .property_mappings  = {

                {"x1",               "X1",               "double"},

                {"y1",               "Y1",               "double"},

                {"x2",               "X2",               "double"},

                {"y2",               "Y2",               "double"},

                {"stroke",           "Stroke",           "brush"},

                {"strokeThickness",  "StrokeThickness",  "double"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Rectangle",

            .platform_type      = "Rectangle",

            .platform_namespace = std::string(SHP_NS),

            .property_mappings  = {

                {"fill",             "Fill",             "brush"},

                {"stroke",           "Stroke",           "brush"},

                {"strokeThickness",  "StrokeThickness",  "double"},

                {"radiusX",          "RadiusX",          "double"},

                {"radiusY",          "RadiusY",          "double"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Ellipse",

            .platform_type      = "Ellipse",

            .platform_namespace = std::string(SHP_NS),

            .property_mappings  = {

                {"fill",             "Fill",             "brush"},

                {"stroke",           "Stroke",           "brush"},

                {"strokeThickness",  "StrokeThickness",  "double"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Path",

            .platform_type      = "Path",

            .platform_namespace = std::string(SHP_NS),

            .property_mappings  = {

                {"data",             "Data",             "skip"},

                {"fill",             "Fill",             "brush"},

                {"stroke",           "Stroke",           "brush"},

                {"strokeThickness",  "StrokeThickness",  "double"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Polygon",

            .platform_type      = "Polygon",

            .platform_namespace = std::string(SHP_NS),

            .property_mappings  = {

                {"points",           "Points",           "skip"},

                {"fill",             "Fill",             "brush"},

                {"stroke",           "Stroke",           "brush"},

                {"strokeThickness",  "StrokeThickness",  "double"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "Polyline",

            .platform_type      = "Polyline",

            .platform_namespace = std::string(SHP_NS),

            .property_mappings  = {

                {"points",           "Points",           "skip"},

                {"fill",             "Fill",             "brush"},

                {"stroke",           "Stroke",           "brush"},

                {"strokeThickness",  "StrokeThickness",  "double"},

            },

        });



        // Icons

        be.controls.push_back({

            .abstract_name      = "SymbolIcon",

            .platform_type      = "SymbolIcon",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"symbol",    "Symbol",    "symbol"},

                {"fontSize",  "FontSize",  "double"},

                {"color",     "Foreground","brush"},

            },        });



        be.controls.push_back({
            .abstract_name      = "FontIcon",
            .platform_type      = "FontIcon",
            .platform_namespace = std::string(NS),
            .property_mappings  = {
                {"glyph",       "Glyph",       ""},
                {"fontSize",    "FontSize",    "double"},
                {"fontFamily",  "FontFamily",  ""},
            },
        });



        be.controls.push_back({
            .abstract_name      = "BitmapIcon",

            .platform_type      = "BitmapIcon",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"source",            "UriSource",         "uri"},

                {"showAsMonochrome",  "ShowAsMonochrome",  ""},

                {"color",             "Foreground",        "brush"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "PathIcon",

            .platform_type      = "PathIcon",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"data",   "Data",       "skip"},

                {"color",  "Foreground", "brush"},

            },

        });



        // Motion & special elements

        be.controls.push_back({

            .abstract_name      = "AnimatedIcon",

            .platform_type      = "AnimatedIcon",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"source",    "Source",    "skip"},

                {"isPlaying", "IsPlaying", ""},

            },

            .event_mappings = {

                {"onLoaded",   "Loaded"},

                {"onUnloaded", "Unloaded"},

            },

        });



        be.controls.push_back({

            .abstract_name      = "ParallaxView",

            .platform_type      = "ParallaxView",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"source",                  "Source",                  "skip"},

                {"horizontalShift",         "HorizontalShift",         "double"},

                {"verticalShift",           "VerticalShift",           "double"},

                {"isHorizontalShiftClamped","IsHorizontalShiftClamped",""},

                {"isVerticalShiftClamped",  "IsVerticalShiftClamped",  ""},

            },

        });

        be.controls.push_back({

            .abstract_name      = "KeyboardAccelerator",

            .platform_type      = "KeyboardAccelerator",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"key",      "Key",      "string"},

                {"modifiers","Modifiers","string"},

            },

            .event_mappings = {

                {"onInvoked", "Invoked"},

            },

        });

        be.controls.push_back({

            .abstract_name      = "ImageIconSource",

            .platform_type      = "ImageIconSource",

            .platform_namespace = std::string(NS),

            .property_mappings  = {

                {"imageSource", "ImageSource", "string"},

            },

        });

        r.register_backend(std::move(be));

    }

};



} // namespace winui::reg

