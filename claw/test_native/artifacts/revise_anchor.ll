; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
@.str.0 = private unnamed_addr constant [6 x i8] c"hello\00"

declare ptr @"claw.runtime.anchor.alloc"(i64)
declare void @"claw.runtime.println.slice"(ptr)
declare void @"claw.runtime.anchor.free"(ptr)

define internal void @"revise_anchor::main"() {
entry:
  %anchor.alloc.0 = call ptr @"claw.runtime.anchor.alloc"(i64 16)
  %str.ptr.1 = getelementptr inbounds [6 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.2 = insertvalue %claw.slice poison, ptr %str.ptr.1, 0
  %str.slice.3 = insertvalue %claw.slice %str.slice.2, i64 5, 1
  store %claw.slice %str.slice.3, ptr %anchor.alloc.0, align 8
  %stable.addr = alloca ptr, align 8
  store ptr %anchor.alloc.0, ptr %stable.addr, align 8
  %load.4 = load ptr, ptr %stable.addr, align 8
  call void @"claw.runtime.println.slice"(ptr %load.4)
  %load.5 = load ptr, ptr %stable.addr, align 8
  call void @"claw.runtime.anchor.free"(ptr %load.5)
  ret void
}

define i32 @main() {
entry:
  call void @"revise_anchor::main"()
  ret i32 0
}
