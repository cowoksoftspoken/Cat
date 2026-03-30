#include "backend/llvm_internal.h"

namespace claw::frontend {

void LlvmEmitter::materializeChoiceBinding(
    const PendingChoiceBinding& binding,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const auto choice = resolveChoiceType(binding.source.type);
    if (!choice.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support choice bindings for type '" + binding.source.type + "'.");
    }

    const auto caseIt = std::find_if(choice->cases.begin(), choice->cases.end(), [&](const ConcreteChoiceCaseInfo& item) {
        return item.name == binding.variantName;
    });
    if (caseIt == choice->cases.end()) {
        throw std::runtime_error("LLVM lowering could not resolve choice variant '" + binding.variantName + "'.");
    }
    if (binding.bindingNames.size() != caseIt->payloadTypes.size() ||
        binding.bindingNames.size() != caseIt->layout.payloadFields.size()) {
        throw std::runtime_error("LLVM lowering found mismatched payload binding information for variant '" + binding.variantName + "'.");
    }
    if (binding.bindingNames.empty()) {
        return;
    }
    if (choice->payloadSizeBytes == 0) {
        throw std::runtime_error("LLVM lowering expected payload storage for bound choice variant '" + binding.variantName + "'.");
    }

    const std::string sourceValue = ensureValue(binding.source, state, lines);
    const std::string choiceAddr = state.temp("choice.case.addr");
    lines.push_back("  " + choiceAddr + " = alloca " + choice->llvmTypeText + ", align " + std::to_string(choice->alignBytes));
    lines.push_back("  store " + choice->llvmTypeText + " " + sourceValue + ", ptr " + choiceAddr + ", align " + std::to_string(choice->alignBytes));

    const size_t paddingBytes = choice->payloadOffsetBytes > choice->tagSizeBytes
        ? choice->payloadOffsetBytes - choice->tagSizeBytes
        : 0;
    const int payloadFieldIndex = paddingBytes > 0 ? 2 : 1;
    const std::string payloadArrayType = "[" + std::to_string(choice->payloadSizeBytes) + " x i8]";
    const std::string payloadSlot = state.temp("choice.payload.slot");
    lines.push_back(
        "  " + payloadSlot + " = getelementptr inbounds " + choice->llvmTypeText + ", ptr " + choiceAddr +
        ", i32 0, i32 " + std::to_string(payloadFieldIndex));
    const std::string payloadBase = state.temp("choice.payload.base");
    lines.push_back("  " + payloadBase + " = getelementptr inbounds " + payloadArrayType + ", ptr " + payloadSlot + ", i32 0, i64 0");

    for (size_t i = 0; i < binding.bindingNames.size(); ++i) {
        const auto& layoutField = caseIt->layout.payloadFields[i];
        const std::string fieldPtr = state.temp("choice.payload.field.ptr");
        lines.push_back(
            "  " + fieldPtr + " = getelementptr inbounds i8, ptr " + payloadBase +
            ", i64 " + std::to_string(layoutField.offsetBytes));
        const std::string loadedValue = state.temp("choice.payload.field");
        lines.push_back(
            "  " + loadedValue + " = load " + llvmType(caseIt->payloadTypes[i]) + ", ptr " + fieldPtr +
            ", align " + std::to_string(layoutField.alignBytes));
        state.namedValues[binding.bindingNames[i]] = loadedValue;
        state.namedAddresses.erase(binding.bindingNames[i]);
        state.valueTypes[binding.bindingNames[i]] = caseIt->payloadTypes[i];
    }
}

IteratorState& LlvmEmitter::ensureIteratorState(
    std::string_view stateKey,
    std::string_view itemName,
    std::string_view itemType,
    FunctionState& state) {
    const std::string key(stateKey);
    const auto it = state.iteratorStates.find(key);
    if (it != state.iteratorStates.end()) {
        return it->second;
    }

    const auto layout = abiLayoutForType(itemType);
    if (!layout.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support iterator item type '" + std::string(itemType) + "'.");
    }

    IteratorState iterState;
    iterState.indexSlot = state.temp(std::string(itemName) + ".iter.index.addr");
    iterState.itemSlot = state.temp(std::string(itemName) + ".iter.item.addr");
    iterState.itemAlign = layout->align;

    state.entryAllocas.push_back("  " + iterState.indexSlot + " = alloca i64, align 8");
    state.entryAllocas.push_back("  store i64 0, ptr " + iterState.indexSlot + ", align 8");
    state.entryAllocas.push_back(
        "  " + iterState.itemSlot + " = alloca " + llvmType(std::string(itemType)) + ", align " + std::to_string(iterState.itemAlign));

    const auto [insertedIt, _] = state.iteratorStates.emplace(key, std::move(iterState));
    return insertedIt->second;
}

void LlvmEmitter::materializeIterBinding(
    const PendingIterBinding& binding,
    FunctionState& state,
    std::vector<std::string>& lines) {
    IteratorState& iterState = ensureIteratorState(binding.stateKey, binding.itemName, binding.itemType, state);
    const auto layout = abiLayoutForType(binding.itemType);
    if (!layout.has_value()) {
        throw std::runtime_error("LLVM lowering does not yet support iterator item type '" + binding.itemType + "'.");
    }

    const std::string iterableValue = ensureValue(binding.iterable, state, lines);
    const std::string indexValue = state.temp("iter.index");
    lines.push_back("  " + indexValue + " = load i64, ptr " + iterState.indexSlot + ", align 8");

    const std::string pointerValue = extractPointer(iterableValue, binding.iterable.type, state, lines);
    const std::string elementPtr = state.temp("iter.item.ptr");
    lines.push_back(
        "  " + elementPtr + " = getelementptr inbounds " + llvmType(binding.itemType) + ", ptr " + pointerValue +
        ", i64 " + indexValue);
    const std::string loadedItem = state.temp("iter.item");
    lines.push_back(
        "  " + loadedItem + " = load " + llvmType(binding.itemType) + ", ptr " + elementPtr +
        ", align " + std::to_string(layout->align));
    lines.push_back(
        "  store " + llvmType(binding.itemType) + " " + loadedItem + ", ptr " + iterState.itemSlot +
        ", align " + std::to_string(layout->align));

    const std::string nextIndex = state.temp("iter.next.index");
    lines.push_back("  " + nextIndex + " = add i64 " + indexValue + ", 1");
    lines.push_back("  store i64 " + nextIndex + ", ptr " + iterState.indexSlot + ", align 8");

    state.namedAddresses[binding.itemName] = iterState.itemSlot;
    state.namedValues.erase(binding.itemName);
    state.params.erase(binding.itemName);
    state.valueTypes[binding.itemName] = binding.itemType;
}

void LlvmEmitter::materializePendingBlockBindings(
    std::string_view blockLabel,
    FunctionState& state,
    std::vector<std::string>& lines) {
    if (const auto it = state.pendingIterBindings.find(std::string(blockLabel)); it != state.pendingIterBindings.end()) {
        for (const auto& binding : it->second) {
            materializeIterBinding(binding, state, lines);
        }
    }

    const auto it = state.pendingChoiceBindings.find(std::string(blockLabel));
    if (it == state.pendingChoiceBindings.end()) {
        return;
    }

    for (const auto& binding : it->second) {
        materializeChoiceBinding(binding, state, lines);
    }
}

void LlvmEmitter::constructChoiceValueAtAddress(
    std::string_view address,
    const std::string& typeText,
    std::string_view variantName,
    const std::vector<LirValue>& payloads,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const auto choice = resolveChoiceType(typeText);
    if (!choice.has_value()) {
        throw std::runtime_error("LLVM lowering could not resolve choice construction type '" + typeText + "'.");
    }

    const auto caseIt = std::find_if(choice->cases.begin(), choice->cases.end(), [&](const ConcreteChoiceCaseInfo& item) {
        return sameBackendIdentifier(item.name, variantName);
    });
    if (caseIt == choice->cases.end()) {
        throw std::runtime_error("LLVM lowering could not resolve choice constructor variant '" + std::string(variantName) + "'.");
    }
    if (payloads.size() != caseIt->payloadTypes.size()) {
        throw std::runtime_error("LLVM lowering found mismatched payload count for choice constructor '" + std::string(variantName) + "'.");
    }

    lines.push_back(
        "  store " + choice->llvmTypeText + " zeroinitializer, ptr " + std::string(address) +
        ", align " + std::to_string(choice->alignBytes));

    const size_t caseIndex = static_cast<size_t>(std::distance(choice->cases.begin(), caseIt));
    const std::string tagPtr = state.temp("choice.tag.ptr");
    lines.push_back(
        "  " + tagPtr + " = getelementptr inbounds " + choice->llvmTypeText + ", ptr " + std::string(address) +
        ", i32 0, i32 0");
    lines.push_back("  store i32 " + std::to_string(caseIndex) + ", ptr " + tagPtr + ", align " + std::to_string(choice->tagSizeBytes));

    if (payloads.empty()) {
        return;
    }

    if (choice->payloadSizeBytes == 0) {
        throw std::runtime_error("LLVM lowering expected payload storage for constructor variant '" + std::string(variantName) + "'.");
    }

    const size_t paddingBytes = choice->payloadOffsetBytes > choice->tagSizeBytes
        ? choice->payloadOffsetBytes - choice->tagSizeBytes
        : 0;
    const int payloadFieldIndex = paddingBytes > 0 ? 2 : 1;
    const std::string payloadArrayType = "[" + std::to_string(choice->payloadSizeBytes) + " x i8]";
    const std::string payloadSlot = state.temp("choice.payload.slot");
    lines.push_back(
        "  " + payloadSlot + " = getelementptr inbounds " + choice->llvmTypeText + ", ptr " + std::string(address) +
        ", i32 0, i32 " + std::to_string(payloadFieldIndex));
    const std::string payloadBase = state.temp("choice.payload.base");
    lines.push_back("  " + payloadBase + " = getelementptr inbounds " + payloadArrayType + ", ptr " + payloadSlot + ", i32 0, i64 0");

    for (size_t i = 0; i < payloads.size(); ++i) {
        const auto& layoutField = caseIt->layout.payloadFields[i];
        const std::string fieldPtr = state.temp("choice.payload.field.ptr");
        lines.push_back(
            "  " + fieldPtr + " = getelementptr inbounds i8, ptr " + payloadBase +
            ", i64 " + std::to_string(layoutField.offsetBytes));
        const std::string operand = ensureValue(payloads[i], state, lines);
        storeValueToAddress(fieldPtr, caseIt->payloadTypes[i], operand, layoutField.alignBytes, lines);
    }
}

void LlvmEmitter::emitChoiceMakeInst(const LirChoiceMakeInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const auto layout = abiLayoutForType(value.type);
    const size_t align = layout.has_value() ? layout->align : 1;
    const std::string resultAddr = state.temp(value.result + ".choice.addr");
    lines.push_back("  " + resultAddr + " = alloca " + llvmType(value.type) + ", align " + std::to_string(align));
    constructChoiceValueAtAddress(resultAddr, value.type, value.variantName, value.payloads, state, lines);
    state.namedAddresses[value.result] = resultAddr;
    state.namedValues.erase(value.result);
    state.params.erase(value.result);
    state.valueTypes[value.result] = value.type;
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

std::optional<std::string> LlvmEmitter::tryAddressOfValue(
    const LirValue& value,
    FunctionState& state,
    std::vector<std::string>&) {
    if (const auto it = state.stackPointers.find(value.text); it != state.stackPointers.end()) {
        return it->second;
    }
    if (const auto it = state.namedAddresses.find(value.text); it != state.namedAddresses.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string LlvmEmitter::ensureAddress(
    const LirValue& value,
    FunctionState& state,
    std::vector<std::string>& lines) {
    if (const auto existing = tryAddressOfValue(value, state, lines); existing.has_value()) {
        return *existing;
    }

    const std::string tempAddr = state.temp("addr");
    const std::string valueType = llvmType(value.type);
    const auto layout = abiLayoutForType(value.type);
    const size_t align = layout.has_value() ? layout->align : 1;
    lines.push_back("  " + tempAddr + " = alloca " + valueType + ", align " + std::to_string(align));
    const std::string operand = ensureValue(value, state, lines);
    lines.push_back("  store " + valueType + " " + operand + ", ptr " + tempAddr + ", align " + std::to_string(align));

    if (!value.text.empty() && !isStringLiteral(value.text) && value.text != "true" && value.text != "false" && !isIntegerLiteral(value.text)) {
        state.namedAddresses[value.text] = tempAddr;
        state.namedValues.erase(value.text);
        state.params.erase(value.text);
    }
    return tempAddr;
}

const LayoutFieldInfo* LlvmEmitter::findShapeFieldLayout(std::string_view typeText, std::string_view field) const {
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(typeText)));
    const auto shapeIt = shapesByName.find(baseType);
    if (shapeIt == shapesByName.end() || !shapeIt->second->layout.has_value()) {
        return nullptr;
    }
    for (const auto& item : shapeIt->second->layout->fields) {
        if (item.name == field) {
            return &item;
        }
    }
    return nullptr;
}

size_t LlvmEmitter::findShapeFieldIndex(std::string_view typeText, std::string_view field) const {
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(typeText)));
    const auto indexMapIt = shapeFieldIndices.find(baseType);
    if (indexMapIt == shapeFieldIndices.end()) {
        throw std::runtime_error("LLVM lowering only supports field access on concrete shapes, got '" + std::string(typeText) + "'.");
    }
    const auto indexIt = indexMapIt->second.find(std::string(field));
    if (indexIt == indexMapIt->second.end()) {
        throw std::runtime_error("Unknown field '" + std::string(field) + "' during LLVM lowering.");
    }
    return indexIt->second;
}

