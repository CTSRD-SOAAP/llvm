; RUN: llc < %s -mtriple=x86_64-apple-darwin -mcpu=corei7 -disable-fp-elim | FileCheck %s
;
; Note: Print verbose stackmaps using -debug-only=stackmaps.

; CHECK-LABEL:  .section  __LLVM_STACKMAPS,__llvm_stackmaps
; CHECK-NEXT:  __LLVM_StackMaps:
; CHECK-NEXT:   .long   0
; Num LargeConstants
; CHECK-NEXT:   .long   1
; CHECK-NEXT:   .quad   4294967296
; Num Callsites
; CHECK-NEXT:   .long   18

; Constant arguments
;
; CHECK-NEXT:   .quad   1
; CHECK-NEXT:   .long   L{{.*}}-_constantargs
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .short  4
; SmallConstant
; CHECK-NEXT:   .byte   4
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .long   65535
; SmallConstant
; CHECK-NEXT:   .byte   4
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .long   65536
; SmallConstant
; CHECK-NEXT:   .byte   4
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .long   -1
; LargeConstant at index 0
; CHECK-NEXT:   .byte   5
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .long   0

define void @constantargs() {
entry:
  %0 = inttoptr i64 12345 to i8*
  tail call void (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.void(i64 1, i32 15, i8* %0, i32 0, i64 65535, i64 65536, i64 4294967295, i64 4294967296)
  ret void
}

; Inline OSR Exit
;
; CHECK-LABEL:  .long   L{{.*}}-_osrinline
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .short  2
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long  0
define void @osrinline(i64 %a, i64 %b) {
entry:
  ; Runtime void->void call.
  call void inttoptr (i64 -559038737 to void ()*)()
  ; Followed by inline OSR patchpoint with 12-byte shadow and 2 live vars.
  call void (i64, i32, ...)* @llvm.experimental.stackmap(i64 3, i32 12, i64 %a, i64 %b)
  ret void
}

; Cold OSR Exit
;
; 2 live variables in register.
;
; CHECK-LABEL:  .long   L{{.*}}-_osrcold
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .short  2
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
define void @osrcold(i64 %a, i64 %b) {
entry:
  %test = icmp slt i64 %a, %b
  br i1 %test, label %ret, label %cold
cold:
  ; OSR patchpoint with 12-byte nop-slide and 2 live vars.
  %thunk = inttoptr i64 -559038737 to i8*
  call void (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.void(i64 4, i32 15, i8* %thunk, i32 0, i64 %a, i64 %b)
  unreachable
ret:
  ret void
}

; Property Read
; CHECK-LABEL:  .long   L{{.*}}-_propertyRead
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .short  2
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
define i64 @propertyRead(i64* %obj) {
entry:
  %resolveRead = inttoptr i64 -559038737 to i8*
  %result = call anyregcc i64 (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.i64(i64 5, i32 15, i8* %resolveRead, i32 1, i64* %obj)
  %add = add i64 %result, 3
  ret i64 %add
}

; Property Write
; CHECK-LABEL:  .long   L{{.*}}-_propertyWrite
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .short  2
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
define void @propertyWrite(i64 %dummy1, i64* %obj, i64 %dummy2, i64 %a) {
entry:
  %resolveWrite = inttoptr i64 -559038737 to i8*
  call anyregcc void (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.void(i64 6, i32 15, i8* %resolveWrite, i32 2, i64* %obj, i64 %a)
  ret void
}

; Void JS Call
;
; 2 live variables in registers.
;
; CHECK-LABEL:  .long   L{{.*}}-_jsVoidCall
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .short  2
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
define void @jsVoidCall(i64 %dummy1, i64* %obj, i64 %arg, i64 %l1, i64 %l2) {
entry:
  %resolveCall = inttoptr i64 -559038737 to i8*
  call void (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.void(i64 7, i32 15, i8* %resolveCall, i32 2, i64* %obj, i64 %arg, i64 %l1, i64 %l2)
  ret void
}

; i64 JS Call
;
; 2 live variables in registers.
;
; CHECK-LABEL:  .long   L{{.*}}-_jsIntCall
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .short  2
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
; CHECK-NEXT:   .byte   1
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  {{[0-9]+}}
; CHECK-NEXT:   .long   0
define i64 @jsIntCall(i64 %dummy1, i64* %obj, i64 %arg, i64 %l1, i64 %l2) {
entry:
  %resolveCall = inttoptr i64 -559038737 to i8*
  %result = call i64 (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.i64(i64 8, i32 15, i8* %resolveCall, i32 2, i64* %obj, i64 %arg, i64 %l1, i64 %l2)
  %add = add i64 %result, 3
  ret i64 %add
}

; Spilled stack map values.
;
; Verify 17 stack map entries.
;
; CHECK-LABEL:  .long L{{.*}}-_spilledValue
; CHECK-NEXT:   .short 0
; CHECK-NEXT:   .short 17
;
; Check that at least one is a spilled entry from RBP.
; Location: Indirect RBP + ...
; CHECK:        .byte 3
; CHECK-NEXT:   .byte 8
; CHECK-NEXT:   .short 6
define void @spilledValue(i64 %arg0, i64 %arg1, i64 %arg2, i64 %arg3, i64 %arg4, i64 %l0, i64 %l1, i64 %l2, i64 %l3, i64 %l4, i64 %l5, i64 %l6, i64 %l7, i64 %l8, i64 %l9, i64 %l10, i64 %l11, i64 %l12, i64 %l13, i64 %l14, i64 %l15, i64 %l16) {
entry:
  call void (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.void(i64 11, i32 15, i8* null, i32 5, i64 %arg0, i64 %arg1, i64 %arg2, i64 %arg3, i64 %arg4, i64 %l0, i64 %l1, i64 %l2, i64 %l3, i64 %l4, i64 %l5, i64 %l6, i64 %l7, i64 %l8, i64 %l9, i64 %l10, i64 %l11, i64 %l12, i64 %l13, i64 %l14, i64 %l15, i64 %l16)
  ret void
}

; Spilled stack map values.
;
; Verify 17 stack map entries.
;
; CHECK-LABEL:  .long L{{.*}}-_spilledStackMapValue
; CHECK-NEXT:   .short 0
; CHECK-NEXT:   .short 17
;
; Check that at least one is a spilled entry from RBP.
; Location: Indirect RBP + ...
; CHECK:        .byte 3
; CHECK-NEXT:   .byte 8
; CHECK-NEXT:   .short 6
define webkit_jscc void @spilledStackMapValue(i64 %l0, i64 %l1, i64 %l2, i64 %l3, i64 %l4, i64 %l5, i64 %l6, i64 %l7, i64 %l8, i64 %l9, i64 %l10, i64 %l11, i64 %l12, i64 %l13, i64 %l14, i64 %l15, i64 %l16) {
entry:
  call void (i64, i32, ...)* @llvm.experimental.stackmap(i64 12, i32 15, i64 %l0, i64 %l1, i64 %l2, i64 %l3, i64 %l4, i64 %l5, i64 %l6, i64 %l7, i64 %l8, i64 %l9, i64 %l10, i64 %l11, i64 %l12, i64 %l13, i64 %l14, i64 %l15, i64 %l16)
  ret void
}

; Spill a subregister stackmap operand.
;
; CHECK-LABEL:  .long L{{.*}}-_spillSubReg
; CHECK-NEXT:   .short 0
; 4 locations
; CHECK-NEXT:   .short 1
;
; Check that the subregister operand is a 4-byte spill.
; Location: Indirect, 4-byte, RBP + ...
; CHECK:        .byte 3
; CHECK-NEXT:   .byte 4
; CHECK-NEXT:   .short 6
define void @spillSubReg(i64 %arg) #0 {
bb:
  br i1 undef, label %bb1, label %bb2

bb1:
  unreachable

bb2:
  %tmp = load i64* inttoptr (i64 140685446136880 to i64*)
  br i1 undef, label %bb16, label %bb17

bb16:
  unreachable

bb17:
  %tmp32 = trunc i64 %tmp to i32
  br i1 undef, label %bb60, label %bb61

bb60:
  tail call void asm sideeffect "nop", "~{ax},~{bx},~{cx},~{dx},~{bp},~{si},~{di},~{r8},~{r9},~{r10},~{r11},~{r12},~{r13},~{r14},~{r15}"() nounwind
  tail call void (i64, i32, ...)* @llvm.experimental.stackmap(i64 13, i32 5, i32 %tmp32)
  unreachable

bb61:
  unreachable
}

; Map a single byte subregister. There is no DWARF register number, so
; we expect the register to be encoded with the proper size and spill offset. We don't know which
;
; CHECK-LABEL:  .long L{{.*}}-_subRegOffset
; CHECK-NEXT:   .short 0
; 2 locations
; CHECK-NEXT:   .short 2
;
; Check that the subregister operands are 1-byte spills.
; Location 0: Register, 4-byte, AL
; CHECK-NEXT:   .byte 1
; CHECK-NEXT:   .byte 1
; CHECK-NEXT:   .short 0
; CHECK-NEXT:   .long 0
;
; Location 1: Register, 4-byte, BL
; CHECK-NEXT:   .byte 1
; CHECK-NEXT:   .byte 1
; CHECK-NEXT:   .short 3
; CHECK-NEXT:   .long 0
define void @subRegOffset(i16 %arg) {
  %v = mul i16 %arg, 5
  %a0 = trunc i16 %v to i8
  tail call void asm sideeffect "nop", "~{bx}"() nounwind
  %arghi = lshr i16 %v, 8
  %a1 = trunc i16 %arghi to i8
  tail call void asm sideeffect "nop", "~{cx},~{dx},~{bp},~{si},~{di},~{r8},~{r9},~{r10},~{r11},~{r12},~{r13},~{r14},~{r15}"() nounwind
  tail call void (i64, i32, ...)* @llvm.experimental.stackmap(i64 14, i32 5, i8 %a0, i8 %a1)
  ret void
}

; Map a constant value.
;
; CHECK-LABEL:  .long L{{.*}}-_liveConstant
; CHECK-NEXT:   .short 0
; 1 location
; CHECK-NEXT:   .short 1
; Loc 0: SmallConstant
; CHECK-NEXT:   .byte   4
; CHECK-NEXT:   .byte   8
; CHECK-NEXT:   .short  0
; CHECK-NEXT:   .long   33

define void @liveConstant() {
  tail call void (i64, i32, ...)* @llvm.experimental.stackmap(i64 15, i32 5, i32 33)
  ret void
}

; Directly map an alloca's address.
;
; Callsite 16
; CHECK-LABEL:  .long L{{.*}}-_directFrameIdx
; CHECK-NEXT:   .short 0
; 1 location
; CHECK-NEXT:   .short	1
; Loc 0: Direct RBP - ofs
; CHECK-NEXT:   .byte	2
; CHECK-NEXT:   .byte	8
; CHECK-NEXT:   .short	6
; CHECK-NEXT:   .long

; Callsite 17
; CHECK-LABEL:  .long	L{{.*}}-_directFrameIdx
; CHECK-NEXT:   .short	0
; 2 locations
; CHECK-NEXT:   .short	2
; Loc 0: Direct RBP - ofs
; CHECK-NEXT:   .byte	2
; CHECK-NEXT:   .byte	8
; CHECK-NEXT:   .short	6
; CHECK-NEXT:   .long
; Loc 1: Direct RBP - ofs
; CHECK-NEXT:   .byte	2
; CHECK-NEXT:   .byte	8
; CHECK-NEXT:   .short	6
; CHECK-NEXT:   .long
define void @directFrameIdx() {
entry:
  %metadata1 = alloca i64, i32 3, align 8
  store i64 11, i64* %metadata1
  store i64 12, i64* %metadata1
  store i64 13, i64* %metadata1
  call void (i64, i32, ...)* @llvm.experimental.stackmap(i64 16, i32 0, i64* %metadata1)
  %metadata2 = alloca i8, i32 4, align 8
  %metadata3 = alloca i16, i32 4, align 8
  call void (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.void(i64 17, i32 5, i8* null, i32 0, i8* %metadata2, i16* %metadata3)
  ret void
}

; Test a 64-bit ID.
;
; CHECK:        .quad 4294967295
; CHECK-LABEL:  .long L{{.*}}-_longid
; CHECK:        .quad 4294967296
; CHECK-LABEL:  .long L{{.*}}-_longid
; CHECK:        .quad 9223372036854775807
; CHECK-LABEL:  .long L{{.*}}-_longid
; CHECK:        .quad -1
; CHECK-LABEL:  .long L{{.*}}-_longid
define void @longid() {
entry:
  tail call void (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.void(i64 4294967295, i32 0, i8* null, i32 0)
  tail call void (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.void(i64 4294967296, i32 0, i8* null, i32 0)
  tail call void (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.void(i64 9223372036854775807, i32 0, i8* null, i32 0)
  tail call void (i64, i32, i8*, i32, ...)* @llvm.experimental.patchpoint.void(i64 -1, i32 0, i8* null, i32 0)
  ret void
}

declare void @llvm.experimental.stackmap(i64, i32, ...)
declare void @llvm.experimental.patchpoint.void(i64, i32, i8*, i32, ...)
declare i64 @llvm.experimental.patchpoint.i64(i64, i32, i8*, i32, ...)
