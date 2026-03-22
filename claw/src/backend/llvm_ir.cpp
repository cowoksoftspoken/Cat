#include "backend/llvm_ir.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace claw::frontend {

namespace {

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

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

std::string stripViewPrefix(std::string_view type) {
    if (type.rfind("look ", 0) == 0 || type.rfind("edit ", 0) == 0) {
        return trim(type.substr(5));
    }
    return trim(type);
}

std::string stripGenericArgs(std::string_view type) {
    const size_t pos = type.find(" of ");
    return pos == std::string_view::npos ? std::string(type) : trim(type.substr(0, pos));
}

bool isStringLiteral(std::string_view text) {
    return text.size() >= 2 && text.front() == '"' && text.back() == '"';
}

bool isIntegerLiteral(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    size_t index = 0;
    if (text.front() == '-') {
        if (text.size() == 1) {
            return false;
        }
        index = 1;
    }
    bool sawDigit = false;
    for (; index < text.size(); ++index) {
        const char c = text[index];
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            sawDigit = true;
            continue;
        }
        if (c == '_') {
            return sawDigit;
        }
        return false;
    }
    return sawDigit;
}

std::string stripLiteralSuffix(std::string_view text) {
    const size_t suffixPos = text.find('_');
    return std::string(suffixPos == std::string_view::npos ? text : text.substr(0, suffixPos));
}

std::string decodeStringLiteral(std::string_view text) {
    if (!isStringLiteral(text)) {
        return std::string(text);
    }

    std::string decoded;
    decoded.reserve(text.size() - 2);
    for (size_t i = 1; i + 1 < text.size(); ++i) {
        const char c = text[i];
        if (c != '\\') {
            decoded.push_back(c);
            continue;
        }
        if (i + 1 >= text.size() - 1) {
            decoded.push_back('\\');
            break;
        }
        const char next = text[++i];
        switch (next) {
        case 'n': decoded.push_back('\n'); break;
        case 'r': decoded.push_back('\r'); break;
        case 't': decoded.push_back('\t'); break;
        case '"': decoded.push_back('"'); break;
        case '\\': decoded.push_back('\\'); break;
        case '0': decoded.push_back('\0'); break;
        default: decoded.push_back(next); break;
        }
    }
    return decoded;
}

std::string escapeLlvmBytes(const std::string& text) {
    std::ostringstream out;
    for (const unsigned char c : text) {
        if (c == '\\') {
            out << "\\5C";
        } else if (c == '"') {
            out << "\\22";
        } else if (c >= 32 && c <= 126) {
            out << static_cast<char>(c);
        } else {
            out << '\\' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(c) << std::nouppercase << std::dec;
        }
    }
    return out.str();
}

std::string quoteGlobal(std::string_view name) {
    return std::string("@\"") + std::string(name) + "\"";
}

std::string quoteType(std::string_view name) {
    return std::string("%\"") + std::string(name) + "\"";
}

std::string localName(std::string_view name) {
    std::string out;
    out.reserve(name.size() + 1);
    out.push_back('%');
    for (const char c : name) {
        if (c == '%') {
            continue;
        }
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.') ? c : '_');
    }
    return out;
}

std::string blockLabel(std::string_view label) {
    std::string out;
    out.reserve(label.size());
    for (const char c : label) {
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == '-') ? c : '_');
    }
    return out.empty() ? std::string("block") : out;
}

std::string tailSegment(std::string_view text) {
    const size_t pos = text.rfind('.');
    return pos == std::string_view::npos ? std::string(text) : std::string(text.substr(pos + 1));
}

std::string receiverSegment(std::string_view text) {
    const size_t pos = text.rfind('.');
    return pos == std::string_view::npos ? std::string() : std::string(text.substr(0, pos));
}

bool isSignedIntegerType(std::string_view typeName) {
    return typeName == "Int8" || typeName == "Int16" || typeName == "Int32" || typeName == "Int64" ||
        typeName == "Int128" || typeName == "ISize";
}

bool isFloatingType(std::string_view typeName) {
    return typeName == "Float32" || typeName == "Float64";
}

struct StringConstantInfo {
    std::string globalName;
    std::string arrayType;
    size_t length = 0;
};

struct FunctionState {
    const LirFunction& function;
    std::unordered_map<std::string, std::string> stackPointers;
    std::unordered_map<std::string, std::string> params;
    std::unordered_map<std::string, std::string> valueTypes;
    int nextLocal = 0;
    int nextInlineBlock = 0;

    std::string temp(std::string_view prefix) {
        return localName(std::string(prefix) + "." + std::to_string(nextLocal++));
    }

    std::string inlineLabel(std::string_view prefix) {
        return blockLabel(function.name + "." + std::string(prefix) + "." + std::to_string(nextInlineBlock++));
    }
};

class LlvmEmitter {
public:
    explicit LlvmEmitter(const LirProgram& program)
        : program(program) {
        collectDecls();
    }

