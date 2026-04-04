cmake_minimum_required(VERSION 3.20)
project(claw CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(claw_frontend
    src/diagnostics/diagnostics.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/ir/air.cpp
    src/ir/oir.cpp
    src/ir/lir.cpp
    src/codegen/llvm.cpp
    src/codegen/llvm_types.cpp
    src/codegen/llvm_values.cpp
    src/codegen/llvm_control.cpp
    src/codegen/llvm_emit.cpp
    src/analysis/types.cpp
    src/analysis/scope.cpp
    src/analysis/sema.cpp
    src/analysis/ownership.cpp
    src/workspace/workspace.cpp
    src/workspace/config.cpp
)
target_include_directories(claw_frontend PUBLIC src)

add_executable(claw
    src/driver/main.cpp
    src/driver/native_build.cpp
)
target_link_libraries(claw PRIVATE claw_frontend)



