#include "analysis/scope.h"

namespace claw::frontend {

Scope::Scope(Scope* parent) : parent(parent) {}

bool Scope::define(const std::string& name, std::shared_ptr<Symbol> sym) {
    if (symbols.find(name) != symbols.end()) {
        return false;
    }
    symbols[name] = std::move(sym);
    return true;
}

std::shared_ptr<Symbol> Scope::lookupLocal(const std::string& name) {
    auto it = symbols.find(name);
    return it != symbols.end() ? it->second : nullptr;
}

std::shared_ptr<Symbol> Scope::lookup(const std::string& name) {
    auto sym = lookupLocal(name);
    if (sym) {
        return sym;
    }
    if (parent) {
        return parent->lookup(name);
    }
    return nullptr;
}

Scope* ScopeTree::currentScope() {
    return current;
}

void ScopeTree::enterScope() {
    auto scope = std::make_unique<Scope>(current);
    current = scope.get();
    allScopes.push_back(std::move(scope));
}

void ScopeTree::exitScope() {
    if (current) {
        current = current->parent;
    }
}

bool ScopeTree::define(const std::string& name, std::shared_ptr<Symbol> sym) {
    if (!current) {
        return false;
    }
    return current->define(name, std::move(sym));
}

std::shared_ptr<Symbol> ScopeTree::lookup(const std::string& name) {
    if (!current) {
        return nullptr;
    }
    return current->lookup(name);
}

} // namespace claw::frontend
