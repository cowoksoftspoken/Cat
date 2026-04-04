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
#include "ast/ast.h"
#include "analysis/sema.h"
#include "codegen/llvm.h"
#include "diagnostics/diagnostics.h"
#include "driver/native_build.h"
#include "ir/air.h"
#include "ir/oir.h"
#include "ir/lir.h"
#include "lexer/lexer.h"
#include "workspace/workspace.h"
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

std::filesystem::path resolveOutputPath(std::string_view rawPath) {
    const std::string msysConverted = maybeConvertMsysPath(rawPath);
    return std::filesystem::path(msysConverted.empty() ? std::string(rawPath) : msysConverted);
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

bool resolveInputPath(
    const std::string& rawPath,
    std::filesystem::path* resolvedPath,
    std::vector<std::filesystem::path>* triedPaths) {
    const auto candidates = buildCandidatePaths(rawPath);
    if (triedPaths) {
        *triedPaths = candidates;
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            if (resolvedPath) {
                *resolvedPath = candidate;
            }
            return true;
        }
    }

    return false;
}

void printOpenFailure(const std::string& rawPath, const std::vector<std::filesystem::path>& triedPaths) {
    std::cerr << "Failed to resolve input path.\n";
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

bool isRootMainEntryUnit(
    const claw::workspace::LoadedProject& project,
    const claw::workspace::LoadedUnit& unit) {
    return project.structuredPackage &&
        unit.path.filename() == "main.cat" &&
        unit.path.parent_path().lexically_normal() == project.packageRoot.lexically_normal();
}

const claw::frontend::FnDecl* findDeclaredMain(const claw::frontend::RealmDecl* realm) {
    if (!realm) {
        return nullptr;
    }

    for (const auto& decl : realm->declarations) {
        if (auto* fn = dynamic_cast<const claw::frontend::FnDecl*>(decl.get())) {
            if (fn->name == "main") {
                return fn;
            }
        }
    }

    return nullptr;
}

size_t countDeclaredMains(const claw::frontend::RealmDecl* realm) {
    if (!realm) {
        return 0;
    }

    size_t count = 0;
    for (const auto& decl : realm->declarations) {
        if (auto* fn = dynamic_cast<const claw::frontend::FnDecl*>(decl.get())) {
            if (fn->name == "main") {
                ++count;
            }
        }
    }

    return count;
}

bool containsMainCallExpr(const claw::frontend::Expr* expr) {
    if (!expr) {
        return false;
    }

    if (auto* call = dynamic_cast<const claw::frontend::CallExpr*>(expr)) {
        if (auto* ident = dynamic_cast<const claw::frontend::IdentExpr*>(call->callee.get())) {
            if (ident->name == "main") {
                return true;
            }
        }

        if (containsMainCallExpr(call->callee.get())) {
            return true;
        }
        for (const auto& arg : call->args) {
            if (containsMainCallExpr(arg.get())) {
                return true;
            }
        }
        return false;
    }

    if (auto* binary = dynamic_cast<const claw::frontend::BinaryExpr*>(expr)) {
        return containsMainCallExpr(binary->left.get()) || containsMainCallExpr(binary->right.get());
    }
    if (auto* member = dynamic_cast<const claw::frontend::MemberExpr*>(expr)) {
        return containsMainCallExpr(member->object.get());
    }
    if (auto* index = dynamic_cast<const claw::frontend::IndexExpr*>(expr)) {
        return containsMainCallExpr(index->object.get()) || containsMainCallExpr(index->index.get());
    }
    if (auto* borrow = dynamic_cast<const claw::frontend::BorrowExpr*>(expr)) {
        return containsMainCallExpr(borrow->target.get());
    }

    return false;
}

const claw::frontend::AstNode* findMainCallNode(const claw::frontend::Stmt* stmt) {
    if (!stmt) {
        return nullptr;
    }

    if (auto* exprStmt = dynamic_cast<const claw::frontend::ExprStmt*>(stmt)) {
        return containsMainCallExpr(exprStmt->expr.get()) ? exprStmt->expr.get() : nullptr;
    }
    if (auto* give = dynamic_cast<const claw::frontend::GiveStmt*>(stmt)) {
        return containsMainCallExpr(give->value.get()) ? give->value.get() : nullptr;
    }
    if (auto* assign = dynamic_cast<const claw::frontend::AssignStmt*>(stmt)) {
        if (containsMainCallExpr(assign->target.get())) {
            return assign->target.get();
        }
        return containsMainCallExpr(assign->value.get()) ? assign->value.get() : nullptr;
    }
    if (auto* bind = dynamic_cast<const claw::frontend::BindingStmt*>(stmt)) {
        return containsMainCallExpr(bind->value.get()) ? bind->value.get() : nullptr;
    }
    if (auto* tryStmt = dynamic_cast<const claw::frontend::TryStmt*>(stmt)) {
        if (containsMainCallExpr(tryStmt->expr.get())) {
            return tryStmt->expr.get();
        }
        if (tryStmt->failBlock) {
            for (const auto& nested : tryStmt->failBlock->statements) {
                if (const auto* found = findMainCallNode(nested.get())) {
                    return found;
                }
            }
        }
        return nullptr;
    }
    if (auto* when = dynamic_cast<const claw::frontend::WhenStmt*>(stmt)) {
        if (containsMainCallExpr(when->condition.get())) {
            return when->condition.get();
        }
        for (const auto& nested : when->thenBlock->statements) {
            if (const auto* found = findMainCallNode(nested.get())) {
                return found;
            }
        }
        if (when->elseBlock) {
            for (const auto& nested : when->elseBlock->statements) {
                if (const auto* found = findMainCallNode(nested.get())) {
                    return found;
                }
            }
        }
        return nullptr;
    }
    if (auto* loop = dynamic_cast<const claw::frontend::LoopStmt*>(stmt)) {
        if (containsMainCallExpr(loop->condition.get())) {
            return loop->condition.get();
        }
        for (const auto& nested : loop->body->statements) {
            if (const auto* found = findMainCallNode(nested.get())) {
                return found;
            }
        }
        return nullptr;
    }
    if (auto* scan = dynamic_cast<const claw::frontend::ScanStmt*>(stmt)) {
        if (containsMainCallExpr(scan->iterable.get())) {
            return scan->iterable.get();
        }
        for (const auto& nested : scan->body->statements) {
            if (const auto* found = findMainCallNode(nested.get())) {
                return found;
            }
        }
        return nullptr;
    }
    if (auto* pick = dynamic_cast<const claw::frontend::PickStmt*>(stmt)) {
        if (containsMainCallExpr(pick->value.get())) {
            return pick->value.get();
        }
        for (const auto& branch : pick->branches) {
            for (const auto& nested : branch.body->statements) {
                if (const auto* found = findMainCallNode(nested.get())) {
                    return found;
                }
            }
        }
        return nullptr;
    }
    if (auto* lift = dynamic_cast<const claw::frontend::LiftStmt*>(stmt)) {
        if (containsMainCallExpr(lift->expr.get())) {
            return lift->expr.get();
        }
        for (const auto& nested : lift->failBlock->statements) {
            if (const auto* found = findMainCallNode(nested.get())) {
                return found;
            }
        }
        return nullptr;
    }
    if (auto* raw = dynamic_cast<const claw::frontend::RawStmt*>(stmt)) {
        for (const auto& nested : raw->body->statements) {
            if (const auto* found = findMainCallNode(nested.get())) {
                return found;
            }
        }
        return nullptr;
    }

    return nullptr;
}

void validateMainDeclarations(const claw::workspace::LoadedProject& project) {
    std::vector<claw::frontend::Diagnostic> diagnostics;

    for (size_t i = 0; i < project.units.size(); ++i) {
        const auto& unit = project.units[i];
        if (!unit.ast) {
            continue;
        }

        const size_t mainCount = countDeclaredMains(unit.ast.get());
        if (mainCount == 0) {
            continue;
        }

        const auto* firstMain = findDeclaredMain(unit.ast.get());
        const bool isEntryUnit = i == project.entryIndex;
        const bool isWorkspaceEntry = isRootMainEntryUnit(project, unit);

        if (!isEntryUnit || (project.structuredPackage && !isWorkspaceEntry)) {
            claw::frontend::Diagnostic diagnostic;
            diagnostic.stage = "entry";
            diagnostic.message = "`fn main` is only allowed in root main.cat.";
            diagnostic.span = firstMain ? firstMain->span : unit.ast->span;
            diagnostic.path = unit.path.string();
            diagnostics.push_back(std::move(diagnostic));
            continue;
        }

        if (mainCount > 1) {
            claw::frontend::Diagnostic diagnostic;
            diagnostic.stage = "entry";
            diagnostic.message = "main.cat must contain exactly one `fn main` declaration.";
            diagnostic.span = firstMain ? firstMain->span : unit.ast->span;
            diagnostic.path = unit.path.string();
            diagnostics.push_back(std::move(diagnostic));
        }
    }

    if (!diagnostics.empty()) {
        throw claw::frontend::DiagnosticError("Entry point validation failed.", std::move(diagnostics));
    }
}

void validateMainCalls(const claw::workspace::LoadedProject& project) {
    std::vector<claw::frontend::Diagnostic> diagnostics;

    for (const auto& unit : project.units) {
        if (!unit.ast) {
            continue;
        }

        for (const auto& decl : unit.ast->declarations) {
            auto* fn = dynamic_cast<const claw::frontend::FnDecl*>(decl.get());
            if (!fn || !fn->body) {
                continue;
            }

            for (const auto& stmt : fn->body->statements) {
                if (const auto* found = findMainCallNode(stmt.get())) {
                    claw::frontend::Diagnostic diagnostic;
                    diagnostic.stage = "entry";
                    diagnostic.message = "`main` is the program entry point and cannot be called like a normal function.";
                    diagnostic.span = found->span;
                    diagnostic.path = unit.path.string();
                    diagnostics.push_back(std::move(diagnostic));
                    break;
                }
            }
        }
    }

    if (!diagnostics.empty()) {
        throw claw::frontend::DiagnosticError("Entry point validation failed.", std::move(diagnostics));
    }
}

std::vector<claw::frontend::Diagnostic> collectProjectWarnings(
    const claw::workspace::LoadedProject& project) {
    std::vector<claw::frontend::Diagnostic> warnings;
    if (project.units.empty()) {
        return warnings;
    }

    const auto& unit = project.units[project.entryIndex];
    if (!isRootMainEntryUnit(project, unit) || !unit.ast) {
        return warnings;
    }

    for (const auto& decl : unit.ast->declarations) {
        if (!decl || !decl->isShared) {
            continue;
        }

        std::string name = "<decl>";
        std::string kind = "declaration";
        if (const auto* fn = dynamic_cast<const claw::frontend::FnDecl*>(decl.get())) {
            name = fn->name;
            kind = "function";
        } else if (const auto* shape = dynamic_cast<const claw::frontend::ShapeDecl*>(decl.get())) {
            name = shape->name;
            kind = "shape";
        } else if (const auto* choice = dynamic_cast<const claw::frontend::ChoiceDecl*>(decl.get())) {
            name = choice->name;
            kind = "choice";
        }

        claw::frontend::Diagnostic diagnostic;
        diagnostic.severity = claw::frontend::DiagnosticSeverity::Warning;
        diagnostic.stage = "module";
        diagnostic.message =
            "Shared " + kind + " '" + name + "' in root main.cat has no effect because the workspace entry is not importable as a module.";
        diagnostic.span = decl->span;
        diagnostic.path = unit.path.string();
        warnings.push_back(std::move(diagnostic));
    }

    return warnings;
}

void neutralizeRootEntrySharedDecls(claw::workspace::LoadedProject& project) {
    if (project.units.empty()) {
        return;
    }

    auto& unit = project.units[project.entryIndex];
    if (!isRootMainEntryUnit(project, unit) || !unit.ast) {
        return;
    }

    for (auto& decl : unit.ast->declarations) {
        if (!decl || !decl->isShared) {
            continue;
        }
        decl->isShared = false;
    }
}

void validateEntryPoint(
    const claw::workspace::LoadedProject& project,
    const claw::frontend::SemanticAnalyzer& sema,
    bool requireEntry) {
    const auto& unit = project.units[project.entryIndex];
    const bool isRootMainEntry = isRootMainEntryUnit(project, unit);

    if (!requireEntry && !isRootMainEntry) {
        return;
    }

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
        diagnostic.message = "Entry module must define `fn main()` with no parameters.";
        diagnostic.span = realm->span;
        diagnostic.path = unit.path.string();
        throw claw::frontend::DiagnosticError("Entry point validation failed.", {diagnostic});
    }

    const auto* signature = sema.lookupFunctionSignature(entryFn);
    const bool validReturn = signature &&
        signature->returnType.viewKind.empty() &&
        ((signature->returnType.name == "Unit") || (signature->returnType.name == "Int32"));
    if (!signature || !signature->paramTypes.empty() || !validReturn) {
        claw::frontend::Diagnostic diagnostic;
        diagnostic.stage = "entry";
        diagnostic.message = "`main` must take no parameters and return Unit or Int32.";
        diagnostic.span = entryFn->span;
        diagnostic.path = unit.path.string();
        throw claw::frontend::DiagnosticError("Entry point validation failed.", {diagnostic});
    }
}
void printUsage() {
    std::cout << "Usage: claw <command> <file.cat|workspace|config> [output.exe]\n"
              << "Commands:\n"
              << "  check         Parse and validate semantics + ownership\n"
              << "  validate      Validate a workspace entry graph rooted at main.cat\n"
              << "  air           Emit analyzed AIR after semantic analysis\n"
              << "  oir           Emit lowered OIR closer to backend\n"
              << "  lir           Emit backend-facing lowering IR derived from OIR\n"
              << "  llvm          Emit LLVM IR lowered from LIR\n"
              << "  build         Compile a validated entry graph into a native executable\n"
              << "Aliases:\n"
              << "  emit-air, emit-oir, emit-lir, emit-llvm, build-native\n";
}