    std::string emit();

private:
    const LirProgram& program;
    std::unordered_map<std::string, const LirFunction*> functionsByName;
    std::unordered_map<std::string, const LirShape*> shapesByName;
    std::unordered_map<std::string, const LirChoice*> choicesByName;
    std::unordered_map<std::string, std::unordered_map<std::string, size_t>> shapeFieldIndices;
    std::unordered_map<std::string, StringConstantInfo> stringPool;
    std::vector<std::string> runtimeDecls;
    std::unordered_set<std::string> runtimeDeclKeys;
    std::vector<std::string> externalDecls;
    std::unordered_set<std::string> externalDeclKeys;
    bool needsTrap = false;

    void collectDecls();
    std::string llvmType(const std::string& typeText) const;
    std::string emitTypeDecls() const;
    const StringConstantInfo& internString(std::string_view literal);
    std::string emitStringGlobals();
    void addRuntimeDecl(const std::string& decl);
    void addExternalDecl(const std::string& decl);
    std::string runtimeSymbol(std::string_view callee, const std::string& argType);
    std::string externalSymbol(const LirCallInst& call);
    std::string emitDeclarations();
    std::string valueTypeForName(std::string_view name, const FunctionState& state) const;
    std::string ensureNamedValue(std::string_view name, FunctionState& state, std::vector<std::string>& lines);
    std::string extractLength(
        const std::string& aggregateValue,
        std::string_view receiverType,
        FunctionState& state,
        std::vector<std::string>& lines);
    std::string extractPointer(
        const std::string& aggregateValue,
        std::string_view receiverType,
        FunctionState& state,
        std::vector<std::string>& lines);
    void emitBoundsCheck(const LirBoundsCheckInst& value, FunctionState& state, std::vector<std::string>& lines);
    void emitBuiltinCall(const LirCallInst& value, FunctionState& state, std::vector<std::string>& lines);
    std::string ensureValue(const LirValue& value, FunctionState& state, std::vector<std::string>& lines);
    std::string symbolForDirectCall(std::string_view callee) const;
    std::string emitFunction(const LirFunction& fn);
    std::string emitFunctionDefs();
};

void LlvmEmitter::collectDecls() {
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            std::visit(Overloaded{
                [&](const LirFunction& fn) {
                    functionsByName[fn.name] = &fn;
                },
                [&](const LirShape& shape) {
                    shapesByName[shape.name] = &shape;
                    std::unordered_map<std::string, size_t> fields;
                    for (size_t i = 0; i < shape.fields.size(); ++i) {
                        fields[shape.fields[i].name] = i;
                    }
                    shapeFieldIndices[shape.name] = std::move(fields);
                },
                [&](const LirChoice& choice) {
                    choicesByName[choice.name] = &choice;
                }
            }, decl);
        }
    }
}

std::string LlvmEmitter::llvmType(const std::string& typeText) const {
    const std::string stripped = stripViewPrefix(typeText);
    const std::string base = stripGenericArgs(stripped);

    if (stripped == "Unit" || base == "Unit") return "void";
    if (stripped == "Bool" || base == "Bool") return "i1";
    if (stripped == "integer literal" || base == "integer literal") return "i32";
    if (stripped == "float literal" || base == "float literal") return "double";
    if (base == "Byte" || base == "Int8" || base == "UInt8" || base == "Bits8") return "i8";
    if (base == "Int16" || base == "UInt16" || base == "Bits16") return "i16";
    if (base == "Rune" || base == "Int32" || base == "UInt32" || base == "Bits32") return "i32";
    if (base == "Int64" || base == "UInt64" || base == "Bits64" || base == "USize" || base == "ISize") return "i64";
    if (base == "Int128" || base == "UInt128" || base == "Bits128") return "i128";
    if (base == "Float32") return "float";
    if (base == "Float64") return "double";
    if (base == "Text") return "%claw.slice";
    if (base == "Bytes") return "%claw.buffer";
    if (base == "Addr" || base == "RawPtr" || base == "RawMut" || base == "Fn" || base == "CStr" ||
        base == "OwnedCStr" || base == "Arena" || base == "Pool" || base == "Anchor" || base == "Table" ||
        base == "Set" || base == "Heap" || base == "Ring") {
        return "ptr";
    }

    const auto shapeIt = shapesByName.find(base);
    if (shapeIt != shapesByName.end() && shapeIt->second->layout.has_value() && !shapeIt->second->layout->isTemplate &&
        shapeIt->second->typeParams.empty()) {
        return quoteType(shapeIt->second->linkage.symbol);
    }
    const auto choiceIt = choicesByName.find(base);
    if (choiceIt != choicesByName.end() && choiceIt->second->layout.has_value() && !choiceIt->second->layout->isTemplate &&
        choiceIt->second->typeParams.empty()) {
        return quoteType(choiceIt->second->linkage.symbol);
    }

    throw std::runtime_error("LLVM lowering does not yet support type '" + typeText + "'.");
}