std::string LlvmEmitter::loadValueFromAddress(
    std::string_view address,
    const std::string& typeText,
    size_t align,
    FunctionState& state,
    std::vector<std::string>& lines) const {
    const std::string loadReg = state.temp("load");
    std::string line = "  " + loadReg + " = load " + llvmType(typeText) + ", ptr " + std::string(address);
    if (align > 0) {
        line += ", align " + std::to_string(align);
    }
    lines.push_back(std::move(line));
    return loadReg;
}

void LlvmEmitter::storeValueToAddress(
    std::string_view address,
    const std::string& typeText,
    const std::string& operand,
    size_t align,
    std::vector<std::string>& lines) const {
    std::string line = "  store " + llvmType(typeText) + " " + operand + ", ptr " + std::string(address);
    if (align > 0) {
        line += ", align " + std::to_string(align);
    }
    lines.push_back(std::move(line));
}

std::string LlvmEmitter::emitFieldAddress(
    const LirValue& object,
    std::string_view field,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const auto* layoutField = findShapeFieldLayout(object.type, field);
    if (layoutField == nullptr) {
        throw std::runtime_error("LLVM lowering only supports field access on concrete shapes, got '" + object.type + "'.");
    }

    const size_t fieldIndex = findShapeFieldIndex(object.type, field);
    const std::string objectAddress = ensureAddress(object, state, lines);
    const std::string fieldPtr = state.temp("field.ptr");
    lines.push_back(
        "  " + fieldPtr + " = getelementptr inbounds " + llvmType(object.type) + ", ptr " + objectAddress +
        ", i32 0, i32 " + std::to_string(fieldIndex));
    return fieldPtr;
}

