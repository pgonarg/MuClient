; Stubs for __ltof3 and __ultof3 required by .NET 10 NativeAOT WorkstationGC on x86/MSVC 14.32
; With .MODEL flat, C, MASM prepends _ to all proc names.
; So "_ltof3 PROC" assembles to symbol "__ltof3" (what the linker expects).

.MODEL flat, C
.586
.XMM

.CODE

; .SAFESEH declarations (register as safe for SEH)
.SAFESEH _ltof3
.SAFESEH _ultof3

_ltof3 PROC
    ; int64_t on stack: [esp+4]=lo dword, [esp+8]=hi dword
    fild    QWORD PTR [esp+4]
    ret
_ltof3 ENDP

_ultof3 PROC
    fild    QWORD PTR [esp+4]
    test    DWORD PTR [esp+8], 80000000h
    jz      done
    fadd    QWORD PTR [two64]
done:
    ret
_ultof3 ENDP

.DATA
two64   DQ  4F80000000000000h   ; 2^64 as IEEE 754 double

END
