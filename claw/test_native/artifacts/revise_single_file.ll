; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
declare void @"claw.runtime.println.i32"(i32)

define internal i32 @"revise_single_file::sum"(i32 %arg.left, i32 %arg.right) {
entry:
  %t0 = add i32 %arg.left, %arg.right
  ret i32 %t0
}

define internal i32 @"revise_single_file::main"() {
entry:
  %t0 = call i32 @"revise_single_file::sum"(i32 1, i32 2)
  call void @"claw.runtime.println.i32"(i32 %t0)
  ret i32 0
}

define i32 @main() {
entry:
  %entry.result.0 = call i32 @"revise_single_file::main"()
  ret i32 %entry.result.0
}
