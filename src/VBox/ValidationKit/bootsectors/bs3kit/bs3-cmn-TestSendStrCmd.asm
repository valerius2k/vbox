; $Id$
;; @file
; BS3Kit - bs3TestSendStrCmd.
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
%include "VBox/VMMDevTesting.mac"

BS3_EXTERN_DATA16 g_fbBs3VMMDevTesting
TMPL_BEGIN_TEXT

;;
; @cproto   BS3_DECL(void) bs3TestSendStrCmd_c16(uint32_t uCmd, const char BS3_FAR *pszString);
;
BS3_PROC_BEGIN_CMN bs3TestSendStrCmd
        BS3_CALL_CONV_PROLOG 2
        push    xBP
        mov     xBP, xSP
        push    xAX
        push    xDX
        push    xSI
        BS3_ONLY_16BIT_STMT push ds

        cmp     byte [g_fbBs3VMMDevTesting], 0
        je      .no_vmmdev

        ; The command (uCmd).
        mov     dx, VMMDEV_TESTING_IOPORT_CMD
        mov     eax, [xBP + xCB]
        out     dx, eax

        ; The string.
        mov     dx, VMMDEV_TESTING_IOPORT_DATA
%ifdef TMPL_16BIT
        lds     si, [xBP + xCB + sCB]
%else
        mov     xSI, [xBP + xCB + sCB]
%endif
.next_char:
        lodsb
        out     dx, al
        test    al, al
        jnz     .next_char

.no_vmmdev:
        BS3_ONLY_16BIT_STMT pop ds
        pop     xSI
        pop     xDX
        pop     xAX
        leave
        BS3_CALL_CONV_EPILOG 2
        ret
BS3_PROC_END_CMN   bs3TestSendStrCmd

