; irqasm.asm - locked 32-bit protected-mode one-shot IRQ entry.

.386p

HC_EHCI        EQU 2
HC_XHCI        EQU 3

IRQ_COUNT      EQU 0
IRQ_FOREIGN    EQU 4
IRQ_OP         EQU 8
IRQ_RT         EQU 12
IRQ_HCTYPE     EQU 16
IRQ_NUM        EQU 20

PIC1_CMD       EQU 020h
PIC1_DATA      EQU 021h
PIC2_CMD       EQU 0A0h
PIC2_DATA      EQU 0A1h
PIC_EOI        EQU 020h

XOP_USBCMD     EQU 000h
XOP_USBSTS     EQU 004h
XRT_IMAN       EQU 020h
CMD_INTE       EQU 000000004h
STS_EINT       EQU 000000008h
IMAN_IP        EQU 000000001h
; IMAN bits 31:2 are RsvdP, so a composed write must preserve what it reads
; there. Same value as IMAN_RSVDP in qual.h - kept in step by hand because this
; file cannot include it.
IMAN_RSVDP     EQU 0FFFFFFFCh

EOP_USBSTS     EQU 004h
EOP_USBINTR    EQU 008h
ESTS_IAA       EQU 000000020h

_DATA SEGMENT DWORD PUBLIC 'DATA'
        EXTRN   _irq_isr_state:BYTE
_DATA ENDS

_TEXT SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  cs:_TEXT, ds:_DATA

        PUBLIC  irq_pm_entry_, irq_pm_entry_end_
irq_pm_entry_:
        push    ds
        push    es
        push    fs
        push    gs
        pushad

        mov     ax,_DATA
        mov     ds,ax
        mov     es,ax

        mov     eax,DWORD PTR [_irq_isr_state+IRQ_HCTYPE]
        cmp     eax,HC_XHCI
        je      irq_xhci
        cmp     eax,HC_EHCI
        je      irq_ehci
        jmp     irq_not_ours

irq_xhci:
        mov     esi,DWORD PTR [_irq_isr_state+IRQ_OP]
        mov     eax,DWORD PTR [esi+XOP_USBSTS]
        test    eax,STS_EINT
        jz      irq_not_ours

        mov     eax,DWORD PTR [esi+XOP_USBCMD]
        and     eax,NOT CMD_INTE
        mov     DWORD PTR [esi+XOP_USBCMD],eax
; USBSTS.EINT is acknowledged BEFORE IMAN.IP: clearing IP first can lose an
; interrupt (docs/usb-xhci-info/xhci-data-structures.md section 3, spec Table 5-21 note).
; Both are RW1C, so each write names only its own bit.
        mov     DWORD PTR [esi+XOP_USBSTS],STS_EINT
        mov     edi,DWORD PTR [_irq_isr_state+IRQ_RT]
; IMAN is read-modify-write over its RsvdP mask, like every other composed write
; in this tool (repo audit D2): IP is RW1C and IE is RW, but bits 31:2 are RsvdP
; and a literal here zeroes all thirty of them. USBCMD and USBSTS above need no
; such read - the first already reads before it masks INTE, and the second is a
; pure RW1C write that names only its own bit.
        mov     eax,DWORD PTR [edi+XRT_IMAN]
        and     eax,IMAN_RSVDP
        or      eax,IMAN_IP
        mov     DWORD PTR [edi+XRT_IMAN],eax
        call    irq_mask_line
        inc     DWORD PTR [_irq_isr_state+IRQ_COUNT]
        jmp     irq_send_eoi

irq_ehci:
        mov     esi,DWORD PTR [_irq_isr_state+IRQ_OP]
        mov     eax,DWORD PTR [esi+EOP_USBSTS]
        test    eax,ESTS_IAA
        jz      irq_not_ours

        mov     DWORD PTR [esi+EOP_USBINTR],0
        mov     DWORD PTR [esi+EOP_USBSTS],ESTS_IAA
        call    irq_mask_line
        inc     DWORD PTR [_irq_isr_state+IRQ_COUNT]
        jmp     irq_send_eoi

irq_not_ours:
        call    irq_mask_line
        inc     DWORD PTR [_irq_isr_state+IRQ_FOREIGN]

irq_send_eoi:
        mov     eax,DWORD PTR [_irq_isr_state+IRQ_NUM]
        cmp     eax,8
        jb      irq_master_eoi
        mov     dx,PIC2_CMD
        mov     al,PIC_EOI
        out     dx,al
irq_master_eoi:
        mov     dx,PIC1_CMD
        mov     al,PIC_EOI
        out     dx,al

        popad
        pop     gs
        pop     fs
        pop     es
        pop     ds
        iretd

irq_mask_line:
        mov     ecx,DWORD PTR [_irq_isr_state+IRQ_NUM]
        cmp     ecx,8
        jae     irq_mask_slave
        mov     dx,PIC1_DATA
        in      al,dx
        mov     ah,1
        shl     ah,cl
        or      al,ah
        out     dx,al
        ret

irq_mask_slave:
        sub     ecx,8
        mov     dx,PIC2_DATA
        in      al,dx
        mov     ah,1
        shl     ah,cl
        or      al,ah
        out     dx,al
        ret

irq_pm_entry_end_:

        ASSUME  ds:NOTHING
_TEXT ENDS

END
