; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
%"revise_view_shape_scope::Parser" = type { %claw.slice, %claw.slice }

@.str.0 = private unnamed_addr constant [6 x i8] c"hello\00"

declare void @"claw.runtime.println.slice"(ptr)

define internal void @"revise_view_shape_scope::main"() {
entry:
  %source.addr = alloca %claw.slice, align 8
  %str.ptr.0 = getelementptr inbounds [6 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.1 = insertvalue %claw.slice poison, ptr %str.ptr.0, 0
  %str.slice.2 = insertvalue %claw.slice %str.slice.1, i64 5, 1
  store %claw.slice %str.slice.2, ptr %source.addr, align 8
  br label %scope_s_0
scope_s_0:
  %t0.addr = alloca %"revise_view_shape_scope::Parser", align 8
  %field.ptr.3 = getelementptr inbounds %"revise_view_shape_scope::Parser", ptr %t0.addr, i32 0, i32 0
  %load.4 = load %claw.slice, ptr %source.addr, align 8
  store %claw.slice %load.4, ptr %field.ptr.3, align 8
  %field.ptr.5 = getelementptr inbounds %"revise_view_shape_scope::Parser", ptr %t0.addr, i32 0, i32 1
  %load.6 = load %claw.slice, ptr %source.addr, align 8
  store %claw.slice %load.6, ptr %field.ptr.5, align 8
  %parser.addr = alloca %"revise_view_shape_scope::Parser", align 8
  %load.7 = load %"revise_view_shape_scope::Parser", ptr %t0.addr, align 8
  store %"revise_view_shape_scope::Parser" %load.7, ptr %parser.addr, align 8
  %field.ptr.8 = getelementptr inbounds %"revise_view_shape_scope::Parser", ptr %parser.addr, i32 0, i32 1
  %t1 = load %claw.slice, ptr %field.ptr.8, align 8
  %addr.9 = alloca %claw.slice, align 8
  store %claw.slice %t1, ptr %addr.9, align 8
  call void @"claw.runtime.println.slice"(ptr %addr.9)
  ; drop parser : Parser[s]
  br label %scope_cont_1
scope_cont_1:
  ; drop source : Str
  ret void
}
