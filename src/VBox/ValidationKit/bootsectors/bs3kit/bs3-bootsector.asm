; $Id$
;; @file
; Generic bootsector for BS3.
;
; This sets up stack at %fff0 and loads the next sectors from the floppy at
; %10000 (1000:0000 in real mode), then starts executing at cs:ip=1000:0000.
;

;
; Copyright (C) 2007-2015 Oracle Corporation
;
; This file is part of VirtualBox Open Source Edition (OSE), as
; available from http://www.virtualbox.org. This file is free software;
; you can redistribute it and/or modify it under the terms of the GNU
; General Public License (GPL) as published by the Free Software
; Foundation, in version 2 as it comes in the "COPYING" file of the
; VirtualBox OSE distribution. VirtualBox OSE is distributed in the
; hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
;
; The contents of this file may alternatively be used under the terms
; of the Common Development and Distribution License Version 1.0
; (CDDL) only, as it comes in the "COPYING.CDDL" file of the
; VirtualBox OSE distribution, in which case the provisions of the
; CDDL are applicable instead of those of the GPL.
;
; You may elect to license modified versions of this file under the
; terms and conditions of either the GPL or the CDDL or both.
;



;*********************************************************************************************************************************
;*      Header Files                                                                                                             *
;*********************************************************************************************************************************
%include "bs3kit.mac"
%include "iprt/asmdefs.mac"
%include "iprt/x86.mac"


%ifdef __YASM__
[map all]
%endif

;
; Start with a jump just to follow the convention.
; Also declare all segments/sections to establish them and their order.
;
        ORG 07c00h

BITS 16
start:
        jmp short bs3InitCode
        db 0ah                          ; Should be nop, but this looks better.
g_OemId:                                ; 003h
        db 'BS3Kit', 0ah, 0ah

;
; DOS 4.0 Extended Bios Parameter Block:
;
g_cBytesPerSector:                      ; 00bh
        dw 512
g_cSectorsPerCluster:                   ; 00dh
        db 1
g_cReservedSectors:                     ; 00eh
        dw 1
g_cFATs:                                ; 010h
        db 0
g_cRootDirEntries:                      ; 011h
        dw 0
g_cTotalSectors:                        ; 013h
        dw 0
g_bMediaDescriptor:                     ; 015h
        db 0
g_cSectorsPerFAT:                       ; 016h
        dw 0
g_cPhysSectorsPerTrack:                 ; 018h
        dw 18
g_cHeads:                               ; 01ah
        dw 2
g_cHiddentSectors:                      ; 01ch
        dd 1
g_cLargeTotalSectors:                   ; 020h - We (ab)use this to indicate the number of sectors to load.
        dd 0
g_bBootDrv:                             ; 024h
        db 80h
g_bFlagsEtc:                            ; 025h
        db 0
g_bExtendedSignature:                   ; 026h
        db 0x29
g_dwSerialNumber:                       ; 027h
        dd 0x0a458634
g_abLabel:                              ; 02bh
        db 'VirtualBox', 0ah
g_abFSType:                             ; 036h
        db 'RawCode', 0ah
g_BpbEnd:                               ; 03ch


