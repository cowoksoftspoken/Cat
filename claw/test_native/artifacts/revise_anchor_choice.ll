; ModuleID = 'claw'
target triple = "x86_64-w64-windows-gnu"

%claw.slice = type { ptr, i64 }
%claw.buffer = type { ptr, i64, i64 }
%"revise_anchor_choice::StableText" = type { i32, [4 x i8], [8 x i8] }

@.str.1 = private unnamed_addr constant [7 x i8] c"choice\00"

@.str.0 = private unnamed_addr constant [5 x i8] c"none\00"

declare void @"claw.runtime.println.slice"(ptr)
declare void @"claw.runtime.anchor.free"(ptr)
declare ptr @"claw.runtime.anchor.alloc"(i64, i64)
declare void @llvm.trap()

define internal void @"revise_anchor_choice::print_value"(%"revise_anchor_choice::StableText" %arg.value) {
entry:
  %choice.tag.0 = extractvalue %"revise_anchor_choice::StableText" %arg.value, 0
  switch i32 %choice.tag.0, label %entry.defect.pick.0 [
    i32 0, label %pick_Some_1
    i32 1, label %pick_None_2
  ]
pick_Some_1:
  %choice.case.addr.1 = alloca %"revise_anchor_choice::StableText", align 8
  store %"revise_anchor_choice::StableText" %arg.value, ptr %choice.case.addr.1, align 8
  %choice.payload.slot.2 = getelementptr inbounds %"revise_anchor_choice::StableText", ptr %choice.case.addr.1, i32 0, i32 2
  %choice.payload.base.3 = getelementptr inbounds [8 x i8], ptr %choice.payload.slot.2, i32 0, i64 0
  %choice.payload.field.ptr.4 = getelementptr inbounds i8, ptr %choice.payload.base.3, i64 0
  %choice.payload.field.5 = load ptr, ptr %choice.payload.field.ptr.4, align 8
  call void @"claw.runtime.println.slice"(ptr %choice.payload.field.5)
  call void @"claw.runtime.anchor.free"(ptr %choice.payload.field.5)
  br label %pick_join_0
pick_None_2:
  %addr.6 = alloca %claw.slice, align 8
  %str.ptr.7 = getelementptr inbounds [5 x i8], ptr @.str.0, i64 0, i64 0
  %str.slice.8 = insertvalue %claw.slice poison, ptr %str.ptr.7, 0
  %str.slice.9 = insertvalue %claw.slice %str.slice.8, i64 4, 1
  store %claw.slice %str.slice.9, ptr %addr.6, align 8
  call void @"claw.runtime.println.slice"(ptr %addr.6)
  br label %pick_join_0
pick_join_0:
  ; phi %phi0 omitted in initial LLVM lowering
  ; drop value : StableText
  ret void
entry.defect.pick.0:
  ; defect unreachable_choice "switch reached no matching case"
  call void @llvm.trap()
  unreachable
}

define internal void @"revise_anchor_choice::main"() {
entry:
  %t0 = call ptr @"claw.runtime.anchor.alloc"(i64 16, i64 8)
  %str.ptr.0 = getelementptr inbounds [7 x i8], ptr @.str.1, i64 0, i64 0
  %str.slice.1 = insertvalue %claw.slice poison, ptr %str.ptr.0, 0
  %str.slice.2 = insertvalue %claw.slice %str.slice.1, i64 6, 1
  store %claw.slice %str.slice.2, ptr %t0, align 8
  %t1.choice.addr.3 = alloca %"revise_anchor_choice::StableText", align 8
  store %"revise_anchor_choice::StableText" zeroinitializer, ptr %t1.choice.addr.3, align 8
  %choice.tag.ptr.4 = getelementptr inbounds %"revise_anchor_choice::StableText", ptr %t1.choice.addr.3, i32 0, i32 0
  store i32 0, ptr %choice.tag.ptr.4, align 4
  %choice.payload.slot.5 = getelementptr inbounds %"revise_anchor_choice::StableText", ptr %t1.choice.addr.3, i32 0, i32 2
  %choice.payload.base.6 = getelementptr inbounds [8 x i8], ptr %choice.payload.slot.5, i32 0, i64 0
  %choice.payload.field.ptr.7 = getelementptr inbounds i8, ptr %choice.payload.base.6, i64 0
  store ptr %t0, ptr %choice.payload.field.ptr.7, align 8
  %load.8 = load %"revise_anchor_choice::StableText", ptr %t1.choice.addr.3, align 8
  call void @"revise_anchor_choice::print_value"(%"revise_anchor_choice::StableText" %load.8)
  %t2.choice.addr.9 = alloca %"revise_anchor_choice::StableText", align 8
  store %"revise_anchor_choice::StableText" zeroinitializer, ptr %t2.choice.addr.9, align 8
  %choice.tag.ptr.10 = getelementptr inbounds %"revise_anchor_choice::StableText", ptr %t2.choice.addr.9, i32 0, i32 0
  store i32 1, ptr %choice.tag.ptr.10, align 4
  %load.11 = load %"revise_anchor_choice::StableText", ptr %t2.choice.addr.9, align 8
  call void @"revise_anchor_choice::print_value"(%"revise_anchor_choice::StableText" %load.11)
  ret void
}

define i32 @main() {
entry:
  call void @"revise_anchor_choice::main"()
  ret i32 0
}