std::string canonicalizeCommand(std::string_view rawCommand) {
    if (rawCommand == "check") {
        return "check";
    }
    if (rawCommand == "validate") {
        return "validate";
    }
    if (rawCommand == "air" || rawCommand == "emit-air") {
        return "air";
    }
    if (rawCommand == "oir" || rawCommand == "emit-oir") {
        return "oir";
    }
    if (rawCommand == "lir" || rawCommand == "emit-lir") {
        return "lir";
    }
    if (rawCommand == "llvm" || rawCommand == "emit-llvm") {
        return "llvm";
    }
    if (rawCommand == "build" || rawCommand == "build-native") {
        return "build";
    }
    return {};
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        printUsage();
        return 1;
    }

    const std::string rawCommand = argv[1];
    const std::string command = canonicalizeCommand(rawCommand);
    const std::string filepath = argv[2];
    const std::string outputArg = argc >= 4 ? argv[3] : std::string{};

    if (command.empty()) {
        std::cerr << "Unknown command: " << rawCommand << "\n";
        printUsage();
        return 1;
    }

    std::filesystem::path openedPath;
    std::vector<std::filesystem::path> triedPaths;
    if (!resolveInputPath(filepath, &openedPath, &triedPaths)) {
        printOpenFailure(filepath, triedPaths);
        return 1;
    }

    const std::string source = std::filesystem::is_regular_file(openedPath)
        ? readFileBestEffort(openedPath.string())
        : std::string{};
    const std::string diagnosticPath = openedPath.empty() ? filepath : openedPath.string();

    try {
        claw::workspace::ProjectLoader loader;
        auto project = loader.load(openedPath.empty() ? std::filesystem::path(filepath) : openedPath);

        const auto warnings = collectProjectWarnings(project);
        if (!warnings.empty()) {
            std::cerr << formatDiagnosticsWithSources(warnings, diagnosticPath, source) << "\n";
        }
        neutralizeRootEntrySharedDecls(project);
        validateMainDeclarations(project);
        validateMainCalls(project);

        std::vector<std::unique_ptr<claw::frontend::SemanticAnalyzer>> analyzers;
        analyzers.reserve(project.units.size());

        for (const auto& unit : project.units) {
            auto sema = std::make_unique<claw::frontend::SemanticAnalyzer>(unit.importedBindings, project.target);
            try {
                sema->analyze(unit.ast.get());
            } catch (const claw::frontend::DiagnosticError& error) {
                throw claw::frontend::DiagnosticError(error.what(), attachPath(error.diagnostics(), unit.path));
            }
            analyzers.push_back(std::move(sema));
        }

        validateEntryPoint(project, *analyzers[project.entryIndex], command == "validate" || command == "build");

        if (command == "air") {
            claw::frontend::AirEmitter air(*analyzers[project.entryIndex]);
            std::cout << air.emit(project.units[project.entryIndex].ast.get());
            return 0;
        }

        std::vector<std::unique_ptr<claw::frontend::OwnershipChecker>> ownershipCheckers;
        ownershipCheckers.reserve(project.units.size());
        for (size_t i = 0; i < project.units.size(); ++i) {
            auto ownership = std::make_unique<claw::frontend::OwnershipChecker>();
            try {
                ownership->check(project.units[i].ast.get(), *analyzers[i]);
            } catch (const claw::frontend::DiagnosticError& error) {
                throw claw::frontend::DiagnosticError(error.what(), attachPath(error.diagnostics(), project.units[i].path));
            }
            ownershipCheckers.push_back(std::move(ownership));
        }

        if (command == "oir" || command == "lir" || command == "llvm" || command == "build") {
            std::vector<claw::frontend::OirUnitView> units;
            units.reserve(project.units.size());
            for (size_t i = 0; i < project.units.size(); ++i) {
                units.push_back(claw::frontend::OirUnitView{project.units[i].ast.get(), analyzers[i].get(), &ownershipCheckers[i]->result()});
            }

            const bool emitWholeProject = project.structuredPackage &&
                project.units[project.entryIndex].path.filename() == "main.cat" &&
                project.units[project.entryIndex].path.parent_path().lexically_normal() == project.packageRoot.lexically_normal();

            if (command == "oir") {
                if (emitWholeProject) {
                    std::cout << claw::frontend::emitOirProgram(project.units[project.entryIndex].ast->name, units);
                } else {
                    claw::frontend::OirEmitter oir(*analyzers[project.entryIndex], &ownershipCheckers[project.entryIndex]->result());
                    std::cout << oir.emit(project.units[project.entryIndex].ast.get());
                }
            } else if (command == "lir") {
                if (emitWholeProject) {
                    std::cout << claw::frontend::emitLirProgram(project.units[project.entryIndex].ast->name, units);
                } else {
                    claw::frontend::OirEmitter oir(*analyzers[project.entryIndex], &ownershipCheckers[project.entryIndex]->result());
                    const std::string entryRealm = project.units[project.entryIndex].ast->name;
                    const claw::frontend::OirProgram program{entryRealm, entryRealm + "::main", {oir.lowerRealm(project.units[project.entryIndex].ast.get())}};
                    std::cout << claw::frontend::emitLirProgram(program);
                }
            } else if (command == "llvm") {
                if (emitWholeProject) {
                    std::cout << claw::codegen::emitLlvmIr(project.units[project.entryIndex].ast->name, units);
                } else {
                    claw::frontend::OirEmitter oir(*analyzers[project.entryIndex], &ownershipCheckers[project.entryIndex]->result());
                    const std::string entryRealm = project.units[project.entryIndex].ast->name;
                    const claw::frontend::OirProgram program{entryRealm, entryRealm + "::main", {oir.lowerRealm(project.units[project.entryIndex].ast.get())}};
                    std::cout << claw::codegen::emitLlvmIr(claw::frontend::buildLirProgram(program));
                }
            } else {
                std::filesystem::path outputPath = outputArg.empty()
                    ? claw::driver::defaultNativeOutputPath(project)
                    : resolveOutputPath(outputArg);
                if (!outputPath.has_extension()) {
                    outputPath += ".exe";
                }
                const auto builtPath = claw::driver::buildNativeExecutable(
                    project,
                    project.units[project.entryIndex].ast->name,
                    units,
                    std::filesystem::path(argv[0]),
                    outputPath);
                std::cout << "Built native executable: " << builtPath.string() << "\n";
            }
            return 0;
        }
        if (command == "validate") {
            std::cout << "Workspace graph validated";
            if (project.config.has_value()) {
                std::cout << " for project " << project.config->name
                          << " (edition " << project.config->edition << ")";
            }
            std::cout << ", entry module: "
                      << project.units[project.entryIndex].ast->name << "\n";
            return 0;
        }

        std::cout << "Check passed for module: " << project.units[project.entryIndex].ast->name
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





