; sysasm.asm - CPUID for the system description (roadmap-hcd.md task 36.48).
;
; Watcom's register calling convention: the first argument in EAX, the
; second in EDX, the result in EAX, and the callee preserves EVERY other
; register. CPUID writes EAX, EBX, ECX and EDX, so each routine saves what
; it touches and restores it before RET. The 36.21 lesson (lesson-cpuid-ebx,
; STOP 0xB8 on Windows 2000) was an EBX that CPUID clobbered unsaved; do not
; drop a push below to "simplify".
;
; CPUID is emitted as bytes (0F A2) so the file assembles under .386p, the
; level the rest of the tool is built at.

.386p

_TEXT SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  cs:_TEXT

        PUBLIC  sys_has_cpuid_, sys_cpuid_

; int sys_has_cpuid(void) - 1 if EFLAGS.ID (bit 21) can be toggled, which
; is the architectural test for CPUID. EFLAGS is restored as found.
sys_has_cpuid_:
        push    ecx
        pushfd
        pop     eax
        mov     ecx,eax
        xor     eax,00200000h
        push    eax
        popfd
        pushfd
        pop     eax
        push    ecx
        popfd
        xor     eax,ecx
        shr     eax,21
        and     eax,1
        pop     ecx
        ret

; void sys_cpuid(u32 leaf /* EAX */, u32 *out4 /* EDX */)
; out4[0..3] = EAX, EBX, ECX, EDX of CPUID(leaf, subleaf 0).
sys_cpuid_:
        push    eax                     ; void, but saved too: no caller
                                        ; is left to guess what Watcom assumes
        push    ebx
        push    ecx
        push    edx
        push    esi
        mov     esi,edx
        xor     ecx,ecx
        db      0Fh,0A2h                ; cpuid
        mov     [esi],eax
        mov     [esi+4],ebx
        mov     [esi+8],ecx
        mov     [esi+12],edx
        pop     esi
        pop     edx
        pop     ecx
        pop     ebx
        pop     eax
        ret

_TEXT ENDS
        END