std::string LlvmEmitter::emitTypeDecls() const {
    std::ostringstream out;
    bool first = true;
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            if (const auto* shape = std::get_if<LirShape>(&decl)) {
                if (!shape->layout.has_value() || shape->layout->isTemplate || !shape->typeParams.empty()) {
                    continue;
                }
                if (!first) {
                    out << "\n";
                }
                first = false;
                out << quoteType(shape->linkage.symbol) << " = type { ";
                for (size_t i = 0; i < shape->fields.size(); ++i) {
                    if (i > 0) {
                        out << ", ";
                    }
                    out << llvmType(shape->fields[i].type);
                }
                out << " }\n";
            } else if (const auto* choice = std::get_if<LirChoice>(&decl)) {
                if (!choice->layout.has_value() || choice->layout->isTemplate || !choice->typeParams.empty()) {
                    continue;
                }
                if (!first) {
                    out << "\n";
                }
                first = false;
                out << quoteType(choice->linkage.symbol) << " = type { i32";
                if (choice->layout->payloadSizeBytes > 0) {
                    out << ", [" << choice->layout->payloadSizeBytes << " x i8]";
                }
                out << " }\n";
            }
        }
    }
    return out.str();
}

const StringConstantInfo& LlvmEmitter::internString(std::string_view literal) {
    const std::string key(literal);
    const auto it = stringPool.find(key);
    if (it != stringPool.end()) {
        return it->second;
    }

    StringConstantInfo info;
    info.globalName = "@.str." + std::to_string(stringPool.size());
    const std::string decoded = decodeStringLiteral(literal);
    info.length = decoded.size();
    info.arrayType = "[" + std::to_string(info.length + 1) + " x i8]";
    stringPool.emplace(key, info);
    return stringPool.find(key)->second;
}

std::string LlvmEmitter::emitStringGlobals() {
    std::ostringstream out;
    bool first = true;
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            if (const auto* fn = std::get_if<LirFunction>(&decl)) {
                for (const auto& block : fn->blocks) {
                    for (const auto& inst : block.insts) {
                        std::visit(Overloaded{
                            [&](const LirStoreInst& value) {
                                if (isStringLiteral(value.value.text)) {
                                    internString(value.value.text);
                                }
                            },
                            [&](const LirReturnInst& value) {
                                if (isStringLiteral(value.value.text)) {
                                    internString(value.value.text);
                                }
                            },
                            [&](const LirDiscardInst& value) {
                                if (isStringLiteral(value.value.text)) {
                                    internString(value.value.text);
                                }
                            },
                            [&](const LirCallInst& value) {
                                for (const auto& arg : value.args) {
                                    if (isStringLiteral(arg.text)) {
                                        internString(arg.text);
                                    }
                                }
                            },
                            [&](const auto&) {}
                        }, inst);
                    }
                }
            }
        }
    }

    for (const auto& [literal, info] : stringPool) {
        const std::string decoded = decodeStringLiteral(literal);
        if (!first) {
            out << "\n";
        }
        first = false;
        out << info.globalName << " = private unnamed_addr constant " << info.arrayType
            << " c\"" << escapeLlvmBytes(decoded) << "\\00\"\n";
    }
    return out.str();
}

void LlvmEmitter::addRuntimeDecl(const std::string& decl) {
    if (runtimeDeclKeys.insert(decl).second) {
        runtimeDecls.push_back(decl);
    }
}

void LlvmEmitter::addExternalDecl(const std::string& decl) {
    if (externalDeclKeys.insert(decl).second) {
        externalDecls.push_back(decl);
    }
}

std::string LlvmEmitter::runtimeSymbol(std::string_view callee, const std::string& argType) {
    const std::string llvmArgType = llvmType(argType);
    std::string suffix;
    if (llvmArgType == "%claw.slice") {
        suffix = "slice";
    } else if (llvmArgType == "%claw.buffer") {
        suffix = "buffer";
    } else if (llvmArgType == "ptr") {
        suffix = "ptr";
    } else {
        suffix = llvmArgType;
    }
    std::replace(suffix.begin(), suffix.end(), '%', '_');
    std::replace(suffix.begin(), suffix.end(), '"', '_');
    std::replace(suffix.begin(), suffix.end(), '.', '_');
    const std::string symbol = quoteGlobal(std::string("claw.runtime.") + std::string(callee) + "." + suffix);
    addRuntimeDecl("declare void " + symbol + "(" + llvmArgType + ")");
    return symbol;
}

std::string LlvmEmitter::externalSymbol(const LirCallInst& call) {
    if (!call.externalInfo.has_value()) {
        throw std::runtime_error("LLVM lowering requires explicit external metadata for external calls.");
    }
    const auto& info = *call.externalInfo;
    if (info.rawOnly || info.opaqueResult) {
        throw std::runtime_error("LLVM lowering does not yet support raw-only or opaque external calls: '" + call.callee + "'.");
    }

    std::vector<std::string> paramTypes;
    paramTypes.reserve(call.args.size());
    for (const auto& arg : call.args) {
        paramTypes.push_back(llvmType(arg.type));
    }

    std::ostringstream decl;
    decl << "declare " << llvmType(call.type) << " " << quoteGlobal(info.linkageName) << "(";
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        if (i > 0) {
            decl << ", ";
        }
        decl << paramTypes[i];
    }
    decl << ")";
    addExternalDecl(decl.str());
    return quoteGlobal(info.linkageName);
}

