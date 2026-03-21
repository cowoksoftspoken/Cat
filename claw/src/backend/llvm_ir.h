#pragma once

#include "ir/lir.h"

#include <string>
#include <string_view>
#include <vector>

namespace claw::frontend {

std::string emitLlvmIr(const LirProgram& program);
std::string emitLlvmIr(std::string_view entryRealm, const std::vector<OirUnitView>& units);

} // namespace claw::frontend
