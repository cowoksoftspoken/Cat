#pragma once

#include "analysis/types.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace claw::frontend {

struct Symbol {
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    bool isMutable = false;
    bool isExternal = false;
    ResolvedType type;
    const BindingStmt* bindingDecl = nullptr;
    std::optional<size_t> viewSourceParamIndex;
    std::vector<std::string> declaredNamedScopes;
    std::shared_ptr<ModuleInfo> moduleInfo;
};

class Scope {
public:
    Scope* parent;
    std::unordered_map<std::string, std::shared_ptr<Symbol>> symbols;

    explicit Scope(Scope* parent = nullptr);

    bool define(const std::string& name, std::shared_ptr<Symbol> sym);
    std::shared_ptr<Symbol> lookup(const std::string& name);
    std::shared_ptr<Symbol> lookupLocal(const std::string& name);
};

class ScopeTree {
public:
    Scope* currentScope();
    void enterScope();
    void exitScope();

    bool define(const std::string& name, std::shared_ptr<Symbol> sym);
    std::shared_ptr<Symbol> lookup(const std::string& name);

private:
    std::vector<std::unique_ptr<Scope>> allScopes;
    Scope* current = nullptr;
};

} // namespace claw::frontend