std::string LlvmEmitter::emitDeclarations() {
    std::ostringstream out;
    bool first = true;
    for (const auto& decl : runtimeDecls) {
        if (!first) {
            out << "\n";
        }
        first = false;
        out << decl;
    }
    for (const auto& decl : externalDecls) {
        if (!first) {
            out << "\n";
        }
        first = false;
        out << decl;
    }
    if (needsTrap) {
        if (!first) {
            out << "\n";
        }
        out << "declare void @llvm.trap()";
    }
    return out.str();
}

std::string LlvmEmitter::valueTypeForName(std::string_view name, const FunctionState& state) const {
    const auto it = state.valueTypes.find(std::string(name));
    return it != state.valueTypes.end() ? it->second : std::string();
}

std::string LlvmEmitter::ensureNamedValue(std::string_view name, FunctionState& state, std::vector<std::string>& lines) {
    const std::string type = valueTypeForName(name, state);
    if (type.empty()) {
        throw std::runtime_error("LLVM lowering could not resolve type for value '" + std::string(name) + "'.");
    }
    return ensureValue(LirValue{std::string(name), type, false}, state, lines);
}

std::string LlvmEmitter::extractLength(
    const std::string& aggregateValue,
    std::string_view receiverType,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const std::string baseType = stripGenericArgs(stripViewPrefix(receiverType));
    if (baseType == "Text") {
        const std::string lengthReg = state.temp("text.len");
        lines.push_back("  " + lengthReg + " = extractvalue %claw.slice " + aggregateValue + ", 1");
        return lengthReg;
    }
    if (baseType == "Bytes") {
        const std::string lengthReg = state.temp("bytes.len");
        lines.push_back("  " + lengthReg + " = extractvalue %claw.buffer " + aggregateValue + ", 1");
        return lengthReg;
    }
    throw std::runtime_error("LLVM lowering does not yet support length extraction for receiver type '" + std::string(receiverType) + "'.");
}

std::string LlvmEmitter::extractPointer(
    const std::string& aggregateValue,
    std::string_view receiverType,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const std::string baseType = stripGenericArgs(stripViewPrefix(receiverType));
    if (baseType == "Text") {
        const std::string pointerReg = state.temp("text.ptr");
        lines.push_back("  " + pointerReg + " = extractvalue %claw.slice " + aggregateValue + ", 0");
        return pointerReg;
    }
    if (baseType == "Bytes") {
        const std::string pointerReg = state.temp("bytes.ptr");
        lines.push_back("  " + pointerReg + " = extractvalue %claw.buffer " + aggregateValue + ", 0");
        return pointerReg;
    }
    throw std::runtime_error("LLVM lowering does not yet support pointer extraction for receiver type '" + std::string(receiverType) + "'.");
}

void LlvmEmitter::emitBoundsCheck(const LirBoundsCheckInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const std::string receiverType = valueTypeForName(value.subject, state);
    if (receiverType.empty()) {
        throw std::runtime_error("LLVM lowering could not resolve bounds-check receiver '" + value.subject + "'.");
    }

    const std::string receiverValue = ensureNamedValue(value.subject, state, lines);
    const std::string lengthValue = extractLength(receiverValue, receiverType, state, lines);
    const std::string failLabel = blockLabel(value.failLabel);
    const std::string continueLabel = state.inlineLabel("bounds.ok");

    if (value.kind == "byte_at") {
        if (value.args.size() != 1) {
            throw std::runtime_error("LLVM lowering expected one argument for byte_at bounds check.");
        }
        const std::string indexValue = ensureValue(value.args[0], state, lines);
        const std::string checkValue = state.temp("bounds.byte_at");
        lines.push_back("  " + checkValue + " = icmp ult i64 " + indexValue + ", " + lengthValue);
        lines.push_back("  br i1 " + checkValue + ", label %" + continueLabel + ", label %" + failLabel);
        lines.push_back(continueLabel + ":");
        return;
    }

    if (value.kind == "first_byte" || value.kind == "last_byte") {
        const std::string checkValue = state.temp("bounds.non_empty");
        lines.push_back("  " + checkValue + " = icmp ne i64 " + lengthValue + ", 0");
        lines.push_back("  br i1 " + checkValue + ", label %" + continueLabel + ", label %" + failLabel);
        lines.push_back(continueLabel + ":");
        return;
    }

    if (value.kind == "slice") {
        if (value.args.size() != 2) {
            throw std::runtime_error("LLVM lowering expected two arguments for slice bounds check.");
        }
        const std::string startValue = ensureValue(value.args[0], state, lines);
        const std::string countValue = ensureValue(value.args[1], state, lines);
        const std::string startOkValue = state.temp("bounds.slice.start");
        const std::string startOkLabel = state.inlineLabel("bounds.slice.start_ok");
        lines.push_back("  " + startOkValue + " = icmp ule i64 " + startValue + ", " + lengthValue);
        lines.push_back("  br i1 " + startOkValue + ", label %" + startOkLabel + ", label %" + failLabel);
        lines.push_back(startOkLabel + ":");
        const std::string remainingValue = state.temp("bounds.slice.remaining");
        lines.push_back("  " + remainingValue + " = sub i64 " + lengthValue + ", " + startValue);
        const std::string countOkValue = state.temp("bounds.slice.count");
        lines.push_back("  " + countOkValue + " = icmp ule i64 " + countValue + ", " + remainingValue);
        lines.push_back("  br i1 " + countOkValue + ", label %" + continueLabel + ", label %" + failLabel);
        lines.push_back(continueLabel + ":");
        return;
    }

    throw std::runtime_error("LLVM lowering does not yet support bounds_check kind '" + value.kind + "'.");
}

