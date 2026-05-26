#pragma once

#include "ast/ast.h"

#include <string_view>
#include <vector>

namespace claw::frontend {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::unique_ptr<RealmDecl> parseFile();

private:
    std::vector<Token> tokens;
    size_t pos = 0;
    std::vector<Diagnostic> diagnostics;

    const Token& peek() const;
    const Token& previous() const;
    Token advance();
    bool check(TokenKind kind) const;
    bool checkAhead(size_t offset, TokenKind kind) const;
    bool match(TokenKind kind);
    const Token& consumeToken(TokenKind kind, const std::string& message);
    const Token& consumeNameToken(const std::string& message);
    void consume(TokenKind kind, const std::string& message);
    bool isAtEnd() const;
    bool isLegacyTypeArgumentSeparator() const;
    bool isNameToken(TokenKind kind) const;
    bool matchNameOrKeyword();
    bool isTopLevelRecoveryPoint(TokenKind kind) const;
    bool isStatementRecoveryPoint(TokenKind kind) const;
    void recordDiagnostic(const DiagnosticError& error);
    void synchronizeTopLevel();
    void synchronizeStatement();
    SourceSpan spanFromToken(const Token& token) const;
    [[noreturn]] void failAtCurrent(const std::string& message);
    [[noreturn]] void failAt(const Token& token, const std::string& message);

    ImportDecl parseImportDeclaration();
    ImportItem parseImportItem();
    void parseTypeParameterList(std::vector<std::string>* out);
    std::vector<std::unique_ptr<TypeNode>> parseBracketTypeArguments();

    std::unique_ptr<Decl> parseDeclaration();
    std::unique_ptr<FnDecl> parseFnDeclaration();
    std::unique_ptr<ShapeDecl> parseShapeDeclaration(bool isViewShape = false);
    std::unique_ptr<ChoiceDecl> parseChoiceDeclaration();
    std::unique_ptr<TypeNode> parseType();

    std::unique_ptr<BlockStmt> parseBlock();
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<GiveStmt> parseGive();
    std::unique_ptr<Stmt> parseBinding(bool isMutable);
    std::unique_ptr<WhenStmt> parseWhen();
    std::unique_ptr<LoopStmt> parseLoop();
    std::unique_ptr<ScanStmt> parseScan();
    std::unique_ptr<PickStmt> parsePick();
    std::unique_ptr<LiftStmt> parseLift();
    std::unique_ptr<ScopeStmt> parseScope();

    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseComparison();
    std::unique_ptr<Expr> parseBinary();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<ShapeInitExpr> parseShapeInitExpr(std::string name, std::string scopeName, SourceSpan span);
};

} // namespace claw::frontend


