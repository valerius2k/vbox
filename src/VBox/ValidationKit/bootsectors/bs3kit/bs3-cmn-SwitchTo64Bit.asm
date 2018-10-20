; $Id$
;; @file
; BS3Kit - Bs3SwitchTo64Bit
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

%ifndef TMPL_CMN_LM

;;
; @cproto   BS3_DECL(void) Bs3SwitchTo64Bit(void);
;
BS3_PROC_BEGIN_CMN Bs3SwitchTo64Bit
 %if TMPL_BITS == 64
        ret
 %else
  %if TMPL_BITS == 16
        sub     sp, 6
        push    bp
        mov     bp, sp
  %else
        push    xPRE [xSP]                ; duplicate the return address
  %endif
        push    sAX

  %if TMPL_BITS == 16
        ; Convert the 16-bit near return into a 32-bit far return
        movzx   eax, word [bp + 8]
        add     eax, BS3_ADDR_BS3TEXT16
        mov     [bp + 2], eax
  %endif

        ; Calc ring addend.
        mov     ax, cs
        and     ax, 3
        shl     ax, BS3_SEL_RING_SHIFT

        ; Set return segment.
        add     ax, BS3_SEL_R0_CS64
  %if TMPL_BITS == 16
        mov     [bp + 6], eax
  %else
        mov     [xSP + xCB*2], eax
  %endif

        ; Load 64-bit segment registers (SS64==DS64).
        add     ax, BS3_SEL_R0_DS64 - BS3_SEL_R0_CS64
        mov     ss, ax
        mov     ds, ax
        mov     es, ax

        ; Restore and return.
        pop     sAX
 %if TMPL_BITS == 16
        leave
        o32 retf
 %else
        retf
 %endif
%endif
BS3_PROC_END_CMN   Bs3SwitchTo64Bit

%endif