void LlvmEmitter::emitBuiltinCall(const LirCallInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const std::string receiverName = receiverSegment(value.callee);
    if (receiverName.empty()) {
        throw std::runtime_error("LLVM lowering expected a builtin receiver for '" + value.callee + "'.");
    }

    const std::string receiverType = valueTypeForName(receiverName, state);
    if (receiverType.empty()) {
        throw std::runtime_error("LLVM lowering could not resolve builtin receiver type for '" + receiverName + "'.");
    }

    const std::string receiverValue = ensureNamedValue(receiverName, state, lines);
    const std::string methodName = tailSegment(value.callee);
    const std::string baseType = stripGenericArgs(stripViewPrefix(receiverType));
    const auto storeResultType = [&]() {
        if (value.result.has_value()) {
            state.valueTypes[*value.result] = value.type;
        }
    };

    if ((baseType == "Text" || baseType == "Bytes") && methodName == "len") {
        if (!value.result.has_value()) {
            return;
        }
        const std::string resultReg = localName(*value.result);
        const std::string lengthValue = extractLength(receiverValue, receiverType, state, lines);
        lines.push_back("  " + resultReg + " = add i64 0, " + lengthValue);
        storeResultType();
        return;
    }

    if ((baseType == "Text" || baseType == "Bytes") && methodName == "is_empty") {
        if (!value.result.has_value()) {
            return;
        }
        const std::string resultReg = localName(*value.result);
        const std::string lengthValue = extractLength(receiverValue, receiverType, state, lines);
        lines.push_back("  " + resultReg + " = icmp eq i64 " + lengthValue + ", 0");
        storeResultType();
        return;
    }

    if (baseType == "Text" && methodName == "byte_at") {
        if (value.args.size() != 1) {
            throw std::runtime_error("LLVM lowering expected one argument for text.byte_at.");
        }
        if (!value.result.has_value()) {
            return;
        }
        const std::string indexValue = ensureValue(value.args[0], state, lines);
        const std::string pointerValue = extractPointer(receiverValue, receiverType, state, lines);
        const std::string elementPtr = state.temp("text.byte_at.ptr");
        lines.push_back("  " + elementPtr + " = getelementptr inbounds i8, ptr " + pointerValue + ", i64 " + indexValue);
        lines.push_back("  " + localName(*value.result) + " = load i8, ptr " + elementPtr);
        storeResultType();
        return;
    }

    if (baseType == "Text" && methodName == "first_byte") {
        if (!value.result.has_value()) {
            return;
        }
        const std::string pointerValue = extractPointer(receiverValue, receiverType, state, lines);
        lines.push_back("  " + localName(*value.result) + " = load i8, ptr " + pointerValue);
        storeResultType();
        return;
    }

    if (baseType == "Text" && methodName == "last_byte") {
        if (!value.result.has_value()) {
            return;
        }
        const std::string lengthValue = extractLength(receiverValue, receiverType, state, lines);
        const std::string indexValue = state.temp("text.last_byte.index");
        lines.push_back("  " + indexValue + " = sub i64 " + lengthValue + ", 1");
        const std::string pointerValue = extractPointer(receiverValue, receiverType, state, lines);
        const std::string elementPtr = state.temp("text.last_byte.ptr");
        lines.push_back("  " + elementPtr + " = getelementptr inbounds i8, ptr " + pointerValue + ", i64 " + indexValue);
        lines.push_back("  " + localName(*value.result) + " = load i8, ptr " + elementPtr);
        storeResultType();
        return;
    }

    if (baseType == "Text" && methodName == "slice") {
        if (value.args.size() != 2) {
            throw std::runtime_error("LLVM lowering expected two arguments for text.slice.");
        }
        if (!value.result.has_value()) {
            return;
        }
        const std::string startValue = ensureValue(value.args[0], state, lines);
        const std::string countValue = ensureValue(value.args[1], state, lines);
        const std::string pointerValue = extractPointer(receiverValue, receiverType, state, lines);
        const std::string slicePtr = state.temp("text.slice.ptr");
        lines.push_back("  " + slicePtr + " = getelementptr inbounds i8, ptr " + pointerValue + ", i64 " + startValue);
        const std::string sliceBase = state.temp("text.slice.base");
        lines.push_back("  " + sliceBase + " = insertvalue %claw.slice poison, ptr " + slicePtr + ", 0");
        lines.push_back("  " + localName(*value.result) + " = insertvalue %claw.slice " + sliceBase + ", i64 " + countValue + ", 1");
        storeResultType();
        return;
    }

    throw std::runtime_error("LLVM lowering does not yet support builtin method '" + value.callee + "'.");
}

