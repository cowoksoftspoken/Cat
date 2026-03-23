#pragma once

#include "ir/oir.h"
#include "module/project.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace claw::driver {

std::filesystem::path defaultNativeOutputPath(const claw::frontend::LoadedProject& project);

std::filesystem::path buildNativeExecutable(
    const claw::frontend::LoadedProject& project,
    std::string_view entryRealm,
    const std::vector<claw::frontend::OirUnitView>& units,
    const std::filesystem::path& compilerBinaryPath,
    const std::filesystem::path& outputPath);

} // namespace claw::driver
