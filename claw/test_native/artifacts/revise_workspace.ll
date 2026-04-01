; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
declare void @"claw.runtime.println.i32"(i32)

define internal i32 @"main::square"(i32 %arg.value) {
entry:
  %t0 = mul i32 %arg.value, %arg.value
  ret i32 %t0
}

define internal void @"main::main"() {
entry:
  %t0 = call i32 @"main::square"(i32 6)
  call void @"claw.runtime.println.i32"(i32 %t0)
  ret void
}

define i32 @main() {
entry:
  call void @"main::main"()
  ret i32 0
}
