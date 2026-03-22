#include "module/project.h"

#include "ast/ast.h"
#include "diagnostics/diagnostics.h"
#include "lexer/lexer.h"
#include "parser/parser.h"

#include <algorithm>
#include <array>
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

ResolvedType makePlainResolvedType(const std::string& name) {
    ResolvedType type;
    type.name = name;
    type.category = TypeCategory::Plain;
    return type;
}

TypeCatalog buildExportTypeCatalog(const LoadedUnit& unit) {
    TypeCatalog catalog;

    for (const auto& binding : unit.importedBindings) {
        if (binding.kind == SymbolKind::Shape) {
            catalog.registerShapeName(
                binding.name,
                binding.shapeInfo.has_value()
                    ? std::optional<size_t>(binding.shapeInfo->typeParams.size())
                    : std::nullopt);
        } else if (binding.kind == SymbolKind::Choice) {
            catalog.registerChoiceName(
                binding.name,
                binding.choiceInfo.has_value()
                    ? std::optional<size_t>(binding.choiceInfo->typeParams.size())
                    : std::nullopt);
        }
    }

    if (!unit.ast) {
        return catalog;
    }

    for (const auto& decl : unit.ast->declarations) {
        if (auto* shape = dynamic_cast<const ShapeDecl*>(decl.get())) {
            catalog.registerShapeName(shape->name, shape->typeParams.size());
        } else if (auto* choice = dynamic_cast<const ChoiceDecl*>(decl.get())) {
            catalog.registerChoiceName(choice->name, choice->typeParams.size());
        }
    }

    return catalog;
}

void attachDiagnosticPath(std::vector<Diagnostic>* diagnostics, const std::filesystem::path& path) {
    if (!diagnostics) {
        return;
    }

    for (auto& diagnostic : *diagnostics) {
        if (diagnostic.path.empty()) {
            diagnostic.path = path.string();
        }
    }
}

bool containsRawAddressType(const ResolvedType& type) {
    if (type.name == "Addr" || type.name == "RawPtr" || type.name == "RawMut") {
        return true;
    }
    for (const auto& param : type.params) {
        if (containsRawAddressType(param)) {
            return true;
        }
    }
    return false;
}

bool requiresFfiStableBoundary(const DependencySpec& dependency, const DependencyFunctionContract& contract) {
    return !contract.rawOnly && !dependency.abi.empty() && dependency.abi != "claw";
}

std::optional<TypeLayoutInfo> computeExternalBoundaryLayout(const ResolvedType& type) {
    static const AnalysisResult emptyAnalysis;
    static const TargetSpec target = defaultTargetSpec();
    return computeTypeLayout(type, emptyAnalysis, target);
}

bool isFfiStableBoundaryType(const ResolvedType& type) {
    const auto layout = computeExternalBoundaryLayout(type);
    return layout.has_value() && layout->ffiStable;
}

void pushContractDiagnostic(
    std::vector<Diagnostic>* diagnostics,
    const std::filesystem::path& path,
    const std::string& dependencyRoot,
    const std::string& itemName,
    const std::string& message) {
    diagnostics->push_back(Diagnostic{
        "config",
        "Dependency contract '" + dependencyRoot + "." + itemName + "': " + message,
        {},
        path.string()});
}


bool canSourceViewReturnFromParam(const ResolvedType& returnType, const ResolvedType& paramType) {
    if (!returnType.isView() || !paramType.isView()) {
        return false;
    }
    if (returnType.name != paramType.name || returnType.params.size() != paramType.params.size()) {
        return false;
    }
    for (size_t i = 0; i < returnType.params.size(); ++i) {
        if (!sameType(returnType.params[i], paramType.params[i])) {
            return false;
        }
    }
    if (returnType.viewKind == paramType.viewKind) {
        return true;
    }
    return returnType.viewKind == "look" && paramType.viewKind == "edit";
}