std::string LlvmEmitter::extractLength(
    const std::string& aggregateValue,
    std::string_view receiverType,
    FunctionState& state,
    std::vector<std::string>& lines) {
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(receiverType)));
    if (baseType == "Str" || baseType == "Span") {
        const std::string lengthReg = state.temp("str.len");
        lines.push_back("  " + lengthReg + " = extractvalue %claw.slice " + aggregateValue + ", 1");
        return lengthReg;
    }
    if (baseType == "Vec") {
        const std::string lengthReg = state.temp("vec.len");
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
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(receiverType)));
    if (baseType == "Str" || baseType == "Span") {
        const std::string pointerReg = state.temp("str.ptr");
        lines.push_back("  " + pointerReg + " = extractvalue %claw.slice " + aggregateValue + ", 0");
        return pointerReg;
    }
    if (baseType == "Vec") {
        const std::string pointerReg = state.temp("vec.ptr");
        lines.push_back("  " + pointerReg + " = extractvalue %claw.buffer " + aggregateValue + ", 0");
        return pointerReg;
    }
    throw std::runtime_error("LLVM lowering does not yet support pointer extraction for receiver type '" + std::string(receiverType) + "'.");
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

    const auto layout = abiLayoutForType(value.type);
    const size_t align = layout.has_value() ? layout->align : 0;

    if (const auto it = state.stackPointers.find(value.text); it != state.stackPointers.end()) {
        return loadValueFromAddress(it->second, value.type, align, state, lines);
    }

    if (const auto it = state.namedValues.find(value.text); it != state.namedValues.end()) {
        return it->second;
    }

    if (const auto it = state.namedAddresses.find(value.text); it != state.namedAddresses.end()) {
        return loadValueFromAddress(it->second, value.type, align, state, lines);
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

} // namespace claw::frontend
