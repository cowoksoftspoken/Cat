#include "backend/llvm_internal.h"

namespace claw::frontend {

void LlvmEmitter::emitIterNext(
    const LirIterNextInst& value,
    std::string_view currentBlockLabel,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const std::string iterableBase = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(value.iterable.type)));
    if (iterableBase != "Span" && iterableBase != "Str") {
        throw std::runtime_error(
            "LLVM lowering does not yet support iter_next over iterable type '" + value.iterable.type + "'.");
    }

    const std::string stateKey = std::string(currentBlockLabel) + "::" + value.itemName;
    IteratorState& iterState = ensureIteratorState(stateKey, value.itemName, value.itemType, state);
    const std::string iterableValue = ensureValue(value.iterable, state, lines);
    const std::string lengthValue = extractLength(iterableValue, value.iterable.type, state, lines);
    const std::string indexValue = state.temp("iter.index");
    lines.push_back("  " + indexValue + " = load i64, ptr " + iterState.indexSlot + ", align 8");
    const std::string hasItem = state.temp("iter.has_item");
    lines.push_back("  " + hasItem + " = icmp ult i64 " + indexValue + ", " + lengthValue);
    state.pendingIterBindings[value.bodyLabel].push_back(PendingIterBinding{value.iterable, value.itemName, value.itemType, stateKey});
    lines.push_back(
        "  br i1 " + hasItem + ", label %" + blockLabel(value.bodyLabel) + ", label %" + blockLabel(value.exitLabel));
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

void LlvmEmitter::emitSwitch(const LirSwitchInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const auto choice = resolveChoiceType(value.value.type);
    if (!choice.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support switch over choice type '" + value.value.type + "'.");
    }

    const std::string switchValue = ensureValue(value.value, state, lines);
    const std::string tagValue = state.temp("choice.tag");
    lines.push_back("  " + tagValue + " = extractvalue " + choice->llvmTypeText + " " + switchValue + ", 0");
    lines.push_back("  switch i32 " + tagValue + ", label %" + blockLabel(value.defectLabel) + " [");

    for (const auto& item : value.cases) {
        size_t tagIndex = choice->cases.size();
        for (size_t i = 0; i < choice->cases.size(); ++i) {
            if (choice->cases[i].name == item.tag) {
                tagIndex = i;
                break;
            }
        }
        if (tagIndex == choice->cases.size()) {
            throw std::runtime_error("LLVM lowering could not resolve switch tag '" + item.tag + "' in choice '" + value.value.type + "'.");
        }

        state.pendingChoiceBindings[item.targetLabel].push_back(PendingChoiceBinding{value.value, item.tag, item.bindings});
        lines.push_back("    i32 " + std::to_string(tagIndex) + ", label %" + blockLabel(item.targetLabel));
    }

    lines.push_back("  ]");
}

