; $Id$
;; @file
; BS3Kit - Bs3SwitchToPP16
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

%include "bs3kit-template-header.mac"


;;
; Switch to 16-bit paged protected mode from any other mode.
;
; @cproto   BS3_DECL(void) Bs3SwitchToPP16(void);
;
; @uses     Nothing (except high 32-bit register parts).
;
; @remarks  Obviously returns to 16-bit mode, even if the caller was
;           in 32-bit or 64-bit mode.
;
BS3_PROC_BEGIN_MODE Bs3SwitchToPP16
%ifdef TMPL_PE16
        ret

%else
        ;
        ; Switch to 16-bit mode and prepare for returning in 16-bit mode.
        ;
 %if TMPL_BITS != 16
        shl     xPRE [xSP + xCB], TMPL_BITS - 16    ; Adjust the return address.
        add     xSP, xCB - 2

        ; Must be in 16-bit segment when calling Bs3SwitchTo16Bit.
        jmp     .sixteen_bit_segment
BS3_BEGIN_TEXT16
        BS3_SET_BITS TMPL_BITS
.sixteen_bit_segment:
 %endif

        ;
        ; Switch to real mode.
        ;
        extern  TMPL_NM(Bs3SwitchToRM)
        call    TMPL_NM(Bs3SwitchToRM)
        BS3_SET_BITS 16

        push    eax
        pushfd

        ;
        ; Make sure PAE is really off.
        ;
        mov     eax, cr4
        test    eax, X86_CR4_PAE
        jz      .cr4_is_fine
        and     eax, ~X86_CR4_PAE
        mov     cr4, eax
.cr4_is_fine:

        ;
        ; Get the page directory (returned in eax).  Will lazy init page tables.
        ;
        extern NAME(Bs3PagingGetRootForPP32_rm)
        call   NAME(Bs3PagingGetRootForPP32_rm)

        cli
        mov     cr3, eax

        ;
        ; Load the GDT and enable PP16.
        ;
BS3_EXTERN_SYSTEM16 Bs3Lgdt_Gdt
BS3_BEGIN_TEXT16
        mov     ax, BS3SYSTEM16
        mov     ds, ax
        lgdt    [Bs3Lgdt_Gdt]

        mov     eax, cr0
        or      eax, X86_CR0_PE | X86_CR0_PG
        mov     cr0, eax
        jmp     BS3_SEL_R0_CS16:.reload_cs_and_stuff
.reload_cs_and_stuff:

        ;
        ; Call rountine for doing mode specific setups.
        ;
        extern  NAME(Bs3EnteredMode_pp16)
        call    NAME(Bs3EnteredMode_pp16)

        popfd
        pop     eax
        ret

 %if TMPL_BITS != 16
TMPL_BEGIN_TEXT
 %endif
%endif
BS3_PROC_END_MODE   Bs3SwitchToPP16

