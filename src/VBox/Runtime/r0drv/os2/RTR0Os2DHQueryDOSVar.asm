; $Id$
;; @file
; IPRT - DevHelp_GetDOSVar, Ring-0 Driver, OS/2.
;

;
; Copyright (c) 1999-2007 knut st. osmundsen <bird-src-spam@anduin.net>
;
; Permission is hereby granted, free of charge, to any person
; obtaining a copy of this software and associated documentation
; files (the "Software"), to deal in the Software without
; restriction, including without limitation the rights to use,
; copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the
; Software is furnished to do so, subject to the following
; conditions:
;
; The above copyright notice and this permission notice shall be
; included in all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
; OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
; WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
; OTHER DEALINGS IN THE SOFTWARE.
;


;*******************************************************************************
;* Header Files                                                                *
;*******************************************************************************
%define RT_INCL_16BIT_SEGMENTS
%include "iprt/asmdefs.mac"
%include "iprt/err.mac"


;*******************************************************************************
;* External Symbols                                                            *
;*******************************************************************************
extern KernThunkStackTo32
extern KernThunkStackTo16
extern NAME(g_fpfnDevHlp)


;*******************************************************************************
;* Defined Constants And Macros                                                *
;*******************************************************************************
%define DevHlp_GetDOSVar    24h


BEGINCODE

;
; Jump table used by RTR0Os2DHQueryDOSVar
;
DosVarJumpTab:
    dd  0                           ; 0 - Reserved
    dd  Load1600                    ; 1 - GIS
    dd  Load1616                    ; 2 - LIS
    dd  0                           ; 3 - Reserved
    dd  Load1616                    ; 4 - VectorSDF
    dd  Load1616                    ; 5 - VectorReboot
    dd  Load1616  ; QWORD !!!       ; 6 - VectorMSATS
    dd  LoadByte                    ; 7 - YieldFlag (Resched)
    dd  LoadByte                    ; 8 - TCYieldFlag (TCResched)
    dd  AsIs                        ; 9 - DOSTable
    dd  Load1616                    ; a - VectorDEKKO
    dd  AsIs                        ; b - CodePgBuff
    dd  Load1616                    ; c - VectorRIPL
    dd  LoadDword                   ; d - InterruptLevel
    dd  AsIs                        ; e - DevClassTables
    dd  AsIs                        ; f - DMQS_Sel
    dd  AsIs                        ;10 - APMInfo
    dd  LoadWord                    ;11 - APM_Length (length of above structure)
    dd  LoadDword                   ;12 - CPUMODE: 0 = uniprocessor, 1=multiprocessor
    dd  LoadDword                   ;13 - PSD flags
    dd  LoadDword                   ;14 - Number of processors online
DosVarJumpTabEnd:
%define DosVarJumpTabSize (DosVarJumpTabEnd - DosVarJumpTab) / 4

;;
; Unified DevHelp_GetDOSVar -> Far 16:16 pointer wrapper.
;
; @param    iVar    [ebp + 08h]     Variable.
; @param    iMember [ebp + 0ch]     Member.
; @param    pfp     [ebp + 10h]     Where to store the variable address (pointer to 16:16).
;
BEGINPROC_EXPORTED RTR0Os2DHQueryDOSVar
    ; switch stack first.
    call    KernThunkStackTo16

    ; normal prolog.
    push    ebp
    mov     ebp, esp
    push    dword [NAME(g_fpfnDevHlp)]  ; ebp - 4
    push    ebx                         ; save ebx
    push    es                          ; save es

    ; setup the devhelp call and switch to
    mov     eax, [ebp + 08h]            ; iVar (8-bit)
    mov     ecx, [ebp + 0ch]            ; iMember (16-bit)
    mov     dl, DevHlp_GetDOSVar

    ; jump to the 16-bit code.
    ;jmp far dword NAME(RTR0Os2DHQueryDOSVar_16) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(RTR0Os2DHQueryDOSVar_16) wrt CODE16
    dw      CODE16
BEGINCODE16
GLOBALNAME RTR0Os2DHQueryDOSVar_16
    call far [ss:ebp - 4]

    ;jmp far dword NAME(RTR0Os2DHQueryDOSVar) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(RTR0Os2DHQueryDOSVar_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
BEGINCODE
GLOBALNAME RTR0Os2DHQueryDOSVar_32
    jc  Error1

    ;
    ; Make ax:ebx contain the pointer and take action according
    ; to the variable jump table.
    ;
    and     ebx, 0000ffffh              ; clean high part of ebx
    movzx   ecx, byte [ebp + 08]        ; iVar
    cmp     ecx, DosVarJumpTabSize
    jg      Error2
    jmp     [DosVarJumpTab + ecx * 4]

    ; Load Byte at ax:ebx.
LoadByte:
    mov     es, ax
    mov     dl, byte [es:ebx]
    movzx   edx, dl
    jmp StoreIt

    ; Load Word at ax:ebx.
LoadWord:
    mov     es, ax
    movzx   edx, word [es:ebx]
    jmp StoreIt

    ; Load selector at ax:ebx.
Load1600:
    mov     es, ax
    movzx   edx, word [es:ebx]
    shl     edx, 16
    jmp StoreIt

    ; Load 16:16 ptr at ax:ebx.
Load1616:
LoadDword:
    mov     es, ax
    mov     edx, dword [es:ebx]
    jmp StoreIt

    ; Move ax:bx into edx.
AsIs:
    mov     dx, ax
    shl     edx, 16
    mov     dx, bx
    jmp StoreIt

Error2:
    mov     eax, VERR_INVALID_PARAMETER
    jmp     Done

Error1:
    mov     eax, VERR_GENERAL_FAILURE
    jmp     Done

StoreIt:
    mov     ecx, [ebp + 10h]
    mov     [ecx], edx
    xor     eax, eax                    ; return success (VINF_SUCCESS == 0)

Done:
    pop     es
    pop     ebx
    leave

    ; switch stack back and return.
    push    eax
    call    KernThunkStackTo32
    pop     eax
    ret
ENDPROC RTR0Os2DHQueryDOSVar