bool LlvmEmitter::emitLift(const LirLiftInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const auto choice = resolveChoiceType(value.value.type);
    if (!choice.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support lift over type '" + value.value.type + "'.");
    }

    const auto okIt = std::find_if(choice->cases.begin(), choice->cases.end(), [](const ConcreteChoiceCaseInfo& item) {
        return sameBackendIdentifier(item.name, "Ok");
    });
    const auto failIt = std::find_if(choice->cases.begin(), choice->cases.end(), [](const ConcreteChoiceCaseInfo& item) {
        return sameBackendIdentifier(item.name, "Fail");
    });
    if (okIt == choice->cases.end() || failIt == choice->cases.end()) {
        throw std::runtime_error("LLVM lowering requires lift choices to define Ok(...) and Fail(...) variants for type '" + value.value.type + "'.");
    }
    if (okIt->payloadTypes.size() != 1 || failIt->payloadTypes.size() != 1) {
        throw std::runtime_error("LLVM lowering currently requires lift variants Ok(...) and Fail(...) to carry exactly one payload each for type '" + value.value.type + "'.");
    }

    const std::string liftedValue = ensureValue(value.value, state, lines);
    const std::string tagValue = state.temp("lift.tag");
    lines.push_back("  " + tagValue + " = extractvalue " + choice->llvmTypeText + " " + liftedValue + ", 0");

    const size_t okIndex = static_cast<size_t>(std::distance(choice->cases.begin(), okIt));
    const std::string checkValue = state.temp("lift.is_ok");
    const std::string successLabel = value.successLabel.empty() ? state.inlineLabel("lift.ok") : blockLabel(value.successLabel);
    const std::string failLabel = value.autoPropagate
        ? state.inlineLabel("try.fail")
        : blockLabel(value.failLabel);
    lines.push_back("  " + checkValue + " = icmp eq i32 " + tagValue + ", " + std::to_string(okIndex));
    lines.push_back("  br i1 " + checkValue + ", label %" + successLabel + ", label %" + failLabel);

    if (value.autoPropagate) {
        const auto returnChoice = resolveChoiceType(state.function.returnType);
        if (!returnChoice.has_value()) {
            throw std::runtime_error("LLVM lowering requires `try` shorthand functions to return Result-compatible types.");
        }
        const auto returnFailIt = std::find_if(returnChoice->cases.begin(), returnChoice->cases.end(), [](const ConcreteChoiceCaseInfo& item) {
            return sameBackendIdentifier(item.name, "Fail");
        });
        if (returnFailIt == returnChoice->cases.end() || returnFailIt->payloadTypes.size() != 1) {
            throw std::runtime_error("LLVM lowering requires propagated Result types to define Fail(E) with a single payload.");
        }

        const std::string failTempName = "__try_fail_" + std::to_string(state.nextInlineBlock++);
        lines.push_back(failLabel + ":");
        materializeChoiceBinding(PendingChoiceBinding{value.value, failIt->name, {failTempName}}, state, lines);

        const std::string returnAddr = state.temp("try.fail.ret.addr");
        const auto returnLayout = abiLayoutForType(state.function.returnType);
        const size_t returnAlign = returnLayout.has_value() ? returnLayout->align : 1;
        lines.push_back("  " + returnAddr + " = alloca " + llvmType(state.function.returnType) + ", align " + std::to_string(returnAlign));
        constructChoiceValueAtAddress(
            returnAddr,
            state.function.returnType,
            returnFailIt->name,
            {LirValue{failTempName, returnFailIt->payloadTypes[0], false}},
            state,
            lines);

        if (state.returnSlot.has_value()) {
            const std::string operand = loadValueFromAddress(returnAddr, state.function.returnType, returnAlign, state, lines);
            storeValueToAddress(*state.returnSlot, state.function.returnType, operand, returnAlign, lines);
            lines.push_back("  ret void");
        } else {
            const std::string operand = loadValueFromAddress(returnAddr, state.function.returnType, returnAlign, state, lines);
            lines.push_back("  ret " + llvmType(state.function.returnType) + " " + operand);
        }
    } else {
        state.pendingChoiceBindings[value.failLabel].push_back(PendingChoiceBinding{value.value, failIt->name, {value.failName}});
    }

    if (!value.successLabel.empty()) {
        state.pendingChoiceBindings[value.successLabel].push_back(PendingChoiceBinding{value.value, okIt->name, {value.okName}});
        return true;
    }

    lines.push_back(successLabel + ":");
    materializeChoiceBinding(PendingChoiceBinding{value.value, okIt->name, {value.okName}}, state, lines);
    return false;
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
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(receiverType)));
    const auto storeResultType = [&]() {
        if (value.result.has_value()) {
            state.valueTypes[*value.result] = value.type;
        }
    };

    if (baseType == "Str" && methodName == "len") {
        if (!value.result.has_value()) {
            return;
        }
        const std::string resultReg = localName(*value.result);
        const std::string lengthValue = extractLength(receiverValue, receiverType, state, lines);
        lines.push_back("  " + resultReg + " = add i64 0, " + lengthValue);
        storeResultType();
        return;
    }

    if (baseType == "Str" && methodName == "is_empty") {
        if (!value.result.has_value()) {
            return;
        }
        const std::string resultReg = localName(*value.result);
        const std::string lengthValue = extractLength(receiverValue, receiverType, state, lines);
        lines.push_back("  " + resultReg + " = icmp eq i64 " + lengthValue + ", 0");
        storeResultType();
        return;
    }

    if (baseType == "Str" && methodName == "byte_at") {
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

    if (baseType == "Str" && methodName == "first_byte") {
        if (!value.result.has_value()) {
            return;
        }
        const std::string pointerValue = extractPointer(receiverValue, receiverType, state, lines);
        lines.push_back("  " + localName(*value.result) + " = load i8, ptr " + pointerValue);
        storeResultType();
        return;
    }

    if (baseType == "Str" && methodName == "last_byte") {
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

    if (baseType == "Str" && methodName == "slice") {
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

} // namespace claw::frontend
