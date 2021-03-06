; $Id$
;; @file
; Compiler support routines.
;

;
; Copyright (C) 2012-2015 Oracle Corporation
;
; This file is part of VirtualBox Open Source Edition (OSE), as
; available from http://www.virtualbox.org. This file is free software;
; you can redistribute it and/or modify it under the terms of the GNU
; General Public License (GPL) as published by the Free Software
; Foundation, in version 2 as it comes in the "COPYING" file of the
; VirtualBox OSE distribution. VirtualBox OSE is distributed in the
; hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
;


;*******************************************************************************
;*  Exported Symbols                                                           *
;*******************************************************************************
public          __U4D
public          __U4M
public          __U8LS
public          __U8RS
ifndef          VBOX_PC_BIOS
public          __I4D
public          __I4M
endif
public          _fmemset_
public          _fmemcpy_



                .386p

_TEXT           segment public 'CODE' use16
                assume cs:_TEXT


;;
; 32-bit unsigned division.
;
; @param    dx:ax   Dividend.
; @param    cx:bx   Divisor.
; @returns  dx:ax   Quotient.
;           cx:bx   Remainder.
;
__U4D:
                pushf
                push    eax
                push    edx
                push    ecx

                rol     eax, 16
                mov     ax, dx
                ror     eax, 16
                xor     edx, edx

                shr     ecx, 16
                mov     cx, bx

                div     ecx                 ; eax:edx / ecx -> eax=quotient, edx=remainder.

                mov     bx, dx
                pop     ecx
                shr     edx, 16
                mov     cx, dx

                pop     edx
                ror     eax, 16
                mov     dx, ax
                add     sp, 2
                pop     ax
                rol     eax, 16

                popf
                ret


ifndef          VBOX_PC_BIOS
;;
; 32-bit signed division.
;
; @param    dx:ax   Dividend.
; @param    cx:bx   Divisor.
; @returns  dx:ax   Quotient.
;           cx:bx   Remainder.
;
__I4D:
                pushf
                push    eax
                push    edx
                push    ecx

                rol     eax, 16
                mov     ax, dx
                ror     eax, 16
                xor     edx, edx

                shr     ecx, 16
                mov     cx, bx

                idiv    ecx                 ; eax:edx / ecx -> eax=quotient, edx=remainder.

                mov     bx, dx
                pop     ecx
                shr     edx, 16
                mov     cx, dx

                pop     edx
                ror     eax, 16
                mov     dx, ax
                add     sp, 2
                pop     ax
                rol     eax, 16

                popf
                ret
endif           ; VBOX_PC_BIOS


;;
; 32-bit unsigned multiplication.
;
; @param    dx:ax   Factor 1.
; @param    cx:bx   Factor 2.
; @returns  dx:ax   Result.
;
__U4M:
                pushf
                push    eax
                push    edx
                push    ecx

                rol     eax, 16
                mov     ax, dx
                ror     eax, 16
                xor     edx, edx

                shr     ecx, 16
                mov     cx, bx

                mul     ecx                 ; eax * ecx -> edx:eax

                pop     ecx

                pop     edx
                ror     eax, 16
                mov     dx, ax
                add     sp, 2
                pop     ax
                rol     eax, 16

                popf
                ret


ifndef          VBOX_PC_BIOS
;;
; 32-bit signed multiplication.
;
; @param    dx:ax   Factor 1.
; @param    cx:bx   Factor 2.
; @returns  dx:ax   Result.
; cx, es may be modified; di is preserved
;
__I4M:
                pushf
                push    eax
                push    edx
                push    ecx
                push    ebx

                rol     eax, 16
                mov     ax, dx
                ror     eax, 16
                xor     edx, edx

                shr     ecx, 16
                mov     cx, bx

                imul    ecx                 ; eax * ecx -> edx:eax

                pop     ebx
                pop     ecx

                pop     edx
                ror     eax, 16
                mov     dx, ax
                add     sp, 2
                pop     ax
                rol     eax, 16

                popf
                ret
endif           ; VBOX_PC_BIOS


;;
; 64-bit left shift.
;
; @param    ax:bx:cx:dx Value.
; @param    si          Shift count.
; @returns  ax:bx:cx:dx Shifted value.
; si is zeroed
;
__U8LS:

                test    si, si
                jz      u8ls_quit
u8ls_rot:
                shl     dx, 1
                rcl     cx, 1
                rcl     bx, 1
                rcl     ax, 1
                dec     si
                jnz     u8ls_rot
u8ls_quit:
                ret


;;
; 64-bit unsigned right shift.
;
; @param    ax:bx:cx:dx Value.
; @param    si          Shift count.
; @returns  ax:bx:cx:dx Shifted value.
; si is zeroed
;
__U8RS:

                test    si, si
                jz      u8rs_quit
u8rs_rot:
                shr     ax, 1
                rcr     bx, 1
                rcr     cx, 1
                rcr     dx, 1
                dec     si
                jnz     u8rs_rot
u8rs_quit:
                ret


;;
; memset taking a far pointer.
;
; cx, es may be modified; di is preserved
;
; @returns  dx:ax unchanged.
; @param    dx:ax   Pointer to the memory.
; @param    bl      The fill value.
; @param    cx      The number of bytes to fill.
;
_fmemset_:
                push    di

                mov     es, dx
                mov     di, ax
                xchg    al, bl
                rep stosb
                xchg    al, bl

                pop     di
                ret


;;
; memcpy taking far pointers.
;
; cx, es may be modified; si, di are preserved
;
; @returns  dx:ax unchanged.
; @param    dx:ax   Pointer to the destination memory.
; @param    cx:bx   Pointer to the source memory.
; @param    sp+2    The number of bytes to copy (dw).
;
_fmemcpy_:
                push    bp
                mov     bp, sp
                push    di
                push    ds
                push    si

                mov     es, dx
                mov     di, ax
                mov     ds, cx
                mov     si, bx
                mov     cx, [bp + 4]
                rep     movsb

                pop     si
                pop     ds
                pop     di
                leave
                ret


_TEXT           ends
                end