std::optional<FunctionSignature> parseDependencyFunctionSignature(
    const std::filesystem::path& configPath,
    const std::string& dependencyRoot,
    const std::string& itemName,
    const DependencySpec& dependency,
    const DependencyFunctionContract& contract,
    std::vector<Diagnostic>* diagnostics) {
    try {
        Lexer lexer(contract.declaration);
        Parser parser(lexer.tokenize());
        auto realm = parser.parseFile();
        if (!realm || realm->declarations.size() != 1) {
            pushContractDiagnostic(diagnostics, configPath, dependencyRoot, itemName, "must contain exactly one function declaration.");
            return std::nullopt;
        }

        auto* fn = dynamic_cast<FnDecl*>(realm->declarations.front().get());
        if (!fn) {
            pushContractDiagnostic(diagnostics, configPath, dependencyRoot, itemName, "must declare a function.");
            return std::nullopt;
        }
        if (fn->name != itemName) {
            pushContractDiagnostic(diagnostics, configPath, dependencyRoot, itemName, "function name must match the imported item name.");
            return std::nullopt;
        }

        TypeCatalog catalog;
        std::vector<Diagnostic> typeDiagnostics;
        FunctionSignature signature;
        signature.isExternal = true;
        signature.externalInfo = ExternalCallableInfo{dependencyRoot, dependency.abi.empty() ? std::string("claw") : dependency.abi, fn->name, contract.rawOnly};

        for (const auto& param : fn->params) {
            signature.paramTypes.push_back(catalog.resolveType(param.type.get(), {}, &typeDiagnostics));
        }
        signature.returnType = fn->returnType
            ? catalog.resolveType(fn->returnType.get(), {}, &typeDiagnostics)
            : makePlainResolvedType("Unit");

        if (!contract.rawOnly) {
            for (const auto& paramType : signature.paramTypes) {
                if (containsRawAddressType(paramType)) {
                    pushContractDiagnostic(diagnostics, configPath, dependencyRoot, itemName, "safe contracts may not expose raw address types in parameters.");
                    return std::nullopt;
                }
            }
            if (containsRawAddressType(signature.returnType)) {
                pushContractDiagnostic(diagnostics, configPath, dependencyRoot, itemName, "safe contracts may not expose raw address types in returns.");
                return std::nullopt;
            }
            if (signature.returnType.viewKind == "edit") {
                pushContractDiagnostic(diagnostics, configPath, dependencyRoot, itemName, "safe contracts may not return edit views.");
                return std::nullopt;
            }
            if (requiresFfiStableBoundary(dependency, contract)) {
                for (const auto& paramType : signature.paramTypes) {
                    if (!isFfiStableBoundaryType(paramType)) {
                        pushContractDiagnostic(
                            diagnostics,
                            configPath,
                            dependencyRoot,
                            itemName,
                            "safe contracts with ABI '" + dependency.abi + "' require FFI-stable parameter types, got " +
                                paramType.describe() + ".");
                        return std::nullopt;
                    }
                }
                if (!isFfiStableBoundaryType(signature.returnType)) {
                    pushContractDiagnostic(
                        diagnostics,
                        configPath,
                        dependencyRoot,
                        itemName,
                        "safe contracts with ABI '" + dependency.abi + "' require an FFI-stable return type, got " +
                            signature.returnType.describe() + ".");
                    return std::nullopt;
                }
            }
        }

        if (signature.returnType.isView()) {
            std::optional<size_t> sourceParam;
            for (size_t i = 0; i < signature.paramTypes.size(); ++i) {
                if (!canSourceViewReturnFromParam(signature.returnType, signature.paramTypes[i])) {
                    continue;
                }
                if (sourceParam.has_value()) {
                    pushContractDiagnostic(diagnostics, configPath, dependencyRoot, itemName, "view-returning contracts must match exactly one view parameter source.");
                    return std::nullopt;
                }
                sourceParam = i;
            }
            if (!sourceParam.has_value()) {
                pushContractDiagnostic(diagnostics, configPath, dependencyRoot, itemName, "view-returning contracts must match exactly one view parameter source.");
                return std::nullopt;
            }
            signature.viewReturnSourceParam = sourceParam;
        }

        if (!typeDiagnostics.empty()) {
            for (const auto& diagnostic : typeDiagnostics) {
                pushContractDiagnostic(diagnostics, configPath, dependencyRoot, itemName, diagnostic.message);
            }
            return std::nullopt;
        }

        return signature;
    } catch (const DiagnosticError& error) {
        for (const auto& diagnostic : error.diagnostics()) {
            pushContractDiagnostic(diagnostics, configPath, dependencyRoot, itemName, diagnostic.message);
        }
        return std::nullopt;
    }
}

} // namespace

