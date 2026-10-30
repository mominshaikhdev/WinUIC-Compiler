# WinUIC Compiler

A framework-agnostic, declarative UI domain-specific language (DSL) for **WinUI 3 / Windows App SDK** and multi-platform native backends. `.winui` combines the expressive power of SwiftUI with native, projection-agnostic metadata compilation. The emitted `.winmd` is consumed by **any** language projection — [**CppWinC**](https://github.com/mominshaikhdev/CppWinC-Compiler) (C++23 named modules) for C++, **CsWinC** for C#, Rust (`windows-rs`), Python (`PyWinC`), and future bindings alike.

---

## Table of Contents

1. [Overview](#overview)
2. [Core Philosophy](#core-philosophy)
3. [Key Features & Windows Platform Capabilities](#key-features--windows-platform-capabilities)
4. [A to Z Grammar Reference](#a-to-z-grammar-reference)
   - [A — Attributes & Decorators](#a--attributes--decorators)
   - [B — Body Blocks & Binding References](#b--body-blocks--binding-references)
   - [C — Control Catalog & Callbacks](#c--control-catalog--callbacks)
   - [D — Directives & Dot-Enums](#d--directives--dot-enums)
   - [E — Expressions & Pratt Precedence](#e--expressions--pratt-precedence)
   - [F — ForEach Iteration](#f--foreach-iteration)
   - [G — Grammar EBNF Specification](#g--grammar-ebnf-specification)
   - [H — Header Imports](#h--header-imports)
   - [I — If/Else & Interpolated Strings](#i--ifelse--interpolated-strings)
   - [K — Keywords Catalog](#k--keywords-catalog)
   - [L — Literals & Lexical Rules](#l--literals--lexical-rules)
   - [M — Modifiers Catalog](#m--modifiers-catalog)
   - [P — Properties & Primitive Types](#p--properties--primitive-types)
   - [R — Range Expressions](#r--range-expressions)
   - [S — Statements in Closures](#s--statements-in-closures)
   - [T — Type System](#t--type-system)
   - [V — Views & Version Directives](#v--views--version-directives)
5. [Complete Code Examples](#complete-code-examples)
7. [`winuic.exe` Compiler Architecture & CLI Reference](#winuicexe-compiler-architecture--cli-reference)
8. [Language Projection Owners Guide](#language-projection-owners-guide)
   - [Consuming winuic Types — The A-Z Language-Agnostic Contract](#consuming-winuic-types--the-a-z-language-agnostic-contract)
   - [C++ Projection (CppWinC) — WinUIC .winmd Consumption Contract](#c-projection-cppwinc--winuic-winmd-consumption-contract)

---

## Overview

`.winui` source files are compiled by **`winuic.exe`** into language-agnostic ECMA-335 metadata (`.winmd`), native controls, and packaged resource payloads. The compiler is **completely projection-agnostic**, allowing any target language projection to consume generated UI components seamlessly. The compiler supports 24 built-in abstract controls mapped to native platform controls (e.g. `Column` → `StackPanel`, `Button` → `Button`, `Label` → `TextBlock`) as well as **dynamic registration for any custom third-party WinUI 3 control**.

> [!IMPORTANT]
> **🚀 FLAGSHIP FEATURE: ZERO MANUAL UI CODE IN ANY LANGUAGE PROJECTION**
> **Target language projections (C++, C#, Rust, Python, etc.) NEVER write or touch UI code in main application code.** All native visual trees, control instantiations, reactive state observers (`@state`, `@binding`), and binary metadata (`.winmd`) are **100% automatically generated** by projection-agnostic compilation in `winuic.exe`. Host application entry points only need to initialize the runtime and launch the generated `App` instance.

---

## Core Philosophy

The `WinUIC` compiler engine is built upon five core architectural pillars:

- **⚡ Zero Overhead Compilation**: `WinUIC` compiles `.winui` views directly into native visual tree instantiations and direct reactive property observers. There is no intermediate representation, no diffing algorithm, no reconciliation latency, and zero GC allocation overhead.
- **🎨 Zero XAML**: Eliminates verbose XML markup, XAML namespace management, complex XAML build targets, and design-time XAML compilation steps in favor of a clean, Swift-inspired declarative DSL syntax.
- **📄 Zero IDL**: Generates ECMA-335 `.winmd` binary metadata directly in memory using C++20 compiler modules, eliminating `.idl` file maintenance, `midl.exe` invocations, and MIDLRT build dependencies.
- **🔗 Zero Code-Behind**: Eliminates partial class boilerplate, `.xaml.cs` or `.xaml.cpp` code-behind glue files, and manual event subscription code. All reactive state observers, bindings, and event callbacks are synthesized directly by `winuic.exe`.
- **🌐 Zero Virtual DOM**: Eliminates synthetic tree structures, reconciliation diffing passes, and runtime vDOM overhead in favor of direct, fine-grained native UI property updates.
- **🧩 Business Logic Friendly**: Zero Code-Behind does NOT mean zero business logic. Each compiled view's class in the `.winmd` carries public surface (state variables, event callbacks, methods) that business-logic files in **any** projection consume directly. Backend services, databases, APIs, and other components interact with the UI through that surface — never through glue code. This is the same principle for all projections (CppWinC's C++23 named modules today, PyWinC, windows-rs, etc. tomorrow).
- **Projection-Agnostic & Type-Safe Metadata**: `winuic.exe` emits no language-specific code. Its output is standard, completely projection-agnostic ECMA-335 `.winmd` binary metadata and `resources.pri` payloads. All view structures, properties, event callbacks, and control definitions remain strictly in type-safe metadata tables (TypeDef, MethodDef, Field, Param, CustomAttribute, TypeRef). Downstream language projections (CppWinC for C++23 modules, PyWinC for Python, windows-rs for Rust, C#/WinRT) consume the exact same type-safe `.winmd` with zero language bias and zero runtime translation overhead. Every type signature, callback signature, and property type is preserved with 100% type safety directly in the CLI metadata tables.
- **🏛️ Metadata-First in WinUI 3**: Shifts WinUI 3 completely away from brittle compile-time markup and code-behind coupling to an open, self-describing **metadata-first** architecture. Controls, containers, layouts, and views emit rich ECMA-335 metadata containing full layout specifications (`WinUIAttribute`) and an extensible control catalog (`WinUIControlCatalogAttribute`). Downstream compilers like `cppwinc` dynamically discover controls, resolve namespaces, evaluate properties, instantiate resources, and assemble views directly from metadata without requiring compiler rebuilds for future controls, layouts, or platform releases.

---

## Key Features & Windows Platform Capabilities

The `.winui` compiler suite and runtime framework provide deep OS-level integration and native compilation features:

### 💡 1. Flagship Feature: Projection-Agnostic Zero Manual UI Code
- **Zero UI Code in Application Code**: Developers and language projections never touch, construct, or layout UI controls manually.
- **Automatic Code & Metadata Generation**: `winuic.exe` parses clean, declarative `.winui` component files and automatically synthesizes all UI control instantiation, reactive property observers, event handler hooks, and binary metadata (`.winmd`).
- **100% Projection-Agnostic**: Emits standardized ECMA-335 metadata (`.winmd`) and packaged resources (`resources.pri`) so any language projection automatically constructs the entire UI visual tree upon runtime class instantiation.

### 2. Per-Project `resources.pri` Generation
- **Automated Per-Project Resource Indexing**: Dynamically indexes all emitted `.winmd` metadata files, XAML view assets, and app resources on a per-project output directory basis (`winuic.exe -o ./out`).
- **Exclusion Filtering**: Automatically excludes non-resource build outputs (`.exe`, `.dll`, `.obj`, `.pdb`, `.log`, `.exp`, `.lib`, `.ifc`, `.iobj`, `.ipdb`, `.pri`), ensuring `resources.pri` only packages genuine application assets.
- **Native Windows MRM Engine Integration**: In-RAM loading of Windows SDK `mrmsupport.dll` native APIs (`MrmCreateResourceIndexer`, `MrmIndexFileAutoQualifiers`, `MrmCreateResourceFile`) to package standalone `resources.pri` binaries without spawning external console processes.
- **In-Memory Binary PRI Fallback Encoder**: A zero-dependency fallback encoder (`mrm_pri2`) that formats and writes binary PRI payload structures directly in RAM to target project directories when SDK DLLs are absent.

### 3. Immersive Dark Titlebar Attribute (`WinUIDarkTitlebarAttribute`)
- **Metadata Serialization**: `winmd_emitter.ixx` serializes `WinUIDarkTitlebarAttribute` into emitted `.winmd` metadata.
- **Projection Execution**: Target language projections parse this attribute from metadata to emit native Desktop Window Manager (`DWMWA_USE_IMMERSIVE_DARK_MODE` = 20) titlebar dark mode initialization automatically inside view initialization.
- **Theme Synchronization**: Titlebar text, caption control buttons (Minimize, Maximize, Close), and background colors dynamically align with dark theme settings without any manual UI or windowing code.

### 4. Application Icon Attribute (`WinUIAppIconAttribute`)
- **Metadata Serialization**: `winmd_emitter.ixx` serializes `WinUIAppIconAttribute` into emitted `.winmd` metadata.
- **Projection Execution**: Target language projections parse this attribute from metadata to emit window message icon bindings (`WM_SETICON` with `ICON_BIG` / `ICON_SMALL`) linking `LoadIconW` executable resources (`1`) automatically inside view initialization.
- **Titlebar & Taskbar Association**: Automatically applies application icon assets to titlebars, Alt+Tab task switchers, and the Windows Taskbar across unpackaged and packaged deployments.

### 5. Dark Context Menu Attribute (`WinUIDarkContextMenuAttribute`)
- **Metadata Serialization**: `winmd_emitter.ixx` serializes `WinUIDarkContextMenuAttribute` into emitted `.winmd` metadata.
- **Projection Execution**: Target language projections parse this attribute from metadata to emit system theme calls (`uxtheme.dll` `SetPreferredAppMode` / `AllowDarkModeForWindow`) automatically inside view initialization.
- **Consistent Dark Popup Styling**: Ensures right-click context menus, dropdown flyouts, text menu items, and popups render with full dark mode styling rather than legacy Win32 light popup menus.

### 6. Component Layout Attribute (`WinUIAttribute`)
- **Metadata Serialization**: Encodes component layout tree JSON payload directly into ECMA-335 `.winmd` metadata.
- **Projection Execution**: Target language projections read the layout tree payload from metadata and generate native UI control instantiation statements and `Content()` panel assignment automatically inside view initialization.

### 7. Control Catalog Attribute (`WinUIControlCatalogAttribute`)
- **Metadata Serialization**: `winmd_emitter.ixx` serializes the **entire control registry** — every abstract control schema, platform mappings, registered platform `resource_dictionaries`, and control-level `resource_dictionary` bindings — as a JSON blob into the `.winmd`, attached to the **`<Module>` TypeDef row (exactly once per module)**.
- **Projection Execution**: Target language projections read this module-level attribute once at assembly load to build their abstract → platform control table, resolving every control name and instantiating all required resource dictionaries found in `WinUIControlCatalogAttribute` **without hardcoding any mapping**.
- **Self-Describing Metadata & Unknown Control Resources**: New controls and specialized platform resource dictionaries added to `winuic.exe` or third-party component libraries propagate to all projections automatically with zero projection rebuilds.

### 10. Glob Pattern & Directory Scanning
- **Glob Expansion**: The CLI accepts wildcard patterns (`*.winui`, `page*.winui`) even when the shell doesn't expand them — the compiler implements its own `*` and `?` pattern matching.
- **Recursive Directory Scan**: Passing a directory recursively scans it for all `.winui` files, processing them in a single compilation run.
- **No Hardcoded Paths**: All paths use environment variables (`%ProgramFiles%`, `%ProgramFiles(x86)%`, `getenv`) — works on any Windows machine.

### 9. Titlebar Text Attribute (`WinUITitlebarAttribute`)
- **Metadata Serialization**: `winmd_emitter.ixx` serializes `WinUITitlebarAttribute` into emitted `.winmd` metadata with the title string as a constructor argument.
- **Projection Execution**: Target language projections parse this attribute to set the window title via the platform API (e.g. `Title(L"...")`).
- **UTF-8 Emoji Support**: The title string is UTF-8 encoded in `.winui` source and converted to wide string at runtime, supporting full Unicode including emoji characters.
- **Global Scope**: When `.extendContentIntoTitleBar` is not set, the titlebar text applies as the global window title.

### 10. Long Path Awareness (`longPathAware`) & UTF-8 Code Page
- **Embedded Win32 Manifest**: `winuic.exe` is compiled with an embedded application manifest (`winui.manifest`) specifying `<longPathAware>true</longPathAware>` to bypass the traditional Windows 260-character `MAX_PATH` limitation.
- **UNC Path Normalization**: The CLI compiler automatically detects paths exceeding 240 characters and applies `\\?\` UNC extended-length prefixes (`normalize_win_path`), preventing file IO failures in deep directory trees.
- **UTF-8 Active Code Page**: Manifest specifies `<activeCodePage>UTF-8</activeCodePage>`, enforcing native UTF-8 string handling across all Windows SDK file and console operations.

### 11. Parallel Batch Compiler & Direct ECMA-335 WinMD Emitter
- **Hardware-Accelerated Parallel Build**: Compiles `.winui` component files concurrently across all available CPU hardware threads via C++20 `std::jthread`.
- **Direct PE Metadata Generation**: Emits ECMA-335 WinRT binary `.winmd` metadata files directly from AST/IR in memory—completely bypassing IDL file generation and `midl.exe` compilation dependencies.

### 12. Reference-Only Asset & LICENSE Copying
- **Smart Asset Detection**: Scans all input `.winui` files for asset references (`ms-appx:///Assets/...`, `"Assets/..."` quoted paths) using simple string matching — no regex, no external dependencies.
- **Source Root Discovery**: Walks up from each input file and checks sibling directories for `Assets/` folders using Win32 APIs, so it finds the original source project even when the `.winui` tree is in a different directory.
- **Copies Only Referenced Files**: Only assets that are actually referenced in the `.winui` source are copied to the output directory — no dead assets, no wasted disk space.
- **Automatic LICENSE Preservation**: Automatically detects any `LICENSE` file in the source root directories and copies it into the output directory next to the generated `.winmd` metadata.

---

## A to Z Grammar Reference

### A — Attributes & Decorators

Property declarations and view classes can be decorated with SwiftUI-style attributes and WinMD metadata decorators:

| Attribute / Decorator | Target | Description | Example |
| :--- | :--- | :--- | :--- |
| **State** | `@state` | Local mutable view state | `@state var count: int = 0` |
| **Typed state** | `@state var x: T[]` / `datetime` / `any` | Types flow through the winmd for projections: `string[]`, `int[]`, `double[]`, `bool[]`, `datetime`, `any` | `@state var items: any[]` |
| **Binding** | `@binding` | Two-way data binding from parent view | `@binding var title: string` |
| **Published** | `@published` | Observable property triggering UI re-render | `@published var isBusy: bool = false` |
| **Computed** | `@computed` | Derived read-only state (getter only, no setter emitted) | `@computed var label: string` |
| **Environment** | `@environment` | Dependency-injection / theme injection from host context | `@environment var theme: string` |
| **WinUIDarkTitlebar** | View metadata | Projects DWM immersive dark titlebar | Emitted in `.winmd` & projected by language projections |
| **WinUIAppIcon** | View metadata | Projects window icon binding (`WM_SETICON`) | Emitted in `.winmd` & projected by language projections |
| **WinUIDarkContextMenu** | View metadata | Projects dark context menus (`uxtheme.dll`) | Emitted in `.winmd` & projected by language projections |
| **WinUILayout** | View metadata | Encodes JSON layout tree for projected generation | Emitted in `.winmd` & projected by language projections |
| **WinUIControlCatalog** | Module metadata | Embeds the full control registry (abstract schemas + platform mappings) as JSON | Emitted once on `<Module>` in `.winmd` & consumed by projections |

#### Example
```winui
export view CounterView {
    @state var count: int = 0;
    @binding var headerText: string;
    @published var isEnabled: bool = true;
}
```

> [!IMPORTANT]
> **📣 PROJECTION OWNER NOTICE: CUSTOM METADATA ATTRIBUTE & WINDOW API CONTRACTS**
> 
> All target language projection authors (C++, C#, Rust, Python, Go, etc.) consuming `.winmd` metadata emitted by **`winuic.exe`** must observe the following handling contracts for `WinUIDarkTitlebarAttribute`, `WinUIAppIconAttribute`, `WinUIDarkContextMenuAttribute`, `WinUITitlebarAttribute`, `WinUIExtendContentIntoTitleBarAttribute`, `WinUIBackdropAttribute`, `WinUIAttribute`, and `WinUIControlCatalogAttribute`:
> 
> 1. **Window-Level Scope Isolation**:
>    - Window-level custom attributes (`WinUIDarkTitlebarAttribute`, `WinUIAppIconAttribute`, `WinUIDarkContextMenuAttribute`) are strictly serialized on `Window` view classes (`TypeDef` extending `Window`).
>    - Module-level custom attributes (`WinUIExtendContentIntoTitleBarAttribute`, `WinUITitlebarAttribute`, `WinUIBackdropAttribute`) are serialized on the **`<Module>` TypeDef row (row 1)**, same as `WinUIControlCatalogAttribute`.
>    - Sub-views (`UserControl` classes) **never** contain window-level attributes in `.winmd` metadata. Projection generators MUST NOT execute window titlebar/icon code during `UserControl` instantiation.
> 
> 2. **Instance-Level HWND Resolution via `IWindowNative`**:
>    - Projections MUST resolve window handles via `IWindowNative::get_WindowHandle(&hWnd)` on `this` Window instance during `InitializeComponent()` instead of assuming a global foreground window (`GetActiveWindow()`).
> 
> 3. **Non-Collision with WinUI 3 Window & AppWindow APIs**:
>    - **`WinUIDarkTitlebarAttribute`**: Configures DWM non-client immersive dark mode (`DWMWA_USE_IMMERSIVE_DARK_MODE` = 20). Co-exists harmoniously with WinUI 3 `ExtendsContentIntoTitleBar`, `AppWindow.TitleBar`, and `SetTitleBar(element)` without overriding custom title bar drag regions or client area extensions.
>    - **`WinUIAppIconAttribute`**: Binds executable resource icons (`WM_SETICON`). Projections SHOULD check `WM_GETICON` before applying the default icon so explicit calls to `SetAppIcon` or `AppWindow.SetIcon` are preserved without collision.
>    - **`WinUIDarkContextMenuAttribute`**: Applies `uxtheme.dll` preferred dark app mode (`AllowDark`) to system popups and context flyouts. Follows dark mode — when `WinUIDarkTitlebarAttribute` is also present, context menus are dark. When dark mode is off, context menus are white.
>    - **`WinUIExtendContentIntoTitleBarAttribute`**: Extends app content into the titlebar region. Takes priority when set — projections should call `ExtendsContentIntoTitleBar(true)` before other titlebar configurations.
>    - **`WinUITitlebarAttribute`**: Sets the window title string (UTF-8, supports emojis). The string is a constructor argument — read it and pass to the platform title API (e.g. `Title(L"...")`).
>    - **`WinUIBackdropAttribute`**: Sets the window backdrop material. The string argument is `"mica"` or `"acrylic"` — resolve to the platform backdrop type (e.g. `MicaBackdrop{}` or `DesktopAcrylicBackdrop{}`).
> 
> 4. **Module-Level Catalog Scope**:
>    - **`WinUIControlCatalogAttribute`** is attached to the **`<Module>` TypeDef (row 1)**, NOT to any view class. Read it **once per `.winmd`**, at module/assembly load, and cache the resulting control table. It MUST NOT be treated as a view-level attribute, and MUST NOT be re-emitted per view.
> 
> 5. **Projection-Agnostic Dynamic Control Resource Dictionaries**:
>    - **Catalog Serialization**: `winuic.exe` serializes all registered platform resource dictionaries (e.g. `XamlControlsResources`, `TabularControlsResources`, `XamlChartsResources`, and any future control libraries) under the top-level `"resource_dictionaries"` array within `WinUIControlCatalogAttribute` on `<Module>`. Furthermore, individual controls declare their required platform or XAML resources via `"resource_dictionary"`.
>    - **Contract for All Language Projections** (C++, C#, Rust, Python, Go):
>      - At application launch/bootstrap, projections MUST inspect `WinUIControlCatalogAttribute` and instantiate all resource dictionaries declared in `"resource_dictionaries"`, appending them to the platform's merged resource dictionaries (`Application.Current.Resources.MergedDictionaries`).
>      - Projections should additionally inspect loaded `.winmd` TypeDef tables for any platform classes ending with `Resources` that derive from `ResourceDictionary`, automatically merging them.
>      - This contract ensures 100% Day-1 compatibility for future controls and third-party control packages across all language projections without requiring code changes or recompilations of the projection tool.

---

### B — Body Blocks & Binding References

#### 1. Body Block (`body { ... }`)
The `body` block contains the view tree hierarchy defining the layout and components:

```winui
export view SimpleCard {
    body {
        Card {
            Label("Card Title")
                .bold()
                .fontSize(18)
        }
    }
}
```

#### 2. Binding Reference (`$name`)
Prefixing a state or property identifier with `$` passes a **two-way binding reference** to child controls or subviews:

```winui
Input(text: $headerText, placeholder: "Enter title...")
Toggle(label: "Enable notifications", isOn: $isEnabled)
```

---

### C — Control Catalog & Callbacks

#### 1. Abstract Control Catalog
`.winui` defines 24 built-in abstract controls categorized into Layout, Content, Input, Navigation, and Feedback:

##### Layout Controls
- **`Column(spacing, alignment, padding)`**: Vertical container (`StackPanel` orientation=Vertical).
- **`Row(spacing, alignment, padding)`**: Horizontal container (`StackPanel` orientation=Horizontal).
- **`Stack(alignment)`**: Z-stack overlay layout (`Grid`).
- **`Grid(columns, rows, spacing)`**: Grid layout.
- **`Scroll(direction)`**: Scrollable container (`ScrollViewer`).
- **`Wrap(itemWidth, itemHeight, maximumRowsOrColumns, orientation)`**: Wrapping flow layout (`VariableSizedWrapGrid`). Children wrap to the next row/column when horizontal space is exhausted.
- **`Spacer`**: Flexible expanding space.
- **`Divider`**: Visual separation line (`MenuFlyoutSeparator` / `Border`).
- **`Card(elevation, cornerRadius)`**: Elevated container card.

##### Content Controls
- **`Label(text, fontSize, fontWeight, color, maxLines, wrapping)`**: Text display (`TextBlock`). Supports direct string positional parameter `Label("Hello")`.
- **`Icon(glyph, fontSize, color)`**: Font icon (`FontIcon`).
- **`Image(source, width, height, fit)`**: Image content (`Image`).

##### Input Controls
- **`Button(content, style, width, isEnabled)`**: Clickable button. Accepts text parameter `Button("Click Me")` or `.onClick { ... }` block.
- **`Input(placeholder, text, isReadOnly)`**: Single-line text input field (`TextBox`). Supports `onChanged(text)`.
- **`TextArea(placeholder, text, maxLength)`**: Multi-line text input (`TextBox` with MultiLine).
- **`Toggle(label, isOn)`**: Toggle switch (`ToggleSwitch`). Accepts `onToggled(isOn)`.
- **`Slider(value, minimum, maximum, step)`**: Range slider (`Slider`). Accepts `onChanged(val)`.
- **`Checkbox(label, isChecked)`**: Checkbox input (`CheckBox`).
- **`Picker(selectedIndex, header)`**: Dropdown selection menu (`ComboBox`).

##### Navigation Controls
- **`TabView(selectedIndex)`**: Tabbed navigation view (`TabView`).
- **`NavView(header, isBackVisible)`**: Navigation panel (`NavigationView`).
- **`Link(text, url)`**: Hyperlink text button (`HyperlinkButton`).

##### Feedback Controls
- **`Progress(value, isIndeterminate, style)`**: Progress bar or ring (`ProgressBar` / `ProgressRing`).
- **`InfoBar(title, message, severity, isOpen)`**: Status banner (`InfoBar`).

#### 2. Callbacks (`callback name(...)`)
Define event delegates exposed by custom components:

```winui
export view CustomButton {
    callback onClicked(buttonId: string);
    callback onReset();

    body {
        Button("Submit") {
            self.onClicked("btn-submit");
        }
    }
}
```

---

### D — Directives & Dot-Enums

#### 1. Version Directive (`#version N`)
Defines the required compiler syntax version at the top of the file:
```winui
#version 1
```

#### 2. File-Level Directives (App-Wide Settings)
Dot-prefixed directives placed before any `export view` declaration configure app-wide window behavior. These are serialized into ECMA-335 custom attributes on the `<Module>` TypeDef row of the emitted `.winmd` and are consumed by language projections.

| Directive | Syntax | Effect |
| :--- | :--- | :--- |
| **`.extendContentIntoTitleBar`** | `.extendContentIntoTitleBar;` | Extends app content into the titlebar region (immersive titlebar) |
| **`.darkTitlebar`** | `.darkTitlebar;` | Sets titlebar theme to dark (`AppTitleBarTheme::Dark`) |
| **`.darkContextMenu`** | `.darkContextMenu;` | Enables dark-themed right-click context menus. Follows dark mode automatically — when `.darkTitlebar` is set, context menus are dark too |
| **`.titlebar`** | `.titlebar("text");` | Sets the global window title. Supports UTF-8 text including emojis (converted to `wstring` at runtime) |
| **`.appIcon`** | `.appIcon("path\\icon.ico");` | Sets the application icon for titlebar, taskbar, and Alt+Tab |
| **`.backdrop`** | `.backdrop(mica);` or `.backdrop(acrylic);` | Sets the window backdrop material |

**Example:**
```winui
#version 1
.extendContentIntoTitleBar;
.darkTitlebar;
.darkContextMenu;
.titlebar("My App \xF0\x9F\x9A\x80");
.appIcon("assets\\icon.ico");
.backdrop(mica);

export view MainWindow
{
    body {
        Button("Click me")
    }
}
```

**Priority rules:**
- `.extendContentIntoTitleBar` is always applied first and takes priority.
- `.darkTitlebar` is mutually independent — it can be used with or without `.extendContentIntoTitleBar`.
- `.darkContextMenu` follows `.darkTitlebar` — if either is set, the context menu is dark.
- `.titlebar` text is global for the app window when `.extendContentIntoTitleBar` is NOT set.

#### 3. Dot-Enums (`.enumValue`)
Dot-prefixed identifier shorthand for enumerations:
```winui
Label("Status")
    .alignment(.center)
    .style(.accent)
```

---

### E — Expressions & Pratt Precedence

Expressions are evaluated using Pratt Parsing with deterministic precedence (low → high):

| Precedence | Level | Operators | Description |
| :---: | :--- | :--- | :--- |
| **1** | Logical OR | `\|\|` | `a \|\| b` |
| **2** | Null-Coalescing | `??` | `a ?? b` (use `b` when `a` is null/empty) |
| **3** | Logical AND | `&&` | `a && b` |
| **4** | Equality | `==`, `!=` | `a == b`, `a != b` |
| **5** | Relational | `<`, `>`, `<=`, `>=` | `count >= 10` |
| **6** | Additive | `+`, `-` | `x + y`, `count - 1` |
| **7** | Multiplicative | `*`, `/` | `total * 1.15` |
| **8** | Unary Prefix | `-`, `!` | `-val`, `!isBusy` |
| **9** | Postfix / Access | `.`, `()`, `[]` | `root.headerText`, `getItems()`, `items[0]` |
| **10** | Ternary | `?` `:` | `flag ? "yes" : "no"` (right-associative, lowest postfix) |

---

### F — ForEach Iteration

Iterates over collections or ranges to dynamically generate UI elements:

```winui
export view ItemList {
    @state var items: string = "Alpha";

    body {
        Column(spacing: 8) {
            ForEach(0...5) { index in
                Label("Item \(index)")
                    .fontSize(14)
            }
        }
    }
}
```

---

### G — Grammar EBNF Specification

```ebnf
SourceFile       ::= VersionDirective? FileDirective* ImportStmt* ViewDecl*

VersionDirective ::= '#' 'version' NUMBER ( '.' NUMBER )?
FileDirective    ::= '.' DirectiveName ( '(' STRING ')' )? ';'
DirectiveName    ::= 'extendContentIntoTitleBar' | 'darkTitlebar' | 'darkContextMenu'
                   | 'titlebar' | 'appIcon' | 'backdrop'
ImportStmt       ::= 'import' '{' IDENT ( ',' IDENT )* '}' 'from' STRING ';'

ViewDecl         ::= 'export' 'view' IDENT '{' ViewMember* '}'
ViewMember       ::= PropertyDecl | CallbackDecl | BodyBlock | BodyItem

PropertyDecl     ::= ( '@state' | '@binding' | '@published' | '@computed' | '@environment' )? 'var' IDENT ':' Type ( '=' Expr )? ';'

CallbackDecl     ::= 'callback' IDENT '(' ParamList? ')' ';'
ParamList        ::= Param ( ',' Param )*
Param            ::= IDENT ':' Type | Type IDENT

BodyBlock        ::= 'body' '{' BodyItem* '}'
BodyItem         ::= ViewNode | ConditionalView | ForEachNode

ViewNode         ::= IDENT ( '(' ArgList? ')' )? ( '{' ViewNodeBlock '}' )? Modifier*
ViewNodeBlock    ::= ( BodyItem | PropertyAssign | ClosureExpr )*

Modifier         ::= '.' IDENT ( '(' ArgList? ')' )? ( '{' ClosureExpr '}' )?
                 |  '.' 'raw' '(' STRING ',' STRING ')'      // verbatim XAML attribute (universal)

RawFragment      ::= 'Raw' '(' ')' Modifier*                 // verbatim XAML subtree view (e.g. .raw("Resource", "<Style …/>"))

ConditionalView  ::= 'if' Expr '{' BodyItem* '}' ( 'else' 'if' Expr '{' BodyItem* '}' )* ( 'else' '{' BodyItem* '}' )?
ForEachNode      ::= 'ForEach' '(' Expr ')' '{' IDENT 'in' BodyItem* '}'

ArgList          ::= Arg ( ',' Arg )*
Arg              ::= ( IDENT ':' )? Expr

Type             ::= 'string' | 'int' | 'bool' | 'float' | 'double'

Expr             ::= TernaryExpr
TernaryExpr      ::= BinaryExpr ( '?' Expr ':' Expr )?
BinaryExpr       ::= UnaryExpr ( BinOp UnaryExpr )*
BinOp            ::= '||' | '??' | '&&' | '==' | '!=' | '<' | '>' | '<=' | '>=' | '+' | '-' | '*' | '/'
UnaryExpr        ::= ( '-' | '!' ) UnaryExpr | PostfixExpr
PostfixExpr      ::= PrimaryExpr ( '.' IDENT | '(' ArgList? ')' | '[' Expr ']' )*
PrimaryExpr      ::= STRING | NUMBER | 'true' | 'false' | '.' IDENT | '$' IDENT
                   | 'root' '.' IDENT | 'self' '.' IDENT
                   | IDENT | '(' Expr ')' | '[' ArgList? ']' | ClosureExpr
BindingRef       ::= '$' IDENT
DotEnumExpr      ::= '.' IDENT
RangeExpr        ::= Expr ( '...' | '..<' ) Expr
ArrayLitExpr     ::= '[' ( Expr ( ',' Expr )* )? ']'
SubscriptExpr    ::= Expr '[' Expr ']'
InterpolatedStr  ::= '"' ( StringChar | '\(' Expr ')' )* '"'

Stmt             ::= AssignStmt | ExprStmt | IfStmt | VarStmt | ReturnStmt | WhileStmt | SwitchStmt
AssignStmt       ::= Expr ( '=' | '+=' | '-=' | '*=' | '/=' ) Expr ';'
VarStmt          ::= ( 'var' | 'let' ) IDENT ( ':' Type )? '=' Expr ';'
IfStmt           ::= 'if' Expr '{' Stmt* '}' ( 'else' 'if' Expr '{' Stmt* '}' )* ( 'else' '{' Stmt* '}' )?
WhileStmt        ::= 'while' Expr '{' Stmt* '}'
SwitchStmt       ::= 'switch' Expr '{' ( 'case' Expr ':' Stmt* )* ( 'default' ':' Stmt* )? '}'
ReturnStmt       ::= 'return' Expr? ';'
```

---

### H — Header Imports

Import symbols from external modular `.winui` files:

```winui
import { HeaderView, UserAvatar } from "components/header.winui";
import { ThemeManager } from "core/theme.winui";
```

---

### I — If/Else & Interpolated Strings

#### 1. Conditional View Trees (`if / else if / else`)
Show or hide controls dynamically based on boolean state, with optional `else if` chains:

```winui
if count > 10 {
    InfoBar(title: "High!", severity: "warning", isOpen: true)
} else if count > 5 {
    Label("Getting there…")
        .foregroundColor("#FFA500")
} else {
    Label("Ready!")
        .foregroundColor("#00FF00")
}
```

#### 2. Interpolated Strings (`\(expr)`)
Embed expressions dynamically inside string literals:

```winui
Label("Current count: \(count + 1)")
Label("User: \(root.username)")
```

#### 3. Ternary Expression (`cond ? then : else`)
Inline conditional value selection in any expression position:

```winui
Label(isEnabled ? "Active" : "Disabled")
    .foregroundColor(isEnabled ? "#00FF88" : "#888888")
```

#### 4. Null-Coalescing (`??`)
Return the left-hand operand when it is non-null/non-empty, or fall back to the right-hand value:

```winui
Label(username ?? "Guest")
```

---

### K — Keywords Catalog

| Category | Keywords |
| :--- | :--- |
| **Structure** | `import`, `from`, `export`, `view`, `body`, `callback`, `var`, `let` |
| **Control Flow** | `if`, `else`, `ForEach`, `in`, `return`, `while`, `switch`, `case`, `default` |
| **Context** | `root`, `self` |
| **Literals** | `true`, `false` |
| **Types** | `string`, `int`, `bool`, `float`, `double` *(`void` is not a keyword — it is only the inferred type of callback parameters declared without one)* |
| **Attributes** | `@state`, `@binding`, `@published`, `@computed`, `@environment` |
| **Directives** | `#version` |

---

### L — Literals & Lexical Rules

- **Comments**:
  - Line comment: `// Single line comment`
  - Block comment: `/* Multi line comment */`
- **String Literals**: Enclosed in double quotes `"Hello World"`, supporting `\(` (interpolation marker, see §I), `\"` (escaped quote, round-trips exactly) and `\uXXXX` (Segoe MDL2 glyph escape, only when exactly 4 hex digits follow).
- **Backslash is literal**: `\` is a plain character inside strings — Windows paths use a single `\`: `Image(source: "assets\icon.png")`. (JSON-style `\\`, `\n`, `\t` escapes are NOT interpreted.)
- **Path separators**: `\` is the only legal path separator in `.winui` source. `\` remains literal everywhere; `/` is rejected by the compiler on path-like properties (`uri` / `bitmap_image` transforms) unless it is part of an `https://` or `ms-appx://` URI (case-insensitive):
  - ✅ `Image(source: "assets\icon.png")`
  - ✅ `BitmapIcon(source: "ms-appx:///Assets/icon.png")` / `WebView2(source: "https://example.com")`
  - ❌ `Image(source: "assets/icon.png")` → `error: path separator '/' is not allowed ...`
- **Number Literals**: Integer (`42`) and Floating Point (`3.14159`).
- **Boolean Literals**: `true` and `false`.
- **CSS Color Literals**: `hsl()`, `hsla()`, `oklch()`, `color(display-p3, ...)` — recognized as single tokens by the lexer. Can be used directly as property values without quotes:
  - `.Foreground(hsl(210, 100%, 50%))` — HSL hue/saturation/lightness
  - `.Background(hsla(0, 100%, 50%, 0.8))` — HSL with alpha
  - `.Foreground(oklch(0.7, 0.15, 180))` — OKLCH perceptual color space
  - `.Background(color(display-p3, 0.2, 0.8, 0.3, 0.7))` — Display P3 wide-gamut
  - All resolve to `ColorHelper::FromArgb(A, R, G, B)` in the C++ projection.
- **Identifiers**: `[a-zA-Z_][a-zA-Z0-9_]*`.

---

### M — Modifiers Catalog

Modifiers refine layout, appearance, text formatting, and attach event handlers. The modifier vocabulary is **registry-driven**: built-in abstract modifiers are resolved against the control registry, and third-party JSON registries (see [Third-Party & Custom Control Registration](#3-third-party--custom-control-registration)) can add arbitrary new modifiers without recompiling `winuic.exe`. Unknown modifiers fail the build with a hard error — nothing is silently dropped.

#### Universal Modifiers (available on every control)
- `.raw(key, value)` — **Verbatim XAML pass-through**: emits the exact `(key, value)` attribute pair into the `.winmd` for the node. Both arguments must be string literals. This is the carrier that guarantees 100% XAML fidelity for attributes with no abstract mapping (e.g. `.raw("x:Name", "RootGrid")`, `.raw("AutomationProperties.AutomationId", "__GoBackInvoker")`, `.raw("Grid.Row", "1")`).
- Event modifiers: `.onClick { ... }`, `.onChanged { ... }`, `.onAppear { ... }`, `.onDisappear { ... }`, plus any event registered per control or via JSON (`onLoaded`, `onSelectionChanged`, …). Each takes a trailing closure: `.onClick { self.DoIt(); }`.

#### Typography Modifiers
- `.fontSize(double)` — Set text point size.
- `.fontWeight(string)` — Set font weight (`"Bold"`, `"SemiBold"`, `"Normal"`).
- `.fontFamily(string)` — Set font family name.
- `.bold()` — Convenience modifier for bold text.
- `.italic()` — Convenience modifier for italic text.

#### Color & Appearance Modifiers
- `.foregroundColor(string)` — Foreground/text color hex or named brush (`"#FFFFFF"`, `"Accent"`).
- `.backgroundColor(string)` — Background color hex or brush.
- `.cornerRadius(double)` — Corner radius in pixels.
- `.opacity(double)` — Element opacity from `0.0` to `1.0`.
- `.border(width: double, color: string)` — Border outline.
- `.shadow(double)` — Drop shadow elevation.
- `.style(string)` — Predefined theme style name.

#### Layout Modifiers
- `.padding(double)` — Uniform padding margin.
- `.width(double)` / `.height(double)` — Fixed width/height.
- `.minWidth(double)` / `.maxWidth(double)` — Width constraints.
- `.minHeight(double)` / `.maxHeight(double)` — Height constraints.
- `.alignment(string)` — Content alignment (`"Center"`, `"Left"`, `"Right"`).

#### Behavior & Event Modifiers
- `.visible(bool)` — Visibility toggle.
- `.disabled(bool)` — Disables interaction.
- `.tooltip(string)` — Tooltip hover text.
- `.onClick { ... }` — Click action closure.
- `.onChanged { val in ... }` — Value change callback closure.
- `.onAppear { ... }` — Called when control is loaded into visual tree.
- `.onDisappear { ... }` — Called when control is unloaded.

---

### P — Properties & Primitive Types

#### 1. Property Syntax
Properties represent member data on views:

```winui
// Modern SwiftUI-style syntax
@state var counter: int = 0;
@binding var pageTitle: string;
@published var isLoaded: bool = false;
@computed var displayLabel: string;      // derived read-only — no setter emitted
@environment var currentTheme: string;   // injected by host / DI container

// let — immutable local binding (alias for var inside closures)
let limit: int = 100;
```

#### 2. Primitive Types
- `string`: UTF-8 / UTF-16 string text.
- `int`: 32-bit signed integer.
- `bool`: Boolean `true` / `false`.
- `float`: Single-precision floating point.
- `double`: Double-precision floating point.
- `void`: Empty return type for callbacks.

---

### R — Range Expressions

Ranges create numeric sequences for iteration or slider bounds:
- **Inclusive Range (`...`)**: `0...10` includes both `0` and `10`.
- **Exclusive Range (`..<`)**: `0..<10` includes `0` up to `9`.

```winui
ForEach(1...5) { step in
    Label("Step \(step)")
}
```

---

### S — Statements in Closures

Trailing event closures and member functions contain full statement execution blocks:

1. **Assignment**: `count = count + 1;` or `count += 1;`
2. **Variable Declaration**: `var temp: int = count * 2;` or immutable `let max: int = 10;`
3. **If Statement**: `if count > 10 { count = 0; }`
4. **While Loop**: `while count > 0 { count -= 1; }`
5. **Switch Statement**: `switch state { case "idle": ... case "busy": ... default: ... }`
6. **Expression Call**: `root.onSubmit("clicked");`
7. **Return**: `return;`

---

### T — Type System

Primitive types are strictly checked during semantic analysis and converted into standardized, projection-agnostic ABI metadata types:

| `.winui` Type | Standard ABI / Metadata Type | Description |
| :--- | :--- | :--- |
| `string` | `String` (UTF-16 HSTRING) | Text string representation |
| `int` | `Int32` (32-bit Signed Int) | 32-bit signed integer |
| `bool` | `Boolean` | True/False boolean flag |
| `float` | `Single` (32-bit Float) | Single-precision floating point |
| `double` | `Double` (64-bit Float) | Double-precision floating point |
| `void` | `Void` | Empty return / delegate action type |

---

### V — Views & Version Directives

Views are the primary unit of composition in `.winui`. Each file can export one or more component views:

```winui
#version 1

export view AppHeader {
    @binding var title: string;

    body {
        Row(spacing: 12) {
            Icon(glyph: "\uE700", fontSize: 20)
            Label(title)
                .bold()
                .fontSize(22)
        }
    }
}
```

#### Verbatim Fragment Views (`Raw()`)
`Raw()` is a reserved pseudo-view that carries a verbatim XAML subtree as its payload — never a real control. It is emitted by [`Xaml2WinUIC`](https://github.com/mominshaikhdev/xaml2WinUIC-compiler) so that resources, styles, templates and other fragment families round-trip byte-for-byte through the `.winui` pipeline:

```winui
export view AppResources {
    body {
        Raw() .raw("Resource", "<ResourceDictionary><ResourceDictionary.MergedDictionaries><XamlControlsResources xmlns=\"using:Microsoft.UI.Xaml.Controls\"/></ResourceDictionary.MergedDictionaries></ResourceDictionary>")
    }
}
```

`WinUIC` serializes the fragment string verbatim into the `.winmd` resource entry — the original XAML text is preserved exactly, including quotes and escaping.

---

## Complete Code Examples

### 1. Interactive Counter Component
```winui
#version 1

export view CounterWidget {
    @state var count: int = 0;

    body {
        Card(elevation: 4, cornerRadius: 8) {
            Column(spacing: 16, padding: 20) {
                Label("Counter: \(count)")
                    .bold()
                    .fontSize(24)
                    .alignment(.center)

                Row(spacing: 12) {
                    Button("Decrement") {
                        count -= 1;
                    }
                    .style(.secondary)

                    Button("Increment") {
                        count += 1;
                    }
                    .style(.accent)
                }

                if count > 10 {
                    InfoBar(
                        title: "High Count!",
                        message: "The count has exceeded 10.",
                        severity: "warning",
                        isOpen: true
                    )
                }
            }
        }
    }
}
```

### 2. User Registration Form
```winui
#version 1

export view RegistrationForm {
    @state var username: string = "";
    @state var agreedToTerms: bool = false;
    callback onRegistered(user: string);

    body {
        Column(spacing: 16, padding: 24) {
            Label("Create Account")
                .fontSize(28)
                .bold()

            Input(text: $username, placeholder: "Enter username")

            Checkbox(label: "I agree to Terms & Conditions", isChecked: $agreedToTerms)

            Button("Register") {
                if agreedToTerms && username != "" {
                    self.onRegistered(username);
                }
            }
            .disabled(!agreedToTerms)
            .style(.accent)
        }
    }
}
```

### 3. Ternary, Null-Coalescing & Array Literal
```winui
#version 1

export view StatusCard {
    @state var score: int = 0;
    @state var playerName: string = "";
    @computed var grade: string;

    body {
        Card(elevation: 4, cornerRadius: 12) {
            Column(spacing: 12, padding: 20) {
                Label(playerName ?? "Anonymous")
                    .bold()
                    .fontSize(20)

                // Ternary expression
                Label(score >= 90 ? "Excellent" : score >= 70 ? "Good" : "Needs Work")
                    .foregroundColor(score >= 90 ? "#00FF88" : score >= 70 ? "#FFD700" : "#FF4444")

                // Array literal passed to a Picker
                Picker(selectedIndex: 0, header: "Difficulty")
            }
        }
    }
}
```

### 4. Switch Statement
```winui
#version 1

export view ThemeSelector {
    @state var selectedTheme: int = 0;

    body {
        Column(spacing: 16) {
            Label("Theme").bold()

            Button("Apply") {
                switch selectedTheme {
                    case 0:
                        selectedTheme = 0;
                    case 1:
                        selectedTheme = 1;
                    default:
                        selectedTheme = 0;
                }
            }
            .style(.accent)
        }
    }
}
```

### 5. While Loop, Subscript & Compound Assignment
```winui
#version 1

export view DataProcessor {
    @state var iterations: int = 0;
    @state var result: int = 1;

    body {
        Column(spacing: 16, padding: 20) {
            Label("Result: \(result)").bold()

            Button("Calculate") {
                // While loop with compound multiply-assign
                var i: int = 0;
                while i < 5 {
                    result *= 2;
                    i += 1;
                }
                iterations += 1;
            }
            .style(.accent)
        }
    }
}
```

### 6. Wrap Layout Container
```winui
#version 1

export view TagCloud {
    body {
        Wrap(itemWidth: 120, itemHeight: 40, maximumRowsOrColumns: 4) {
            Button("Swift").style(.secondary)
            Button("Kotlin").style(.secondary)
            Button("Rust").style(.secondary)
            Button("C#").style(.secondary)
            Button("C++").style(.secondary)
            Button("TypeScript").style(.secondary)
        }
    }
}
```

### 7. @computed & @environment Properties
```winui
#version 1

export view UserProfile {
    @state var firstName: string = "";
    @state var lastName: string = "";
    @computed var fullName: string;        // derived — no setter generated
    @environment var accentColor: string;  // injected by host container

    body {
        Column(spacing: 12, padding: 20) {
            Label("Profile").bold().fontSize(24)
            Input(text: $firstName, placeholder: "First name")
            Input(text: $lastName, placeholder: "Last name")
            Label("Hello, \(firstName)!")
                .foregroundColor(accentColor ?? "Accent")
        }
    }
}
```

---



## `winuic.exe` Compiler Architecture & CLI Reference

`winuic.exe` is the native Release C++23 compiler executable for `.winui` source code. It compiles declarative `.winui` component files into WinRT PE binary metadata (`.winmd`) and packaged resource payloads (`resources.pri`).

### 1. Compiler Architecture Pipeline

```
  [.winui source] ──> [Lexer] ──> [Pratt Parser] ──> [Semantic Analyzer] ──> [IR Module]
                                                                                   │
    ┌──────────────────────────────────────────────────────────────────────────────┘
    ▼
 [WinMD Emitter] ──> Direct ECMA-335 Binary PE Metadata (.winmd) & In-RAM PRI Encoder
```

- **Lexer (`lexer.ixx`)**: Lexical tokenization supporting Unicode glyph escapes, string interpolation (`\(` ... `\)`), and numerical range tokens (`...`, `..<`).
- **Pratt Parser (`parser.ixx`)**: Top-down operator precedence parsing for expressions, view hierarchies, closures, and state declarations.
- **Control Registry (`registry.ixx`)**: Central database containing 24 built-in WinUI controls alongside **Dynamic Custom Control Synthesis**.
- **Semantic Analyzer (`semantic.ixx`)**: AST validation, scope analysis, two-way `$binding` resolution, state property indexing, and IR lowering.
- **Direct WinMD Emitter (`winmd_emitter.ixx`)**: Direct ECMA-335 binary Metadata Emitter emitting PE header structures, metadata tables (`TypeDef`, `MethodDef`, `Property`), string heaps, and blob heaps — **bypassing IDL text and `midl.exe` entirely**.
- **In-Memory PRI Encoder**: Direct binary `resources.pri` encoding without external resource compiler dependencies.

---

### 2. CLI Usage & Flags Reference

#### Syntax
```cmd
winuic.exe [options] [files...]
winuic.exe -f file1.winui,file2.winui -o ./out -n MyNamespace -v
```

#### Command Line Flags

| Flag | Long Flag | Description | Default |
| :--- | :--- | :--- | :--- |
| `-f` | `-files <csv>` | Comma-separated list of `.winui` files to compile | `<positional args>` |
| `-o` | `-out_dir <dir>` | Target output directory for generated `.winmd` and `.pri` files | `./out` |
| `-n` | `-namespace <ns>` | Project root namespace for generated WinRT metadata; **overrides any derived name** — names the emitted `.winmd` file (`<ns>.winmd`), the metadata module string and every class namespace. Default: spaces are hyphen-joined (`WinUI Gallery` → `WinUI-Gallery`) | `MyGeneratedUI` |
| `-p` | `-project <name>` | Project name (defaults to namespace name) | Same as namespace |
| `-c` | `-control <file>` | Path to a control-registry JSON (repeatable, no recompile). **Any file name is accepted** — there is no hardcoded `winui.json`; every `*.json` sitting next to `winuic.exe` is auto-loaded as a control registry (non-registry JSON files are skipped silently). Explicit `-control` files are applied on top and win on duplicate names | `*.json` next to `winuic.exe` (auto-loaded) |
| `-v` | `-verbose` | Enable detailed diagnostic output and timing stats | `false` |
| `-h` | `-help` | Show CLI usage help text and flags summary | — |

---

### 3. Third-Party & Custom Control Registration

`winuic.exe` features **Automatic Dynamic Custom Control Registration**. You can use any custom or third-party WinUI 3 control (`MyCustomChart`, `UserAvatar`, `CustomGraphView`, etc.) in `.winui` source files without manual schema configuration.

> [!IMPORTANT]
> **SPECIAL NOTE — THIRD-PARTY CONTROLS NEVER REQUIRE A RECOMPILE**
> The extensibility contract is a hard architectural guarantee, not a fallback:
> - **Any JSON file name works.** There is no hardcoded `winui.json`. Every `*.json` next to `winuic.exe` is auto-loaded on every run (non-registry JSON is skipped silently), and `-c/-control <file>` adds per-project registries on top — the later file wins on duplicate `abstract_name`s.
> - **Unknown controls still compile.** A control with no registry entry is synthesized on the fly: it takes arbitrary named arguments, modifiers and `.raw(key, value)` passthrough, and becomes legal WinRT metadata — so even a brand-new WinUI 3 or third-party control released tomorrow works with the binaries you already have.
> - **Registry JSON upgrades behavior only.** When a control IS registered, its properties are type-checked, events gain `.onX { }` closures, and modifiers resolve to platform setters — the same JSON files make both `WinUIC` and `Xaml2WinUIC` understand a control: one shared schema, zero recompilation, zero code changes.
> - **Fidelity is never sacrificed for extensibility.** Anything outside the abstract mapping is carried verbatim via `.raw()` pairs and `Raw()` fragments into the `.winmd`, so projections can always recover the original XAML surface.

#### Example (`CustomControlsTest.winui`)
```winui
#version 1

export view CustomControlsTest {
    @state var chartTitle: string = "Quarterly Revenue";

    body {
        Column(spacing: 16) {
            Label("Third-Party Custom Controls")
                .bold()
                .fontSize(22)

            // Arbitrary custom controls automatically registered on the fly
            MyCustomChart(title: chartTitle)
                .width(400)
                .height(250)

            UserAvatar(name: "John Doe", status: "Active")
                .cornerRadius(50.0)

            CustomGraphView(dataPoints: 12)
        }
    }
}
```

#### Compilation Command
```cmd
winuic.exe -f winui_src\CustomControlsTest.winui -o out -n MyCompany.Controls -v
```
`winuic.exe` dynamically synthesizes control schemas on the fly, emitting compliant ECMA-335 `.winmd` metadata for all user-defined WinUI 3 components.

---

## Language Projection Owners Guide

> **Audience**: Authors of language projections that consume `winuic.exe` output — [**CppWinC**](https://github.com/mominshaikhdev/CppWinC-Compiler) (C++23 named modules), **CsWinC** (C#), Rust (`windows-rs`), Python (`PyWinC`), Swift, Kotlin/JVM, or any future binding layer.

This section is the single authoritative reference for every obligation a projection has when consuming a `.winmd` produced by `winuic.exe`.

---

### Overview: The Projection Contract

`winuic.exe` is **completely platform-agnostic**. It emits an ECMA-335 binary `.winmd` that encodes:

- The view's **type identity** (`IViewName` interface + `ViewName` runtime class in your declared namespace)
- The view's **ABI surface** (properties, events, callbacks) as standard WinRT metadata
- Four **semantic hint attributes** on each runtime class, plus one **module-scoped control catalog attribute** (see below)

What the `.winmd` intentionally does **not** contain:

| Not in .winmd | Who supplies it |
|---|---|
| Platform base class (`UserControl`, `Window`, `QWidget`, …) | **Your projection** |
| UI framework includes / imports | **Your projection** |
| Control instantiation code | **Your projection** (driven by `WinUIAttribute`) |
| Event subscription glue | **Your projection** (driven by `WinUIAttribute`) |
| Dark mode / icon / context menu Win32 calls | **Your projection** (triggered by hint attributes) |

> [!NOTE]
> The abstract → platform **control mapping table IS embedded in the `.winmd`** as the module-scoped `WinUIControlCatalogAttribute` (see below). Projections SHOULD build their control tables from this attribute instead of hardcoding them; the attribute automatically stays in sync with the compiler's registry.

---

### Reading Attributes from the .winmd

The four view-scoped hint attributes live in the ECMA-335 `CustomAttribute` table. Walk that table and match rows whose `Parent` coded index resolves to a `TypeDef` row with the `tdWindowsRuntime` flag set. Identify the attribute by its type name string:

| Attribute type name | Namespace | Parent scope |
|---|---|---|
| `WinUIAttribute` | Your emitted module namespace `<ns>` | Runtime class (`TypeDef`) |
| `WinUIDarkTitlebarAttribute` | `<ns>` | Runtime class (`TypeDef`) |
| `WinUIAppIconAttribute` | `<ns>` | Runtime class (`TypeDef`) |
| `WinUIDarkContextMenuAttribute` | `<ns>` | Runtime class (`TypeDef`) |
| `WinUIControlCatalogAttribute` | `<ns>` | **`<Module>` row 1 (module scope, once)** |

The conformance attributes (`DefaultAttribute`, `GuidAttribute`, `ActivatableAttribute`) are in `Windows.Foundation.Metadata` and follow standard WinRT projection conventions.

> [!IMPORTANT]
> `WinUIControlCatalogAttribute` is the ONLY attribute whose `Parent` coded index resolves to **TypeDef row 1 — the `<Module>` pseudo-type** (coded `(1 << 5) | 3` = 35, `TypeDef` tag = 3), not to a view class. It is never attached to the Module-table row (tag 7). Readers that enumerate custom attributes per-view `TypeDef` rows 2..N will NOT see it — read the custom attributes of TypeDef row 1 instead (this is the same row the CLR uses for assembly-level attributes).

#### Language-neutral walk pseudocode

```text
// ── Phase 0: module-scoped catalog (read ONCE, from <Module> TypeDef row 1) ──
for each CustomAttribute row CA where Parent resolves to TypeDef row 1:
    attr_name = resolve_type_name(CA.Type)
    if attr_name == "WinUIControlCatalogAttribute":
        catalog = json_decode(custom_attr_string(blobs[CA.Value]))
        control_table = build_control_table(catalog)   // abstract → platform map

// ── Phase 1: per-view attributes ──
for each TypeDef row T where T.Flags has tdWindowsRuntime:
    view_name = strings[T.Name]
    for each CustomAttribute row CA where Parent resolves to T:
        attr_name = resolve_type_name(CA.Type)   // walk MemberRef → TypeRef → strings

        if attr_name == "WinUIAttribute":
            json_tree = decode_layout_blob(blobs[CA.Value])
            generate_ui_code(view_name, json_tree, control_table)

        elif attr_name == "WinUIDarkTitlebarAttribute":
            emit_dark_titlebar(view_name)

        elif attr_name == "WinUIAppIconAttribute":
            emit_app_icon(view_name)

        elif attr_name == "WinUIDarkContextMenuAttribute":
            emit_dark_context_menu(view_name)
```

#### Blob wire format — `WinUIAttribute` (single string argument)

```
Offset   Size   Content
------   ----   -------
0        2      Prolog: 0x0001 (little-endian, ECMA-335 §23.3 fixed prefix)
2        var    SerString: ECMA-335 compressed-length + UTF-8 payload
  2+0    1|2    Compressed string length:
                  • If byte & 0x80 == 0 → 1-byte length (0x00–0x7F)
                  • If byte & 0x80 != 0 → 2-byte length: ((b0 & 0x7F) << 8) | b1
  2+L    n      Raw UTF-8 bytes of the JSON layout string
last     2      NumNamedArgs: 0x0000
```

#### Blob wire format — marker attributes (no arguments)

```
Offset   Size   Content
------   ----   -------
0        2      Prolog: 0x0001
2        2      NumNamedArgs: 0x0000
```

---

### WinUIAttribute — JSON Schema

The decoded string is a **JSON array** of body nodes representing the complete view layout tree.

#### Body node variants

**Control node** (most common):

```json
{
  "control": "<AbstractControlName>",
  "props":   { "<key>": <Value>, ... },
  "children": [ <BodyNode>, ... ]
}
```

**Conditional node** (`if`/`else`):

```json
{
  "control":  "If",
  "props":    { "condition": <Value> },
  "children": [ <BodyNode>, ... ]
}
```

**ForEach node**:

```json
{
  "control":  "ForEach",
  "props":    { "collection": <Value> },
  "children": [ <BodyNode>, ... ]
}
```

#### Value types

| JSON form | Meaning |
|---|---|
| `"a literal string"` | String constant |
| `42` / `3.14` | Numeric constant |
| `true` / `false` | Boolean constant |
| `"$varName"` | `@state` / `@binding` reference — wire into your reactive binding system |
| `"(expr ? a : b)"` | Inline expression string — emit verbatim or transpile |
| `".enumValue"` | Dot-enum value (`.accent`, `.vertical`, `.bold`) — map via your control registry |

#### Abstract control → platform mapping

| Abstract name | WinUI3 (CppWinC) | Qt | Web |
|---|---|---|---|
| `Column` | `StackPanel` (Vertical) | `QVBoxLayout` | `flex-col div` |
| `Row` | `StackPanel` (Horizontal) | `QHBoxLayout` | `flex-row div` |
| `Stack` | `Grid` (z-stack) | `QStackedLayout` | `relative div` |
| `Grid` | `Grid` | `QGridLayout` | `grid div` |
| `Scroll` | `ScrollViewer` | `QScrollArea` | `overflow-auto div` |
| `Wrap` | `VariableSizedWrapGrid` | `QFlowLayout` | `flex-wrap div` |
| `Spacer` | `Rectangle` (stretch) | `QSpacerItem` | `flex:1 div` |
| `Divider` | `NavigationViewItemSeparator` | `QFrame (HLine)` | `<hr>` |
| `Card` | `Border` | `QFrame` | `card div` |
| `Label` | `TextBlock` | `QLabel` | `<p>` |
| `Icon` | `FontIcon` | `QLabel (icon)` | `<i>` |
| `Image` | `Image` | `QLabel (pixmap)` | `<img>` |
| `Button` | `Button` | `QPushButton` | `<button>` |
| `Input` | `TextBox` | `QLineEdit` | `<input type="text">` |
| `TextArea` | `TextBox` (AcceptsReturn=True) | `QPlainTextEdit` | `<textarea>` |
| `Toggle` | `ToggleSwitch` | `QCheckBox` | `<input type="checkbox">` |
| `Slider` | `Slider` | `QSlider` | `<input type="range">` |
| `Checkbox` | `CheckBox` | `QCheckBox` | `<input type="checkbox">` |
| `Picker` | `ComboBox` | `QComboBox` | `<select>` |
| `TabView` | `TabView` | `QTabWidget` | `<div role="tablist">` |
| `NavView` | `NavigationView` | `QTreeWidget` | `<nav>` |
| `Link` | `HyperlinkButton` | `QLabel (openExternalLinks)` | `<a>` |
| `Progress` | `ProgressBar` / `ProgressRing` | `QProgressBar` | `<progress>` |
| `InfoBar` | `InfoBar` | `QMessageBox` | `<div role="alert">` |
| `Dialog` | `ContentDialog` | `QDialog` | `<dialog>` |
| `Tooltip` | `ToolTip` | `QToolTip` | `title` attr |

#### Common prop keys

| Key | Type | Meaning |
|---|---|---|
| `text` / `content` / `label` | string / `"$var"` | Primary text content |
| `spacing` | number | Gap between children (logical px) |
| `padding` | number | Inner padding (logical px) |
| `fontSize` | number | Font size |
| `fontWeight` | dot-enum / string | `.bold`, `.semiBold`, `.regular` |
| `fontFamily` | string | Font family name |
| `bold` | `"true"` | Bold shorthand |
| `italic` | `"true"` | Italic shorthand |
| `foregroundColor` / `backgroundColor` | string | Hex color or system token |
| `width` / `height` | number | Explicit size |
| `minWidth` / `minHeight` / `maxWidth` / `maxHeight` | number | Size constraints |
| `alignment` | dot-enum | `.leading`, `.center`, `.trailing`, `.stretch` |
| `cornerRadius` | number | Corner rounding |
| `opacity` | number | 0.0–1.0 |
| `style` | dot-enum | `.accent`, `.outlined`, `.secondary` |
| `visible` | bool / `"$var"` | Visibility binding |
| `disabled` | bool / `"$var"` | Disabled binding |
| `isOn` / `isChecked` / `isOpen` | bool / `"$var"` | Toggle / checkbox / bar state |
| `value` / `minimum` / `maximum` / `step` | number / `"$var"` | Slider / progress values |
| `placeholder` | string | Input hint text |
| `selectedIndex` | number / `"$var"` | Picker / tab selection |
| `source` | string | Image source URI |
| `glyph` | string | Icon glyph character |
| `severity` | string | InfoBar severity (`info`, `warning`, `error`, `success`) |
| `itemWidth` / `itemHeight` | number | Wrap panel item dimensions |
| `maximumRowsOrColumns` | number | Wrap panel max row/column count |
| `orientation` | dot-enum | `.horizontal`, `.vertical` |

> [!NOTE]
> The abstract → platform mapping table above is a **static snapshot** for human reference. The **authoritative** abstract → platform mapping is embedded in every emitted `.winmd` via `WinUIControlCatalogAttribute` — projections MUST build their control tables from that attribute rather than from this document.

---

### WinUIControlCatalogAttribute — JSON Schema

`WinUIControlCatalogAttribute` embeds the **complete control registry** compiled into `winuic.exe`: every abstract control schema (name, category, capabilities, properties, events) plus its compiled-in **WinUI 3 platform mapping** (platform type, namespace, fixed properties, per-property setters & value transforms) — plus any third-party backend controls merged in at compile time via `-c/-control <file>.json`. The catalog is **language-projection agnostic**: it contains WinRT API names only (full namespaces, bare enum member names), never language-specific spellings, aliases, or helpers — each projection resolves its own includes/qualification.

- **Scope**: `<Module>` TypeDef row 1 — exactly **once** per `.winmd`, never on view classes
- **Wire format**: identical to `WinUIAttribute` (2-byte prolog `0x0001`, SerString compressed length, UTF-8 JSON payload, `0x0000` NumNamedArgs)
- **Size**: ~60 KB for the full 104-control catalog (24 legacy abstracts + 80 real WinUI 3 controls)

#### Top-level shape

```json
{
  "version": 1,
  "backend": "winui3",
  "controls": [ { "<ControlEntry>", ... } ]
}
```

#### Control entry

```json
{
  "abstract_name": "Column",
  "category": "layout",
  "accepts_children": true,
  "accepts_text_content": false,
  "description": "...",
  "platform_type": "StackPanel",
  "platform_namespace": "Microsoft.UI.Xaml.Controls",
  "fixed_properties": [
    { "name": "Orientation", "value": "Vertical" }
  ],
  "properties": [
    { "name": "spacing", "type": "double",
      "platform_setter": "Spacing", "transform": "double" }
  ],
  "events": [
    { "name": "onClick", "platform_event": "Click" }
  ]
}
```

#### Runtime extensibility — any-name JSON auto-load and `-c/-control <file>.json`

WinUIC compiles with the built-in `winui3` backend (104 controls). To add **future or third-party WinUI 3 controls without recompiling winuic.exe**:

1. **Auto-loaded `*.json` (any name)**: every `*.json` sitting next to `winuic.exe` is merged into the built-in backend on **every** run — there is no hardcoded `winui.json`, any file name works. Non-registry JSON files are skipped silently. Add your controls in any such file and they become legal in `.winui` source and appear in the emitted catalog — no flags, no recompile.
2. **`-c/-control <file>.json` (per-project)**: one or more files applied on top of the auto-loaded registries (and any prior `-control` files). For duplicate `abstract_name`s the later file wins — both for the platform mapping and for explicitly set capability flags (`accepts_children` / `accepts_text_content`, last-writer-wins).

```
WinUIC -f app.winui -control third_party.json -o out -n MyApp
```

The files use the same neutral schema as the catalog above (no headers, no language-specific aliases — full WinRT namespaces, bare enum members). Controls are **merged** into the built-in backend (replace-or-append per `abstract_name`), and every new abstract name is registered as a control schema, so it becomes legal in `.winui` source and appears in the emitted catalog:

```json
{
  "backend": "winui3",
  "controls": [
    {
      "abstract_name": "MyChart",
      "platform_type": "ChartView",
      "platform_namespace": "Contoso.Controls",
      "accepts_children": true,
      "category": "custom",
      "fixed_properties": [ { "name": "IsInteractive", "value": "true" } ],
      "properties": [
        { "abstract_name": "dataSource", "platform_setter": "DataSource", "transform": "box", "type": "string" }
      ],
      "events": [
        { "abstract_name": "onPointSelected", "platform_event": "PointSelected" }
      ]
    }
  ]
}
```

Optional fields default to: `accepts_children=false`, `accepts_text_content=false`, `category="custom"`. Unknown `transform` names fall back to pass-through (see transforms above for the recognized set).

#### Comprehensive multi-control registry example

A full-featured registry covering every capability — layout containers, typed properties with all common transforms, events, fixed properties and multiple categories (one file serves both `WinUIC` and `xaml2winuic`; the `xaml` / `xamlEvents` / `universalMods` / `fragFamilies` keys are conversion-side extras that WinUIC ignores):

```json
{
  "backend": "winui3",
  "controls": [
    {
      "abstract_name": "AreaChart",
      "platform_type": "AreaChart",
      "platform_namespace": "Contoso.Visualization",
      "category": "custom",
      "accepts_children": false,
      "accepts_text_content": false,
      "fixed_properties": [
        { "name": "IsInteractive", "value": "true" },
        { "name": "AnimationDuration", "value": "0.25" }
      ],
      "properties": [
        { "abstract_name": "dataSource", "platform_setter": "DataSource", "transform": "box", "type": "string" },
        { "abstract_name": "seriesCount", "platform_setter": "SeriesCount", "transform": "int" },
        { "abstract_name": "lineWidth",   "platform_setter": "LineWidth",   "transform": "double" },
        { "abstract_name": "accent",      "platform_setter": "AccentBrush", "transform": "brush" },
        { "abstract_name": "padding",     "platform_setter": "Padding",     "transform": "thickness" },
        { "abstract_name": "renderUri",   "platform_setter": "RenderUri",   "transform": "uri" }
      ],
      "events": [
        { "abstract_name": "onPointSelected", "platform_event": "PointSelected" },
        { "abstract_name": "onSeriesToggle",  "platform_event": "SeriesVisibilityChanged" }
      ]
    },
    {
      "abstract_name": "RadialGauge",
      "platform_type": "RadialGauge",
      "platform_namespace": "MyLib.Controls",
      "category": "third-party",
      "accepts_children": false,
      "accepts_text_content": false,
      "properties": [
        { "abstract_name": "minimum",     "platform_setter": "Minimum", "transform": "double" },
        { "abstract_name": "maximum",     "platform_setter": "Maximum", "transform": "double" },
        { "abstract_name": "value",       "platform_setter": "Value",   "transform": "double" },
        { "abstract_name": "tickSpacing", "platform_setter": "TickSpacing", "transform": "double" },
        { "abstract_name": "unit",        "platform_setter": "Unit",    "transform": "enum" },
        { "abstract_name": "isAnimated",  "platform_setter": "IsAnimated", "transform": "nullable_bool" }
      ],
      "events": [
        { "abstract_name": "onValueChanged", "platform_event": "ValueChanged" }
      ]
    },
    {
      "abstract_name": "LedStrip",
      "platform_type": "LedStrip",
      "platform_namespace": "Fabrikam.Hardware",
      "category": "custom",
      "accepts_children": false,
      "accepts_text_content": true,
      "properties": [
        { "abstract_name": "ledCount",  "platform_setter": "LedCount",  "transform": "int" },
        { "abstract_name": "glow",      "platform_setter": "GlowBrush", "transform": "brush" },
        { "abstract_name": "isLit",     "platform_setter": "IsLit",     "transform": "nullable_bool" }
      ],
      "events": [
        { "abstract_name": "onTapped", "platform_event": "Tapped" }
      ]
    }
  ]
}
```

Used from `.winui` source:

```winui
#version 1

export view Dashboard {
    body {
        AreaChart(dataSource: "q1/q2", seriesCount: 3, lineWidth: 2.5, accent: "#1E90FF")
            .onPointSelected { self.Refresh(); }
            .width(640)
            .height(320)

        RadialGauge(minimum: 0, maximum: 100, value: 42, tickSpacing: 10, unit: "percent", isAnimated: true)
            .onValueChanged { self.SyncLabel(); }
            .width(200)
            .height(200)

        LedStrip("Top banner")     // accepts_text_content: positional string
            .ledCount(120)
            .glow("#FF4500")
            .isLit(true)
            .onTapped { self.ToggleStrip(); }
    }
}
```

#### Field reference

| Field | Always present? | Meaning |
|---|---|---|
| `version` | yes | Catalog schema version (`1`). Projections MUST reject unknown versions. |
| `backend` | yes | Backend the platform fields describe (`"winui3"`). |
| `abstract_name` | yes | Name used in `.winui` source and in `WinUIAttribute` JSON (`"control"` values). |
| `category` | yes | Registry group: `layout`, `content`, `input`, `navigation`, `feedback`, `menus`, `collections`, `datetime`, `status`, `dialogs`, `scrolling`, `media`, `text`, `shapes`, `icons`, `motion`, `custom`. |
| `accepts_children` | yes | `true` ⇒ `"children"` array in layout JSON is legal and must be iterated. |
| `accepts_text_content` | yes | `true` ⇒ first positional string argument is text content. |
| `description` | if set | Human-readable summary. |
| `platform_type` | if mapped | WinUI 3 class name to instantiate (e.g. `StackPanel`). |
| `platform_namespace` | if mapped | Full WinRT namespace of the platform type (e.g. `Microsoft.UI.Xaml.Controls`) — projection-side syntax for qualification. |
| `fixed_properties` | if any | Properties assigned unconditionally at construction (e.g. `Column` → `Orientation = Vertical`). |
| `properties[].name` | if props | Abstract property name (matches layout JSON `"props"` keys). |
| `properties[].type` | if props | `.winui` type string: `string`, `int`, `bool`, `float`, `double`, `void`. |
| `properties[].platform_setter` | if mapped | Platform property/method to call (e.g. `Spacing`). |
| `properties[].transform` | if mapped | Value conversion to apply before the setter (see table). |
| `events[].name` | if events | Abstract event name (e.g. `onClick`). |
| `events[].platform_event` | if mapped | Platform event to subscribe (e.g. `Click`). |

#### Value transforms (`properties[].transform`)

| Transform | Conversion contract |
|---|---|
| *(empty)* | Pass the value through unchanged. |
| `double` / `int` | Numeric literal — no conversion. |
| `box` | Wrap value in `IInspectable`/`Box<...>` (for `DependencyObject`-typed properties). |
| `brush` | Hex string `"#RRGGBB"` or theme token → platform brush (`SolidColorBrush`). |
| `thickness` | Number or `"l,t,r,b"` → platform `Thickness`. |
| `corner_radius` | Number → platform `CornerRadius`. |
| `enum` | String / dot-enum → platform enum member. |
| `symbol` | String glyph (`"\uE74E"` / Segoe MDL2 name) → platform icon symbol enum. |
| `font_weight` | String (`"Bold"`, `"SemiBold"`, …) → platform font weight. |
| `visibility` | Bool → platform `Visibility` (`Visible`/`Collapsed`). |
| `nullable_bool` | Bool/`"$var"` → nullable bool (`bool?`). |
| `datetime` | ISO-8601 string → platform date/time (`DateTimeOffset`). |
| `uri` | String → platform `Uri`. |
| `geopoint` | `"lat,lon"` → platform geographic point. |

#### Required projection algorithm

1. **Load once**: decode the catalog at module load; keep it cached. (Never per-view.)
2. **Resolve controls**: for every `"control"` name in `WinUIAttribute` JSON, look up `control_table[abstract_name]`:
   - Found → instantiate `platform_type` (namespace `platform_namespace`), set `fixed_properties` first, then map each `"props"` key through `properties[].platform_setter` applying `transform`.
   - Not found → treat the control name as the platform type itself (PascalCase convention) with no mappings; WinUIC accepts any name not in the registry as a dynamic custom control.
3. **Children/text**: use `accepts_children` to decide whether to iterate `"children"`; use `accepts_text_content` to decide whether the positional string argument is content.
4. **Events**: subscribe `platform_event` (from `events[].platform_event`) when the layout node's props contain the abstract `onXxx` name.

---

### WinUIDarkTitlebarAttribute

**Presence** on a runtime class signals that its window should render an immersive dark titlebar.

#### Win32 / Desktop implementation

```cpp
// Call during window init, after HWND is created:
BOOL dark = TRUE;
DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE /*= 20*/, &dark, sizeof dark);
```

#### AppWindow (WinUI3 / WinAppSDK) implementation

```csharp
var titleBar = AppWindow.TitleBar;
titleBar.ExtendsContentIntoTitleBar = true;
titleBar.ButtonBackgroundColor         = Colors.Transparent;
titleBar.ButtonForegroundColor         = Colors.White;
titleBar.ButtonHoverBackgroundColor    = Color.FromArgb(0x33, 0xFF, 0xFF, 0xFF);
titleBar.ButtonPressedBackgroundColor  = Color.FromArgb(0x66, 0xFF, 0xFF, 0xFF);
titleBar.BackgroundColor               = Colors.Transparent;
titleBar.InactiveBackgroundColor       = Colors.Transparent;
```

---

### WinUIAppIconAttribute

**Presence** signals that the window should bind the application icon from executable resource ID `1`.

#### Win32 implementation

```cpp
// Call during window init:
HINSTANCE hInst = GetModuleHandleW(NULL);
HICON big   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                  GetSystemMetrics(SM_CXICON),   GetSystemMetrics(SM_CYICON),   0);
HICON small = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)big);
SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
```

#### Packaged app (WinAppSDK)

```csharp
AppWindow.SetIcon(Path.Combine(Package.Current.InstalledLocation.Path, "Assets/AppIcon.ico"));
```

---

### WinUIDarkContextMenuAttribute

**Presence** signals the process should opt into dark Win32 popup menus and flyouts. **Call once, process-wide, before any window is shown.**

#### uxtheme.dll ordinal implementation

```cpp
// Load by name (no import lib needed):
HMODULE hUx = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

// Ordinal 135 — SetPreferredAppMode(PreferredAppMode mode)
// PreferredAppMode: Default=0, AllowDark=1, ForceDark=2, ForceLight=3
using fn135_t = DWORD(WINAPI*)(DWORD);
auto SetPreferredAppMode = (fn135_t)GetProcAddress(hUx, MAKEINTRESOURCEA(135));
if (SetPreferredAppMode) SetPreferredAppMode(1 /*AllowDark*/);

// Ordinal 136 — FlushMenuThemes() — forces existing menu handles to re-theme
using fn136_t = void(WINAPI*)();
auto FlushMenuThemes = (fn136_t)GetProcAddress(hUx, MAKEINTRESOURCEA(136));
if (FlushMenuThemes) FlushMenuThemes();

// Per-HWND: Ordinal 133 — AllowDarkModeForWindow(HWND, bool)
using fn133_t = bool(WINAPI*)(HWND, bool);
auto AllowDarkModeForWindow = (fn133_t)GetProcAddress(hUx, MAKEINTRESOURCEA(133));
if (AllowDarkModeForWindow) AllowDarkModeForWindow(hwnd, true);
```

---

### Base Class Injection (Your Responsibility)

All TypeDef rows in the `.winmd` have `Extends = 0`. This is intentional — the emitter knows nothing about your platform. **You supply the base class.**

#### Recommended decision logic

```text
function choose_base(view_name, layout_json):
    // Check first root control for strong signals
    root = layout_json[0].control if layout_json else ""
    if root in ["NavView", "TabView"]:
        return PLATFORM_NAVIGATION_WINDOW

    // Name-based convention (opt-in; projections may override)
    if view_name == "App":
        return PLATFORM_APPLICATION
    if view_name.ends_with("Window"):
        return PLATFORM_WINDOW

    // Default: reusable component
    return PLATFORM_COMPONENT
```

| View role | WinUI3 | Qt | Web |
|---|---|---|---|
| Application | `Microsoft.UI.Xaml.Application` | `QApplication` | — |
| Top-level window | `Microsoft.UI.Xaml.Window` | `QMainWindow` | `<body>` |
| Reusable component | `Microsoft.UI.Xaml.Controls.UserControl` | `QWidget` | `HTMLElement` |

---

### Per-Language Code Examples

#### CsWinC — C# (.NET / System.Reflection.Metadata)

```csharp
using var pe = new PEReader(File.OpenRead("MyApp.winmd"));
var md = pe.GetMetadataReader();

// Phase 0: module-scoped control catalog (read ONCE, from <Module> TypeDef row 1)
foreach (var cah in md.GetCustomAttributes(MetadataTokens.TypeDefinitionHandle(1)))
{
    var ca       = md.GetCustomAttribute(cah);
    var attrName = ResolveAttrName(md, ca.Constructor);
    if (attrName == "WinUIControlCatalogAttribute")
    {
        var reader = md.GetBlobReader(ca.Value);
        reader.ReadUInt16();                          // skip prolog 0x0001
        string json = reader.ReadSerializedString();
        controlTable = BuildControlTable(JsonDocument.Parse(json));
    }
}

// Phase 1: per-view attributes
foreach (var tdh in md.TypeDefinitions) {
    var td   = md.GetTypeDefinition(tdh);
    var name = md.GetString(td.Name);

    foreach (var cah in td.GetCustomAttributes()) {
        var ca       = md.GetCustomAttribute(cah);
        var attrName = ResolveAttrName(md, ca.Constructor); // walk MemberRef→TypeRef→Name

        if (attrName == "WinUIAttribute") {
            var reader = md.GetBlobReader(ca.Value);
            reader.ReadUInt16();                  // skip prolog 0x0001
            string json = reader.ReadSerializedString();
            GenerateUI(name, JsonDocument.Parse(json).RootElement, controlTable);
        }
        else if (attrName == "WinUIDarkTitlebarAttribute")   EmitDarkTitlebar(name);
        else if (attrName == "WinUIAppIconAttribute")         EmitAppIcon(name);
        else if (attrName == "WinUIDarkContextMenuAttribute") EmitDarkContextMenu(name);
    }
}
```

#### CppWinC (IMetaDataImport2)

```cpp
IMetaDataDispenserEx* dispenser;
CoCreateInstance(CLSID_CorMetaDataDispenser, nullptr, CLSCTX_INPROC_SERVER,
                 IID_IMetaDataDispenserEx, (void**)&dispenser);

IMetaDataImport2* import;
dispenser->OpenScope(L"MyApp.winmd", ofRead, IID_IMetaDataImport2, (IUnknown**)&import);

// Enumerate types, then per-type enumerate CustomAttribute
// When attr name == "WinUIAttribute":
//   const BYTE* blob; ULONG blobLen;
//   import->GetCustomAttributeByName(td, L"MyApp.WinUIAttribute", (const void**)&blob, &blobLen);
//   blob += 2;  // skip prolog
//   uint8_t len_byte = *blob++;
//   uint32_t json_len = (len_byte & 0x80) ? ((len_byte & 0x7F) << 8 | *blob++) : len_byte;
//   std::string json(reinterpret_cast<const char*>(blob), json_len);
//   generate_ui_code(type_name, json);
```

#### Rust (`windows` crate + `serde_json`)

```rust
// Open .winmd with IMetaDataImport2 via windows::Win32::System::WinRT::Metadata
// Walk TypeDef table for tdWindowsRuntime types
// For each CustomAttribute whose type name matches "WinUIAttribute":
fn read_layout_blob(blob: &[u8]) -> serde_json::Value {
    let blob = &blob[2..];                           // skip prolog
    let (json_len, body) = if blob[0] & 0x80 != 0 {
        (((blob[0] & 0x7F) as usize) << 8 | blob[1] as usize, &blob[2..])
    } else {
        (blob[0] as usize, &blob[1..])
    };
    serde_json::from_slice(&body[..json_len]).unwrap()
}
```

#### Python

```python
import json, struct

def read_layout_blob(blob: bytes) -> list:
    # Skip 2-byte prolog
    i = 2
    b = blob[i]
    if b & 0x80:
        str_len = ((b & 0x7F) << 8) | blob[i + 1]
        i += 2
    else:
        str_len = b
        i += 1
    return json.loads(blob[i : i + str_len].decode("utf-8"))

# Usage:
control_table = {}
# <Module> TypeDef (row 1) catalog — read ONCE, not per-type
for attr_name, blob in enumerate_typedef_attrs("MyApp.winmd", 1):
    if attr_name == "WinUIControlCatalogAttribute":
        catalog = read_layout_blob(blob)          # same wire format
        control_table = {c["abstract_name"]: c for c in catalog["controls"]}

for typedef, attrs in enumerate_winmd_types("MyApp.winmd"):
    for attr_name, blob in attrs:
        if attr_name == "WinUIAttribute":
            layout = read_layout_blob(blob)
            generate_ui_code(typedef.name, layout, control_table)
        elif attr_name == "WinUIDarkTitlebarAttribute":
            emit_dark_titlebar(typedef.name)
        elif attr_name == "WinUIAppIconAttribute":
            emit_app_icon(typedef.name)
        elif attr_name == "WinUIDarkContextMenuAttribute":
            emit_dark_context_menu(typedef.name)
```

---

### Quick-Reference Checklist for Projection Authors

- [ ] Read `WinUIControlCatalogAttribute` **once** from the `<Module>` row; build and cache the abstract → platform control table (do NOT hardcode a mapping)
- [ ] Walk `CustomAttribute` table for every `TypeDef` with `tdWindowsRuntime` flag
- [ ] Decode `WinUIAttribute` blob: skip 2-byte prolog → compressed length → UTF-8 → JSON parse
- [ ] Resolve every `"control"` name in the layout JSON via the catalog (`platform_type`, `platform_namespace`); apply `fixed_properties` first, then map props through `platform_setter` + `transform`
- [ ] Wire `"$varName"` values into your reactive state / binding system
- [ ] Inject the correct platform **base class** for each view (`Extends = 0` in `.winmd` is intentional)
- [ ] Emit `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` when `WinUIDarkTitlebarAttribute` present
- [ ] Emit `WM_SETICON` / `SendMessageW` bindings when `WinUIAppIconAttribute` present
- [ ] Call `uxtheme.dll` ordinals 133/135/136 when `WinUIDarkContextMenuAttribute` present
- [ ] **Never hardcode** `Microsoft.UI.Xaml` or any platform namespace — those live only in your projection, never in the `.winmd`

---

### Consuming winuic Types — The A-Z Language-Agnostic Contract

> This is the complete, language-neutral contract for consuming **types** out
> of a `winuic` `.winmd`. Every rule below holds for ALL projections — C++,
> C#, Rust, Python, Swift, Kotlin, and any future binding — so that any
> projection can reproduce the authored app **100% cleanly** without reading
> another projection's source. Nothing here is WinUI-specific unless marked;
> a projection for a different UI backend follows the identical contract and
> only substitutes its own platform mapping (from the embedded control
> catalog).

#### 1. Naming & identity (the namespace switch wins)

- The CLI `-namespace`/`-n` switch **overrides any derived name**: it names
  the emitted `.winmd` file (`<ns>.winmd`), the ECMA-335 `<Module>` name
  string, and the namespace of every emitted type. Without `-n`, the
  sanitized module name of the first `.winui` source is used.
- **One naming rule across the pipeline**: spaces are hyphen-joined once
  (`WinUI Gallery` → `WinUI-Gallery`); the `-namespace`/`-n` value is used
  verbatim. The same name forms the `.winmd`, the metadata namespace and the
  final exe/`.pri`.
- Every view `V` in namespace `<ns>` surfaces as:
  - a runtime class `<ns>.V` (activatable — see §7),
  - an `<ns>.V` marker interface used as the type's identity handle,
  - a default constructor that materializes the whole visual tree.
- `Extends` is intentionally **empty** in the `.winmd`. The projection MUST
  inject the platform base class by view role (Application / top-level
  Window / reusable component) — see the view-role table above.

#### 2. The type surface of a view

Each view's public WinRT surface is authored, not guessed:

| `.winui` source | `.winmd` surface | Projection obligation |
|---|---|---|
| `@state var x: T = init` | property `T x` (get/set) | Emit a real property with initial value; writes notify dependents |
| `@binding var x: T` | property `T x` (get/set, two-way) | Wire setter back into the owner (two-way); reads forward |
| `@computed var y: T` | read-only property | Recompute from dependency set on any dependency write |
| `@environment var e: T` | property with environment lookup | Resolve from the app/environment scope, fall back to default |
| `callback onFoo(args…)` | method / event with matching signature | Emit callable member; `.onFoo { }` registrations bind to it |
| `import`d code-behind members | not in `.winmd` | The projection (or host app) supplies implementations for referenced members |

Type mapping is fixed and lossless: `string`→UTF-16 string, `int`→32-bit
signed, `double`→IEEE-754 64-bit, `bool`→boolean, `datetime`→the platform
instant type, `T[]`→read-write vector of `T`. Nothing may be silently
narrowed or boxed differently per language.

#### 3. Layout tree consumption (`WinUIAttribute`)

The layout JSON is the single source of truth for the visual tree. For every
control node:

1. Resolve `control` through the **embedded catalog** (`abstract_name` →
   `platform_type` + `platform_namespace`). Never hardcode the mapping.
2. Apply `fixed_properties` first, then each `props` entry through the
   catalog's `platform_setter` + `transform` for that property.
3. Attach `children` according to the control's **composition kind**:

   | Composition kind | Child attachment |
   |---|---|
   | `panel` | append to the children collection |
   | `content` | single child through the content property (wrap multiple children in a stack) |
   | `items` | append to the items collection (or its named variant, e.g. primary commands) |
   | `items_panel` | append to the menu/commands items collection |
   | `documents` | append to the inlines collection |
   | `blocks` | append to the blocks collection |
   | `columns` | append to the columns collection |
   | `navigation` | first child = pane content, remaining = menu items |
   | `navigation_item` | child goes through the item content property |
   | `popup` | single child via the popup child property |
   | `charts` | route each child by its own type (data / axes / series collections) |

4. Value kinds in `props` are language-neutral:
   | JSON form | Obligation |
   |---|---|
   | string / number / bool | constant — emit typed literal |
   | `"$name"` | state/binding reference — route through the reactive system (§4) |
   | `".enumValue"` | dot-enum — resolve via the catalog's enum vocabulary |
   | `"(expr …)"` | inline expression — emit verbatim in the projection language |

5. `.raw(key, value)` pairs and `Raw()` fragments carry the **verbatim XAML
   surface** of everything the abstract model does not cover. Projections
   MUST preserve them (as attached-property application, attribute setting,
   or verbatim resource text) — dropping them loses authored fidelity.

#### 4. Reactive binding obligations

- Every `$ref` read MUST observe its source property; every two-way binding
  MUST write back on change (toggle/slider/checkbox/text input round-trips
  are part of the contract).
- Conditionals (`if`/`else` nodes) and `ForEach` nodes re-evaluate on
  dependency change: `If` swaps the subtree, `ForEach` re-materializes its
  body per collection element.
- Bindings are late-bound: the tree is constructed once, values flow through
  the binding system afterwards. Projections must not constant-fold `$ref`
  values at construction time.

#### 5. Resources, templates and assets

- Style/brush/template fragments authored in `.winui` travel verbatim into
  the `.winmd` (raw pairs) and into the per-project `resources.pri` —
  projections emit them into the resource system of their platform.
- `winuic` copies **only the assets the project actually references**
  (reference-only asset copying) and indexes them into `resources.pri`.
  Projections that stage assets next to the binary MUST mirror that
  reference-only behavior and MUST also write the package index under the
  binary's own name (`<ExeName>.pri` next to `<ExeName>`) because unpackaged
  MRT Core only finds `<ExeName>.pri`.

#### 6. App-level hint attributes

`WinUIDarkTitlebarAttribute`, `WinUIAppIconAttribute`,
`WinUIDarkContextMenuAttribute` and `WinUITitlebarAttribute` are
Win32-flavored capabilities with platform-neutral meaning: "this app wants a
dark titlebar / icon / dark context menu / custom titlebar text". Each
projection translates that intent into its platform's API (Win32 projections
call `DwmSetWindowAttribute` / `WM_SETICON` / uxtheme ordinals; other
platforms use their own mechanisms). The attributes never dictate a specific
OS call inside the metadata.

#### 7. Activation contract

- Every emitted runtime class must be constructible by the projection's
  activation path: packaged activations use the normal WinRT registration;
  unpackaged projections must arrange an in-process factory (manifest
  reg-free activation, or a local factory registry consulted before
  `RoActivateInstance`) so `<ns>.V()` works without any install step.
- View construction order is deterministic: state properties first (with
  defaults), then the visual tree per `WinUIAttribute`, then bindings attach.
  Event callbacks may fire during construction only if the platform can
  deliver them (guard with the same readiness rules the platform requires).

#### 8. Determinism guarantees a projection may rely on

- The `.winmd` contains **no language-specific constructs**: no textual
  includes, no per-language type spellings, no code-generation artifacts.
  Every language decision lives in the consuming projection.
- The same inputs always produce the same `.winmd` bytes (parallel batching
  does not change output), so projections may cache aggressively.
- The control catalog attribute is the **authoritative** control surface;
  when compiler and document disagree, the catalog wins.

---

## [CppWinC](https://github.com/mominshaikhdev/CppWinC-Compiler) Projection — WinUIC .winmd Consumption Contract

> **Audience**: [**CppWinC**](https://github.com/mominshaikhdev/CppWinC-Compiler) (`cppwinc.exe`) — C++23 named modules, fast ABI only.
> This is the concrete, implementation-verified algorithm used by the CppWinC
> projection to turn a WinUIC `.winmd` into a compiling WinUI 3 app
> (see [`cppwinc/README.md`]([cppwinc/README.md](https://github.com/mominshaikhdev/CppWinC-Compiler/README.md)) and
> [`WinUI-Gallery/README.md`]([WinUI-Gallery/README.md](https://github.com/microsoft/WinUI-Gallery/README.md))).
> Everything below is **projection knowledge** — none of it lives in the
> `.winmd` itself.

### 1. WinMD loading

- Load all input winmds into one cache: `out\WinUI-Gallery.winmd` (authored
  module) + platform winmds (WASDK WinUI, Foundation, InteractiveExperiences,
  WebView2, Windows SDK).
- **Read the control catalog exactly once** from the authored module's
  `<Module>` TypeDef (row 1): `WinUIControlCatalogAttribute` → decode
  (prolog `0x0001`, SerString length, UTF-8 JSON, `0x0000` NumNamedArgs) →
  `{version, backend, controls[]}` → build `abstract_name → entry` table,
  cached per database.
- Per-view attributes (`WinUIAttribute`, `WinUIDarkTitlebarAttribute`,
  `WinUIAppIconAttribute`, `WinUIDarkContextMenuAttribute`) live on the
  runtime-class TypeDef rows (2..N), identified by attribute type name
  resolved through `MemberRef → TypeRef → strings`. The window-level
  attributes exist **only** on `Window`-extending classes.

### 2. Layout JSON → C++ generation algorithm

For each view with a `WinUIAttribute` payload, walk the JSON body
array (root nodes), producing `InitializeComponent()` statements inside the
view's `%_base<D>` template class:

1. **Node kinds**: `control` nodes, `If` (props `condition`), `ForEach`,
   and the projection-side specials `Card` / `Divider` / `Spacer`
   (not in the catalog; see specials below). The leading
   `{"control":"state","props":{"vars":[...]}}` node is consumed first —
   it is **not** a tree node.
2. **Control resolution** (in order):
   a. **Component view** — a TypeDef in the authored module with the same
      name (skipping the view itself and `Window`-extending types; a
      component view with props/children/events is treated as a platform
      control usage, not a page embed). Bare usage →
      `winrt::<ns>::<View>()`. A bare `Window`-extending view yields `{}`
      (the host emit instantiates it).
   b. **Catalog entry** → `platform_type` in `platform_namespace`.
   c. **Platform type by name** (PascalCase convention) resolved from
      platform metadata — `Microsoft.UI.Xaml*` wins over `Windows.UI.Xaml*`
      duplicates; `Windows.UI.Xaml`-only hits are rejected (a WinUI 3 tree
      cannot host UWP elements); unresolved → neutral `TextBlock`
      placeholder so the tree compiles.
3. **Property emission order**: `fixed_properties` first (unconditional),
   then `"props"` keys mapped through `properties[].platform_setter` +
   `transform`. Values:
   - `"$name"` → **state binding**: apply the member expression to the
     control immediately, and if a two-way event exists (`Text→TextChanged`,
     `IsOn→Toggled`, `SelectedIndex→SelectionChanged`, `Value→ValueChanged`)
     and the catalog advertises it, subscribe a handler that writes the
     control value back into the state member and calls `RefreshState()`.
   - `"(...)"` → IR expression, emit verbatim (`self->m_x` → `m_x`).
   - `{var}` interpolated string → `L"a" + winrt::to_hstring(m_var) + L"b"`.
4. **Transforms** (`properties[].transform`): `double/int` passthrough;
   `box` (`ItemsSource` splits `"a, b, c"` on `,` or `>` into
   `single_threaded_vector<hstring>`); `thickness` (1 or 4 values);
   `corner_radius`; `enum` (member resolved from platform metadata,
   `Microsoft.UI.Xaml*` namespace preferred); `font_weight`; `visibility`;
   `symbol`; `nullable_bool`; `bitmap_image`; `uri`; `brush` (#RRGGBB →
   `SolidColorBrush`, else `TryLookup` theme brush); `resource` (accent →
   `AccentButtonStyle`, subtle → `SubtleButtonStyle`); `datetime`
   (`HH:MM[:SS]` → `TimeSpan`, ISO-8601 → `clock::from_time_t`);
   `geopoint`; `skip` → drop the property.
5. **Specialized enum transforms** (abstract value → platform enum member,
   type resolved from metadata): `info_bar_severity` (info→Informational),
   `command_bar_label_position`, `map_style`, `nav_back_enum`,
   `nav_pane_display_mode`, `number_box_spin_placement`,
   `pips_pager_buttons`, `progress_style`, `refresh_pull_direction`
   (top/bottom/left/right → TopToBottom/BottomToTop/LeftToRight/RightToLeft),
   `refresh_visualizer_orientation`, `refresh_visualizer_state`,
   `scroll_bar_visibility`, `scroll_content_orientation` (→ `Orientation`),
   `scroll_mode`, `split_view_display_mode`, `split_view_placement`,
   `stretch_direction`, `stretch` (fill→Fill, fit→Uniform, crop→UniformToFill),
   `swipe_mode`, `teaching_tip_placement`, `zoom_mode`, `placement_mode`,
   `annotated_scroll_bar_kind`, `calendar_view_display_mode`. Unknown
   transform → omit the property. Bare member values fall back to
   `ui_pascal_case(value)`.
6. **Children attachment** (projection knowledge): `StackPanel/Grid/
   VariableSizedWrapGrid/Canvas/RelativePanel/ItemsStackPanel/ItemsWrapGrid`
   → `Children().Append`; `TabView/FlipView/ListView/GridView/
   ItemsRepeater/MenuFlyout/CommandBarFlyout/CommandBar/NavigationView` →
   `Items().Append`; `TreeView` → `RootNodes().Append`; `MenuBar` →
   `MenuBarItems().Append`; `SwipeControl` → `LeftItems().Append`;
   `Border/Viewbox/Popup` → single-child `Child`; default → `Content`.
   `Row`/`Column` containing a `Spacer` becomes a **Grid with Auto/Star
   columns** (`ColumnSpacing`) so the Spacer absorbs leftover space;
   Spacer itself is a `Border` (16×16).
7. **Specials**: `Card` → `Border` (corner 8, padding 16, theme brushes
   `CardBackgroundFillColorDefaultBrush`/`CardStrokeColorDefaultBrush`, multi
   child → inner `StackPanel` spacing 12); `Divider` → 1px `Border` +
   `DividerStrokeColorDefaultBrush`; `ForEach` → `StackPanel` wrapping the
   generated children; `If` inside a refresh → inline `if (cond) { … }`
   rebuilding into the container.
8. **Conditional gating (stateful views)**: any container with an `If`
   child — or the root of a view with `@state`/`$` bindings — becomes a
   class member `m_gated_N` (declared `T m_gated_N { nullptr };`). At init
   the tree is built into the member; `RefreshState()` clears it
   (`Children().Clear()` / `Items().Clear()` / `Child(nullptr)` /
   `Content(nullptr)` per attach kind) and re-runs the entries — stable
   children unconditionally, `If`s under `if (cond)` / `else` (recorded as
   `condition` and `!(condition)` entries).
9. **Events**: a trailing action closure subscribes the control's default
   event (catalog `events[]` first, then the fallback table: Click family,
   ToggleSwitch→Toggled, TextBox/AutoSuggestBox/RichEditBox→TextChanged,
   PasswordBox→PasswordChanged, list controls→SelectionChanged,
   Slider/NumberBox→ValueChanged, DatePicker→DateChanged,
   TimePicker→TimeChanged); named `onXxx` props map through
   `events[].platform_event` (`onClick→Click`, `onChanged/onTextChanged→
   TextChanged`, `onToggled→Toggled`, `onValueChanged→ValueChanged`,
   `onSelectionChanged→SelectionChanged`, `onChecked→Checked`,
   `onUnchecked→Unchecked`). Handler bodies are rendered from serialized
   action statements (`m_var op value`, `open(name)`/`close(name)` →
   `m_<isOpen-var> = true/false`); unknown calls are dropped. Every handler
   ends with `RefreshState();` when the view has state.
10. **State members**: `@state` vars become class members
    (`int/bool/double/hstring` by type; string initial → escaped `L"…"`);
    `open_vars` collects `isOpen`-bound vars so no-arg `open()/close()`
    resolve.
11. **Root wiring**: first root var → `static_cast<D*>(this)->Content(var)`
    — note the projection supplies its own `Content()` (a
    `UIElement m_content` member in `%_base`); after the tree,
    `RefreshState()` if gated.

### 3. Window views (chrome attributes)

For `Window`-extending views, `InitializeComponent()` additionally emits
(only when the corresponding attribute is present):

- **Dark titlebar**: query `IWindowNative` (`get_WindowHandle`), fall back
  to `GetActiveWindow()`; `SetWindowTextW(hWnd, L"WinUI Gallery")`;
  `DwmSetWindowAttribute` 20 (dark mode TRUE), 35 (background `0x00202020`),
  36 (text `0x00FFFFFF`).
- **App icon**: if `WM_GETICON(ICON_BIG)` is empty → `LoadIconW(module, 1)`
  → `WM_SETICON` big+small.
- **Dark context menu**: `uxtheme.dll` ordinal 135 `SetPreferredAppMode(2)`
  + ordinal 133 `AllowDarkModeForWindow(hWnd, TRUE)`.
- Window-class views are **never** embedded in another view's tree; they are
  instantiated by the App host.

### 4. App class (extends Application)

`write_component_h` emits the App struct with:

- `OnLaunched`: append `XamlControlsResources()` to
  `Application::Current().Resources().MergedDictionaries()`, `winrt::make`
  the window component (named by the App's layout root, fallback = first
  `Window`-extending view), copy its `Content()` into `m_window.Content`,
  `Activate()`, `Title(L"WinUI Gallery")`. Errors log `startup_error.log`
  (CTOR-HR / ONLAUNCHED-HR / ONLAUNCHED-STD / ONLAUNCHED-UNKNOWN).
- `IXamlMetadataProvider` members backed by
  `XamlControlsXamlMetaDataProvider` (`GetXamlType` ×2, `GetXmlnsDefinitions`).

### 5. Component templates & fast ABI

- `%_base<D, typename B = UserControl, I...> : implements<D, <ns>::%%%, I...>`
  with `using base_type/class_type/implements_type`, `m_content` +
  `Content()`, `GetRuntimeClassName() → L"<ns>.<Name>"`.
- External (platform) base → `impl::require<D, …>` for non-overridable
  interfaces, `protected impl::require<D, …>` + `friend consume_t/require_one`
  for protected/overridable ones; authored base → `impl::base<D>, B` +
  `composing` + overridable interfaces; no base → `no_module_lock, B`.
- **Fast ABI only**: `has_fastabi(type)` is always true; non-default
  fast interfaces are tearoffs via `make_fast_abi_forwarder(...get_abi<
  class_type>(), guid_of<I>(), offset)` in `query_interface_tearoff` (offset =
  base count + default-interface method count + accumulated slot counts);
  `WINRT_FAST_ABI_SIZE` vtable slots 6..1023 reference `winrt_ff_thunk%`
  forwarders; `base_%()` returns `static_cast<D&>(*this).get_abi<Base>()`.
  No slow-ABI fallback exists.
- `factory_implementation::% : %T<%, implementation::%>` with
  `IActivationFactory` (+ statics/composable interfaces) and forwarders:
  `ActivateInstance() → make<T>()` (or `[[noreturn]]` throw
  `hresult_not_implemented` when no default ctor), constructor forwarders
  `make<T>(args)`, static forwarders `T::m(args)`, composable forwarders
  `impl::composable_factory<T>::CreateInstance<Ret>(…, base, inner)`.
- `%T = %_base<D, I...>` alias + `__has_include` chain for the optional
  `%.winui.g.h` authoring header.

### 6. Activation glue

- `module.g.cpp`-equivalent: `winrt_make_%()` → `detach_abi(make<
  <ns>::factory_implementation::%>())`; `%_can_unload_now()`;
  `%_get_activation_factory(name)` matching **suffix** `L"<ns>.<Name>"`
  (right-to-left `std::equal`); `WINRT_GetActivationFactory` /
  `WINRT_CanUnloadNow` (WRL `Microsoft::WRL::Module` hook when
  `_WRL_MODULE_H_`).
- `-component_opt` emits per-class activation factories as the single (module) path.

### 7. Platform namespace coverage

Generated code needs projection modules for: `Microsoft.UI.Xaml`,
`Microsoft.UI.Xaml.Controls(.Primitives)`, `Microsoft.UI.Xaml.Shapes`,
`Microsoft.UI.Xaml.Media(.Imaging)`, `Microsoft.UI.Xaml.Input`,
`Microsoft.UI.Text`, `Windows.Foundation(.Collections)`,
`Windows.Devices.Geolocation` — plus every catalog `platform_namespace`
that exists in the input metadata (third-party controls merged via WinUIC's
`-c` switch) — and `Windows.UI.Xaml.Interop`,
`Microsoft.UI.Xaml.Markup`, `Microsoft.UI.Xaml.XamlTypeInfo` for the App
class. All of these are emitted as named C++23 modules, not headers.

## Registry override hatch (`"override": true`)

Third-party registry JSON files are merged **official-first** by default: a
JSON entry may *extend* a built-in control (extra properties/events/fixed
mappings) but never hijacks its platform type, namespace, or existing
mappings, and it cannot flip `accepts_children` / `accepts_text_content` on
an official control.

When an official control is poorly implemented in the built-in registry, a
registry entry can opt out of the merge with an explicit per-control flag:

```json
{ "abstract_name": "RichTextBlock", "platform_type": "RichTextBlock",
  "override": true,
  "properties": [ ... replaces the official list ... ],
  "events":     [ ... ] }
```

`"override": true` **replaces** the official definition wholesale — schema
(properties, events, capability flags) and backend (platform type, namespace,
mappings, fixed properties). Everything else keeps the extend-never-hijack
behavior.

## Related Projects

| Project | Description |
|---|---|
| [Xaml2WinUIC-compiler](https://github.com/mominshaikhdev/Xaml2WinUIC-compiler) | XAML-to-`.winui` transpiler — converts WinUI 3 XAML markup into the `.winui` DSL |
| [CppWinC](https://github.com/mominshaikhdev/CppWinC-Compiler) | C++23 named modules projection — consumes `.winmd` from `WinUIC` and emits compilable `.ixx` modules |