;
; Where to real init code starts.
;
bs3InitCode:
        cli

        ; save the registers.
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGS.rax], eax
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGS.rsp], esp
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGS.rbp], ebp
        mov     ax, ss
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGS.ss], ax
        mov     ax, ds
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGS.ds], ax
        mov     ax, es
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGS.es], ax
        mov     ax, fs
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGS.fs], ax
        mov     ax, gs

        ; set up the segment reisters and stack.
        xor     eax, eax
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     esp, BS3_ADDR_STACK
        mov     ebp, esp
        mov     [ebp], eax               ; clear the first 16 bytes (terminates the ebp chain)
        mov     [ebp + 04h], eax
        mov     [ebp + 08h], eax
        mov     [ebp + 0ch], eax

        ; Save more registers now that ds is known and the stack is usable.
        pushfd
        pop     eax
        mov     [BS3_ADDR_REG_SAVE + BS3REGS.rflags], eax
        mov     [BS3_ADDR_REG_SAVE + BS3REGS.rbx], ebx
        mov     [BS3_ADDR_REG_SAVE + BS3REGS.rcx], ecx
        mov     [BS3_ADDR_REG_SAVE + BS3REGS.rdx], edx
        mov     [BS3_ADDR_REG_SAVE + BS3REGS.rsi], esi
        mov     [BS3_ADDR_REG_SAVE + BS3REGS.rdi], edi
        mov     eax, cr2
        mov     [BS3_ADDR_REG_SAVE + BS3REGS.cr2], eax
        mov     eax, cr3
        mov     [BS3_ADDR_REG_SAVE + BS3REGS.cr3], eax
        mov     eax, cr4
        mov     [BS3_ADDR_REG_SAVE + BS3REGS.cr4], eax
        mov     byte [BS3_ADDR_REG_SAVE + BS3REGS.cBits], 16
        xor     eax, eax
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGS.cs], ax
        mov     ax, start
        mov     [cs:BS3_ADDR_REG_SAVE + BS3REGS.rip], eax

        ; Make sure caching is enabled and alignment is off.
        mov     eax, cr0
        mov     [BS3_ADDR_REG_SAVE + BS3REGS.cr0], eax
        and     eax, ~(X86_CR0_NW | X86_CR0_CD | X86_CR0_AM)
        mov     cr0, eax

        ; Load all the code.
        mov     [g_bBootDrv], dl
        call    bs3InitLoadImage

        ;
        ; Call the user 'main' procedure (shouldn't return).
        ;
        call    BS3_SEL_TEXT16:0000h

        ; Panic/hang.
Bs3Panic:
        cli
        hlt
        jmp     Bs3Panic



;;
; Loads the image off the floppy.
;
; This uses g_cLargeTotalSectors to figure out how much to load.
;
; Clobbers everything except ebp and esp.  Panics on failure.
;
; @param    dl          The boot drive number (from BIOS).
; @uses     ax, cx, bx, esi, di
;
BEGINPROC bs3InitLoadImage
        push    bp
        mov     bp, sp
        push    es
%define bSavedDiskNo    byte [bp - 04h]
        push    dx
%define bMaxSector      byte [bp - 06h]
        push    0
%define bMaxHead        byte [bp - 08h]
        push    0
%define bMaxCylinder    byte [bp - 0ah]
        push    0

        ;
        ; Try figure the geometry.
        ;
        mov     ah, 08h
        int     13h
        jc      .failure
        mov     bMaxSector, cl
        mov     bMaxHead, dh
        mov     bMaxCylinder, ch
        mov     dl, bSavedDiskNo

        ;
        ; Reload all the sectors one at a time (avoids problems).
        ;
        mov     esi, [g_cLargeTotalSectors]
        dec     esi
        mov     di, BS3_ADDR_LOAD / 16  ; The current load segment.
        mov     cx, 0002h               ; ch/cylinder=0 (0-based); cl/sector=2 (1-based)
        xor     dh, dh                  ; dh/head=0
.the_load_loop:
        xor     bx, bx
        mov     es, di                  ; es:bx -> buffer
        mov     ax, 0201h               ; al=1 sector; ah=read function
        int     13h
        jc      .failure

        ; advance to the next sector/head/cylinder.
        inc     cl
        cmp     cl, bMaxSector
        jbe     .adv_addr

        mov     cl, 1
        inc     dh
        cmp     dh, bMaxHead
        jbe     .adv_addr

        mov     dh, 0
        inc     ch

.adv_addr:
        add     di, 512 / 16
        dec     si
        jnz     .the_load_loop

        add     sp, 3*2
        pop     dx
        pop     es
        leave
        ret

        ;
        ; Something went wrong, display a message.
        ;
.failure:
        pusha

        ; print message
        mov     si, .s_szErrMsg
        mov     ah, 0eh
        xor     bx, bx
.failure_next_char:
        lodsb
        int     10h
        cmp     si, .s_szErrMsgEnd
        jb      .failure_next_char

        ; format the error number.
        movzx   bx, byte [bp - 2 - 1]    ; read the ah of the pusha frame
        shr     bl, 4
        mov     al, [bx + .s_achHex]
        int     10h

        movzx   bx, byte [bp - 2 - 1]    ; read the ah of the pusha frame
        and     bl, 0fh
        mov     al, [bx + .s_achHex]
        int     10h

        ; panic
        popa
        call    Bs3Panic
.s_szErrMsg:
        db 13, 10, 'read error: '
.s_szErrMsgEnd:
.s_achHex:
        db '0123456789abcdef'
ENDPROC bs3InitLoadImage


;
; Pad the remainder of the sector with int3's and end it with the DOS signature.
;
bs3Padding:
        times ( 510 - ( (bs3Padding - start) % 512 ) ) db 0cch
        db      055h, 0aah

