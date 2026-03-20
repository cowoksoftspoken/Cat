#include "parser/parser.h"

#include <stdexcept>
#include <utility>

namespace claw::frontend {

Parser::Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

const Token& Parser::peek() const {
    return tokens[pos];
}

const Token& Parser::previous() const {
    return pos > 0 ? tokens[pos - 1] : tokens[0];
}

Token Parser::advance() {
    if (!isAtEnd()) {
        ++pos;
    }
    return previous();
}

bool Parser::check(TokenKind kind) const {
    if (isAtEnd()) {
        return kind == TokenKind::Eof;
    }
    return peek().kind == kind;
}

bool Parser::checkAhead(size_t offset, TokenKind kind) const {
    return pos + offset < tokens.size() && tokens[pos + offset].kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

const Token& Parser::consumeToken(TokenKind kind, const std::string& message) {
    if (!check(kind)) {
        failAtCurrent(message);
    }

    const Token& token = peek();
    advance();
    return token;
}

void Parser::consume(TokenKind kind, const std::string& message) {
    consumeToken(kind, message);
}

bool Parser::isAtEnd() const {
    return peek().kind == TokenKind::Eof;
}

bool Parser::isTypeArgumentSeparator() const {
    if (!check(TokenKind::Comma)) {
        return false;
    }

    return !(checkAhead(1, TokenKind::Identifier) && checkAhead(2, TokenKind::Colon));
}

bool Parser::matchNameOrKeyword() {
    const auto kind = peek().kind;
    if (kind == TokenKind::Identifier || kind == TokenKind::KwFail ||
        kind == TokenKind::KwLook || kind == TokenKind::KwEdit ||
        kind == TokenKind::KwOf || kind == TokenKind::KwAs ||
        kind == TokenKind::KwOver || kind == TokenKind::KwSuper) {
        advance();
        return true;
    }
    return false;
}

bool Parser::isTopLevelRecoveryPoint(TokenKind kind) const {
    return kind == TokenKind::KwImport || kind == TokenKind::KwFn || kind == TokenKind::KwShape ||
           kind == TokenKind::KwChoice || kind == TokenKind::KwShare || kind == TokenKind::Eof;
}

bool Parser::isStatementRecoveryPoint(TokenKind kind) const {
    return kind == TokenKind::KwHold || kind == TokenKind::KwSlot || kind == TokenKind::KwGive ||
           kind == TokenKind::KwWhen || kind == TokenKind::KwLoop || kind == TokenKind::KwScan ||
           kind == TokenKind::KwPick || kind == TokenKind::KwLift || kind == TokenKind::KwStop ||
           kind == TokenKind::KwSkip || kind == TokenKind::KwRaw || kind == TokenKind::RBrace ||
           isTopLevelRecoveryPoint(kind);
}

void Parser::recordDiagnostic(const DiagnosticError& error) {
    diagnostics.insert(diagnostics.end(), error.diagnostics().begin(), error.diagnostics().end());
}

void Parser::synchronizeTopLevel() {
    while (!isAtEnd()) {
        if (isTopLevelRecoveryPoint(peek().kind)) {
            return;
        }
        advance();
    }
}

void Parser::synchronizeStatement() {
    while (!isAtEnd()) {
        if (isStatementRecoveryPoint(peek().kind)) {
            return;
        }
        advance();
    }
}

SourceSpan Parser::spanFromToken(const Token& token) const {
    return SourceSpan{token.line, token.column, token.length == 0 ? 1 : token.length};
}

[[noreturn]] void Parser::failAtCurrent(const std::string& message) {
    failAt(peek(), message);
}

[[noreturn]] void Parser::failAt(const Token& token, const std::string& message) {
    throw DiagnosticError(
        "Parsing failed.",
        std::vector<Diagnostic>{Diagnostic{"parse", message, spanFromToken(token)}});
}

std::unique_ptr<RealmDecl> Parser::parseFile() {
    auto realm = std::make_unique<RealmDecl>();
    realm->span = spanFromToken(peek());

    if (match(TokenKind::KwRealm)) {
        realm->span = spanFromToken(previous());
        consumeToken(TokenKind::Identifier, "Expected realm name");
        realm->name = previous().text;
        while (match(TokenKind::Dot)) {
            consumeToken(TokenKind::Identifier, "Expected realm name segment");
            realm->name += "." + previous().text;
        }
    }

    while (!isAtEnd()) {
        try {
            if (match(TokenKind::KwImport)) {
                ImportDecl imp;
                imp.span = spanFromToken(previous());

                if (match(TokenKind::KwSuper)) {
                    imp.isSuper = true;
                    imp.modulePath = "super";
                    consume(TokenKind::Dot, "Expected '.' after 'super'");
                    consume(TokenKind::LBrace, "Expected '{' for import items");
                    if (!check(TokenKind::RBrace)) {
                        do {
                            consumeToken(TokenKind::Identifier, "Expected import item name");
                            imp.specificItems.push_back(previous().text);
                        } while (match(TokenKind::Comma));
                    }
                    consume(TokenKind::RBrace, "Expected '}' after import items");
                } else {
                    consumeToken(TokenKind::Identifier, "Expected module name");
                    imp.modulePath = previous().text;
                    while (match(TokenKind::Dot)) {
                        if (check(TokenKind::LBrace)) {
                            consume(TokenKind::LBrace, "Expected '{' for import items");
                            if (!check(TokenKind::RBrace)) {
                                do {
                                    consumeToken(TokenKind::Identifier, "Expected import item");
                                    imp.specificItems.push_back(previous().text);
                                } while (match(TokenKind::Comma));
                            }
                            consume(TokenKind::RBrace, "Expected '}' after import items");
                            break;
                        }

                        consumeToken(TokenKind::Identifier, "Expected module path segment");
                        imp.modulePath += "." + previous().text;
                    }
                }

                realm->imports.push_back(std::move(imp));
                continue;
            }

            auto decl = parseDeclaration();
            if (decl) {
                realm->declarations.push_back(std::move(decl));
            }
        } catch (const DiagnosticError& error) {
            recordDiagnostic(error);
            synchronizeTopLevel();
        }
    }

    if (!diagnostics.empty()) {
        throw DiagnosticError(
            "Parsing failed with " + std::to_string(diagnostics.size()) + " errors.",
            diagnostics);
    }

    return realm;
}

std::unique_ptr<Decl> Parser::parseDeclaration() {
    bool isShared = match(TokenKind::KwShare);

    if (match(TokenKind::KwFn)) {
        auto fn = parseFnDeclaration();
        fn->isShared = isShared;
        return fn;
    }
    if (match(TokenKind::KwShape)) {
        auto shape = parseShapeDeclaration();
        shape->isShared = isShared;
        return shape;
    }
    if (match(TokenKind::KwChoice)) {
        auto choice = parseChoiceDeclaration();
        choice->isShared = isShared;
        return choice;
    }

    failAtCurrent("Unexpected token at top level.");
}

std::unique_ptr<FnDecl> Parser::parseFnDeclaration() {
    auto fn = std::make_unique<FnDecl>();
    fn->span = spanFromToken(previous());

    consumeToken(TokenKind::Identifier, "Expected function name");
    fn->name = previous().text;

    consume(TokenKind::LParen, "Expected '(' for parameters");
    if (!check(TokenKind::RParen)) {
        do {
            FnParam param;
            consumeToken(TokenKind::Identifier, "Expected parameter name");
            param.span = spanFromToken(previous());
            param.name = previous().text;
            consume(TokenKind::Colon, "Expected ':' after param name");
            param.type = parseType();
            fn->params.push_back(std::move(param));
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, "Expected ')' after parameters");

    if (match(TokenKind::Arrow)) {
        fn->returnType = parseType();
    }

    fn->body = parseBlock();
    return fn;
}

std::unique_ptr<ShapeDecl> Parser::parseShapeDeclaration() {
    auto shape = std::make_unique<ShapeDecl>();
    shape->span = spanFromToken(previous());

    consumeToken(TokenKind::Identifier, "Expected shape name");
    shape->name = previous().text;

    if (match(TokenKind::KwOf)) {
        do {
            consumeToken(TokenKind::Identifier, "Expected type parameter");
            shape->typeParams.push_back(previous().text);
        } while (match(TokenKind::Comma));
    }

    consume(TokenKind::LBrace, "Expected '{' to start shape body");
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        ShapeField field;
        field.isShared = match(TokenKind::KwShare);
        consumeToken(TokenKind::Identifier, "Expected field name");
        field.span = spanFromToken(previous());
        field.name = previous().text;
        consume(TokenKind::Colon, "Expected ':' after field name");
        field.type = parseType();
        shape->fields.push_back(std::move(field));
    }
    consume(TokenKind::RBrace, "Expected '}' to end shape body");
    return shape;
}

std::unique_ptr<ChoiceDecl> Parser::parseChoiceDeclaration() {
    auto choice = std::make_unique<ChoiceDecl>();
    choice->span = spanFromToken(previous());

    consumeToken(TokenKind::Identifier, "Expected choice name");
    choice->name = previous().text;

    if (match(TokenKind::KwOf)) {
        do {
            consumeToken(TokenKind::Identifier, "Expected type parameter");
            choice->typeParams.push_back(previous().text);
        } while (match(TokenKind::Comma));
    }

    consume(TokenKind::LBrace, "Expected '{' to start choice body");
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        ChoiceVariant variant;
        if (!matchNameOrKeyword()) {
            failAtCurrent("Expected variant tag");
        }
        variant.span = spanFromToken(previous());
        variant.tag = previous().text;

        if (match(TokenKind::LParen)) {
            if (!check(TokenKind::RParen)) {
                do {
                    FnParam payload;
                    consumeToken(TokenKind::Identifier, "Expected payload name");
                    payload.span = spanFromToken(previous());
                    payload.name = previous().text;
                    consume(TokenKind::Colon, "Expected ':' after payload name");
                    payload.type = parseType();
                    variant.payloads.push_back(std::move(payload));
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RParen, "Expected ')' after payloads");
        }

        choice->variants.push_back(std::move(variant));
    }
    consume(TokenKind::RBrace, "Expected '}' to end choice body");
    return choice;
}

std::unique_ptr<TypeNode> Parser::parseType() {
    auto type = std::make_unique<TypeNode>();
    const Token startToken = peek();
    type->span = spanFromToken(startToken);

    if (match(TokenKind::KwLook)) {
        type->viewKind = "look";
    } else if (match(TokenKind::KwEdit)) {
        type->viewKind = "edit";
    }

    consumeToken(TokenKind::Identifier, "Expected type name");
    type->name = previous().text;

    if (match(TokenKind::KwOf)) {
        type->params.push_back(parseType());
        while (isTypeArgumentSeparator()) {
            advance();
            type->params.push_back(parseType());
        }
    }

    return type;
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
    const Token& openBrace = consumeToken(TokenKind::LBrace, "Expected '{' to start block");
    auto block = std::make_unique<BlockStmt>();
    block->span = spanFromToken(openBrace);

    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        try {
            block->statements.push_back(parseStatement());
        } catch (const DiagnosticError& error) {
            recordDiagnostic(error);
            synchronizeStatement();
        }
    }

    consume(TokenKind::RBrace, "Expected '}' to end block");
    return block;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    if (match(TokenKind::KwHold)) return parseBinding(false);
    if (match(TokenKind::KwSlot)) return parseBinding(true);
    if (match(TokenKind::KwGive)) return parseGive();
    if (match(TokenKind::KwWhen)) return parseWhen();
    if (match(TokenKind::KwLoop)) return parseLoop();
    if (match(TokenKind::KwScan)) return parseScan();
    if (match(TokenKind::KwPick)) return parsePick();
    if (match(TokenKind::KwLift)) return parseLift();
    if (match(TokenKind::KwStop)) {
        auto stmt = std::make_unique<StopStmt>();
        stmt->span = spanFromToken(previous());
        return stmt;
    }
    if (match(TokenKind::KwSkip)) {
        auto stmt = std::make_unique<SkipStmt>();
        stmt->span = spanFromToken(previous());
        return stmt;
    }

    if (match(TokenKind::KwRaw)) {
        auto raw = std::make_unique<RawStmt>();
        raw->span = spanFromToken(previous());
        raw->body = parseBlock();
        return raw;
    }

    auto expr = parseExpression();
    if (match(TokenKind::Equal)) {
        auto assign = std::make_unique<AssignStmt>();
        assign->span = expr->span;
        assign->target = std::move(expr);
        assign->value = parseExpression();
        return assign;
    }

    auto stmt = std::make_unique<ExprStmt>();
    stmt->span = expr->span;
    stmt->expr = std::move(expr);
    return stmt;
}

std::unique_ptr<GiveStmt> Parser::parseGive() {
    auto give = std::make_unique<GiveStmt>();
    give->span = spanFromToken(previous());
    if (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        give->value = parseExpression();
    }
    return give;
}

std::unique_ptr<BindingStmt> Parser::parseBinding(bool isMutable) {
    auto binding = std::make_unique<BindingStmt>();
    binding->span = spanFromToken(previous());
    binding->isMutable = isMutable;

    consumeToken(TokenKind::Identifier, "Expected variable name");
    binding->name = previous().text;

    if (match(TokenKind::Colon)) {
        binding->type = parseType();
    }

    if (match(TokenKind::Equal)) {
        binding->value = parseExpression();
    }
    return binding;
}

std::unique_ptr<WhenStmt> Parser::parseWhen() {
    auto stmt = std::make_unique<WhenStmt>();
    stmt->span = spanFromToken(previous());
    stmt->condition = parseExpression();
    stmt->thenBlock = parseBlock();

    if (match(TokenKind::KwOtherwise)) {
        stmt->elseBlock = parseBlock();
    }
    return stmt;
}

std::unique_ptr<LoopStmt> Parser::parseLoop() {
    auto stmt = std::make_unique<LoopStmt>();
    stmt->span = spanFromToken(previous());

    if (!check(TokenKind::LBrace)) {
        stmt->condition = parseExpression();
    }
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<ScanStmt> Parser::parseScan() {
    auto stmt = std::make_unique<ScanStmt>();
    stmt->span = spanFromToken(previous());
    consumeToken(TokenKind::Identifier, "Expected iteration variable name");
    stmt->itemName = previous().text;
    consume(TokenKind::KwOver, "Expected 'over' after scan variable");
    stmt->iterable = parseExpression();
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<PickStmt> Parser::parsePick() {
    auto stmt = std::make_unique<PickStmt>();
    stmt->span = spanFromToken(previous());
    stmt->value = parseExpression();

    consume(TokenKind::LBrace, "Expected '{' to start pick block");
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        PickBranch branch;
        if (!matchNameOrKeyword()) {
            failAtCurrent("Expected variant tag in pick branch");
        }
        branch.span = spanFromToken(previous());
        branch.tag = previous().text;

        if (match(TokenKind::LParen)) {
            if (!check(TokenKind::RParen)) {
                do {
                    consumeToken(TokenKind::Identifier, "Expected binding name");
                    branch.bindings.push_back(previous().text);
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RParen, "Expected ')' after pattern bindings");
        }

        branch.body = parseBlock();
        stmt->branches.push_back(std::move(branch));
    }
    consume(TokenKind::RBrace, "Expected '}' to end pick block");
    return stmt;
}

std::unique_ptr<LiftStmt> Parser::parseLift() {
    auto stmt = std::make_unique<LiftStmt>();
    stmt->span = spanFromToken(previous());
    stmt->expr = parseExpression();

    consume(TokenKind::KwAs, "Expected 'as' after lift expression");
    consumeToken(TokenKind::Identifier, "Expected success binding name");
    stmt->valueName = previous().text;

    consume(TokenKind::KwFail, "Expected 'fail' keyword");
    consumeToken(TokenKind::Identifier, "Expected failure binding name");
    stmt->failName = previous().text;

    stmt->failBlock = parseBlock();
    return stmt;
}

std::unique_ptr<Expr> Parser::parseExpression() {
    return parseComparison();
}

std::unique_ptr<Expr> Parser::parseComparison() {
    auto left = parseBinary();

    while (match(TokenKind::EqEq) || match(TokenKind::NotEq) ||
           match(TokenKind::Less) || match(TokenKind::LessEq) ||
           match(TokenKind::Greater) || match(TokenKind::GreaterEq)) {
        auto expr = std::make_unique<BinaryExpr>();
        expr->span = left->span;
        expr->op = previous().text;
        expr->left = std::move(left);
        expr->right = parseBinary();
        left = std::move(expr);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseBinary() {
    auto left = parseCall();
    while (match(TokenKind::Plus) || match(TokenKind::Minus) ||
           match(TokenKind::Star) || match(TokenKind::Slash)) {
        auto expr = std::make_unique<BinaryExpr>();
        expr->span = left->span;
        expr->op = previous().text;
        expr->left = std::move(left);
        expr->right = parseCall();
        left = std::move(expr);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseCall() {
    auto expr = parsePrimary();
    while (true) {
        if (match(TokenKind::LParen)) {
            auto call = std::make_unique<CallExpr>();
            call->span = expr->span;
            call->callee = std::move(expr);
            if (!check(TokenKind::RParen)) {
                do {
                    call->args.push_back(parseExpression());
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RParen, "Expected ')' after arguments");
            expr = std::move(call);
            continue;
        }

        if (match(TokenKind::Dot)) {
            consumeToken(TokenKind::Identifier, "Expected member name after '.'");
            auto member = std::make_unique<MemberExpr>();
            member->span = expr->span;
            member->object = std::move(expr);
            member->member = previous().text;
            expr = std::move(member);
            continue;
        }

        break;
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    if (check(TokenKind::Error)) {
        failAtCurrent("Unexpected character in source.");
    }

    if (match(TokenKind::IntLiteral)) {
        auto expr = std::make_unique<IntExpr>();
        expr->span = spanFromToken(previous());
        expr->value = previous().text;
        return expr;
    }
    if (match(TokenKind::FloatLiteral)) {
        auto expr = std::make_unique<FloatExpr>();
        expr->span = spanFromToken(previous());
        expr->value = previous().text;
        return expr;
    }
    if (match(TokenKind::Identifier)) {
        auto expr = std::make_unique<IdentExpr>();
        expr->span = spanFromToken(previous());
        expr->name = previous().text;
        return expr;
    }
    if (match(TokenKind::StringLiteral)) {
        auto expr = std::make_unique<StringExpr>();
        expr->span = spanFromToken(previous());
        expr->value = previous().text;
        return expr;
    }
    if (match(TokenKind::LParen)) {
        auto expr = parseExpression();
        consume(TokenKind::RParen, "Expected ')' after grouped expression");
        return expr;
    }

    failAtCurrent("Expected expression.");
}

} // namespace claw::frontend
