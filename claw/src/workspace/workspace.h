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
}

namespace claw::workspace {

using claw::frontend::Diagnostic;
using claw::frontend::ImportedBinding;
using RealmDecl = claw::frontend::RealmDecl;
using claw::frontend::SourceSpan;
using claw::frontend::TargetSpec;

struct LoadedUnit {
    std::filesystem::path path;
    std::string source;
    std::unique_ptr<RealmDecl> ast;
    std::vector<ImportedBinding> importedBindings;
};

struct DependencyFunctionContract {
    std::string declaration;
    bool rawOnly = true;
};

struct DependencySpec {
    std::string version;
    std::string abi = "claw";
    std::unordered_map<std::string, DependencyFunctionContract> functions;
};

struct ProjectConfig {
    std::filesystem::path path;
    std::string name;
    std::string version;
    std::string edition;
    std::unordered_map<std::string, std::string> dependencies;
    std::unordered_map<std::string, DependencySpec> dependencySpecs;
};

struct LoadedProject {
    std::filesystem::path packageRoot;
    std::filesystem::path inputPath;
    std::optional<std::filesystem::path> configPath;
    std::optional<ProjectConfig> config;
    TargetSpec target;
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
    };

    struct ExportSummary {
        std::unordered_map<std::string, ImportedBinding> sharedItems;
    };

    LoadedProject project;
    std::unordered_map<std::string, size_t> unitIndexByPath;
    std::unordered_map<std::string, ModuleManifest> manifestCache;
    std::unordered_map<std::string, ExportSummary> exportCache;
    std::unordered_set<std::string> loadStack;

    std::filesystem::path normalizePath(const std::filesystem::path& path) const;
    std::optional<std::filesystem::path> findWorkspaceRoot(const std::filesystem::path& startDir) const;
    std::optional<std::filesystem::path> detectWorkspaceConfig(const std::filesystem::path& rootDir) const;
    bool isSupportedConfigFile(const std::filesystem::path& path) const;
    std::string readFileText(const std::filesystem::path& path) const;
    ProjectConfig loadProjectConfig(const std::filesystem::path& path) const;
    std::unique_ptr<RealmDecl> parseSourceFile(const std::filesystem::path& path, const std::string& source) const;
    ModuleManifest loadManifest(const std::filesystem::path& path);
    std::optional<ModuleManifest> tryLoadManifest(const std::filesystem::path& path);
    void validateRealmPath(LoadedUnit& unit) const;
    ExportSummary buildExportSummary(const LoadedUnit& unit);
    size_t loadUnitRecursive(const std::filesystem::path& sourcePath);
    std::vector<ImportedBinding> resolveImports(const LoadedUnit& unit);

    std::filesystem::path resolveWorkspaceEntryPath(const std::filesystem::path& workspaceRoot) const;
    std::optional<std::filesystem::path> tryResolveRealmFile(const std::vector<std::string>& segments);
    std::optional<std::filesystem::path> tryResolveNamespaceDir(const std::vector<std::string>& segments);
    std::filesystem::path resolveSiblingModule(const std::filesystem::path& importerPath, const std::string& name, SourceSpan span = {});
    std::vector<std::string> splitRealmPath(const std::string& realmPath) const;
    bool isExternalRoot(const std::vector<std::string>& segments) const;
    bool isWorkspaceEntrySource(const std::filesystem::path& path) const;
    bool refersToWorkspaceEntryRealm(const std::vector<std::string>& segments) const;
    void throwDiagnostic(
        const std::filesystem::path& path,
        const std::string& stage,
        const std::string& message,
        SourceSpan span = {}) const;
    [[noreturn]] void throwDiagnostics(std::vector<Diagnostic> diagnostics) const;
};

} // namespace claw::workspace