std::string LlvmEmitter::ensureValue(const LirValue& value, FunctionState& state, std::vector<std::string>& lines) {
    const std::string llvmValueType = llvmType(value.type);
    if (llvmValueType == "void" || value.isUnit) {
        return {};
    }

    if (isStringLiteral(value.text)) {
        const auto& stringConst = internString(value.text);
        const std::string ptrReg = state.temp("str.ptr");
        lines.push_back("  " + ptrReg + " = getelementptr inbounds " + stringConst.arrayType + ", ptr " + stringConst.globalName + ", i64 0, i64 0");
        const std::string sliceBase = state.temp("str.slice");
        lines.push_back("  " + sliceBase + " = insertvalue %claw.slice poison, ptr " + ptrReg + ", 0");
        const std::string sliceValue = state.temp("str.slice");
        lines.push_back("  " + sliceValue + " = insertvalue %claw.slice " + sliceBase + ", i64 " + std::to_string(stringConst.length) + ", 1");
        return sliceValue;
    }

    if (const auto it = state.stackPointers.find(value.text); it != state.stackPointers.end()) {
        const std::string loadReg = state.temp(std::string(value.text) + ".load");
        lines.push_back("  " + loadReg + " = load " + llvmValueType + ", ptr " + it->second);
        return loadReg;
    }

    if (const auto it = state.params.find(value.text); it != state.params.end()) {
        return it->second;
    }

    if (!value.text.empty() && value.text.front() == '%') {
        return localName(value.text);
    }
    if (value.text == "true") {
        return "true";
    }
    if (value.text == "false") {
        return "false";
    }
    if (isIntegerLiteral(value.text)) {
        return stripLiteralSuffix(value.text);
    }

    throw std::runtime_error("LLVM lowering does not yet support value '" + value.text + "' of type '" + value.type + "'.");
}

std::string LlvmEmitter::symbolForDirectCall(std::string_view callee) const {
    const auto directIt = functionsByName.find(std::string(callee));
    if (directIt != functionsByName.end()) {
        return quoteGlobal(directIt->second->linkage.symbol);
    }

    const std::string tail = tailSegment(callee);
    const auto tailIt = functionsByName.find(tail);
    if (tailIt != functionsByName.end()) {
        return quoteGlobal(tailIt->second->linkage.symbol);
    }

    throw std::runtime_error("Unable to resolve direct call target '" + std::string(callee) + "' in LLVM lowering.");
}

