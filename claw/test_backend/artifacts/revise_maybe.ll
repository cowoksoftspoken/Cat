; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
@.str.1 = private unnamed_addr constant [3 x i8] c"C@\00"

@.str.0 = private unnamed_addr constant [6 x i8] c"world\00"

declare void @"claw.runtime.println.i32"(i32)
declare void @"claw.runtime.println.slice"(ptr)
declare void @llvm.trap()

define internal %claw.slice @"revise_maybe::greet"(ptr %arg.name.addr) {
entry:
  %load.0 = load { i32, [4 x i8], [16 x i8] }, ptr %arg.name.addr, align 8
  %choice.tag.1 = extractvalue { i32, [4 x i8], [16 x i8] } %load.0, 0
  switch i32 %choice.tag.1, label %entry.defect.pick.0 [
    i32 1, label %pick_Some_0
    i32 0, label %pick_None_1
  ]
pick_Some_0:
  %load.2 = load { i32, [4 x i8], [16 x i8] }, ptr %arg.name.addr, align 8
  %choice.case.addr.3 = alloca { i32, [4 x i8], [16 x i8] }, align 8
  store { i32, [4 x i8], [16 x i8] } %load.2, ptr %choice.case.addr.3, align 8
  %choice.payload.slot.4 = getelementptr inbounds { i32, [4 x i8], [16 x i8] }, ptr %choice.case.addr.3, i32 0, i32 2
  %choice.payload.base.5 = getelementptr inbounds [16 x i8], ptr %choice.payload.slot.4, i32 0, i64 0
  %choice.payload.field.ptr.6 = getelementptr inbounds i8, ptr %choice.payload.base.5, i64 0
  %choice.payload.field.7 = load %claw.slice, ptr %choice.payload.field.ptr.6, align 8
  ; drop name : Maybe[Str]
  ret %claw.slice %choice.payload.field.7
pick_None_1:
  ; drop name : Maybe[Str]
  %str.ptr.8 = getelementptr inbounds [6 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.9 = insertvalue %claw.slice poison, ptr %str.ptr.8, 0
  %str.slice.10 = insertvalue %claw.slice %str.slice.9, i64 5, 1
  ret %claw.slice %str.slice.10
entry.defect.pick.0:
  ; defect unreachable_choice "switch reached no matching case"
  call void @llvm.trap()
  unreachable
}

define internal i32 @"revise_maybe::add_one"({ i32, [4 x i8] } %arg.value) {
entry:
  %choice.tag.0 = extractvalue { i32, [4 x i8] } %arg.value, 0
  switch i32 %choice.tag.0, label %entry.defect.pick.0 [
    i32 1, label %pick_Some_0
    i32 0, label %pick_None_1
  ]
pick_Some_0:
  %choice.case.addr.1 = alloca { i32, [4 x i8] }, align 4
  store { i32, [4 x i8] } %arg.value, ptr %choice.case.addr.1, align 4
  %choice.payload.slot.2 = getelementptr inbounds { i32, [4 x i8] }, ptr %choice.case.addr.1, i32 0, i32 1
  %choice.payload.base.3 = getelementptr inbounds [4 x i8], ptr %choice.payload.slot.2, i32 0, i64 0
  %choice.payload.field.ptr.4 = getelementptr inbounds i8, ptr %choice.payload.base.3, i64 0
  %choice.payload.field.5 = load i32, ptr %choice.payload.field.ptr.4, align 4
  %t0 = add i32 %choice.payload.field.5, 1
  ; drop value : Maybe[Int32]
  ret i32 %t0
pick_None_1:
  ; drop value : Maybe[Int32]
  ret i32 0
entry.defect.pick.0:
  ; defect unreachable_choice "switch reached no matching case"
  call void @llvm.trap()
  unreachable
}

define internal void @"revise_maybe::main"() {
entry:
  %t0.choice.addr.0 = alloca { i32, [4 x i8] }, align 4
  store { i32, [4 x i8] } zeroinitializer, ptr %t0.choice.addr.0, align 4
  %choice.tag.ptr.1 = getelementptr inbounds { i32, [4 x i8] }, ptr %t0.choice.addr.0, i32 0, i32 0
  store i32 1, ptr %choice.tag.ptr.1, align 4
  %choice.payload.slot.2 = getelementptr inbounds { i32, [4 x i8] }, ptr %t0.choice.addr.0, i32 0, i32 1
  %choice.payload.base.3 = getelementptr inbounds [4 x i8], ptr %choice.payload.slot.2, i32 0, i64 0
  %choice.payload.field.ptr.4 = getelementptr inbounds i8, ptr %choice.payload.base.3, i64 0
  store i32 41, ptr %choice.payload.field.ptr.4, align 4
  %a.addr = alloca { i32, [4 x i8] }, align 4
  %load.5 = load { i32, [4 x i8] }, ptr %t0.choice.addr.0, align 4
  store { i32, [4 x i8] } %load.5, ptr %a.addr, align 4
  %t1.choice.addr.6 = alloca { i32, [4 x i8] }, align 4
  store { i32, [4 x i8] } zeroinitializer, ptr %t1.choice.addr.6, align 4
  %choice.tag.ptr.7 = getelementptr inbounds { i32, [4 x i8] }, ptr %t1.choice.addr.6, i32 0, i32 0
  store i32 0, ptr %choice.tag.ptr.7, align 4
  %b.addr = alloca { i32, [4 x i8] }, align 4
  %load.8 = load { i32, [4 x i8] }, ptr %t1.choice.addr.6, align 4
  store { i32, [4 x i8] } %load.8, ptr %b.addr, align 4
  %load.9 = load { i32, [4 x i8] }, ptr %a.addr, align 4
  %t2 = call i32 @"revise_maybe::add_one"({ i32, [4 x i8] } %load.9)
  call void @"claw.runtime.println.i32"(i32 %t2)
  %load.10 = load { i32, [4 x i8] }, ptr %b.addr, align 4
  %t3 = call i32 @"revise_maybe::add_one"({ i32, [4 x i8] } %load.10)
  call void @"claw.runtime.println.i32"(i32 %t3)
  %t4.choice.addr.11 = alloca { i32, [4 x i8], [16 x i8] }, align 8
  store { i32, [4 x i8], [16 x i8] } zeroinitializer, ptr %t4.choice.addr.11, align 8
  %choice.tag.ptr.12 = getelementptr inbounds { i32, [4 x i8], [16 x i8] }, ptr %t4.choice.addr.11, i32 0, i32 0
  store i32 1, ptr %choice.tag.ptr.12, align 4
  %choice.payload.slot.13 = getelementptr inbounds { i32, [4 x i8], [16 x i8] }, ptr %t4.choice.addr.11, i32 0, i32 2
  %choice.payload.base.14 = getelementptr inbounds [16 x i8], ptr %choice.payload.slot.13, i32 0, i64 0
  %choice.payload.field.ptr.15 = getelementptr inbounds i8, ptr %choice.payload.base.14, i64 0
  %str.ptr.16 = getelementptr inbounds [3 x i8], ptr @.str.1, i64 0, i64 0
  %str.slice.17 = insertvalue %claw.slice poison, ptr %str.ptr.16, 0
  %str.slice.18 = insertvalue %claw.slice %str.slice.17, i64 2, 1
  store %claw.slice %str.slice.18, ptr %choice.payload.field.ptr.15, align 8
  %t5 = call %claw.slice @"revise_maybe::greet"(ptr %t4.choice.addr.11)
  %addr.19 = alloca %claw.slice, align 8
  store %claw.slice %t5, ptr %addr.19, align 8
  call void @"claw.runtime.println.slice"(ptr %addr.19)
  %t6.choice.addr.20 = alloca { i32, [4 x i8], [16 x i8] }, align 8
  store { i32, [4 x i8], [16 x i8] } zeroinitializer, ptr %t6.choice.addr.20, align 8
  %choice.tag.ptr.21 = getelementptr inbounds { i32, [4 x i8], [16 x i8] }, ptr %t6.choice.addr.20, i32 0, i32 0
  store i32 0, ptr %choice.tag.ptr.21, align 4
  %t7 = call %claw.slice @"revise_maybe::greet"(ptr %t6.choice.addr.20)
  %addr.22 = alloca %claw.slice, align 8
  store %claw.slice %t7, ptr %addr.22, align 8
  call void @"claw.runtime.println.slice"(ptr %addr.22)
  ret void
}
