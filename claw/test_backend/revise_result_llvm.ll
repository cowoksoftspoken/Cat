; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
@.str.0 = private unnamed_addr constant [4 x i8] c"bad\00"

declare void @"claw.runtime.println.i32"(i32)
declare void @"claw.runtime.println.slice"(ptr)

define internal void @"revise_result_llvm::step"(ptr %ret.slot, i1 %arg.flag) {
entry:
  br i1 %arg.flag, label %when_then_0, label %when_join_1
when_then_0:
  %t0.choice.addr.0 = alloca { i32, [4 x i8], [16 x i8] }, align 8
  store { i32, [4 x i8], [16 x i8] } zeroinitializer, ptr %t0.choice.addr.0, align 8    
  %choice.tag.ptr.1 = getelementptr inbounds { i32, [4 x i8], [16 x i8] }, ptr %t0.choice.addr.0, i32 0, i32 0
  store i32 0, ptr %choice.tag.ptr.1, align 4
  %choice.payload.slot.2 = getelementptr inbounds { i32, [4 x i8], [16 x i8] }, ptr %t0.choice.addr.0, i32 0, i32 2
  %choice.payload.base.3 = getelementptr inbounds [16 x i8], ptr %choice.payload.slot.2, i32 0, i64 0
  %choice.payload.field.ptr.4 = getelementptr inbounds i8, ptr %choice.payload.base.3, i64 0
  store i32 9, ptr %choice.payload.field.ptr.4, align 4
  %load.5 = load { i32, [4 x i8], [16 x i8] }, ptr %t0.choice.addr.0, align 8
  store { i32, [4 x i8], [16 x i8] } %load.5, ptr %ret.slot, align 8
  ret void
when_join_1:
  %t1.choice.addr.6 = alloca { i32, [4 x i8], [16 x i8] }, align 8
  store { i32, [4 x i8], [16 x i8] } zeroinitializer, ptr %t1.choice.addr.6, align 8    
  %choice.tag.ptr.7 = getelementptr inbounds { i32, [4 x i8], [16 x i8] }, ptr %t1.choice.addr.6, i32 0, i32 0
  store i32 1, ptr %choice.tag.ptr.7, align 4
  %choice.payload.slot.8 = getelementptr inbounds { i32, [4 x i8], [16 x i8] }, ptr %t1.choice.addr.6, i32 0, i32 2
  %choice.payload.base.9 = getelementptr inbounds [16 x i8], ptr %choice.payload.slot.8, i32 0, i64 0
  %choice.payload.field.ptr.10 = getelementptr inbounds i8, ptr %choice.payload.base.9, 
i64 0
  %str.ptr.11 = getelementptr inbounds [4 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.12 = insertvalue %claw.slice poison, ptr %str.ptr.11, 0
  %str.slice.13 = insertvalue %claw.slice %str.slice.12, i64 3, 1
  store %claw.slice %str.slice.13, ptr %choice.payload.field.ptr.10, align 8
  %load.14 = load { i32, [4 x i8], [16 x i8] }, ptr %t1.choice.addr.6, align 8
  store { i32, [4 x i8], [16 x i8] } %load.14, ptr %ret.slot, align 8
  ret void
}

define internal void @"revise_result_llvm::show"(i1 %arg.flag) {
entry:
  %call.ret.addr.0 = alloca { i32, [4 x i8], [16 x i8] }, align 8
  call void @"revise_result_llvm::step"(ptr %call.ret.addr.0, i1 %arg.flag)
  %load.1 = load { i32, [4 x i8], [16 x i8] }, ptr %call.ret.addr.0, align 8
  %lift.tag.2 = extractvalue { i32, [4 x i8], [16 x i8] } %load.1, 0
  %lift.is_ok.3 = icmp eq i32 %lift.tag.2, 0
  br i1 %lift.is_ok.3, label %entry.lift_ok.0, label %try_fail_0
entry.lift_ok.0:
  %load.4 = load { i32, [4 x i8], [16 x i8] }, ptr %call.ret.addr.0, align 8
  %choice.case.addr.5 = alloca { i32, [4 x i8], [16 x i8] }, align 8
  store { i32, [4 x i8], [16 x i8] } %load.4, ptr %choice.case.addr.5, align 8
  %choice.payload.slot.6 = getelementptr inbounds { i32, [4 x i8], [16 x i8] }, ptr %choice.case.addr.5, i32 0, i32 2
  %choice.payload.base.7 = getelementptr inbounds [16 x i8], ptr %choice.payload.slot.6, i32 0, i64 0
  %choice.payload.field.ptr.8 = getelementptr inbounds i8, ptr %choice.payload.base.7, i64 0
  %choice.payload.field.9 = load i32, ptr %choice.payload.field.ptr.8, align 4
  call void @"claw.runtime.println.i32"(i32 %choice.payload.field.9)
  ret void
try_fail_0:
  %load.10 = load { i32, [4 x i8], [16 x i8] }, ptr %call.ret.addr.0, align 8
  %choice.case.addr.11 = alloca { i32, [4 x i8], [16 x i8] }, align 8
  store { i32, [4 x i8], [16 x i8] } %load.10, ptr %choice.case.addr.11, align 8        
  %choice.payload.slot.12 = getelementptr inbounds { i32, [4 x i8], [16 x i8] }, ptr %choice.case.addr.11, i32 0, i32 2
  %choice.payload.base.13 = getelementptr inbounds [16 x i8], ptr %choice.payload.slot.12, i32 0, i64 0
  %choice.payload.field.ptr.14 = getelementptr inbounds i8, ptr %choice.payload.base.13, i64 0
  %choice.payload.field.15 = load %claw.slice, ptr %choice.payload.field.ptr.14, align 8  %addr.16 = alloca %claw.slice, align 8
  store %claw.slice %choice.payload.field.15, ptr %addr.16, align 8
  call void @"claw.runtime.println.slice"(ptr %addr.16)
  ; drop err : Str
  ret void
}

define internal void @"revise_result_llvm::main"() {
entry:
  call void @"revise_result_llvm::show"(i1 true)
  ret void
}