std::string LlvmEmitter::emitFunction(const LirFunction& fn) {
    FunctionState state{fn};
    for (const auto& param : fn.params) {
        state.params[param.name] = localName("arg." + param.name);
        state.valueTypes[param.name] = param.type;
    }

    std::ostringstream out;
    out << "define " << llvmType(fn.returnType) << " " << quoteGlobal(fn.linkage.symbol) << "(";
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << llvmType(fn.params[i].type) << " " << state.params[fn.params[i].name];
    }
    out << ") {\n";

    for (size_t blockIndex = 0; blockIndex < fn.blocks.size(); ++blockIndex) {
        const auto& block = fn.blocks[blockIndex];
        out << blockLabel(block.label) << ":\n";
        bool hasTerminator = false;
        std::vector<std::string> blockLines;

        for (const auto& inst : block.insts) {
            std::visit(Overloaded{
                [&](const LirObjectInst& value) {
                    const std::string pointerName = localName(value.name + ".addr");
                    state.stackPointers[value.name] = pointerName;
                    state.valueTypes[value.name] = value.type;
                    blockLines.push_back("  " + pointerName + " = alloca " + llvmType(value.type));
                },
                [&](const LirStoreInst& value) {
                    const auto targetIt = state.stackPointers.find(value.target);
                    if (targetIt == state.stackPointers.end()) {
                        throw std::runtime_error("LLVM lowering expected stack storage for '" + value.target + "'.");
                    }
                    const std::string operand = ensureValue(value.value, state, blockLines);
                    blockLines.push_back("  store " + llvmType(value.value.type) + " " + operand + ", ptr " + targetIt->second);
                },
                [&](const LirReturnInst& value) {
                    const std::string returnType = llvmType(fn.returnType);
                    if (returnType == "void") {
                        blockLines.push_back("  ret void");
                    } else {
                        const std::string operand = ensureValue(value.value, state, blockLines);
                        blockLines.push_back("  ret " + returnType + " " + operand);
                    }
                    hasTerminator = true;
                },
                [&](const LirDiscardInst& value) {
                    (void)ensureValue(value.value, state, blockLines);
                },
                [&](const LirDropInst& value) {
                    blockLines.push_back("  ; drop " + value.name + " : " + value.type);
                },
                [&](const LirBranchInst& value) {
                    std::string operand = ensureValue(value.condition, state, blockLines);
                    std::string conditionType = llvmType(value.condition.type);
                    if (conditionType != "i1") {
                        const std::string compareReg = state.temp("cond");
                        blockLines.push_back("  " + compareReg + " = icmp ne " + conditionType + " " + operand + ", 0");
                        operand = compareReg;
                    }
                    blockLines.push_back("  br i1 " + operand + ", label %" + blockLabel(value.trueLabel) + ", label %" + blockLabel(value.falseLabel));
                    hasTerminator = true;
                },
                [&](const LirGotoInst& value) {
                    blockLines.push_back("  br label %" + blockLabel(value.targetLabel));
                    hasTerminator = true;
                },
                [&](const LirCallInst& value) {
                    if (value.kind == LirCallKind::Builtin) {
                        emitBuiltinCall(value, state, blockLines);
                        return;
                    }

                    std::vector<std::string> args;
                    std::vector<std::string> argTypes;
                    for (const auto& arg : value.args) {
                        argTypes.push_back(llvmType(arg.type));
                        args.push_back(ensureValue(arg, state, blockLines));
                    }

                    std::string calleeSymbol;
                    switch (value.kind) {
                    case LirCallKind::Direct:
                    case LirCallKind::ModuleDirect:
                        calleeSymbol = symbolForDirectCall(value.callee);
                        break;
                    case LirCallKind::Runtime:
                        if (value.args.size() != 1) {
                            throw std::runtime_error("LLVM lowering currently supports exactly one runtime call argument.");
                        }
                        calleeSymbol = runtimeSymbol(value.callee, value.args.front().type);
                        break;
                    case LirCallKind::External:
                        calleeSymbol = externalSymbol(value);
                        break;
                    case LirCallKind::Builtin:
                        throw std::runtime_error("LLVM lowering routed builtin call incorrectly for '" + value.callee + "'.");
                    case LirCallKind::Foreign:
                        throw std::runtime_error("LLVM lowering does not yet support foreign calls without typed contracts.");
                    }

                    std::ostringstream call;
                    const std::string returnType = llvmType(value.type);
                    if (value.result.has_value() && returnType != "void") {
                        call << "  " << localName(*value.result) << " = ";
                    } else {
                        call << "  ";
                    }
                    call << "call " << returnType << " " << calleeSymbol << "(";
                    for (size_t i = 0; i < args.size(); ++i) {
                        if (i > 0) {
                            call << ", ";
                        }
                        call << argTypes[i] << " " << args[i];
                    }
                    call << ")";
                    blockLines.push_back(call.str());
                    if (value.result.has_value() && returnType != "void") {
                        state.valueTypes[*value.result] = value.type;
                    }
                },
                [&](const LirBinaryInst& value) {
                    const std::string resultType = llvmType(value.type);
                    const std::string operandType = llvmType(value.left.type);
                    const std::string left = ensureValue(value.left, state, blockLines);
                    const std::string right = ensureValue(value.right, state, blockLines);
                    const std::string baseType = stripGenericArgs(stripViewPrefix(value.left.type));
                    std::ostringstream line;
                    line << "  " << localName(value.result) << " = ";
                    if (value.op == "add") {
                        line << (isFloatingType(baseType) ? "fadd " : "add ") << resultType;
                    } else if (value.op == "sub") {
                        line << (isFloatingType(baseType) ? "fsub " : "sub ") << resultType;
                    } else if (value.op == "mul") {
                        line << (isFloatingType(baseType) ? "fmul " : "mul ") << resultType;
                    } else if (value.op == "div") {
                        if (isFloatingType(baseType)) {
                            line << "fdiv " << resultType;
                        } else {
                            line << (isSignedIntegerType(baseType) ? "sdiv " : "udiv ") << resultType;
                        }
                    } else if (value.op == "eq" || value.op == "neq" || value.op == "lt" || value.op == "lte" || value.op == "gt" || value.op == "gte") {
                        if (isFloatingType(baseType)) {
                            line << "fcmp ";
                            if (value.op == "eq") line << "oeq ";
                            else if (value.op == "neq") line << "one ";
                            else if (value.op == "lt") line << "olt ";
                            else if (value.op == "lte") line << "ole ";
                            else if (value.op == "gt") line << "ogt ";
                            else line << "oge ";
                        } else {
                            line << "icmp ";
                            if (value.op == "eq") line << "eq ";
                            else if (value.op == "neq") line << "ne ";
                            else if (value.op == "lt") line << (isSignedIntegerType(baseType) ? "slt " : "ult ");
                            else if (value.op == "lte") line << (isSignedIntegerType(baseType) ? "sle " : "ule ");
                            else if (value.op == "gt") line << (isSignedIntegerType(baseType) ? "sgt " : "ugt ");
                            else line << (isSignedIntegerType(baseType) ? "sge " : "uge ");
                        }
                        line << operandType;
                    } else {
                        throw std::runtime_error("LLVM lowering does not yet support binary op '" + value.op + "'.");
                    }
                    line << " " << left << ", " << right;
                    blockLines.push_back(line.str());
                    state.valueTypes[value.result] = value.type;
                },
                [&](const LirFieldInst& value) {
                    const std::string objectBaseType = stripGenericArgs(stripViewPrefix(value.object.type));
                    const auto shapeIt = shapesByName.find(objectBaseType);
                    if (shapeIt == shapesByName.end()) {
                        throw std::runtime_error("LLVM lowering only supports field access on concrete shapes, got '" + value.object.type + "'.");
                    }
                    const auto indexIt = shapeFieldIndices[objectBaseType].find(value.field);
                    if (indexIt == shapeFieldIndices[objectBaseType].end()) {
                        throw std::runtime_error("Unknown field '" + value.field + "' during LLVM lowering.");
                    }
                    const std::string objectValue = ensureValue(value.object, state, blockLines);
                    blockLines.push_back("  " + localName(value.result) + " = extractvalue " + llvmType(value.object.type) + " " + objectValue + ", " + std::to_string(indexIt->second));
                    state.valueTypes[value.result] = value.type;
                },
                [&](const LirPhiInst& value) {
                    state.valueTypes[value.result] = value.type;
                    blockLines.push_back("  ; phi " + value.result + " omitted in initial LLVM lowering");
                },
                [&](const LirDefectInst& value) {
                    needsTrap = true;
                    blockLines.push_back("  ; defect " + value.kind + " \"" + value.detail + "\"");
                    blockLines.push_back("  call void @llvm.trap()");
                    blockLines.push_back("  unreachable");
                    hasTerminator = true;
                },
                [&](const LirBoundsCheckInst& value) {
                    emitBoundsCheck(value, state, blockLines);
                },
                [&](const LirStoreFieldInst&) {
                    throw std::runtime_error("LLVM lowering does not yet support store_field.");
                },
                [&](const LirIterNextInst&) {
                    throw std::runtime_error("LLVM lowering does not yet support iter_next.");
                },
                [&](const LirSwitchInst&) {
                    throw std::runtime_error("LLVM lowering does not yet support switch over choice values.");
                },
                [&](const LirLiftInst&) {
                    throw std::runtime_error("LLVM lowering does not yet support lift.");
                },
                [&](const LirBreakInst&) {
                    throw std::runtime_error("LLVM lowering does not yet support break lowering yet.");
                },
                [&](const LirContinueInst&) {
                    throw std::runtime_error("LLVM lowering does not yet support continue lowering yet.");
                }
            }, inst);
        }

        for (const auto& line : blockLines) {
            out << line << "\n";
        }
        if (!hasTerminator) {
            if (blockIndex + 1 < fn.blocks.size()) {
                out << "  br label %" << blockLabel(fn.blocks[blockIndex + 1].label) << "\n";
            } else if (llvmType(fn.returnType) == "void") {
                out << "  ret void\n";
            } else {
                out << "  unreachable\n";
            }
        }
    }

    out << "}\n";
    return out.str();
}

