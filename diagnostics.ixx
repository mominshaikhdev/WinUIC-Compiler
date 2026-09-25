// diagnostics.ixx — Structured compiler diagnostics for the .winui compiler
// Provides Severity levels, SourceLocation tracking, individual Diagnostic
// records, and a DiagnosticBag accumulator with formatted output.
// Copyright (c) 2026 Momin Shaikh. All rights reserved.

module;
#include <string>
#include <string_view>
#include <vector>
#include <format>
#include <print>
#include <cstdint>
#include <span>
#include <algorithm>
#include <ranges>
export module winui.diagnostics;

export namespace winui::diag {

// Source Location

struct SourceLocation {
    std::string file;
    std::size_t line   = 1;
    std::size_t column = 1;

    [[nodiscard]] auto to_string() const -> std::string {
        return std::format("{}:{}:{}", file, line, column);
    }
};

// Severity

enum class Severity : std::uint8_t {
    Note,
    Warning,
    Error,
};

[[nodiscard]] constexpr auto severity_label(Severity s) -> std::string_view {
    switch (s) {
        case Severity::Note:    return "note";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
    }
    std::unreachable();
}

// Single Diagnostic

struct Diagnostic {
    Severity       severity;
    std::string    message;
    SourceLocation location;

    [[nodiscard]] auto format() const -> std::string {
        return std::format("{}: {}: {}",
            location.to_string(),
            severity_label(severity),
            message);
    }
};

// Diagnostic Bag (accumulator)

class DiagnosticBag {
public:
    // Report helpers
    auto error(SourceLocation loc, std::string msg) -> void {
        if (m_diagnostics.size() >= 500) return;
        m_diagnostics.push_back(Diagnostic{
            .severity = Severity::Error,
            .message  = std::move(msg),
            .location = std::move(loc),
        });
    }

    auto warning(SourceLocation loc, std::string msg) -> void {
        if (m_diagnostics.size() >= 500) return;
        m_diagnostics.push_back(Diagnostic{
            .severity = Severity::Warning,
            .message  = std::move(msg),
            .location = std::move(loc),
        });
    }

    auto note(SourceLocation loc, std::string msg) -> void {
        if (m_diagnostics.size() >= 500) return;
        m_diagnostics.push_back(Diagnostic{
            .severity = Severity::Note,
            .message  = std::move(msg),
            .location = std::move(loc),
        });
    }

    // Generic report
    auto report(Severity sev, SourceLocation loc, std::string msg) -> void {
        if (m_diagnostics.size() >= 500) return;
        m_diagnostics.push_back(Diagnostic{
            .severity = sev,
            .message  = std::move(msg),
            .location = std::move(loc),
        });
    }

    // Queries
    [[nodiscard]] auto has_errors() const noexcept -> bool {
        return std::ranges::any_of(m_diagnostics, [](auto const& d) {
            return d.severity == Severity::Error;
        });
    }

    [[nodiscard]] auto error_count() const noexcept -> std::size_t {
        return static_cast<std::size_t>(
            std::ranges::count_if(m_diagnostics, [](auto const& d) {
                return d.severity == Severity::Error;
            }));
    }

    [[nodiscard]] auto warning_count() const noexcept -> std::size_t {
        return static_cast<std::size_t>(
            std::ranges::count_if(m_diagnostics, [](auto const& d) {
                return d.severity == Severity::Warning;
            }));
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return m_diagnostics.empty();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return m_diagnostics.size();
    }

    [[nodiscard]] auto diagnostics() const noexcept
        -> std::span<Diagnostic const> {
        return m_diagnostics;
    }

    // Format all diagnostics into a single multi-line string
    [[nodiscard]] auto format_all() const -> std::string {
        std::string result;
        for (auto const& d : m_diagnostics) {
            result += d.format();
            result += '\n';
        }
        if (!m_diagnostics.empty()) {
            auto summary = std::format(
                "{} error(s), {} warning(s) generated.\n",
                error_count(), warning_count());
            result += summary;
        }
        return result;
    }

    // Merge another bag into this one
    auto merge(DiagnosticBag const& other) -> void {
        m_diagnostics.insert(
            m_diagnostics.end(),
            other.m_diagnostics.begin(),
            other.m_diagnostics.end());
    }

    // Clear all diagnostics
    auto clear() noexcept -> void {
        m_diagnostics.clear();
    }

private:
    std::vector<Diagnostic> m_diagnostics;
};

} // namespace winui::diag
