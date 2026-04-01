; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
define internal i32 @"revise_exit_code::main"() {
entry:
  ret i32 7
}

define i32 @main() {
entry:
  %entry.result.0 = call i32 @"revise_exit_code::main"()
  ret i32 %entry.result.0
}