std::string LlvmEmitter::emitFunctionDefs() {
    std::ostringstream out;
    bool first = true;
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            if (const auto* fn = std::get_if<LirFunction>(&decl)) {
                if (!first) {
                    out << "\n";
                }
                first = false;
                out << emitFunction(*fn);
            }
        }
    }
    return out.str();
}

std::string LlvmEmitter::emit() {
    std::ostringstream out;
    out << "; ModuleID = 'claw'\n";
    out << "target triple = \"x86_64-pc-windows-gnu\"\n\n";
    out << "%claw.slice = type { ptr, i64 }\n";
    out << "%claw.buffer = type { ptr, i64, i64 }\n";

    const auto typeDecls = emitTypeDecls();
    if (!typeDecls.empty()) {
        out << typeDecls << "\n";
    }
    const auto globals = emitStringGlobals();
    if (!globals.empty()) {
        out << globals << "\n";
    }
    const auto definitions = emitFunctionDefs();
    const auto declarations = emitDeclarations();
    if (!declarations.empty()) {
        out << declarations << "\n\n";
    }
    out << definitions;
    return out.str();
}

} // namespace

std::string emitLlvmIr(const LirProgram& program) {
    return LlvmEmitter(program).emit();
}

std::string emitLlvmIr(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    return emitLlvmIr(buildLirProgram(entryRealm, units));
}

} // namespace claw::frontend
