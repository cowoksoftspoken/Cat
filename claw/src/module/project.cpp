#include "module/project.h"

#include "ast/ast.h"
#include "diagnostics/diagnostics.h"
#include "lexer/lexer.h"
#include "parser/parser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace claw::frontend {

namespace {

std::string trim(std::string_view text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

bool isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifierContinue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifier(std::string_view text) {
    if (text.empty() || !isIdentifierStart(text.front())) {
        return false;
    }
    for (size_t i = 1; i < text.size(); ++i) {
        if (!isIdentifierContinue(text[i])) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> splitCommaList(std::string_view text) {
    std::vector<std::string> items;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const size_t end = comma == std::string_view::npos ? text.size() : comma;
        items.push_back(trim(text.substr(start, end - start)));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return items;
}

std::string removeLineComment(std::string_view line) {
    const size_t comment = line.find("//");
    return trim(comment == std::string_view::npos ? line : line.substr(0, comment));
}

} // namespace

std::filesystem::path ProjectLoader::normalizePath(const std::filesystem::path& path) const {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

std::filesystem::path ProjectLoader::findPackageRoot(const std::filesystem::path& startDir) const {
    std::filesystem::path best;
    auto current = normalizePath(startDir);

    while (!current.empty()) {
        if (std::filesystem::exists(current / "modules.cat")) {
            best = current;
        }
        if (current == current.root_path()) {
            break;
        }
        current = current.parent_path();
    }

    return best.empty() ? normalizePath(startDir) : best;
}

std::string ProjectLoader::readFileText(const std::filesystem::path& path) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throwDiagnostic(path, "module", "Failed to open source file.");
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::unique_ptr<RealmDecl> ProjectLoader::parseSourceFile(
    const std::filesystem::path& path,
    const std::string& source) const {
    try {
        Lexer lexer(source);
        Parser parser(lexer.tokenize());
        return parser.parseFile();
    } catch (const DiagnosticError& error) {
        std::vector<Diagnostic> diagnostics = error.diagnostics();
        for (auto& diagnostic : diagnostics) {
            diagnostic.path = path.string();
        }
        throw DiagnosticError(error.what(), std::move(diagnostics));
    }
}

void ProjectLoader::throwDiagnostic(
    const std::filesystem::path& path,
    const std::string& stage,
    const std::string& message,
    SourceSpan span) const {
    Diagnostic diagnostic;
    diagnostic.stage = stage;
    diagnostic.message = message;
    diagnostic.span = span;
    diagnostic.path = path.string();
    throw DiagnosticError(message, std::vector<Diagnostic>{diagnostic});
}

[[noreturn]] void ProjectLoader::throwDiagnostics(std::vector<Diagnostic> diagnostics) const {
    throw DiagnosticError(
        "Project loading failed with " + std::to_string(diagnostics.size()) + " errors.",
        std::move(diagnostics));
}

ProjectLoader::ModuleManifest ProjectLoader::loadManifest(const std::filesystem::path& path) {
    const auto normalized = normalizePath(path);
    const std::string key = normalized.string();
    const auto cached = manifestCache.find(key);
    if (cached != manifestCache.end()) {
        return cached->second;
    }

    ModuleManifest manifest;
    manifest.path = normalized;
    manifest.source = readFileText(normalized);

    std::vector<Diagnostic> diagnostics;
    std::istringstream lines(manifest.source);
    std::string rawLine;
    size_t lineNumber = 0;
    while (std::getline(lines, rawLine)) {
        ++lineNumber;
        const std::string line = removeLineComment(rawLine);
        if (line.empty()) {
            continue;
        }

        if (line.rfind("pub modules", 0) == 0) {
            const size_t braceOpen = line.find('{');
            const size_t braceClose = line.rfind('}');
            if (braceOpen == std::string::npos || braceClose == std::string::npos || braceClose < braceOpen) {
                diagnostics.push_back(Diagnostic{"manifest", "Expected 'pub modules {a, b}'.", {lineNumber, 1, line.size()}, normalized.string()});
                continue;
            }

            for (const auto& item : splitCommaList(std::string_view(line).substr(braceOpen + 1, braceClose - braceOpen - 1))) {
                if (!isIdentifier(item)) {
                    diagnostics.push_back(Diagnostic{"manifest", "Invalid module name: '" + item + "'.", {lineNumber, braceOpen + 2, std::max<size_t>(1, item.size())}, normalized.string()});
                    continue;
                }
                manifest.publishedModules.insert(item);
            }
            continue;
        }

        if (line.rfind("pub entry", 0) == 0) {
            const std::string value = trim(std::string_view(line).substr(std::string_view("pub entry").size()));
            if (value.empty()) {
                diagnostics.push_back(Diagnostic{"manifest", "Expected entry realm after 'pub entry'.", {lineNumber, 1, line.size()}, normalized.string()});
                continue;
            }

            bool valid = true;
            for (const auto& segment : splitRealmPath(value)) {
                if (!isIdentifier(segment)) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                diagnostics.push_back(Diagnostic{"manifest", "Invalid entry realm: '" + value + "'.", {lineNumber, 1, line.size()}, normalized.string()});
                continue;
            }
            if (manifest.entryRealm.has_value()) {
                diagnostics.push_back(Diagnostic{"manifest", "Duplicate 'pub entry' declaration.", {lineNumber, 1, line.size()}, normalized.string()});
                continue;
            }
            manifest.entryRealm = value;
            continue;
        }

        diagnostics.push_back(Diagnostic{"manifest", "Unknown manifest declaration.", {lineNumber, 1, line.size()}, normalized.string()});
    }

    if (!diagnostics.empty()) {
        throwDiagnostics(std::move(diagnostics));
    }

    manifestCache[key] = manifest;
    return manifest;
}

std::optional<ProjectLoader::ModuleManifest> ProjectLoader::tryLoadManifest(const std::filesystem::path& path) {
    const auto normalized = normalizePath(path);
    if (!std::filesystem::exists(normalized)) {
        return std::nullopt;
    }
    return loadManifest(normalized);
}

std::vector<std::string> ProjectLoader::splitRealmPath(const std::string& realmPath) const {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= realmPath.size()) {
        const size_t dot = realmPath.find('.', start);
        const size_t end = dot == std::string::npos ? realmPath.size() : dot;
        parts.push_back(realmPath.substr(start, end - start));
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    return parts;
}

bool ProjectLoader::isExternalRoot(const std::vector<std::string>& segments) const {
    if (segments.empty()) {
        return false;
    }
    return segments.front() == "core" || segments.front() == "std" || segments.front() == "sys";
}

std::optional<std::filesystem::path> ProjectLoader::tryResolveNamespaceDir(const std::vector<std::string>& segments) {
    auto currentDir = project.packageRoot;
    for (const auto& segment : segments) {
        const auto manifestOpt = tryLoadManifest(currentDir / "modules.cat");
        if (!manifestOpt.has_value() || manifestOpt->publishedModules.find(segment) == manifestOpt->publishedModules.end()) {
            return std::nullopt;
        }
        currentDir /= segment;
    }
    return normalizePath(currentDir);
}

std::optional<std::filesystem::path> ProjectLoader::tryResolveRealmFile(const std::vector<std::string>& segments) {
    if (segments.empty()) {
        return std::nullopt;
    }

    std::vector<std::string> parentSegments;
    if (segments.size() > 1) {
        parentSegments.assign(segments.begin(), segments.end() - 1);
    }

    auto parentDir = parentSegments.empty()
        ? std::optional<std::filesystem::path>(project.packageRoot)
        : tryResolveNamespaceDir(parentSegments);
    if (!parentDir.has_value()) {
        return std::nullopt;
    }

    const auto manifestOpt = tryLoadManifest(parentDir.value() / "modules.cat");
    if (manifestOpt.has_value() && manifestOpt->publishedModules.find(segments.back()) == manifestOpt->publishedModules.end()) {
        return std::nullopt;
    }

    const auto candidate = normalizePath(parentDir.value() / (segments.back() + ".cat"));
    return std::filesystem::exists(candidate) ? std::optional<std::filesystem::path>(candidate) : std::nullopt;
}

std::filesystem::path ProjectLoader::resolveSiblingModule(
    const std::filesystem::path& importerPath,
    const std::string& name) {
    const auto candidate = normalizePath(importerPath.parent_path() / (name + ".cat"));
    if (!std::filesystem::exists(candidate)) {
        throwDiagnostic(
            importerPath,
            "module",
            "Unable to resolve sibling module '" + name + "' via super import.");
    }
    return candidate;
}

void ProjectLoader::validateRealmPath(const LoadedUnit& unit) const {
    if (!project.structuredPackage || !unit.ast) {
        return;
    }

    const auto relative = unit.path.lexically_relative(project.packageRoot);
    std::filesystem::path expectedPath = relative;
    expectedPath.replace_extension();

    std::string expectedRealm;
    for (const auto& part : expectedPath) {
        const std::string segment = part.string();
        if (segment == ".") {
            continue;
        }
        if (!expectedRealm.empty()) {
            expectedRealm += ".";
        }
        expectedRealm += segment;
    }

    if (unit.ast->name.empty()) {
        throwDiagnostic(unit.path, "module", "Missing realm declaration. Expected realm '" + expectedRealm + "'.", unit.ast->span);
    }
    if (unit.ast->name != expectedRealm) {
        throwDiagnostic(
            unit.path,
            "module",
            "Realm path mismatch: expected '" + expectedRealm + "', got '" + unit.ast->name + "'.",
            unit.ast->span);
    }
}

ProjectLoader::ExportSummary ProjectLoader::buildExportSummary(const LoadedUnit& unit) {
    const std::string key = unit.path.string();
    const auto cached = exportCache.find(key);
    if (cached != exportCache.end()) {
        return cached->second;
    }

    ExportSummary summary;
    for (const auto& decl : unit.ast->declarations) {
        if (!decl->isShared) {
            continue;
        }

        if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            summary.sharedItems[fn->name] = SymbolKind::Function;
        } else if (auto* shape = dynamic_cast<const ShapeDecl*>(decl.get())) {
            summary.sharedItems[shape->name] = SymbolKind::Shape;
        } else if (auto* choice = dynamic_cast<const ChoiceDecl*>(decl.get())) {
            summary.sharedItems[choice->name] = SymbolKind::Choice;
        }
    }

    exportCache[key] = summary;
    return summary;
}

size_t ProjectLoader::loadUnitRecursive(const std::filesystem::path& sourcePath) {
    const auto normalized = normalizePath(sourcePath);
    const std::string key = normalized.string();
    const auto existing = unitIndexByPath.find(key);
    if (existing != unitIndexByPath.end()) {
        return existing->second;
    }
    if (loadStack.find(key) != loadStack.end()) {
        throwDiagnostic(normalized, "module", "Cyclic realm dependency detected.");
    }

    loadStack.insert(key);

    LoadedUnit unit;
    unit.path = normalized;
    unit.source = readFileText(normalized);
    unit.ast = parseSourceFile(normalized, unit.source);
    validateRealmPath(unit);

    project.units.push_back(std::move(unit));
    const size_t index = project.units.size() - 1;
    unitIndexByPath[key] = index;

    project.units[index].importedBindings = resolveImports(project.units[index]);

    loadStack.erase(key);
    return index;
}

std::vector<ImportedBinding> ProjectLoader::resolveImports(const LoadedUnit& unit) {
    std::vector<ImportedBinding> bindings;
    const auto importerPath = unit.path;
    const auto imports = unit.ast ? unit.ast->imports : std::vector<ImportDecl>{};

    for (const auto& imp : imports) {
        if (imp.isSuper) {
            for (const auto& item : imp.specificItems) {
                loadUnitRecursive(resolveSiblingModule(importerPath, item));
                bindings.push_back(ImportedBinding{item, SymbolKind::Module});
            }
            continue;
        }

        const auto baseSegments = splitRealmPath(imp.modulePath);
        if (imp.specificItems.empty()) {
            const auto resolved = tryResolveRealmFile(baseSegments);
            if (resolved.has_value()) {
                loadUnitRecursive(resolved.value());
                bindings.push_back(ImportedBinding{baseSegments.back(), SymbolKind::Module});
                continue;
            }
            if (isExternalRoot(baseSegments)) {
                bindings.push_back(ImportedBinding{baseSegments.back(), SymbolKind::Module});
                continue;
            }
            throwDiagnostic(importerPath, "module", "Unable to resolve imported module '" + imp.modulePath + "'.", imp.span);
        }

        const auto baseModule = tryResolveRealmFile(baseSegments);
        if (baseModule.has_value()) {
            const size_t baseIndex = loadUnitRecursive(baseModule.value());
            const auto summary = buildExportSummary(project.units[baseIndex]);
            for (const auto& item : imp.specificItems) {
                const auto it = summary.sharedItems.find(item);
                if (it == summary.sharedItems.end()) {
                    throwDiagnostic(
                        importerPath,
                        "module",
                        "Module '" + imp.modulePath + "' does not share item '" + item + "'.",
                        imp.span);
                }
                bindings.push_back(ImportedBinding{item, it->second});
            }
            continue;
        }

        std::vector<std::string> namespaceSegments = baseSegments;
        if (const auto namespaceDir = tryResolveNamespaceDir(namespaceSegments)) {
            const auto namespaceManifest = tryLoadManifest(namespaceDir.value() / "modules.cat");
            for (const auto& item : imp.specificItems) {
                if (!namespaceManifest.has_value() || namespaceManifest->publishedModules.find(item) == namespaceManifest->publishedModules.end()) {
                    throwDiagnostic(
                        importerPath,
                        "module",
                        "Namespace '" + imp.modulePath + "' does not publish module '" + item + "'.",
                        imp.span);
                }
                const auto childFile = normalizePath(namespaceDir.value() / (item + ".cat"));
                if (!std::filesystem::exists(childFile)) {
                    throwDiagnostic(importerPath, "module", "Missing module file for '" + imp.modulePath + "." + item + "'.", imp.span);
                }
                loadUnitRecursive(childFile);
                bindings.push_back(ImportedBinding{item, SymbolKind::Module});
            }
            continue;
        }

        if (isExternalRoot(baseSegments)) {
            for (const auto& item : imp.specificItems) {
                bindings.push_back(ImportedBinding{item, std::isupper(static_cast<unsigned char>(item.front())) != 0 ? SymbolKind::Shape : SymbolKind::Module});
            }
            continue;
        }

        throwDiagnostic(importerPath, "module", "Unable to resolve import group rooted at '" + imp.modulePath + "'.", imp.span);
    }

    return bindings;
}

std::filesystem::path ProjectLoader::resolveEntryPath(const ModuleManifest& manifest) {
    if (!manifest.entryRealm.has_value()) {
        throwDiagnostic(manifest.path, "manifest", "Missing 'pub entry' declaration.");
    }

    const auto segments = splitRealmPath(*manifest.entryRealm);
    const auto resolved = tryResolveRealmFile(segments);
    if (!resolved.has_value()) {
        throwDiagnostic(manifest.path, "manifest", "Unable to resolve entry realm '" + *manifest.entryRealm + "'.");
    }
    return resolved.value();
}

LoadedProject ProjectLoader::load(const std::filesystem::path& inputPath) {
    project = LoadedProject{};
    unitIndexByPath.clear();
    manifestCache.clear();
    exportCache.clear();
    loadStack.clear();

    project.inputPath = normalizePath(inputPath);

    if (project.inputPath.filename() == "modules.cat") {
        const auto manifest = loadManifest(project.inputPath);
        project.packageRoot = project.inputPath.parent_path();
        project.manifestPath = manifest.path;
        project.entryRealm = manifest.entryRealm;
        project.structuredPackage = true;
        project.entryIndex = loadUnitRecursive(resolveEntryPath(manifest));
        return std::move(project);
    }

    project.packageRoot = findPackageRoot(project.inputPath.parent_path());
    project.structuredPackage = std::filesystem::exists(project.packageRoot / "modules.cat");
    project.entryIndex = loadUnitRecursive(project.inputPath);
    return std::move(project);
}

} // namespace claw::frontend


