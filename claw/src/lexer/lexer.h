#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace claw::frontend {

enum class TokenKind {
    // Keywords
    KwFn, KwVal, KwVar, KwHold, KwSlot, KwReturn, KwGive, KwIf, KwElse, KwWhen, KwOtherwise,
    KwLoop, KwScan, KwStop, KwSkip, KwShape, KwChoice, KwView, KwScope,
    KwImport, KwShare, KwSuper, KwPub, KwModules,
    KwAs, KwOver, KwOf, KwFail, KwPick, KwLift, KwRaw,
    KwRef, KwMut, KwTry, KwForeign, KwStatic,
    KwContract, KwImplements, KwWith, KwSelf, KwTrue, KwFalse,
    KwLook, KwEdit, KwRealm,

    // Punctuation
    LBracket, RBracket,
    LBrace, RBrace,
    LParen, RParen,
    Colon,
    Equal,
    Arrow,
    Comma,
    Dot,
    Bang,
    EqEq, NotEq,
    Less, LessEq,
    Greater, GreaterEq,
    Plus, Minus, Star, Slash,

    // Literals/Identifiers
    Identifier,
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    CharLiteral,

    // Special
    Eof,
    Error
};

struct Token {
    TokenKind kind;
    std::string text;
    size_t line;
    size_t column;
    size_t length;
};

class Lexer {
public:
    explicit Lexer(std::string_view source);
    std::vector<Token> tokenize();

private:
    std::string_view source;
    size_t pos = 0;
    size_t line = 1;
    size_t column = 1;

    char peek() const;
    char peekAhead(size_t offset) const;
    char advance();
    bool isAtEnd() const;
    void skipWhitespace();
    bool skipBlockComment();

    Token readIdentifierOrKeyword();
    Token readNumber();
    Token readString();
    Token readChar();
    Token makeToken(
        TokenKind kind,
        std::string text,
        size_t startLine,
        size_t startColumn,
        size_t length) const;
};

} // namespace claw::frontend

