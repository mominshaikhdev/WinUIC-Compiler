// main.cpp — Entry point for the .winui compiler CLI
// Links against the compiled compiler C++23 modules (*.obj) and
// provides a parallel, multi-file compilation pipeline.

// Build:
// cl /std:c++latest /EHsc main.cpp *.obj /Fe:winuic.exe
// Copyright (c) 2026 Momin Shaikh. All rights reserved.

#include <filesystem>

import winui.cli;

auto main(int argc, char** argv) -> int {
    return winui::cli::run(argc, argv);
}