std::filesystem::path ProjectLoader::normalizePath(const std::filesystem::path& path) const {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

std::optional<std::filesystem::path> ProjectLoader::findWorkspaceRoot(const std::filesystem::path& startDir) const {
    auto current = normalizePath(startDir);

    while (!current.empty()) {
        if (std::filesystem::exists(current / "main.cat")) {
            return current;
        }
        if (current == current.root_path()) {
            break;
        }
        current = current.parent_path();
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> ProjectLoader::detectWorkspaceConfig(const std::filesystem::path& rootDir) const {
    static const std::array<const char*, 6> preferredNames = {
        "claw.toml",
        "claw.yaml",
        "claw.yml",
        "project.toml",
        "project.yaml",
        "project.yml",
    };

    for (const auto* name : preferredNames) {
        const auto candidate = normalizePath(rootDir / name);
        if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

bool ProjectLoader::isSupportedConfigFile(const std::filesystem::path& path) const {
    const auto extension = path.extension().string();
    return extension == ".toml" || extension == ".yaml" || extension == ".yml";
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
            diagnostics.push_back(Diagnostic{
                "manifest",
                "Entry is defined by root main.cat. modules.cat only supports 'pub modules { ... }'.",
                {lineNumber, 1, line.size()},
                normalized.string()});
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

    const std::string& root = segments.front();
    if (root == "core" || root == "std" || root == "sys") {
        return true;
    }

    return project.config.has_value() && project.config->dependencies.find(root) != project.config->dependencies.end();
}

bool ProjectLoader::isWorkspaceEntrySource(const std::filesystem::path& path) const {
    if (!project.structuredPackage) {
        return false;
    }
    return normalizePath(path) == normalizePath(project.packageRoot / "main.cat");
}

bool ProjectLoader::refersToWorkspaceEntryRealm(const std::vector<std::string>& segments) const {
    return project.structuredPackage && segments.size() == 1 && segments.front() == "main";
}

std::optional<std::filesystem::path> ProjectLoader::tryResolveNamespaceDir(const std::vector<std::string>& segments) {
    auto currentDir = project.packageRoot;
    for (const auto& segment : segments) {
        const auto candidateDir = normalizePath(currentDir / segment);
        if (!std::filesystem::exists(candidateDir) || !std::filesystem::is_directory(candidateDir)) {
            return std::nullopt;
        }

        if (currentDir != project.packageRoot) {
            const auto manifestOpt = tryLoadManifest(currentDir / "modules.cat");
            if (!manifestOpt.has_value() || manifestOpt->publishedModules.find(segment) == manifestOpt->publishedModules.end()) {
                return std::nullopt;
            }
        }

        currentDir = candidateDir;
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

    if (parentDir.value() != project.packageRoot) {
        const auto manifestOpt = tryLoadManifest(parentDir.value() / "modules.cat");
        if (!manifestOpt.has_value() || manifestOpt->publishedModules.find(segments.back()) == manifestOpt->publishedModules.end()) {
            return std::nullopt;
        }
    }

    const auto candidate = normalizePath(parentDir.value() / (segments.back() + ".cat"));
    if (!std::filesystem::exists(candidate) || !std::filesystem::is_regular_file(candidate)) {
        return std::nullopt;
    }
    if (isWorkspaceEntrySource(candidate)) {
        return std::nullopt;
    }
    return candidate;
}
std::filesystem::path ProjectLoader::resolveSiblingModule(
    const std::filesystem::path& importerPath,
    const std::string& name,
    SourceSpan span) {
    const auto candidate = normalizePath(importerPath.parent_path() / (name + ".cat"));
    if (!std::filesystem::exists(candidate)) {
        throwDiagnostic(
            importerPath,
            "module",
            "Unable to resolve sibling module '" + name + "' via super import.",
            span);
    }
    if (isWorkspaceEntrySource(candidate)) {
        throwDiagnostic(
            importerPath,
            "module",
            "Workspace entry 'main.cat' cannot be imported or treated as a module surface.",
            span);
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
    if (!unit.ast || isWorkspaceEntrySource(unit.path)) {
        exportCache[key] = summary;
        return summary;
    }

    TypeCatalog catalog = buildExportTypeCatalog(unit);
    std::vector<Diagnostic> diagnostics;

    for (const auto& decl : unit.ast->declarations) {
        if (!decl->isShared) {
            continue;
        }

        if (auto* fn = dynamic_cast<const FnDecl*>(decl.get())) {
            ImportedBinding binding;
            binding.name = fn->name;
            binding.kind = SymbolKind::Function;

            FunctionSignature signature;
            signature.isExternal = true;
            for (const auto& param : fn->params) {
                signature.paramTypes.push_back(catalog.resolveType(param.type.get(), {}, &diagnostics));
            }
            signature.returnType = fn->returnType
                ? catalog.resolveType(fn->returnType.get(), {}, &diagnostics)
                : makePlainResolvedType("Unit");
            binding.functionSignature = std::move(signature);
            summary.sharedItems[fn->name] = std::move(binding);
            continue;
        }

        if (auto* shape = dynamic_cast<const ShapeDecl*>(decl.get())) {
            ImportedBinding binding;
            binding.name = shape->name;
            binding.kind = SymbolKind::Shape;

            ShapeInfo info;
            info.typeParams = shape->typeParams;
            std::unordered_set<std::string> localTypeParams(shape->typeParams.begin(), shape->typeParams.end());
            for (const auto& field : shape->fields) {
                info.fields[field.name] = catalog.resolveType(field.type.get(), localTypeParams, &diagnostics);
                info.fieldOrder.push_back(field.name);
            }
            binding.shapeInfo = std::move(info);
            summary.sharedItems[shape->name] = std::move(binding);
            continue;
        }

        if (auto* choice = dynamic_cast<const ChoiceDecl*>(decl.get())) {
            ImportedBinding binding;
            binding.name = choice->name;
            binding.kind = SymbolKind::Choice;

            ChoiceInfo info;
            info.typeParams = choice->typeParams;
            std::unordered_set<std::string> localTypeParams(choice->typeParams.begin(), choice->typeParams.end());
            for (const auto& variant : choice->variants) {
                ChoiceVariantInfo variantInfo;
                for (const auto& payload : variant.payloads) {
                    variantInfo.payloadTypes.push_back(catalog.resolveType(payload.type.get(), localTypeParams, &diagnostics));
                }
                info.variants[variant.tag] = std::move(variantInfo);
                info.variantOrder.push_back(variant.tag);
            }
            binding.choiceInfo = std::move(info);
            summary.sharedItems[choice->name] = std::move(binding);
        }
    }

    attachDiagnosticPath(&diagnostics, unit.path);
    if (!diagnostics.empty()) {
        throwDiagnostics(std::move(diagnostics));
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

    auto makeModuleBinding = [&](const std::string& name, size_t unitIndex) {
        ImportedBinding binding;
        binding.name = name;
        binding.kind = SymbolKind::Module;

        auto moduleInfo = std::make_shared<ModuleInfo>();
        moduleInfo->realmName = project.units[unitIndex].ast ? project.units[unitIndex].ast->name : std::string{};
        moduleInfo->exportedItems = buildExportSummary(project.units[unitIndex]).sharedItems;
        binding.moduleInfo = std::move(moduleInfo);
        return binding;
    };

    auto resolveDependencyFunctionBinding = [&](const std::string& dependencyRoot, const std::string& itemName) {
        ImportedBinding binding;
        binding.name = itemName;
        binding.kind = SymbolKind::Function;

        if (!project.config.has_value()) {
            return binding;
        }

        const auto specIt = project.config->dependencySpecs.find(dependencyRoot);
        if (specIt == project.config->dependencySpecs.end()) {
            return binding;
        }

        const auto contractIt = specIt->second.functions.find(itemName);
        if (contractIt == specIt->second.functions.end()) {
            return binding;
        }

        std::vector<Diagnostic> diagnostics;
        const auto signature = parseDependencyFunctionSignature(
            project.config->path,
            dependencyRoot,
            itemName,
            specIt->second,
            contractIt->second,
            &diagnostics);
        if (!diagnostics.empty()) {
            throwDiagnostics(std::move(diagnostics));
        }
        if (signature.has_value()) {
            binding.functionSignature = *signature;
        }
        return binding;
    };

    auto makeExternalModuleBinding = [&](const std::string& dependencyRoot) {
        ImportedBinding binding;
        binding.name = dependencyRoot;
        binding.kind = SymbolKind::Module;

        if (!project.config.has_value()) {
            return binding;
        }

        const auto specIt = project.config->dependencySpecs.find(dependencyRoot);
        if (specIt == project.config->dependencySpecs.end() || specIt->second.functions.empty()) {
            return binding;
        }

        auto moduleInfo = std::make_shared<ModuleInfo>();
        moduleInfo->realmName = dependencyRoot;
        std::vector<Diagnostic> diagnostics;
        for (const auto& [functionName, contract] : specIt->second.functions) {
            ImportedBinding exported;
            exported.name = functionName;
            exported.kind = SymbolKind::Function;
            const auto signature = parseDependencyFunctionSignature(
                project.config->path,
                dependencyRoot,
                functionName,
                specIt->second,
                contract,
                &diagnostics);
            if (signature.has_value()) {
                exported.functionSignature = *signature;
            }
            moduleInfo->exportedItems[functionName] = std::move(exported);
        }
        if (!diagnostics.empty()) {
            throwDiagnostics(std::move(diagnostics));
        }

        binding.moduleInfo = std::move(moduleInfo);
        return binding;
    };

    for (const auto& imp : imports) {
        if (imp.isSuper) {
            for (const auto& item : imp.specificItems) {
                const size_t moduleIndex = loadUnitRecursive(resolveSiblingModule(importerPath, item, imp.span));
                bindings.push_back(makeModuleBinding(item, moduleIndex));
            }
            continue;
        }

        const auto baseSegments = splitRealmPath(imp.modulePath);
        if (refersToWorkspaceEntryRealm(baseSegments)) {
            throwDiagnostic(
                importerPath,
                "module",
                "Workspace entry 'main.cat' cannot be imported or treated as a module surface.",
                imp.span);
        }
        if (imp.specificItems.empty()) {
            const auto resolved = tryResolveRealmFile(baseSegments);
            if (resolved.has_value()) {
                const size_t moduleIndex = loadUnitRecursive(resolved.value());
                bindings.push_back(makeModuleBinding(baseSegments.back(), moduleIndex));
                continue;
            }
            if (isExternalRoot(baseSegments)) {
                bindings.push_back(makeExternalModuleBinding(baseSegments.front()));
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
                bindings.push_back(it->second);
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
                const size_t moduleIndex = loadUnitRecursive(childFile);
                bindings.push_back(makeModuleBinding(item, moduleIndex));
            }
            continue;
        }

        if (isExternalRoot(baseSegments)) {
            for (const auto& item : imp.specificItems) {
                if (std::isupper(static_cast<unsigned char>(item.front())) != 0) {
                    bindings.push_back(ImportedBinding{item, SymbolKind::Shape});
                } else {
                    bindings.push_back(resolveDependencyFunctionBinding(baseSegments.front(), item));
                }
            }
            continue;
        }

        throwDiagnostic(importerPath, "module", "Unable to resolve import group rooted at '" + imp.modulePath + "'.", imp.span);
    }

    return bindings;
}

std::filesystem::path ProjectLoader::resolveWorkspaceEntryPath(const std::filesystem::path& workspaceRoot) const {
    const auto candidate = normalizePath(workspaceRoot / "main.cat");
    if (!std::filesystem::exists(candidate) || !std::filesystem::is_regular_file(candidate)) {
        throwDiagnostic(
            workspaceRoot,
            "workspace",
            "Workspace root must contain main.cat as the entry source file.");
    }
    return candidate;
}
LoadedProject ProjectLoader::load(const std::filesystem::path& inputPath) {
    project = LoadedProject{};
    project.target = defaultTargetSpec();
    unitIndexByPath.clear();
    manifestCache.clear();
    exportCache.clear();
    loadStack.clear();

    project.inputPath = normalizePath(inputPath);

    auto loadWorkspaceConfigIfPresent = [&]() {
        if (project.configPath.has_value()) {
            project.config = loadProjectConfig(*project.configPath);
        } else {
            project.config.reset();
        }
    };

    if (std::filesystem::is_directory(project.inputPath)) {
        project.packageRoot = project.inputPath;
        project.configPath = detectWorkspaceConfig(project.packageRoot);
        project.structuredPackage = true;
        loadWorkspaceConfigIfPresent();
        project.entryIndex = loadUnitRecursive(resolveWorkspaceEntryPath(project.packageRoot));
        return std::move(project);
    }

    if (isSupportedConfigFile(project.inputPath)) {
        project.packageRoot = normalizePath(project.inputPath.parent_path());
        project.configPath = project.inputPath;
        project.structuredPackage = true;
        loadWorkspaceConfigIfPresent();
        project.entryIndex = loadUnitRecursive(resolveWorkspaceEntryPath(project.packageRoot));
        return std::move(project);
    }

    if (project.inputPath.filename() == "modules.cat") {
        const auto workspaceRoot = findWorkspaceRoot(project.inputPath.parent_path());
        if (!workspaceRoot.has_value()) {
            throwDiagnostic(
                project.inputPath,
                "workspace",
                "modules.cat is a folder module index. Build the workspace root that contains main.cat.");
        }
        project.packageRoot = *workspaceRoot;
        project.configPath = detectWorkspaceConfig(project.packageRoot);
        project.structuredPackage = true;
        loadWorkspaceConfigIfPresent();
        project.entryIndex = loadUnitRecursive(resolveWorkspaceEntryPath(project.packageRoot));
        return std::move(project);
    }

    if (project.inputPath.filename() == "main.cat") {
        project.packageRoot = normalizePath(project.inputPath.parent_path());
        project.configPath = detectWorkspaceConfig(project.packageRoot);
        project.structuredPackage = true;
        loadWorkspaceConfigIfPresent();
        project.entryIndex = loadUnitRecursive(project.inputPath);
        return std::move(project);
    }

    if (const auto workspaceRoot = findWorkspaceRoot(project.inputPath.parent_path())) {
        project.packageRoot = *workspaceRoot;
        project.configPath = detectWorkspaceConfig(project.packageRoot);
        project.structuredPackage = true;
        loadWorkspaceConfigIfPresent();
        project.entryIndex = loadUnitRecursive(project.inputPath);
        return std::move(project);
    }

    project.packageRoot = normalizePath(project.inputPath.parent_path());
    project.entryIndex = loadUnitRecursive(project.inputPath);
    return std::move(project);
}

} // namespace claw::frontend


