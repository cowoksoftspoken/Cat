; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
@.str.0 = private unnamed_addr constant [6 x i8] c"scope\00"

declare void @"claw.runtime.println.slice"(ptr)

define internal %claw.slice @"revise_scope_refs::identity"(%claw.slice %arg.input) {
entry:
  ret %claw.slice %arg.input
}

define internal void @"revise_scope_refs::main"() {
entry:
  %source.addr = alloca %claw.slice, align 8
  %str.ptr.0 = getelementptr inbounds [6 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.1 = insertvalue %claw.slice poison, ptr %str.ptr.0, 0
  %str.slice.2 = insertvalue %claw.slice %str.slice.1, i64 5, 1
  store %claw.slice %str.slice.2, ptr %source.addr, align 8
  br label %scope_s_0
scope_s_0:
  %view.addr = alloca %claw.slice, align 8
  %load.3 = load %claw.slice, ptr %source.addr, align 8
  store %claw.slice %load.3, ptr %view.addr, align 8
  %load.4 = load %claw.slice, ptr %view.addr, align 8
  %t0 = call %claw.slice @"revise_scope_refs::identity"(%claw.slice %load.4)
  %again.addr = alloca %claw.slice, align 8
  store %claw.slice %t0, ptr %again.addr, align 8
  call void @"claw.runtime.println.slice"(ptr %again.addr)
  br label %scope_cont_1
scope_cont_1:
  ; drop source : Str
  ret void
}

define i32 @main() {
entry:
  call void @"revise_scope_refs::main"()
  ret i32 0
}
