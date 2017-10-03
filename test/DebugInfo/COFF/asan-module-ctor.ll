; RUN: llc -mcpu=core2 -mtriple=i686-pc-win32 -O0 < %s | FileCheck --check-prefix=X86 %s

; This LL file was generated by running clang on the following code with
; -fsanitize=address
; D:\asan.c:
; 1 int foo(void) {
; 2   return 0;
; 3 }

; The module ctor has no debug info.  All we have to do is don't crash.
; X86: _asan.module_ctor:
; X86-NEXT: L{{.*}}:
; X86-NEXT: # BB
; X86-NEXT: calll   ___asan_init_v3
; X86-NEXT: retl

; Make sure we don't put any DWARF debug info for ASan-instrumented modules.
; X86-NOT: DWARF

; ModuleID = 'asan.c'
target datalayout = "e-m:w-p:32:32-i64:64-f80:32-n8:16:32-S32"
target triple = "i686-pc-win32"

@llvm.global_ctors = appending global [1 x { i32, void ()* }] [{ i32, void ()* } { i32 1, void ()* @asan.module_ctor }]

; Function Attrs: nounwind sanitize_address
define i32 @foo() #0 !dbg !4 {
entry:
  ret i32 0, !dbg !10
}

define internal void @asan.module_ctor() {
  call void @__asan_init_v3()
  ret void
}

declare void @__asan_init_v3()

declare void @__asan_report_load1(i32)

declare void @__asan_report_load2(i32)

declare void @__asan_report_load4(i32)

declare void @__asan_report_load8(i32)

declare void @__asan_report_load16(i32)

declare void @__asan_report_store1(i32)

declare void @__asan_report_store2(i32)

declare void @__asan_report_store4(i32)

declare void @__asan_report_store8(i32)

declare void @__asan_report_store16(i32)

declare void @__asan_report_load_n(i32, i32)

declare void @__asan_report_store_n(i32, i32)

declare void @__asan_handle_no_return()

declare void @__sanitizer_cov()

declare void @__sanitizer_ptr_cmp(i32, i32)

declare void @__sanitizer_ptr_sub(i32, i32)

declare void @__asan_before_dynamic_init(i32)

declare void @__asan_after_dynamic_init()

declare void @__asan_register_globals(i32, i32)

declare void @__asan_unregister_globals(i32, i32)

attributes #0 = { nounwind sanitize_address "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-realign-stack" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!7, !8}
!llvm.ident = !{!9}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, producer: "clang version 3.5.0 ", isOptimized: false, emissionKind: LineTablesOnly, file: !1, enums: !2, retainedTypes: !2, globals: !2, imports: !2)
!1 = !DIFile(filename: "asan.c", directory: "D:\5C")
!2 = !{}
!4 = distinct !DISubprogram(name: "foo", line: 1, isLocal: false, isDefinition: true, virtualIndex: 6, flags: DIFlagPrototyped, isOptimized: false, unit: !0, scopeLine: 1, file: !1, scope: !5, type: !6, variables: !2)
!5 = !DIFile(filename: "asan.c", directory: "D:C")
!6 = !DISubroutineType(types: !2)
!7 = !{i32 2, !"CodeView", i32 1}
!8 = !{i32 1, !"Debug Info Version", i32 3}
!9 = !{!"clang version 3.5.0 "}
!10 = !DILocation(line: 2, scope: !4)
