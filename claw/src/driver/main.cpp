#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "analysis/ownership.h"
#include "analysis/sema.h"
#include "diagnostics/diagnostics.h"
#include "ir/air.h"
#include "ir/oir.h"
#include "lexer/lexer.h"
#include "module/project.h"
#include "parser/parser.h"

namespace {

std::string maybeConvertMsysPath(std::string_view rawPath) {
    if (rawPath.size() < 4 || rawPath[0] != '/' || rawPath[2] != '/' ||
        !std::isalpha(static_cast<unsigned char>(rawPath[1]))) {
        return {};
    }

    std::string converted;
    converted += static_cast<char>(std::toupper(static_cast<unsigned char>(rawPath[1])));
    converted += ':';
    converted += '/';

    for (size_t i = 3; i < rawPath.size(); ++i) {
        converted += rawPath[i];
    }

    return converted;
}

std::vector<std::filesystem::path> buildCandidatePaths(const std::string& rawPath) {
    std::vector<std::filesystem::path> candidates;
    candidates.emplace_back(rawPath);

    const std::string msysConverted = maybeConvertMsysPath(rawPath);
    if (!msysConverted.empty() && msysConverted != rawPath) {
        candidates.emplace_back(msysConverted);
    }

    return candidates;
}

bool isMissingPathError(const std::error_code& ec) {
    return ec == std::make_error_code(std::errc::no_such_file_or_directory) ||
           ec == std::make_error_code(std::errc::not_a_directory);
}

std::string describePathKind(const std::filesystem::path& path) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (!exists) {
        if (!ec || isMissingPathError(ec)) {
            return "missing";
        }
        return std::string("status unavailable: ") + ec.message();
    }

    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec) {
        return std::string("status unavailable: ") + ec.message();
    }
    if (std::filesystem::is_regular_file(status)) {
        return "regular file";
    }
    if (std::filesystem::is_directory(status)) {
        return "directory";
    }
    if (std::filesystem::is_symlink(status)) {
        return "symlink";
    }
    return "other";
}

bool openSourceFile(
    const std::string& rawPath,
    std::ifstream* file,
    std::filesystem::path* openedPath,
    std::vector<std::filesystem::path>* triedPaths) {
    const auto candidates = buildCandidatePaths(rawPath);
    if (triedPaths) {
        *triedPaths = candidates;
    }

    for (const auto& candidate : candidates) {
        std::ifstream attempt(candidate, std::ios::binary);
        if (attempt.is_open()) {
            *file = std::move(attempt);
            if (openedPath) {
                *openedPath = candidate;
            }
            return true;
        }
    }

    return false;
}

void printOpenFailure(const std::string& rawPath, const std::vector<std::filesystem::path>& triedPaths) {
    std::cerr << "Failed to open source file.\n";
    std::cerr << "  requested path: " << rawPath << "\n";

    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    std::cerr << "  current working directory: "
              << (ec ? std::string("<unavailable: ") + ec.message() + ">" : cwd.string())
              << "\n";

    for (size_t i = 0; i < triedPaths.size(); ++i) {
        const auto& tried = triedPaths[i];
        std::error_code absEc;
        const auto absolute = std::filesystem::absolute(tried, absEc);
        std::error_code existsEc;
        const bool exists = std::filesystem::exists(tried, existsEc);

        std::cerr << "  tried path[" << i << "]: " << tried.string() << "\n";
        std::cerr << "    absolute: "
                  << (absEc ? std::string("<unavailable: ") + absEc.message() + ">" : absolute.string())
                  << "\n";
        std::cerr << "    exists: ";
        if (existsEc && !isMissingPathError(existsEc)) {
            std::cerr << "unknown: " << existsEc.message() << "\n";
        } else {
            std::cerr << (exists ? "yes" : "no") << "\n";
        }
        std::cerr << "    kind: " << describePathKind(tried) << "\n";
    }

    std::cerr << "  hint: pass a path relative to the current working directory or an absolute Windows path.\n";
    if (!maybeConvertMsysPath(rawPath).empty()) {
        std::cerr << "  hint: the path also looked like an MSYS-style path, so the compiler tried a Windows-form variant too.\n";
    }
}

std::string readFileBestEffort(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string formatDiagnosticsWithSources(
    const std::vector<claw::frontend::Diagnostic>& diagnostics,
    std::string_view fallbackPath,
    std::string_view fallbackSource) {
    std::unordered_map<std::string, std::string> sourceCache;
    sourceCache[std::string(fallbackPath)] = std::string(fallbackSource);

    std::ostringstream out;
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        const auto& diagnostic = diagnostics[i];
        const std::string effectivePath = diagnostic.path.empty() ? std::string(fallbackPath) : diagnostic.path;
        if (sourceCache.find(effectivePath) == sourceCache.end()) {
            sourceCache[effectivePath] = readFileBestEffort(effectivePath);
        }
        if (i > 0) {
            out << "\n\n";
        }
        out << claw::frontend::formatDiagnostic(diagnostic, fallbackPath, sourceCache[effectivePath]);
    }
    return out.str();
}

