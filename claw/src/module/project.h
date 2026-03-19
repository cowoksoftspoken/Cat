#pragma once

#include "analysis/types.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace claw::frontend {

struct RealmDecl;

struct LoadedUnit {
    std::filesystem::path path;
    std::string source;
    std::unique_ptr<RealmDecl> ast;
    std::vector<ImportedBinding> importedBindings;
};

struct LoadedProject {
    std::filesystem::path packageRoot;
    std::filesystem::path inputPath;
    std::optional<std::filesystem::path> manifestPath;
    std::optional<std::string> entryRealm;
    bool structuredPackage = false;
    size_t entryIndex = 0;
    std::vector<LoadedUnit> units;
};

class ProjectLoader {
public:
    LoadedProject load(const std::filesystem::path& inputPath);

private:
    struct ModuleManifest {
        std::filesystem::path path;
        std::string source;
        std::unordered_set<std::string> publishedModules;
        std::optional<std::string> entryRealm;
    };

    struct ExportSummary {
        std::unordered_map<std::string, SymbolKind> sharedItems;
    };

    LoadedProject project;
    std::unordered_map<std::string, size_t> unitIndexByPath;
    std::unordered_map<std::string, ModuleManifest> manifestCache;
    std::unordered_map<std::string, ExportSummary> exportCache;
    std::unordered_set<std::string> loadStack;

    std::filesystem::path normalizePath(const std::filesystem::path& path) const;
    std::filesystem::path findPackageRoot(const std::filesystem::path& startDir) const;
    std::string readFileText(const std::filesystem::path& path) const;
    std::unique_ptr<RealmDecl> parseSourceFile(const std::filesystem::path& path, const std::string& source) const;
    ModuleManifest loadManifest(const std::filesystem::path& path);
    std::optional<ModuleManifest> tryLoadManifest(const std::filesystem::path& path);
    void validateRealmPath(const LoadedUnit& unit) const;
    ExportSummary buildExportSummary(const LoadedUnit& unit);
    size_t loadUnitRecursive(const std::filesystem::path& sourcePath);
    std::vector<ImportedBinding> resolveImports(const LoadedUnit& unit);

    std::filesystem::path resolveEntryPath(const ModuleManifest& manifest);
    std::optional<std::filesystem::path> tryResolveRealmFile(const std::vector<std::string>& segments);
    std::optional<std::filesystem::path> tryResolveNamespaceDir(const std::vector<std::string>& segments);
    std::filesystem::path resolveSiblingModule(const std::filesystem::path& importerPath, const std::string& name);
    std::vector<std::string> splitRealmPath(const std::string& realmPath) const;
    bool isExternalRoot(const std::vector<std::string>& segments) const;
    void throwDiagnostic(
        const std::filesystem::path& path,
        const std::string& stage,
        const std::string& message,
        SourceSpan span = {}) const;
    [[noreturn]] void throwDiagnostics(std::vector<Diagnostic> diagnostics) const;
};

} // namespace claw::frontend
