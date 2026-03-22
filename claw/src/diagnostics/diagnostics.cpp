#include "diagnostics/diagnostics.h"

#include <algorithm>
#include <sstream>

namespace claw::frontend {

namespace {

std::string severityName(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Error:
        return "error";
    case DiagnosticSeverity::Warning:
        return "warning";
    }
    return "error";
}

std::string trimLineEnding(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    return std::string(line);
}

std::string lineAt(std::string_view source, size_t targetLine) {
    size_t currentLine = 1;
    size_t lineStart = 0;

    for (size_t i = 0; i <= source.size(); ++i) {
        if (i == source.size() || source[i] == '\n') {
            if (currentLine == targetLine) {
                return trimLineEnding(source.substr(lineStart, i - lineStart));
            }
            ++currentLine;
            lineStart = i + 1;
        }
    }

    return {};
}

std::string caretLine(const SourceSpan& span) {
    if (!span.isValid()) {
        return {};
    }

    const size_t padding = span.column > 0 ? span.column - 1 : 0;
    const size_t width = std::max<size_t>(1, span.length);
    return std::string(padding, ' ') + std::string(width, '^');
}

} // namespace

bool SourceSpan::isValid() const {
    return line > 0 && column > 0;
}

DiagnosticError::DiagnosticError(std::string summary, std::vector<Diagnostic> diagnostics)
    : summary_(std::move(summary)), diagnostics_(std::move(diagnostics)) {}

const char* DiagnosticError::what() const noexcept {
    return summary_.c_str();
}

const std::vector<Diagnostic>& DiagnosticError::diagnostics() const {
    return diagnostics_;
}

std::string formatDiagnostic(
    const Diagnostic& diagnostic,
    std::string_view path,
    std::string_view source) {
    std::ostringstream out;
    const std::string effectivePath = diagnostic.path.empty() ? std::string(path) : diagnostic.path;

    out << severityName(diagnostic.severity);
    if (!diagnostic.stage.empty()) {
        out << "[" << diagnostic.stage << "]";
    }
    out << ": " << diagnostic.message;

    if (!diagnostic.span.isValid()) {
        out << "\n --> " << effectivePath;
        return out.str();
    }

    out << "\n --> " << effectivePath << ":" << diagnostic.span.line << ":" << diagnostic.span.column;

    const std::string lineText = lineAt(source, diagnostic.span.line);
    if (!lineText.empty()) {
        const std::string lineNumber = std::to_string(diagnostic.span.line);
        out << "\n " << lineNumber << " | " << lineText;
        out << "\n " << std::string(lineNumber.size(), ' ') << " | " << caretLine(diagnostic.span);
    }

    return out.str();
}

std::string formatDiagnostics(
    const std::vector<Diagnostic>& diagnostics,
    std::string_view path,
    std::string_view source) {
    std::ostringstream out;
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i > 0) {
            out << "\n\n";
        }
        out << formatDiagnostic(diagnostics[i], path, source);
    }
    return out.str();
}

} // namespace claw::frontend