std::vector<claw::frontend::Diagnostic> attachPath(
    std::vector<claw::frontend::Diagnostic> diagnostics,
    const std::filesystem::path& path) {
    for (auto& diagnostic : diagnostics) {
        if (diagnostic.path.empty()) {
            diagnostic.path = path.string();
        }
    }
    return diagnostics;
}

void validateEntryPoint(
    const claw::frontend::LoadedProject& project,
    const claw::frontend::SemanticAnalyzer& sema,
    bool requireEntry) {
    if (!requireEntry && !project.manifestPath.has_value()) {
        return;
    }

    const auto& unit = project.units[project.entryIndex];
    const auto* realm = unit.ast.get();
    const claw::frontend::FnDecl* entryFn = nullptr;
    for (const auto& decl : realm->declarations) {
        if (auto* fn = dynamic_cast<const claw::frontend::FnDecl*>(decl.get())) {
            if (fn->name == "main") {
                entryFn = fn;
                break;
            }
        }
    }

    if (!entryFn) {
        claw::frontend::Diagnostic diagnostic;
        diagnostic.stage = "entry";
        diagnostic.message = "Entry module must define `fn main() -> Int32` or `fn main()` .";
        diagnostic.span = realm->span;
        diagnostic.path = unit.path.string();
        throw claw::frontend::DiagnosticError("Entry point validation failed.", {diagnostic});
    }

    const auto* signature = sema.lookupFunctionSignature(entryFn);
    const bool validReturn = signature &&
        ((signature->returnType.name == "Int32" && signature->returnType.viewKind.empty()) ||
         (signature->returnType.name == "Unit" && signature->returnType.viewKind.empty()));
    if (!signature || !signature->paramTypes.empty() || !validReturn) {
        claw::frontend::Diagnostic diagnostic;
        diagnostic.stage = "entry";
        diagnostic.message = "`main` must take no parameters and return Int32 or Unit.";
        diagnostic.span = entryFn->span;
        diagnostic.path = unit.path.string();
        throw claw::frontend::DiagnosticError("Entry point validation failed.", {diagnostic});
    }
}

void printUsage() {
    std::cout << "Usage: claw <command> <file.cat|modules.cat>\n"
              << "Commands:\n"
              << "  check      Parse and validate semantics + ownership\n"
              << "  emit-air   Emit analyzed IR view after semantic analysis\n"
              << "  emit-oir   Emit lowered OIR view closer to backend\n"
              << "  build      Validate an entry graph ready for backend lowering\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    const std::string command = argv[1];
    const std::string filepath = argv[2];

    if (command != "check" && command != "build" && command != "emit-air" && command != "emit-oir") {
        std::cerr << "Unknown command: " << command << "\n";
        printUsage();
        return 1;
    }

    std::ifstream file;
    std::filesystem::path openedPath;
    std::vector<std::filesystem::path> triedPaths;
    if (!openSourceFile(filepath, &file, &openedPath, &triedPaths)) {
        printOpenFailure(filepath, triedPaths);
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();
    const std::string diagnosticPath = openedPath.empty() ? filepath : openedPath.string();

    try {
        claw::frontend::ProjectLoader loader;
        auto project = loader.load(openedPath.empty() ? std::filesystem::path(filepath) : openedPath);

        std::vector<std::unique_ptr<claw::frontend::SemanticAnalyzer>> analyzers;
        analyzers.reserve(project.units.size());

        for (const auto& unit : project.units) {
            auto sema = std::make_unique<claw::frontend::SemanticAnalyzer>(unit.importedBindings);
            try {
                sema->analyze(unit.ast.get());
            } catch (const claw::frontend::DiagnosticError& error) {
                throw claw::frontend::DiagnosticError(error.what(), attachPath(error.diagnostics(), unit.path));
            }
            analyzers.push_back(std::move(sema));
        }

        validateEntryPoint(project, *analyzers[project.entryIndex], command == "build");

        if (command == "emit-air") {
            claw::frontend::AirEmitter air(*analyzers[project.entryIndex]);
            std::cout << air.emit(project.units[project.entryIndex].ast.get());
            return 0;
        }

        for (size_t i = 0; i < project.units.size(); ++i) {
            claw::frontend::OwnershipChecker ownership;
            try {
                ownership.check(project.units[i].ast.get(), *analyzers[i]);
            } catch (const claw::frontend::DiagnosticError& error) {
                throw claw::frontend::DiagnosticError(error.what(), attachPath(error.diagnostics(), project.units[i].path));
            }
        }

        if (command == "emit-oir") {
            claw::frontend::OirEmitter oir(*analyzers[project.entryIndex]);
            std::cout << oir.emit(project.units[project.entryIndex].ast.get());
            return 0;
        }

        if (command == "build") {
            std::cout << "Build graph validated for entry realm: "
                      << project.units[project.entryIndex].ast->name << "\n";
            return 0;
        }

        std::cout << "Check passed for realm: " << project.units[project.entryIndex].ast->name
                  << "\n";
    } catch (const claw::frontend::DiagnosticError& e) {
        std::cerr << formatDiagnosticsWithSources(e.diagnostics(), diagnosticPath, source) << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Compilation failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}