; $Id$
;; @file
; VBoxFS - OS/2 Shared Folders, all assembly code (16 -> 32 thunking mostly).
;

;
; Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
; Copyright (c) 2015-2018 Valery V. Sedletski <_valerius-no-spam@mail.ru>
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
%include "VBox/VBoxGuest.mac"


;*******************************************************************************
;*  Defined Constants And Macros                                               *
;*******************************************************************************
%define ERROR_NOT_SUPPORTED         50
%define ERROR_INVALID_PARAMETER     87
%define ERROR_PROTECTION_VIOLATION  115
%define DevHlp_AttachDD             2ah
%define DevHlp_AllocGDTSelector     2dh
%define DevHlp_FreeGDTSelector      53h
%define DevHlp_VMProcessToGlobal    59h
%define DevHlp_LinToGDTSelector     5ch

;;
; Prints a string to the VBox log port.
%macro DEBUG_STR16 1
%ifdef DEBUG
segment DATA16
%%my_dbg_str: db %1, 0ah, 0
segment CODE16
    push    ax
    mov     ax, %%my_dbg_str
    call    NAME(dbgstr16)
    pop     ax
%endif
%endmacro


%macro VBOXFS_EP16_BEGIN 2
global %1
%1:
    DEBUG_STR16 {'VBoxFS: ', %2}

%endmacro

%macro VBOXFS_EP16_END 1
global %1_EndProc
%1_EndProc:
%endmacro


%macro VBOXFS_EP32_BEGIN 2
global %1
%1:
;    DEBUG_STR16 {'VBoxFS: ', %2}

%endmacro

%macro VBOXFS_EP32_END 1
global %1_EndProc
%1_EndProc:
%endmacro

;;
; Used to taking us to 32-bit and reserving a parameter frame.
;
; @param    %1      The function name
; @param    %2      The number of bytes to reserve
;
%macro VBOXFS_TO_32 2
    ; prologue
    push    ebp
    mov     ebp, esp                    ; bp
    push    ds                          ; bp - 2
    push    es                          ; bp - 4

    ; Reserve the 32-bit parameter and align the stack on a 16 byte
    ; boundary to make GCC really happy.
    sub     sp, %2
    and     sp, 0fff0h

    ;jmp far dword NAME(%i %+ _32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(%1 %+ _32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
segment TEXT32
GLOBALNAME %1 %+ _32
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax

    call    KernThunkStackTo32

%endmacro VBOXFS_TO_32 1

;;
; The counter part to VBOXFS_TO_32
;
; @param    %1      The function name
;
%macro VBOXFS_TO_16 1
    push    eax
    call    KernThunkStackTo16
    pop     eax

    ;jmp far dword NAME(%1 %+ _16) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(%1 %+ _16) wrt CODE16
    dw      CODE16
segment CODE16
GLOBALNAME %1 %+ _16

    ; Epilogue
    lea     sp, [bp - 4h]
    pop     es
    pop     ds
    mov     esp, ebp
    pop     ebp
%endmacro


;;
; Used to taking us to 16-bit and reserving a parameter frame.
;
; @param    %1      The function name
; @param    %2      The number of bytes to reserve
;
%macro VBOXFS_32_TO_16 2
    ; prologue
    push    ebp
    mov     ebp, esp                    ; ebp
    push    edi                         ; ebp - 4
    push    ebx                         ; ebp - 8
    push    ds                          ; ebp - 0c
    push    es                          ; ebp - 10

    ; Reserve the 16-bit parameters and align the stack on a 16 byte
    ; boundary to make GCC really happy.
    sub     esp, %2
    and     esp, 0fffffff0h

    call    KernThunkStackTo16

    xor   ebx, ebx

    ;jmp far dword NAME(%i %+ _16)
    db      066h
    db      0eah
    dw      NAME(%1 %+ _16) wrt CODE16
    dw      CODE16
segment CODE16
GLOBALNAME %1 %+ _16
    push    ax

    mov     ax, DATA16
    mov     ds, ax
    mov     es, ax

    pop     ax

%endmacro VBOXFS_32_TO_16 1


;;
; The counter part to VBOXFS_32_TO_16
;
; @param    %1      The function name
;
%macro VBOXFS_16_TO_32 1
    ;jmp far dword NAME(%1 %+ _32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(%1 %+ _32) wrt FLAT
    dw      TEXT32 wrt FLAT
segment TEXT32
GLOBALNAME %1 %+ _32
    push    ax

    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax

    call    KernThunkStackTo32

    pop     ax

%endmacro

;;
; Allocate a GDT selector
;
; @param   %1     The function name
; @param   %2     esp offset to the selector
;
%macro VBOXFS_ALLOCGDTSEL 1
    mov     eax, ss
    mov     es, ax
    lea     edi, [esp + %1]                      ; &sel in ES:DI
    mov     ecx, 1                               ; one selector
    mov     dl, DevHlp_AllocGDTSelector
    call    far [NAME(g_fpfnDevHlp)]
%endmacro

;;
; Map Linear address to a GDT selector
;
; @param   %1     Selector esp offset
; @param   %2     Linear address ebp offset
; @param   %3     Size (immediate)
;
; carry flag if unsuccessful
;
%macro VBOXFS_LINTOGDTSEL 3
    xor     eax, eax
    mov     ax,  [esp + %1]                     ; sel
    mov     ebx, [ebp + %2]                     ; lin
    mov     ecx, %3                             ; size
    mov     dl, DevHlp_LinToGDTSelector
    call    far [NAME(g_fpfnDevHlp)]
%endmacro

;;
; Free GDT selector
;
; @param   %1     Selector esp offset
;
%macro VBOXFS_FREEGDTSEL 1
    mov     ax, [esp + %1]                      ; sel
    mov     dl, DevHlp_FreeGDTSelector
    call    far [NAME(g_fpfnDevHlp)]
%endmacro

;;
; Process to Global
;
; @param   %1     Linear address ebp offset
; @param   %2     size
; @param   %3     Action flags
;
%macro VBOXFS_PROCESSTOGLOBAL 3
    mov    ebx, [ebp + %1]                      ; lin
    mov    ecx, %2                              ; size
    mov    eax, %3                              ; flags
    mov    dl,  DevHlp_VMProcessToGlobal
    call    far [NAME(g_fpfnDevHlp)]
%endmacro


;;
; Take off the old stack frame
;
%macro VBOXFS_EPILOGUE 0
    ; Epilogue
    lea     esp, [ebp - 10h]

    pop     es
    pop     ds
    pop     ebx
    pop     edi
    mov     esp, ebp
    pop     ebp
%endmacro

;;
; Thunks the given 16:16 pointer to a flat pointer.
;
; @param    %1      The ebp offset of the input.
; @param    %2      The esp offset of the output.
; @users    eax
;
%macro VBOXFS_FARPTR_2_FLAT 2
    mov     eax, dword [ebp + (%1)]
    push    eax
    call    KernSelToFlat
    add     esp, 4
    mov     [esp + (%2)], eax
%endmacro


;;
; Put address of an input variable and put it at an output offset.
;
; @param    %1      The esp offset of the input.
; @param    %2      The esp offset of the output.
; @users    eax, edx

%macro VBOXFS_PUTVARADDR 2
    lea     edx, [esp + (%1)]
    mov     ax, ss
    shl     eax, 10h
    mov     ax, dx
    mov     [esp + (%2)], eax
%endmacro


;;
; Converts the 16:16 pointer on stack to a FLAT pointer.
;
; @param    %1      The esp offset of the input
; @param    %2      The ebp offset of the input
; @users    eax, ecx
;
%macro VBOXFS_THUNK_FARPTR_2_FLAT 2
    mov     eax, [esp + (%1)]

    push    eax
    call    KernSelToFlat
    add     sp, 4

    mov     ecx, [ebp + (%2)]
    mov     [ecx], eax
%endmacro


;;
; Thunks the given 16:16 struct sffsd pointer to a flat pointer.
;
; @param    %1      The ebp offset of the input.
; @param    %2      The esp offset of the output.
; @users    eax, ecx
;
%macro VBOXFS_PSFFSD_2_FLAT 2
    lds     cx, [ebp + (%1)]
    and     ecx, 0ffffh
    mov     eax, dword [ecx]
    mov     cx, DATA32 wrt FLAT
    mov     [esp + (%2)], eax
    mov     ds, cx
%endmacro


;;
; Thunks the given 16:16 struct cdfsd pointer to a flat pointer.
;
; @param    %1      The ebp offset of the input.
; @param    %2      The esp offset of the output.
; @users    eax, ecx
;
%macro VBOXFS_PCDFSD_2_FLAT 2
    lds     cx, [ebp + (%1)]
    and     ecx, 0ffffh
    mov     eax, dword [ecx]
    mov     cx, DATA32 wrt FLAT
    mov     [esp + (%2)], eax
    mov     ds, cx
%endmacro

;;
; Thunks the given 16:16 struct fsfsd pointer to a flat pointer.
;
; @param    %1      The ebp offset of the input.
; @param    %2      The esp offset of the output.
; @users    eax, ecx
;
%macro VBOXFS_PFSFSD_2_FLAT 2
    lds     cx, [ebp + (%1)]
    and     ecx, 0ffffh
    mov     eax, dword [ecx]
    mov     cx, DATA32 wrt FLAT
    mov     [esp + (%2)], eax
    mov     ds, cx
%endmacro


;*******************************************************************************
;* External Symbols                                                            *
;*******************************************************************************
segment TEXT32
;segment CODE32
extern KernThunkStackTo32
extern KernThunkStackTo16
extern KernSelToFlat
segment CODE16
extern FSH_FORCENOSWAP
extern DOS16WRITE
extern DOS16GETINFOSEG
extern DOS16GETENV
extern FSH_GETVOLPARM
extern FSH_QSYSINFO
extern FSH_PROBEBUF
extern FSH_WILDMATCH

segment TEXT32
;segment CODE32
extern NAME(FS32_ALLOCATEPAGESPACE)
extern NAME(FS32_ATTACH)
extern NAME(FS32_CANCELLOCKREQUEST)
extern NAME(FS32_CANCELLOCKREQUESTL)
extern NAME(FS32_CHDIR)
extern FS32_CHGFILEPTRL
extern NAME(FS32_CLOSE)
extern NAME(FS32_COMMIT)
extern NAME(FS32_COPY)
extern NAME(FS32_DELETE)
extern NAME(FS32_DOPAGEIO)
extern NAME(FS32_EXIT)
extern NAME(FS32_FILEATTRIBUTE)
extern NAME(FS32_FILEINFO)
extern NAME(FS32_FILEIO)
extern NAME(FS32_FILELOCKS)
extern NAME(FS32_FILELOCKSL)
extern NAME(FS32_FINDCLOSE)
extern NAME(FS32_FINDFIRST)
extern NAME(FS32_FINDFROMNAME)
extern NAME(FS32_FINDNEXT)
extern NAME(FS32_FINDNOTIFYCLOSE)
extern NAME(FS32_FINDNOTIFYFIRST)
extern NAME(FS32_FINDNOTIFYNEXT)
extern NAME(FS32_FLUSHBUF)
extern NAME(FS32_FSCTL)
extern NAME(FS32_FSINFO)
extern NAME(FS32_IOCTL)
extern NAME(FS32_MKDIR)
extern NAME(FS32_MOUNT)
extern NAME(FS32_MOVE)
extern NAME(FS32_NEWSIZEL)
extern NAME(FS32_NMPIPE)
extern NAME(FS32_OPENCREATE)
extern NAME(FS32_OPENPAGEFILE)
extern NAME(FS32_PATHINFO)
extern NAME(FS32_PROCESSNAME)
extern FS32_READ
extern NAME(FS32_RMDIR)
extern NAME(FS32_SETSWAP)
extern NAME(FS32_SHUTDOWN)
extern NAME(FS32_VERIFYUNCNAME)
extern FS32_WRITE

extern NAME(VBoxFSR0Init)


;*******************************************************************************
;*  IFS Helpers                                                                *
;*******************************************************************************
segment TEXT32

;;
; @cproto APIRET APIENTRY FSH32_GETVOLPARM(USHORT hVPB, PVPFSI *ppvpfsi, PVPFSD *ppvpfsd);
VBOXFS_EP32_BEGIN   FSH32_GETVOLPARM, 'FSH32_GETVOLPARM'
    ; switch to 16-bits and reserve place in stack for pvpfsi/pvpfsd and FSH_GETVOLPARM args (2+3=5)
    VBOXFS_32_TO_16     FSH32_GETVOLPARM, 5*4
segment CODE16
    mov     cx, [ebp + 8h]            ; hVPB
    mov     [esp + 2*4], cx
    ; reserve place for ppvpfsi far16 pointer on stack
    VBOXFS_PUTVARADDR 4*4, 1*4        ; ppvpfsi
    ; reserve place for ppvpfsd far16 pointer on stack
    VBOXFS_PUTVARADDR 3*4, 0*4        ; ppvpfsd
    call    far FSH_GETVOLPARM
    ; switch back to 32 bits
    VBOXFS_16_TO_32     FSH32_GETVOLPARM
    ; convert pvpfsd to FLAT
    VBOXFS_THUNK_FARPTR_2_FLAT 2 + 0*4, 4*4
    ; convert pvpfsi to FLAT
    VBOXFS_THUNK_FARPTR_2_FLAT 2 + 1*4, 3*4
    ; restore stack
    VBOXFS_EPILOGUE
    ret
VBOXFS_EP32_END     FSH32_GETVOLPARM


;;
; @cproto APIRET APIENTRY FSH32_QSYSINFO(USHORT index, char *pData, USHORT cbData);
VBOXFS_EP32_BEGIN   FSH32_QSYSINFO, 'FSH32_QSYSINFO'
    ; switch to 16-bits and reserve place in stack for FSH_QSYSINFO args (2*4 bytes)
    VBOXFS_32_TO_16     FSH32_QSYSINFO, 2*4
segment CODE16
    mov     cx, [ebp + 8h]            ; index
    mov     [esp + 6], cx
    ; reserve place for pData far16 pointer on stack
    push  ds
    mov   ax, DATA32 wrt FLAT
    mov   ds, ax
    mov   ecx, _KernTKSSBase
    mov   ecx, [ds:ecx]
    pop   ds
    mov   eax, [ebp + 0ch]             ; get a FLAT pointer to pData
    sub   eax, ecx                     ; convert it
    mov   cx, ss                       ; to a far 16:16
    shl   ecx, 16                      ; pointer
    mov   cx, ax                       ;
    mov   [esp + 2], ecx               ;
    mov   cx, [ebp + 10h]              ; cbData
    mov   [esp], cx
    call  far FSH_QSYSINFO
    ; switch back to 32 bits
    VBOXFS_16_TO_32     FSH32_QSYSINFO
    ; restore stack
    VBOXFS_EPILOGUE
    ret
VBOXFS_EP32_END     FSH32_QSYSINFO


;;
; @cproto APIRET APIENTRY FSH32_PROBEBUF(ULONG operation, char *pData, ULONG cbData);
VBOXFS_EP32_BEGIN   FSH32_PROBEBUF, 'FSH32_PROBEBUF'
    ; switch to 16-bits and reserve place in stack for one selector and three vars
    VBOXFS_32_TO_16     FSH32_PROBEBUF, 10
segment CODE16
    mov     cx, [ebp + 8h]             ; operation
    mov     [esp + 6], cx

    ; alloc a GDT selector for pData
    VBOXFS_ALLOCGDTSEL     8
    jnc   FSH32_PROBEBUF_ok1
    mov   ebx, ERROR_PROTECTION_VIOLATION
    jmp   NAME(FSH32_PROBEBUF_exit2)
FSH32_PROBEBUF_ok1:
    ; Convert address from current process address space to system one
    VBOXFS_PROCESSTOGLOBAL 0xc, [ebp + 10h], [ebp + 8h]
    jnc   FSH32_PROBEBUF_ok2
    mov   ebx, ERROR_PROTECTION_VIOLATION
    jmp   NAME(FSH32_PROBEBUF_exit1)
FSH32_PROBEBUF_ok2:
    mov   [ebp + 0ch], eax
    ; map pData FLAT addr to an allocated selector
    VBOXFS_LINTOGDTSEL     8, 0xc, [ebp + 10h]
    jnc   FSH32_PROBEBUF_ok3
    mov   ebx, ERROR_PROTECTION_VIOLATION
    jmp   NAME(FSH32_PROBEBUF_exit1)
FSH32_PROBEBUF_ok3:
    ; store a far pointer to pData
    mov   eax, [esp + 8]
    shl   eax, 16
    mov   [esp + 2], eax

    mov     cx, [ebp + 10h]            ; cbData
    mov     [esp], cx

    call  far FSH_PROBEBUF

    ; save return code
    xor   ebx, ebx
    mov   bx, ax

    ; -2*4 is because of "ret 8" command at the end of last function
    sub   esp, 8

GLOBALNAME FSH32_PROBEBUF_exit1
    ; free GDT selectors
    VBOXFS_FREEGDTSEL 8
GLOBALNAME FSH32_PROBEBUF_exit2

    add   esp, 8

    ; switch back to 32 bits
    VBOXFS_16_TO_32     FSH32_PROBEBUF

    ; restore return code
    mov   eax, ebx

    ; restore stack
    VBOXFS_EPILOGUE
    ret
VBOXFS_EP32_END     FSH32_PROBEBUF

;;
; APIRET APIENTRY FSH32_WILDMATCH(char *pPat, char *pStr);
VBOXFS_EP32_BEGIN   FSH32_WILDMATCH, 'FSH32_WILDMATCH'
    ; switch to 16-bits and reserve place in stack for two selectors and two far ptrs (2+2=4)
    VBOXFS_32_TO_16     FSH32_WILDMATCH, 4*4
segment CODE16
    ; alloc a GDT selector for pPat
    VBOXFS_ALLOCGDTSEL     3*4
    jc    NAME(FSH32_WILDMATCH_exit2)
    ; map pPat FLAT addr to an allocated selector
    VBOXFS_LINTOGDTSEL     3*4, 0x8, 0x10000
    jc    NAME(FSH32_WILDMATCH_exit2)
    ; store a far pointer to pPat
    mov   eax, [esp + 3*4]
    shl   eax, 16
    mov   [esp + 1*4], eax

    ; alloc a GDT selector for pStr
    VBOXFS_ALLOCGDTSEL     2*4
    jc    NAME(FSH32_WILDMATCH_exit1)
    ; map pStr FLAT addr to an allocated selector
    VBOXFS_LINTOGDTSEL     2*4, 0xc, 0x10000
    jc    NAME(FSH32_WILDMATCH_exit1)
    ; store a far pointer to pStr
    mov   eax, [esp + 2*4]
    shl   eax, 16
    mov   [esp + 0*4], eax

    call  far FSH_WILDMATCH

    ; save return code
    xor   ebx, ebx
    mov   bx, ax

    ; -2*4 is because of "ret 8" command at the end of last function
    sub   esp, 2*4

    ; free GDT selectors
    VBOXFS_FREEGDTSEL 2*4
GLOBALNAME FSH32_WILDMATCH_exit1
    VBOXFS_FREEGDTSEL 3*4
GLOBALNAME FSH32_WILDMATCH_exit2

    add   esp, 2*4

    ; switch back to 32 bits
    VBOXFS_16_TO_32     FSH32_WILDMATCH

    ; restore return code
    mov   eax, ebx

    ; restore stack
    VBOXFS_EPILOGUE
    ret
VBOXFS_EP32_END     FSH32_WILDMATCH

;;
;   void LogPrint(const char *str);
;
;   A 32-bit wrapper for a 16-bit LogPrintf
;   routine from QSINIT / os4ldr / ArcaOS loader
;
;   @param a FLAT pointer to an output string
;
VBOXFS_EP32_BEGIN LogPrint, 'LogPrint'
    ; switch to 16-bits and reserve one dword on a stack
    VBOXFS_32_TO_16     LogPrint, 1*4
segment CODE16
    push  ds
    mov   ax, DATA32 wrt FLAT
    mov   ds, ax
    mov   ecx, _KernTKSSBase
    mov   ecx, [ds:ecx]
    pop   ds
    mov   eax, [ebp + 8h]              ; get a FLAT pointer to the output string
    sub   eax, ecx                     ; convert it
    mov   cx, ss                       ; to a far 16:16
    shl   ecx, 16                      ; pointer
    mov   cx, ax                       ;
    mov   [esp + 0*4], ecx
    call  far [NAME(g_fpLog_printf)]
    ; switch back to 32 bits
    VBOXFS_16_TO_32     LogPrint
    ; restore stack
    VBOXFS_EPILOGUE
    ret
VBOXFS_EP32_END   LogPrint

;*******************************************************************************
;*  Global Variables                                                           *
;*******************************************************************************
segment DATA16

;;
; The file system name.
global FS_NAME
FS_NAME:
    db 'VBOXFS',0

;;
; File system attributes
; The 32-bit version is only used to indicate that this is a 32-bit file system.
;
%define FSA_REMOTE      0001h           ; remote file system.
%define FSA_UNC         0002h           ; implements UNC.
%define FSA_LOCK        0004h           ; needs lock notification.
%define FSA_LVL7        0008h           ; accept level 7 (case preserving path request).
%define FSA_PSVR        0010h           ; (named) pipe server.
%define FSA_LARGEFILE   0020h           ; large file support.
align 16
global FS_ATTRIBUTE
global FS32_ATTRIBUTE
FS_ATTRIBUTE:
FS32_ATTRIBUTE:
    dd FSA_REMOTE + FSA_LARGEFILE + FSA_UNC + FSA_LVL7; + FSA_LOCK

;; 64-bit mask.
; bit 0 - don't get the ring-0 spinlock.
; bit 6 - don't get the subsystem ring-0 spinlock.
global FS_MPSAFEFLAGS2
FS_MPSAFEFLAGS2:
    dd  0
    dd  0

;;
; Set after VBoxFSR0Init16Bit has been called.
GLOBALNAME g_fDoneRing0
    db 0

align 4
;;
; The device helper (IPRT expects this name).
; (This is set by FS_INIT.)
GLOBALNAME g_fpfnDevHlp
    dd 0

;;
; Whether initialization should be verbose or quiet.
GLOBALNAME g_fVerbose
    db 1

;; DEBUGGING DEBUGGING
GLOBALNAME g_u32Info
    dd 0

;GLOBALNAME g_fpfnDos16GetEnv
;    dw  DOS16GETENV
;    dw  seg DOS16GETENV

;GLOBALNAME g_fpfnDos16GetInfoSeg
;    dw  DOS16GETINFOSEG
;    dw  seg DOS16GETINFOSEG

;; Far pointer to DOS16WRITE (corrected set before called).
; Just a 'temporary' hack to work around a wlink/nasm issue.
GLOBALNAME g_fpfnDos16Write
    dw  DOS16WRITE
    dw  seg DOS16WRITE

;; Far pointer to Log_printf routine
;  of QSINIT / os4ldr
GLOBALNAME g_fpLog_printf
    dd  0

;;
; The attach dd data.
GLOBALNAME g_VBoxGuestAttachDD
    dd 0
    dw 0
    dd 0
    dw 0
;;
; The AttachDD name of the VBoxGuest.sys driver.
GLOBALNAME g_szVBoxGuestName
    db VBOXGUEST_DEVICE_NAME, 0
;;
; The VBoxGuest IDC connection data.
GLOBALNAME g_VBoxGuestIDC
    times VBGOS2IDC_size db 0

;;
; This must be present, we've got fixups against it.
segment DATA32
extern _KernTKSSBase

g_pfnDos16Write:
    dd  DOS16WRITE       ; flat

;g_pfnDos16GetInfoSeg:
;    dd  DOS16GETINFOSEG  ; flat

;g_pfnDos16GetEnv:
;    dd  DOS16GETENV      ; flat

GLOBALNAME g_fLog_enable
    dd  0

GLOBALNAME g_fHideLFN
    dd  0

GLOBALNAME g_Cp
    dd  0

;GLOBALNAME g_selGIS
;    dw  0

;GLOBALNAME g_selEnv
;    dw  0

GLOBALNAME g_fLogPrint
    db  0

;
;
;  16-bit entry point thunking.
;  16-bit entry point thunking.
;  16-bit entry point thunking.
;
;
segment CODE16


;;
; @cproto int FS_ALLOCATEPAGESPACE(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG cb, USHORT cbWantContig)
VBOXFS_EP16_BEGIN   FS_ALLOCATEPAGESPACE, 'FS_ALLOCATEPAGESPACE'
VBOXFS_TO_32        FS_ALLOCATEPAGESPACE, 4*4
    movzx   ecx, word [ebp + 08h]       ; cbWantContig
    mov     [esp + 3*4], ecx
    mov     edx, [ebp + 0ah]            ; cb
    mov     [esp + 2*4], edx
    ;VBOXFS_PSFFSD_2_FLAT  0eh, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  0eh, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  12h, 0*4      ; psffsi
    call    NAME(FS32_ALLOCATEPAGESPACE)
VBOXFS_TO_16        FS_ALLOCATEPAGESPACE
    retf    0eh
VBOXFS_EP16_END     FS_ALLOCATEPAGESPACE

;;
; @cproto int FS_ATTACH(USHORT flag, PCSZ pszDev, PVPFSD pvpfsd, PCDFSD pcdfsd, PBYTE pszParm, PUSHORT pcbParm)
;
VBOXFS_EP16_BEGIN   FS_ATTACH, 'FS_ATTACH'
    ;
    ; Initialized ring-0 yet? (this is a likely first entry point)
    ;
    push    ds
    mov     ax, DATA16
    mov     ds, ax
    test    byte [NAME(g_fDoneRing0)], 1
    jnz     .DoneRing0
    call    NAME(VBoxFSR0Init16Bit)
.DoneRing0:
    pop     ds

VBOXFS_TO_32        FS_ATTACH, 6*4
    VBOXFS_FARPTR_2_FLAT  08h, 5*4      ; pcbParm
    VBOXFS_FARPTR_2_FLAT  0ch, 4*4      ; pszParm
    VBOXFS_FARPTR_2_FLAT  10h, 3*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  14h, 2*4      ; pvpfsd
    VBOXFS_FARPTR_2_FLAT  18h, 1*4      ; pszDev
    movzx   ecx, word [ebp + 1ch]       ; fFlag
    mov     [esp + 0*4], ecx
    call    NAME(FS32_ATTACH)
VBOXFS_TO_16        FS_ATTACH
    retf    16h
VBOXFS_EP16_END     FS_ATTACH


;;
; @cproto int FS_CANCELLOCKREQUEST(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelock far *pLockRange)
VBOXFS_EP16_BEGIN   FS_CANCELLOCKREQUEST, 'FS_CANCELLOCKREQUEST'
VBOXFS_TO_32        FS_CANCELLOCKREQUEST, 3*4
    VBOXFS_FARPTR_2_FLAT  08h, 2*4      ; pLockRange
    ;VBOXFS_PSFFSD_2_FLAT  0ch, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  0ch, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  10h, 0*4      ; psffsi
    call    NAME(FS32_CANCELLOCKREQUEST)
VBOXFS_TO_16        FS_CANCELLOCKREQUEST
    retf    0ch
VBOXFS_EP16_END     FS_CANCELLOCKREQUEST


;;
; @cproto int FS_CANCELLOCKREQUESTL(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelockl far *pLockRange)
VBOXFS_EP16_BEGIN   FS_CANCELLOCKREQUESTL, 'FS_CANCELLOCKREQUESTL'
VBOXFS_TO_32        FS_CANCELLOCKREQUESTL, 3*4
    VBOXFS_FARPTR_2_FLAT  08h, 2*4      ; pLockRange
    ;VBOXFS_PSFFSD_2_FLAT  0ch, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  0ch, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  10h, 0*4      ; psffsi
    call    NAME(FS32_CANCELLOCKREQUESTL)
VBOXFS_TO_16        FS_CANCELLOCKREQUESTL
    retf    0ch
VBOXFS_EP16_END     FS_CANCELLOCKREQUESTL


;;
; @cproto int FS_CHDIR(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszDir, USHORT iCurDirEnd)
VBOXFS_EP16_BEGIN   FS_CHDIR, 'FS_CHDIR'
VBOXFS_TO_32        FS_CHDIR, 5*4
    movzx   ecx, word [ebp + 08h]       ; iCurDirEnd
    mov     [esp + 4*4], ecx
    VBOXFS_FARPTR_2_FLAT  0ah, 3*4      ; pszDir
    VBOXFS_FARPTR_2_FLAT  0eh, 2*4      ; pcdfsd (use slow thunk here, see flag)
    VBOXFS_FARPTR_2_FLAT  12h, 1*4      ; pcdfsi
    movzx   eax, word [ebp + 16h]       ; flag
    mov     [esp + 0*4], eax
    call    NAME(FS32_CHDIR)
VBOXFS_TO_16        FS_CHDIR
    retf    10h
VBOXFS_EP16_END     FS_CHDIR


; @cproto int FS_CHGFILEPTR(PSFFSI psffsi, PVBOXSFFSD psffsd, LONG off, USHORT usMethod, USHORT IOflag)
VBOXFS_EP16_BEGIN   FS_CHGFILEPTR, 'FS_CHGFILEPTR'
VBOXFS_TO_32        FS_CHGFILEPTR, 6*4
    movzx   ecx, word [ebp + 08h]       ; IOflag
    mov     [esp + 5*4], ecx
    movzx   edx, word [ebp + 0ah]       ; usMethod
    mov     [esp + 4*4], edx
    mov     eax, [ebp + 0ch]            ; off
    mov     [esp + 2*4], eax
    rol     eax, 1                      ; high dword - is there a better way than this?
    and     eax, 1
    mov     edx, 0ffffffffh
    mul     edx
    mov     [esp + 3*4], eax
    ;VBOXFS_PSFFSD_2_FLAT  10h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  10h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  14h, 0*4      ; psffsi
    call    FS32_CHGFILEPTRL
VBOXFS_TO_16        FS_CHGFILEPTR
    retf    10h
VBOXFS_EP16_END     FS_CHGFILEPTR


;;
; @cproto int FS_CLOSE(USHORT type, USHORT IOflag, PSFFSI psffsi, PVBOXSFFSD psffsd)
;
VBOXFS_EP16_BEGIN   FS_CLOSE, 'FS_CLOSE'
VBOXFS_TO_32        FS_CLOSE, 4*4
    ;VBOXFS_PSFFSD_2_FLAT  08h, 3*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  08h, 3*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  0ch, 2*4      ; psffsi
    movzx   ecx, word [ebp + 10h]       ; IOflag
    mov     [esp + 1*4], ecx
    movzx   edx, word [ebp + 12h]       ; type
    mov     [esp + 0*4], edx
    call    NAME(FS32_CLOSE)
VBOXFS_TO_16        FS_CLOSE
    retf    0ch
VBOXFS_EP16_END     FS_CLOSE


;;
; @cproto int FS_COMMIT(USHORT type, USHORT IOflag, PSFFSI psffsi, PVBOXSFFSD psffsd)
;
VBOXFS_EP16_BEGIN   FS_COMMIT, 'FS_COMMIT'
VBOXFS_TO_32        FS_COMMIT, 4*4
    ;VBOXFS_PSFFSD_2_FLAT  08h, 3*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  08h, 3*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  0ch, 2*4      ; psffsi
    movzx   ecx, word [ebp + 10h]       ; IOflag
    mov     [esp + 1*4], ecx
    movzx   edx, word [ebp + 12h]       ; type
    mov     [esp + 0*4], edx
    call    NAME(FS32_COMMIT)
VBOXFS_TO_16        FS_COMMIT
    retf    0ch
VBOXFS_EP16_END     FS_COMMIT

;;
; @cproto int FS_COPY(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszSrc, USHORT iSrcCurDirEnd
;                     PCSZ pszDst, USHORT iDstCurDirEnd, USHORT nameType);
VBOXFS_EP16_BEGIN   FS_COPY, 'FS_COPY'
VBOXFS_TO_32        FS_COPY, 8*4
    movzx   ecx, word [ebp + 08h]       ; nameType
    mov     [esp + 7*4], ecx
    movzx   edx, word [ebp + 0ah]       ; iDstCurDirEnd
    mov     [esp + 6*4], edx
    VBOXFS_FARPTR_2_FLAT  0ch, 5*4      ; pszDst
    movzx   eax, word [ebp + 10h]       ; iSrcCurDirEnd
    mov     [esp + 4*4], eax
    VBOXFS_FARPTR_2_FLAT  12h, 3*4      ; pszSrc
    ;VBOXFS_PCDFSD_2_FLAT  16h, 2*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  16h, 2*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  1ah, 1*4      ; psffsi
    movzx   ecx, word [ebp + 1eh]       ; flag
    mov     [esp + 0*4], ecx
    call    NAME(FS32_COPY)
VBOXFS_TO_16        FS_COPY
    retf    18h
VBOXFS_EP16_END     FS_COPY


;;
; @cproto int FS_DELETE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszFile, USHORT iCurDirEnd);
VBOXFS_EP16_BEGIN   FS_DELETE, 'FS_DELETE'
VBOXFS_TO_32        FS_DELETE, 4*4
    movzx   ecx, word [ebp + 08h]       ; iCurDirEnd
    mov     [esp + 3*4], ecx
    VBOXFS_FARPTR_2_FLAT  0ah, 2*4      ; pszFile
    ;VBOXFS_PCDFSD_2_FLAT  0eh, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  0eh, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  12h, 0*4      ; pcdfsi
    call    NAME(FS32_DELETE)
VBOXFS_TO_16        FS_DELETE
    retf    0eh
VBOXFS_EP16_END FS_DELETE


;;
; @cproto int FS_DOPAGEIO(PSFFSI psffsi, PVBOXSFFSD psffsd, struct PageCmdHeader far *pList)
VBOXFS_EP16_BEGIN   FS_DOPAGEIO, 'FS_DOPAGEIO'
VBOXFS_TO_32        FS_DOPAGEIO, 3*4
    VBOXFS_FARPTR_2_FLAT  08h, 2*4      ; pList
    ;VBOXFS_PSFFSD_2_FLAT  0ch, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  0ch, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  10h, 0*4      ; psffsi
    call    NAME(FS32_DOPAGEIO)
VBOXFS_TO_16        FS_DOPAGEIO
    retf    0ch
VBOXFS_EP16_END     FS_DOPAGEIO

;;
; @cproto void FS_EXIT(USHORT uid, USHORT pid, USHORT pdb)
VBOXFS_EP16_BEGIN   FS_EXIT, 'FS_EXIT'
    ;
    ; Initialized ring-0 yet? (this is a likely first entry point)
    ;
    push    ds
    mov     ax, DATA16
    mov     ds, ax
    test    byte [NAME(g_fDoneRing0)], 1
    jnz     .DoneRing0
    call    NAME(VBoxFSR0Init16Bit)
.DoneRing0:
    pop     ds

VBOXFS_TO_32        FS_EXIT, 3*4
    movzx   ecx, word [ebp + 08h]       ; pdb
    mov     [esp + 2*4], ecx
    movzx   edx, word [ebp + 0ah]       ; pib
    mov     [esp + 1*4], edx
    movzx   eax, word [ebp + 0ch]       ; uid
    mov     [esp + 0*4], eax
    call    NAME(FS32_EXIT)
VBOXFS_TO_16        FS_EXIT
    retf    6h
VBOXFS_EP16_END     FS_EXIT


;;
; @cproto int FS_FILEATTRIBUTE(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd, PUSHORT pAttr);
;
VBOXFS_EP16_BEGIN   FS_FILEATTRIBUTE, 'FS_FILEATTRIBUTE'
VBOXFS_TO_32        FS_FILEATTRIBUTE, 6*4
    VBOXFS_FARPTR_2_FLAT  08h, 5*4      ; pAttr
    movzx   ecx, word [ebp + 0ch]       ; iCurDirEnd
    mov     [esp + 4*4], ecx
    VBOXFS_FARPTR_2_FLAT  0eh, 3*4      ; pszName
    ;VBOXFS_PCDFSD_2_FLAT  12h, 2*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  12h, 2*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  16h, 1*4      ; pcdfsi
    movzx   edx, word [ebp + 1ah]       ; flag
    mov     [esp + 0*4], edx
    call    NAME(FS32_FILEATTRIBUTE)
VBOXFS_TO_16        FS_FILEATTRIBUTE
    retf    14h
VBOXFS_EP16_END     FS_FILEATTRIBUTE


;;
; @cproto  int FS_FILEINFO(USHORT flag, PSFFSI psffsi, PVBOXSFFSD psffsd, USHORT level,
;                          PBYTE pData, USHORT cbData, USHORT IOflag);
VBOXFS_EP16_BEGIN   FS_FILEINFO, 'FS_FILEINFO'
VBOXFS_TO_32        FS_FILEINFO, 7*4
    movzx   ecx, word [ebp + 08h]       ; IOflag
    mov     [esp + 6*4], ecx
    movzx   edx, word [ebp + 0ah]       ; cbData
    mov     [esp + 5*4], edx
    VBOXFS_FARPTR_2_FLAT  0ch, 4*4      ; pData
    movzx   eax, word [ebp + 10h]       ; level
    mov     [esp + 3*4], eax
    ;VBOXFS_PSFFSD_2_FLAT  12h, 2*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  12h, 2*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  16h, 1*4      ; psffsi
    movzx   ecx, word [ebp + 1ah]       ; flag
    mov     [esp + 0*4], ecx
    call    NAME(FS32_FILEINFO)
VBOXFS_TO_16        FS_FILEINFO
    retf    14h
VBOXFS_EP16_END     FS_FILEINFO


;;
; @cproto  int FS_FILEIO(PSFFSI psffsi, PVBOXSFFSD psffsd, PBYTE pCmdList, USHORT cbCmdList,
;                        PUSHORT poError, USHORT IOflag);
VBOXFS_EP16_BEGIN   FS_FILEIO, 'FS_FILEIO'
VBOXFS_TO_32        FS_FILEIO, 6*4
    movzx   ecx, word [ebp + 08h]       ; IOFlag
    mov     [esp + 5*4], ecx
    VBOXFS_FARPTR_2_FLAT  0ah, 4*4      ; poError
    movzx   edx, word [ebp + 0eh]       ; cbCmdList
    mov     [esp + 3*4], edx
    VBOXFS_FARPTR_2_FLAT  10h, 2*4      ; pCmdList
    ;VBOXFS_PSFFSD_2_FLAT  14h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  14h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  18h, 0*4      ; psffsi
    call    NAME(FS32_FILEIO)
VBOXFS_TO_16        FS_FILEIO
    retf    14h
VBOXFS_EP16_END     FS_FILEIO


;;
; @cproto  int FS_FILELOCKS(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelock far *pUnLockRange
;                           struct filelock far *pLockRange, ULONG timeout, ULONG flags)
VBOXFS_EP16_BEGIN   FS_FILELOCKS, 'FS_FILELOCKS'
VBOXFS_TO_32        FS_FILELOCKS, 6*4
    mov     ecx, [ebp + 08h]            ; flags
    mov     [esp + 5*4], ecx
    mov     edx, [ebp + 0ch]            ; timeout
    mov     [esp + 4*4], edx
    VBOXFS_FARPTR_2_FLAT  10h, 3*4      ; pLockRange
    VBOXFS_FARPTR_2_FLAT  14h, 2*4      ; pUnLockRange
    ;VBOXFS_PSFFSD_2_FLAT  18h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  18h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  1ch, 0*4      ; psffsi
    call    NAME(FS32_FILELOCKS)
VBOXFS_TO_16        FS_FILELOCKS
    retf    18h
VBOXFS_EP16_END     FS_FILELOCKS


;;
; @cproto  int FS_FILELOCKSL(PSFFSI psffsi, PVBOXSFFSD psffsd, struct filelockl far *pUnLockRange
;                            struct filelockl far *pLockRange, ULONG timeout, ULONG flags)
VBOXFS_EP16_BEGIN   FS_FILELOCKSL, 'FS_FILELOCKSL'
VBOXFS_TO_32        FS_FILELOCKSL, 6*4
    mov     ecx, [ebp + 08h]            ; flags
    mov     [esp + 5*4], ecx
    mov     edx, [ebp + 0ch]            ; timeout
    mov     [esp + 4*4], edx
    VBOXFS_FARPTR_2_FLAT  10h, 3*4      ; pLockRange
    VBOXFS_FARPTR_2_FLAT  14h, 2*4      ; pUnLockRange
    ;VBOXFS_PSFFSD_2_FLAT  18h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  18h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  1ch, 0*4      ; psffsi
    call    NAME(FS32_FILELOCKSL)
VBOXFS_TO_16        FS_FILELOCKSL
    retf    18h
VBOXFS_EP16_END     FS_FILELOCKSL


;;
; @cproto int FS_FINDCLOSE(PFSFSI pfsfsi, PVBOXSFFS pfsfsd);
;
VBOXFS_EP16_BEGIN   FS_FINDCLOSE, 'FS_FINDCLOSE'
VBOXFS_TO_32        FS_FINDCLOSE, 2*4
    ;VBOXFS_PFSFSD_2_FLAT  08h, 1*4      ; pfsfsd
    VBOXFS_FARPTR_2_FLAT  08h, 1*4      ; pfsfsd
    VBOXFS_FARPTR_2_FLAT  0ch, 0*4      ; pfsfsi
    call    NAME(FS32_FINDCLOSE)
VBOXFS_TO_16        FS_FINDCLOSE
    retf    8h
VBOXFS_EP16_END     FS_FINDCLOSE


;;
; @cproto int FS_FINDFIRST(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd, USHORT attr,
;                          PFSFSI pfsfsi, PVBOXSFFS pfsfsd, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
;                          USHORT level, USHORT flags);
;
VBOXFS_EP16_BEGIN   FS_FINDFIRST, 'FS_FINDFIRST'
VBOXFS_TO_32        FS_FINDFIRST, 12*4
    movzx   ecx, word [ebp + 08h]       ; flags
    mov     [esp + 11*4], ecx
    movzx   edx, word [ebp + 0ah]       ; level
    mov     [esp + 10*4], edx
    VBOXFS_FARPTR_2_FLAT  0ch, 9*4      ; pcMatch
    movzx   eax, word [ebp + 10h]       ; cbData
    mov     [esp + 8*4], eax
    VBOXFS_FARPTR_2_FLAT  12h, 7*4      ; pbData
    VBOXFS_FARPTR_2_FLAT  16h, 6*4      ; pfsfsd
    VBOXFS_FARPTR_2_FLAT  1ah, 5*4      ; pfsfsi
    movzx   ecx, word [ebp + 1eh]       ; attr
    mov     [esp + 4*4], ecx
    movzx   edx, word [ebp + 20h]       ; iCurDirEnd
    mov     [esp + 3*4], edx
    VBOXFS_FARPTR_2_FLAT  22h, 2*4      ; pszName
    ;VBOXFS_PCDFSD_2_FLAT  26h, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  26h, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  2ah, 0*4      ; pcdfsi
    call    NAME(FS32_FINDFIRST)
VBOXFS_TO_16        FS_FINDFIRST
    retf    26h
VBOXFS_EP16_END FS_FINDFIRST


;;
; @cproto int FS_FINDFROMNAME(PFSFSI pfsfsi, PVBOXSFFS pfsfsd, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
;                             USHORT level, ULONG position, PCSZ pszName, USHORT flag)
;
VBOXFS_EP16_BEGIN   FS_FINDFROMNAME, 'FS_FINDFROMNAME'
VBOXFS_TO_32        FS_FINDFROMNAME, 9*4
    movzx   ecx, word [ebp + 08h]       ; flag
    mov     [esp + 8*4], ecx
    VBOXFS_FARPTR_2_FLAT  0ah, 7*4      ; pszName
    mov     edx, [ebp + 0eh]            ; position
    mov     [esp + 6*4], edx
    movzx   eax, word [ebp + 12h]       ; level
    mov     [esp + 5*4], eax
    VBOXFS_FARPTR_2_FLAT  14h, 4*4      ; pcMatch
    movzx   eax, word [ebp + 18h]       ; cbData
    mov     [esp + 3*4], eax
    VBOXFS_FARPTR_2_FLAT  1ah, 2*4      ; pbData
    ;VBOXFS_PFSFSD_2_FLAT  1eh, 1*4      ; pfsfsd
    VBOXFS_FARPTR_2_FLAT  1eh, 1*4      ; pfsfsd
    VBOXFS_FARPTR_2_FLAT  22h, 0*4      ; pfsfsi
    call    NAME(FS32_FINDFROMNAME)
VBOXFS_TO_16        FS_FINDFROMNAME
    retf    1eh
VBOXFS_EP16_END     FS_FINDFROMNAME


;;
; @cproto int FS_FINDNEXT(PFSFSI pfsfsi, PVBOXSFFS pfsfsd, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
;                         USHORT level, USHORT flag)
;
VBOXFS_EP16_BEGIN   FS_FINDNEXT, 'FS_FINDNEXT'
VBOXFS_TO_32        FS_FINDNEXT, 7*4
    movzx   ecx, word [ebp + 08h]       ; flag
    mov     [esp + 6*4], ecx
    movzx   eax, word [ebp + 0ah]       ; level
    mov     [esp + 5*4], eax
    VBOXFS_FARPTR_2_FLAT  0ch, 4*4      ; pcMatch
    movzx   eax, word [ebp + 10h]       ; cbData
    mov     [esp + 3*4], eax
    VBOXFS_FARPTR_2_FLAT  12h, 2*4      ; pbData
    ;VBOXFS_PFSFSD_2_FLAT  16h, 1*4      ; pfsfsd
    VBOXFS_FARPTR_2_FLAT  16h, 1*4      ; pfsfsd
    VBOXFS_FARPTR_2_FLAT  1ah, 0*4      ; pfsfsi
    call    NAME(FS32_FINDNEXT)
VBOXFS_TO_16        FS_FINDNEXT
    retf    16h
VBOXFS_EP16_END     FS_FINDNEXT


;;
; @cproto int FS_FINDNOTIFYCLOSE(USHORT handle);
;
VBOXFS_EP16_BEGIN   FS_FINDNOTIFYCLOSE, 'FS_FINDNOTIFYCLOSE'
VBOXFS_TO_32        FS_FINDNOTIFYCLOSE, 1*4
    movzx   ecx, word [ebp + 08h]       ; handle
    mov     [esp + 0*4], ecx
    call    NAME(FS32_FINDNOTIFYCLOSE)
VBOXFS_TO_16        FS_FINDNOTIFYCLOSE
    retf    2h
VBOXFS_EP16_END     FS_FINDNOTIFYCLOSE


;;
; @cproto int FS_FINDNOTIFYFIRST(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd, USHORT attr,
;                                PUSHORT pHandle, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
;                                USHORT level, USHORT flags);
;
VBOXFS_EP16_BEGIN   FS_FINDNOTIFYFIRST, 'FS_FINDNOTIFYFIRST'
VBOXFS_TO_32        FS_FINDNOTIFYFIRST, 11*4
    movzx   ecx, word [ebp + 08h]       ; flags
    mov     [esp + 10*4], ecx
    movzx   edx, word [ebp + 0ah]       ; level
    mov     [esp + 9*4], edx
    VBOXFS_FARPTR_2_FLAT  0ch, 8*4      ; pcMatch
    movzx   eax, word [ebp + 10h]       ; cbData
    mov     [esp + 7*4], eax
    VBOXFS_FARPTR_2_FLAT  12h, 6*4      ; pbData
    VBOXFS_FARPTR_2_FLAT  16h, 5*4      ; pHandle
    movzx   ecx, word [ebp + 1ah]       ; attr
    mov     [esp + 4*4], ecx
    movzx   edx, word [ebp + 1ch]       ; iCurDirEnd
    mov     [esp + 3*4], edx
    VBOXFS_FARPTR_2_FLAT  1eh, 2*4      ; pszName
    ;VBOXFS_PCDFSD_2_FLAT  22h, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  22h, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  26h, 0*4      ; pcdfsi
    call    NAME(FS32_FINDNOTIFYFIRST)
VBOXFS_TO_16        FS_FINDNOTIFYFIRST
    retf    22h
VBOXFS_EP16_END     FS_FINDNOTIFYFIRST


;;
; @cproto int FS_FINDNOTIFYNEXT(USHORT handle, PBYTE pbData, USHORT cbData, PUSHORT pcMatch,
;                               USHORT level, ULONG timeout)
;
VBOXFS_EP16_BEGIN FS_FINDNOTIFYNEXT, 'FS_FINDNOTIFYNEXT'
VBOXFS_TO_32        FS_FINDNOTIFYNEXT, 6*4
    mov     ecx, [ebp + 08h]            ; timeout
    mov     [esp + 5*4], ecx
    movzx   edx, word [ebp + 0ch]       ; level
    mov     [esp + 4*4], edx
    VBOXFS_FARPTR_2_FLAT  0eh, 3*4      ; pcMatch
    movzx   eax, word [ebp + 12h]       ; cbData
    mov     [esp + 2*4], eax
    VBOXFS_FARPTR_2_FLAT  14h, 1*4      ; pbData
    movzx   ecx, word [ebp + 18h]       ; handle
    mov     [esp + 0*4], ecx
    call    NAME(FS32_FINDNOTIFYNEXT)
VBOXFS_TO_16        FS_FINDNOTIFYNEXT
    retf    12h
VBOXFS_EP16_END     FS_FINDNOTIFYNEXT


;; @cproto int FS_FLUSHBUF(USHORT hVPB, USHORT flag);
VBOXFS_EP16_BEGIN FS_FLUSHBUF, 'FS_FLUSHBUF'
VBOXFS_TO_32        FS_FLUSHBUF, 2*4
    movzx   edx, word [ebp + 08h]       ; flag
    mov     [esp + 1*4], edx
    ;movzx   eax, word [ebp + 0ch]       ; hVPB
    movzx   eax, word [ebp + 0ah]       ; hVPB
    mov     [esp + 0*4], eax
    call    NAME(FS32_FLUSHBUF)
VBOXFS_TO_16        FS_FLUSHBUF
    retf    4h
VBOXFS_EP16_END FS_FLUSHBUF


;; @cproto int FS_FSCTL(union argdat far *pArgdat, USHORT iArgType, USHORT func,
;                       PVOID pParm, USHORT lenParm, PUSHORT plenParmIO,
;                       PVOID pData, USHORT lenData, PUSHORT plenDataIO);
VBOXFS_EP16_BEGIN FS_FSCTL, 'FS_FSCTL'
    ;
    ; Initialized ring-0 yet? (this is a likely first entry point)
    ;
    push    ds
    mov     ax, DATA16
    mov     ds, ax
    test    byte [NAME(g_fDoneRing0)], 1
    jnz     .DoneRing0
    call    NAME(VBoxFSR0Init16Bit)
.DoneRing0:
    pop     ds

VBOXFS_TO_32        FS_FSCTL, 9*4
    VBOXFS_FARPTR_2_FLAT  08h, 8*4      ; plenDataIO
    movzx   ecx, word [ebp + 0ch]       ; lenData
    mov     [esp + 7*4], ecx
    VBOXFS_FARPTR_2_FLAT  0eh, 6*4      ; pData
    VBOXFS_FARPTR_2_FLAT  12h, 5*4      ; plenParmIO
    movzx   ecx, word [ebp + 16h]       ; lenParm
    mov     [esp + 4*4], ecx
    VBOXFS_FARPTR_2_FLAT  18h, 3*4      ; pParm
    movzx   edx, word [ebp + 1ch]       ; func
    mov     [esp + 2*4], edx
    movzx   eax, word [ebp + 1eh]       ; iArgType
    mov     [esp + 1*4], eax
    VBOXFS_FARPTR_2_FLAT  20h, 0*4      ; pArgdat
    call    NAME(FS32_FSCTL)
VBOXFS_TO_16        FS_FSCTL
    retf    1ch
VBOXFS_EP16_END FS_FSCTL


;; @cproto int FS_FSINFO(USHORT flag, USHORT hVPB, PBYTE pbData, USHORT cbData, USHORT level)
VBOXFS_EP16_BEGIN FS_FSINFO, 'FS_FSINFO'
VBOXFS_TO_32        FS_FSINFO, 5*4
    movzx   ecx, word [ebp + 08h]       ; level
    mov     [esp + 4*4], ecx
    movzx   edx, word [ebp + 0ah]       ; cbData
    mov     [esp + 3*4], edx
    VBOXFS_FARPTR_2_FLAT  0ch, 2*4      ; pbData
    movzx   edx, word [ebp + 10h]       ; hVPB
    mov     [esp + 1*4], edx
    movzx   eax, word [ebp + 12h]       ; flag
    mov     [esp + 0*4], eax
    call    NAME(FS32_FSINFO)
VBOXFS_TO_16        FS_FSINFO
    ;retf    14h
    retf    0ch
VBOXFS_EP16_END     FS_FSINFO


;;
; @cproto int FS_IOCTL(PSFFSI psffsi, PVBOXSFFSD psffsd, USHORT cat, USHORT func,
;                      PVOID pParm, USHORT lenParm, PUSHORT plenParmIO,
;                      PVOID pData, USHORT lenData, PUSHORT plenDataIO);
VBOXFS_EP16_BEGIN   FS_IOCTL, 'FS_IOCTL'
VBOXFS_TO_32        FS_IOCTL, 10*4
    VBOXFS_FARPTR_2_FLAT  08h, 9*4      ; plenDataIO
    movzx   ecx, word [ebp + 0ch]       ; lenData
    mov     [esp + 8*4], ecx
    VBOXFS_FARPTR_2_FLAT  0eh, 7*4      ; pData
    VBOXFS_FARPTR_2_FLAT  12h, 6*4      ; plenParmIO
    movzx   ecx, word [ebp + 16h]       ; lenParm
    mov     [esp + 5*4], ecx
    VBOXFS_FARPTR_2_FLAT  18h, 4*4      ; pParm
    movzx   edx, word [ebp + 1ch]       ; func
    mov     [esp + 3*4], edx
    movzx   eax, word [ebp + 1eh]       ; cat
    mov     [esp + 2*4], eax
    ;VBOXFS_PSFFSD_2_FLAT  20h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  20h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  24h, 0*4      ; psffsi
    call    NAME(FS32_IOCTL)
VBOXFS_TO_16        FS_IOCTL
    retf    20h
VBOXFS_EP16_END     FS_IOCTL


;;
; @cproto int FS_MKDIR(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd,
;                      PBYTE pEABuf, USHORT flag);
VBOXFS_EP16_BEGIN   FS_MKDIR, 'FS_MKDIR'
VBOXFS_TO_32        FS_MKDIR, 6*4
    movzx   ecx, word [ebp + 08h]       ; flag
    mov     [esp + 5*4], ecx
    VBOXFS_FARPTR_2_FLAT  0ah, 4*4      ; pEABuf
    movzx   edx, word [ebp + 0eh]       ; iCurDirEnd
    mov     [esp + 3*4], edx
    VBOXFS_FARPTR_2_FLAT  10h, 2*4      ; pszName
    ;VBOXFS_PCDFSD_2_FLAT  14h, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  14h, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  18h, 0*4      ; pcdfsi
    call    NAME(FS32_MKDIR)
VBOXFS_TO_16        FS_MKDIR
    retf    14h
VBOXFS_EP16_END     FS_MKDIR


;;
; @cproto int FS_MOUNT(USHORT flag, PVPFSI pvpfsi, PVBOXSFVP pvpfsd, USHORT hVPB, PCSZ pszBoot)
VBOXFS_EP16_BEGIN   FS_MOUNT, 'FS_MOUNT'
    ;
    ; Initialized ring-0 yet? (this is a likely first entry point)
    ;
    push    ds
    mov     ax, DATA16
    mov     ds, ax
    test    byte [NAME(g_fDoneRing0)], 1
    jnz     .DoneRing0
    call    NAME(VBoxFSR0Init16Bit)
.DoneRing0:
    pop     ds

VBOXFS_TO_32        FS_MOUNT, 5*4
    VBOXFS_FARPTR_2_FLAT  08h, 4*4      ; pszBoot
    movzx   ecx, word [ebp + 0ch]       ; hVPB
    mov     [esp + 3*4], ecx
    VBOXFS_FARPTR_2_FLAT  0eh, 2*4      ; pvpfsd
    VBOXFS_FARPTR_2_FLAT  12h, 1*4      ; pvpfsi
    movzx   ecx, word [ebp + 16h]       ; flag
    mov     [esp + 0*4], ecx
    call    NAME(FS32_MOUNT)
VBOXFS_TO_16        FS_MOUNT
    retf    10h
VBOXFS_EP16_END     FS_MOUNT


;;
; @cproto int FS_MOVE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszSrc, USHORT iSrcCurDirEnd
;                     PCSZ pszDst, USHORT iDstCurDirEnd, USHORT type)
VBOXFS_EP16_BEGIN   FS_MOVE, 'FS_MOVE'
VBOXFS_TO_32        FS_MOVE, 7*4
    movzx   ecx, word [ebp + 08h]       ; type
    mov     [esp + 6*4], ecx
    movzx   edx, word [ebp + 0ah]       ; iDstCurDirEnd
    mov     [esp + 5*4], edx
    VBOXFS_FARPTR_2_FLAT  0ch, 4*4      ; pszDst
    movzx   eax, word [ebp + 10h]       ; iSrcCurDirEnd
    mov     [esp + 3*4], eax
    VBOXFS_FARPTR_2_FLAT  12h, 2*4      ; pszSrc
    ;VBOXFS_PCDFSD_2_FLAT  16h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  16h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  1ah, 0*4      ; psffsi
    call    NAME(FS32_MOVE)
VBOXFS_TO_16        FS_MOVE
    retf    16h
VBOXFS_EP16_END     FS_MOVE


;;
; @cproto int FS_NEWSIZE(PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG cbFile, USHORT IOflag);
VBOXFS_EP16_BEGIN   FS_NEWSIZE, 'FS_NEWSIZE'
VBOXFS_TO_32        FS_NEWSIZE, 5*4     ; thunking to longlong edition.
    movzx   ecx, word [ebp + 08h]       ; IOflag
    mov     [esp + 4*4], ecx
    mov     eax, [ebp + 0ah]            ; cbFile (ULONG -> LONGLONG)
    mov     dword [esp + 3*4], 0
    mov     [esp + 2*4], eax
    ;VBOXFS_PSFFSD_2_FLAT  0eh, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  0eh, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  12h, 0*4      ; psffsi
    call            NAME(FS32_NEWSIZEL)
VBOXFS_TO_16        FS_NEWSIZE
    retf    0eh
VBOXFS_EP16_END     FS_NEWSIZE


;;
; @cproto int FS_NEWSIZEL(PSFFSI psffsi, PVBOXSFFSD psffsd, LONGLONG cbFile, USHORT IOflag);
VBOXFS_EP16_BEGIN FS_NEWSIZEL, 'FS_NEWSIZEL'
VBOXFS_TO_32        FS_NEWSIZEL, 5*4
    movzx   ecx, word [ebp + 08h]       ; IOflag
    mov     [esp + 4*4], ecx
    mov     eax, [ebp + 0ah]            ; cbFile
    mov     edx, [ebp + 0eh]
    mov     [esp + 3*4], edx
    mov     [esp + 2*4], eax
    ;VBOXFS_PSFFSD_2_FLAT  12h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  12h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  16h, 0*4      ; psffsi
    call            NAME(FS32_NEWSIZEL)
VBOXFS_TO_16        FS_NEWSIZEL
    retf    12h
VBOXFS_EP16_END FS_NEWSIZEL


;;
; @cproto int FS_NMPIPE(PSFFSI psffsi, PVBOXSFFSD psffsd, USHORT OpType, union npoper far *pOpRec,
;                       PBYTE pData, PCSZ pszName);
VBOXFS_EP16_BEGIN   FS_NMPIPE, 'FS_NMPIPE'
VBOXFS_TO_32        FS_NMPIPE, 6*4
    VBOXFS_FARPTR_2_FLAT  08h, 5*4      ; pszName
    VBOXFS_FARPTR_2_FLAT  0ch, 4*4      ; pData
    VBOXFS_FARPTR_2_FLAT  10h, 3*4      ; pOpRec
    movzx   ecx, word [ebp + 14h]       ; OpType
    mov     [esp + 2*4], ecx
    VBOXFS_FARPTR_2_FLAT  16h, 1*4      ; psffsd (take care...)
    VBOXFS_FARPTR_2_FLAT  1ah, 0*4      ; psffsi
    call            NAME(FS32_NMPIPE)
VBOXFS_TO_16        FS_NMPIPE
    retf    16h
VBOXFS_EP16_END     FS_NMPIPE


;;
; @cproto int FS_OPENCREATE(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd,
;                           PSFFSI psffsi, PVBOXSFFSD psffsd, ULONG ulOpenMode, USHORT usOpenFlag,
;                           PUSHORT pusAction, USHORT usAttr, PBYTE pcEABuf, PUSHORT pfgenflag);
VBOXFS_EP16_BEGIN   FS_OPENCREATE, 'FS_OPENCREATE'
VBOXFS_TO_32        FS_OPENCREATE, 12*4
    VBOXFS_FARPTR_2_FLAT  08h, 11*4     ; pfgenflag
    VBOXFS_FARPTR_2_FLAT  0ch, 10*4     ; pcEABuf
    movzx   ecx, word [ebp + 10h]       ; usAttr
    mov     [esp + 9*4], ecx
    VBOXFS_FARPTR_2_FLAT  12h, 8*4      ; pusAction
    movzx   edx, word [ebp + 16h]       ; usOpenFlag
    mov     [esp + 7*4], edx
    mov     eax, [ebp + 18h]            ; ulOpenMode
    mov     [esp + 6*4], eax
    VBOXFS_FARPTR_2_FLAT  1ch, 5*4      ; psffsd (new, no short cuts)
    VBOXFS_FARPTR_2_FLAT  20h, 4*4      ; psffsi
    movzx   ecx, word [ebp + 24h]       ; iCurDirEnd
    mov     [esp + 3*4], ecx
    VBOXFS_FARPTR_2_FLAT  26h, 2*4      ; pszName
    ;VBOXFS_PCDFSD_2_FLAT  2ah, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  2ah, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  2eh, 0*4      ; pcdfsi
    call    NAME(FS32_OPENCREATE)
VBOXFS_TO_16        FS_OPENCREATE
    retf    42
VBOXFS_EP16_END FS_OPENCREATE


;;
; @cproto int FS_OPENPAGEFILE(PULONG pFlag, PULONG pcMaxReq, PCSZ pszName, PSFFSI psffsi, PVBOXSFFSD psffsd,
;                             USHORT ulOpenMode, USHORT usOpenFlag, USHORT usAttr, ULONG Reserved)
VBOXFS_EP16_BEGIN   FS_OPENPAGEFILE, 'FS_OPENPAGEFILE'
VBOXFS_TO_32        FS_OPENPAGEFILE, 9*4
    mov     ecx, [ebp + 08h]            ; Reserved
    mov     [esp + 8*4], ecx
    movzx   edx, word [ebp + 0ch]       ; usAttr
    mov     [esp + 7*4], edx
    movzx   eax, word [ebp + 0eh]       ; usOpenFlag
    mov     [esp + 6*4], eax
    movzx   ecx, word [ebp + 10h]       ; usOpenMode
    mov     [esp + 5*4], ecx
    VBOXFS_FARPTR_2_FLAT  12h, 4*4      ; psffsd (new, no short cuts)
    VBOXFS_FARPTR_2_FLAT  16h, 3*4      ; psffsi
    VBOXFS_FARPTR_2_FLAT  1ah, 2*4      ; pszName
    VBOXFS_FARPTR_2_FLAT  1eh, 1*4      ; pcMaxReq
    VBOXFS_FARPTR_2_FLAT  22h, 0*4      ; pFlag
    call    NAME(FS32_OPENPAGEFILE)
VBOXFS_TO_16        FS_OPENPAGEFILE
    retf    1eh
VBOXFS_EP16_END     FS_OPENPAGEFILE


;;
; @cproto int FS_PATHINFO(USHORT flag, PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnt,
;                         USHORT level, PBYTE pData, USHORT cbData);
VBOXFS_EP16_BEGIN   FS_PATHINFO, 'FS_PATHINFO'
VBOXFS_TO_32        FS_PATHINFO, 8*4
    movzx   ecx, word [ebp + 08h]       ; cbData
    mov     [esp + 7*4], ecx
    VBOXFS_FARPTR_2_FLAT  0ah, 6*4      ; pData
    movzx   edx, word [ebp + 0eh]       ; level
    mov     [esp + 5*4], edx
    movzx   eax, word [ebp + 10h]       ; iCurDirEnd
    mov     [esp + 4*4], eax
    VBOXFS_FARPTR_2_FLAT  12h, 3*4      ; pszName
    ;VBOXFS_PCDFSD_2_FLAT  16h, 2*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  16h, 2*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  1ah, 1*4      ; pcdfsi
    movzx   edx, word [ebp + 1eh]       ; flag
    mov     [esp + 0*4], edx
    call    NAME(FS32_PATHINFO)
VBOXFS_TO_16        FS_PATHINFO
    retf    18h
VBOXFS_EP16_END     FS_PATHINFO


;; @cproto int FS_PROCESSNAME(PSZ pszName);
VBOXFS_EP16_BEGIN FS_PROCESSNAME, 'FS_PROCESSNAME'
VBOXFS_TO_32        FS_PROCESSNAME, 1*4
    VBOXFS_FARPTR_2_FLAT  08h, 0*4      ; pszName
    call    NAME(FS32_PROCESSNAME)
VBOXFS_TO_16        FS_PROCESSNAME
    retf    4h
VBOXFS_EP16_END FS_PROCESSNAME


;;
; @cproto int FS_READ(PSFFSI psffsi, PVBOXSFFSD psffsd, PBYTE pbData, PUSHORT pcbData, USHORT IOflag)
VBOXFS_EP16_BEGIN   FS_READ, 'FS_READ'
VBOXFS_TO_32        FS_READ, 6*4        ; extra local for ULONG cbDataTmp.
;    push    es
    movzx   ecx, word [ebp + 08h]       ; IOflag
    mov     [esp + 4*4], ecx
    les     dx, [ebp + 0ah]             ; cbDataTmp = *pcbData;
    movzx   edx, dx
    lea     ecx, [esp + 5*4]            ; pcbData = &cbDataTmp
    movzx   eax, word [es:edx]
    mov     [ecx], eax
    mov     [esp + 3*4], ecx
    mov     edx, DATA32
    mov     es, edx
    VBOXFS_FARPTR_2_FLAT  0eh, 2*4      ; pbData
    ;VBOXFS_PSFFSD_2_FLAT  12h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  12h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  16h, 0*4      ; psffsi
    call    FS32_READ

    les     dx, [ebp + 0ah]             ; *pcbData = cbDataTmp;
    movzx   edx, dx
    mov     cx, [esp + 5*4]
    mov     [es:edx], cx
    mov     edx, DATA32
    mov     es, edx

VBOXFS_TO_16        FS_READ

;    pop     es
    retf    12h
VBOXFS_EP16_END     FS_READ


;;
; @cproto int FS_RMDIR(PCDFSI pcdfsi, PVBOXSFCD pcdfsd, PCSZ pszName, USHORT iCurDirEnd);
;
VBOXFS_EP16_BEGIN   FS_RMDIR, 'FS_RMDIR'
VBOXFS_TO_32        FS_RMDIR, 4*4
    movzx   edx, word [ebp + 08h]       ; iCurDirEnd
    mov     [esp + 3*4], edx
    VBOXFS_FARPTR_2_FLAT  0ah, 2*4      ; pszName
    ;VBOXFS_PCDFSD_2_FLAT  0eh, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  0eh, 1*4      ; pcdfsd
    VBOXFS_FARPTR_2_FLAT  12h, 0*4      ; pcdfsi
    call    NAME(FS32_RMDIR)
VBOXFS_TO_16        FS_RMDIR
    ;retf    14h
    retf    0eh
VBOXFS_EP16_END     FS_RMDIR


;;
; @cproto int FS_SETSWAP(PSFFSI psffsi, PVBOXSFFSD psffsd);
;
VBOXFS_EP16_BEGIN FS_SETSWAP, 'FS_SETSWAP'
VBOXFS_TO_32        FS_SETSWAP, 2*4
    ;VBOXFS_PSFFSD_2_FLAT  08h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  08h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  0ch, 0*4      ; psffsi
    call    NAME(FS32_SETSWAP)
VBOXFS_TO_16        FS_SETSWAP
    retf    8h
VBOXFS_EP16_END     FS_SETSWAP


;;
; @cproto int FS_SHUTDOWN(USHORT type, ULONG reserved);
;
VBOXFS_EP16_BEGIN   FS_SHUTDOWN, 'FS_SHUTDOWN'
;VBOXFS_TO_32        FS_SHUTDOWN, 3*4
VBOXFS_TO_32        FS_SHUTDOWN, 2*4
    mov     eax, [ebp + 08h]            ; reserved
    mov     [esp + 1*4], eax
;    movzx   edx, word [ebp + 0ah]       ; type
    movzx   edx, word [ebp + 0ch]       ; type
    mov     [esp + 0*4], edx
    call    NAME(FS32_SHUTDOWN)
VBOXFS_TO_16        FS_SHUTDOWN
    retf    6h
VBOXFS_EP16_END     FS_SHUTDOWN


;;
; @cproto int FS_VERIFYUNCNAME(USHORT flag, PCSZ pszName)
;
VBOXFS_EP16_BEGIN   FS_VERIFYUNCNAME, 'FS_VERIFYUNCNAME'
VBOXFS_TO_32        FS_VERIFYUNCNAME, 2*4
    VBOXFS_FARPTR_2_FLAT  08h, 1*4      ; pszName
    movzx   edx, word [ebp + 0ch]       ; flag
    mov     [esp + 0*4], edx
    call    NAME(FS32_VERIFYUNCNAME)
VBOXFS_TO_16        FS_VERIFYUNCNAME
    retf    6h
VBOXFS_EP16_END     FS_VERIFYUNCNAME


;;
; @cproto int FS_WRITE(PSFFSI psffsi, PVBOXSFFSD psffsd, PBYTE pbData, PUSHORT pcbData, USHORT IOflag)
VBOXFS_EP16_BEGIN   FS_WRITE, 'FS_WRITE'
VBOXFS_TO_32        FS_WRITE, 6*4       ; extra local for ULONG cbDataTmp.
;    push    es
    movzx   ecx, word [ebp + 08h]       ; IOflag
    mov     [esp + 4*4], ecx
    les     dx, [ebp + 0ah]             ; cbDataTmp = *pcbData;
    movzx   edx, dx
    lea     ecx, [esp + 5*4]            ; pcbData = &cbDataTmp
    movzx   eax, word [es:edx]
    mov     [ecx], eax
    mov     [esp + 3*4], ecx
    mov     edx, DATA32
    mov     es, edx
    VBOXFS_FARPTR_2_FLAT  0eh, 2*4      ; pbData
    ;VBOXFS_PSFFSD_2_FLAT  12h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  12h, 1*4      ; psffsd
    VBOXFS_FARPTR_2_FLAT  16h, 0*4      ; psffsi
    call    FS32_WRITE

    les     dx, [ebp + 0ah]             ; *pcbData = cbDataTmp;
    movzx   edx, dx
    mov     cx, [esp + 5*4]
    mov     [es:edx], cx
    mov     edx, DATA32
    mov     es, edx

VBOXFS_TO_16        FS_WRITE

;    pop     es
    retf    12h
VBOXFS_EP16_END     FS_WRITE






;
;
;  Init code starts here
;  Init code starts here
;  Init code starts here
;
;


;;
; Ring-3 Init (16-bit).
;
; @param    pMiniFS         [bp + 08h]      The mini-FSD. (NULL)
; @param    fpfnDevHlp      [bp + 0ch]      The address of the DevHlp router.
; @param    pszCmdLine      [bp + 10h]      The config.sys command line.
;
VBOXFS_EP16_BEGIN FS_INIT, 'FS_INIT'
;    DEBUG_STR16 'VBoxFS: FS_INIT - enter'
    push    ebp
    mov     ebp, esp
    push    ds                          ; bp - 02h
    push    es                          ; bp - 04h
    push    esi                         ; bp - 08h
    push    edi                         ; bp - 0ch

    mov     ax, DATA16
    mov     ds, ax
    mov     es, ax

    ;
    ; Save the device help entry point.
    ;
    mov     eax, [bp + 0ch]
    mov     [NAME(g_fpfnDevHlp)], eax

    ;
    ; Parse the command line.
    ; Doing this in assembly is kind of ugly...
    ;
    cmp     word [bp + 10h + 2], 3
    jbe near .no_command_line
    lds     si, [bp + 10h]              ; ds:si -> command line iterator.
.parse_next:

    ; skip leading blanks.
.parse_next_char:
    mov     di, si                      ; DI = start of argument.
    lodsb
    cmp     al, ' '
    je      .parse_next_char
    cmp     al, 9                       ; tab
    je      .parse_next_char
    cmp     al, 0
    je near .parse_done

    ; check for '/' or '-'
    cmp     al, '/'
    je      .parse_switch
    cmp     al, '-'
    je      .parse_switch
    jmp     .parse_error

    ; parse switches.
.parse_switch:
    lodsb
    cmp     al, 0
    je      .parse_error
    and     al, ~20h                    ; uppercase

    cmp     al, 'C'                     ; /CP:<codepage> - force codepage
    je      .parse_cp
    cmp     al, 'H'                     ; /H - hide long file names for VDM's
    je      .hidelfn
    cmp     al, 'D'                     ; /D - output debug messages
    je      .debug
    cmp     al, 'V'                     ; /V - verbose
    je      .parse_verbose
    cmp     al, 'Q'                     ; /Q - quiet.
    je      .parse_quiet
    jmp     .parse_error

.parse_cp:
    lodsb
    cmp     al, 0
    je      .parse_next
    and     al, ~20h                    ; uppercase
    cmp     al, 'P'                     ; 
    jne     .parse_next
    lodsb
    cmp     al, ':'
    jne     .parse_next

    push    edx
    push    ebx
    xor     edx, edx
    mov     ebx, 10

.digit:
    xor     eax, eax
    lodsb
    cmp     al, ' '
    je      .end_cp
    cmp     al, 0
    je      .end_cp
    cmp     al, '0'
    jb      .end_cp
    cmp     al, '9'
    ja      .end_cp
    sub     al, '0'
    movzx   eax, al
    add     edx, eax
    mov     eax, edx
    xor     edx, edx
    mul     ebx
    mov     edx, eax
    jmp     .digit
.end_cp:
    mov     eax, edx
    xor     edx, edx
    div     ebx
    mov     edx, eax
    push    ds
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     eax, NAME(g_Cp wrt FLAT)
    mov     dword [eax], edx
    pop     ds
    pop     ebx
    pop     edx
    jmp     .parse_next

.parse_verbose:
    mov     byte [es:NAME(g_fVerbose)], 1
    jmp     .parse_next

.parse_quiet:
    mov     byte [es:NAME(g_fVerbose)], 0
    jmp     .parse_next

.hidelfn:
    push    ds
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     eax, NAME(g_fHideLFN wrt FLAT)
    mov     dword [eax], 1
    pop     ds
    jmp     .parse_next

.debug:
    push    ds
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     eax, NAME(g_fLog_enable wrt FLAT)
    mov     dword [eax], 1
    pop     ds
    jmp     .parse_next

.parse_error:
segment DATA16
.szSyntaxError:
    db 0dh, 0ah, 'VBoxFS.ifs: command line parse error at: ', 0
.szNewLine:
    db 0dh, 0ah, 0dh, 0ah, 0
segment CODE16
    mov     bx, .szSyntaxError
    call    NAME(FS_INIT_FPUTS)

    push    es
    push    ds
    pop     es
    mov     bx, di
    call    NAME(FS_INIT_FPUTS)
    pop     es

    mov     bx, .szNewLine
    call    NAME(FS_INIT_FPUTS)

    mov     ax, ERROR_INVALID_PARAMETER
    jmp     .done

.parse_done:
    mov     ax, DATA16
    mov     ds, ax
.no_command_line:

    ;
    ; Write our greeting to STDOUT.
    ; APIRET  _Pascal DosWrite(HFILE hf, PVOID pvBuf, USHORT cbBuf, PUSHORT pcbBytesWritten);
    ;
    cmp     byte [NAME(g_fVerbose)], 0
    je near .quiet
segment DATA16
.szMessage:
    db 'VirtualBox Guest Additions IFS for OS/2', 0dh, 0ah, 0
segment CODE16
    mov     bx, .szMessage
    call    NAME(FS_INIT_FPUTS)
.quiet:

%if 0
    ;
    ; Get Global InfoSeg selector
    ; APIRET  _Pascal DosGetInfoSeg(PSEL pselGlobal, PSEL pselLocal);
    ;
    call    NAME(FS_INIT_GET_GINFOSEG)
    push    ds
    push    ecx
    mov     cx, DATA32 wrt FLAT
    mov     ds, cx
    mov     ecx, NAME(g_selGIS wrt FLAT)
    mov     word [ecx], ax
    pop     ecx
    pop     ds

    ;
    ; Get Global Environment selector
    ; APIRET  _Pascal DosGetEnv(PSEL pselEnv, PSEL pcmdOffset);
    ;
    call    NAME(FS_INIT_GET_ENV)
    push    ds
    push    ecx
    mov     cx, DATA32 wrt FLAT
    mov     ds, cx
    mov     ecx, NAME(g_selEnv wrt FLAT)
    mov     word [ecx], ax
    pop     ecx
    pop     ds
%endif

    ; return success.
    xor     eax, eax
.done:
    lea     sp, [bp - 0ch]
    pop     edi
    pop     esi
    pop     es
    pop     ds
    mov     esp, ebp
    pop     ebp
    DEBUG_STR16 'VBoxFS: FS_INIT - leave'
    retf    0ch
VBOXFS_EP16_END FS_INIT


;;
; Dos16Write wrapper.
;
; @param    es:bx       String to print. (zero terminated)
; @uses     nothing.
GLOBALNAME FS_INIT_FPUTS
    push    bp
    mov     bp, sp
    push    es                          ; bp - 02h
    push    ds                          ; bp - 04h
    push    ax                          ; bp - 06h
    push    bx                          ; bp - 08h
    push    cx                          ; bp - 0ah
    push    dx                          ; bp - 0ch
    push    si                          ; bp - 0eh
    push    di                          ; bp - 10h

    ; cx = strlen(es:bx)
    xor     al, al
    mov     di, bx
    mov     cx, 0ffffh
    cld
    repne scasb
    not     cx
    dec     cx

    ; APIRET  _Pascal DosWrite(HFILE hf, PVOID pvBuf, USHORT cbBuf, PUSHORT pcbBytesWritten);
    push    cx
    mov     ax, sp                      ; cbBytesWritten
    push    1                           ; STDOUT
    push    es                          ; pvBuf
    push    bx
    push    cx                          ; cbBuf
    push    ss                          ; pcbBytesWritten
    push    ax
%if 0 ; wlink/nasm generates a non-aliased fixup here which results in 16-bit offset with the flat 32-bit selector.
    call far DOS16WRITE
%else
    ; convert flat pointer to a far pointer using the tiled algorithm.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     eax, g_pfnDos16Write wrt FLAT
    movzx   eax, word [eax + 2]                     ; High word of the flat address (in DATA32).
    shl     ax, 3
    or      ax, 0007h
    mov     dx, DATA16
    mov     ds, dx
    mov     [NAME(g_fpfnDos16Write) + 2], ax        ; Update the selector (in DATA16).
    ; do the call
    call far [NAME(g_fpfnDos16Write)]
%endif

    lea     sp, [bp - 10h]
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    pop     ds
    pop     es
    pop     bp
    ret
ENDPROC FS_INIT_FPUTS


%if 0

;;
; Dos16GetInfoSeg wrapper.
;
; @param    none
; @uses     nothing.
; @returns  a GIS selector in ax
GLOBALNAME FS_INIT_GET_GINFOSEG
    push    bp
    mov     bp, sp
    push    es                          ; bp - 02h
    push    ds                          ; bp - 04h
    push    bx                          ; bp - 06h
    push    cx                          ; bp - 08h
    push    dx                          ; bp - 0ah
    push    si                          ; bp - 0ch
    push    di                          ; bp - 0eh

    ; APIRET  _Pascal DosGetInfoSeg(PSEL pselGlobal, PSEL pselLocal);
    xor     ecx, ecx
    xor     ax, ax
    push    ax                          ; selGlobal
    mov     cx, sp
    push    ax                          ; selLocal
    mov     dx, sp
    push    ss                          ;
    push    cx                          ; pselGlobal
    push    ss                          ;
    push    dx                          ; pselLocal
    
%if 1 ; wlink/nasm generates a non-aliased fixup here which results in 16-bit offset with the flat 32-bit selector.
    call far DOS16GETINFOSEG
%else
    ; convert flat pointer to a far pointer using the tiled algorithm.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     eax, g_pfnDos16GetInfoSeg wrt FLAT
    movzx   eax, word [eax + 2]                     ; High word of the flat address (in DATA32).
    shl     ax, 3
    or      ax, 0007h
    mov     dx, DATA16
    mov     ds, dx
    mov     [NAME(g_fpfnDos16GetInfoSeg) + 2], ax        ; Update the selector (in DATA16).
    ; do the call
    call far [NAME(g_fpfnDos16GetInfoSeg)]
%endif

    mov     esp, ecx
    mov     ax, [esp]

    lea     sp, [bp - 0eh]
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ds
    pop     es
    pop     bp
    ret
ENDPROC FS_INIT_GET_GINFOSEG


;;
; Dos16GetEnv wrapper.
;
; @param    none
; @uses     nothing.
; @returns  an environment selector in ax
GLOBALNAME FS_INIT_GET_ENV
    push    bp
    mov     bp, sp
    push    es                          ; bp - 02h
    push    ds                          ; bp - 04h
    push    bx                          ; bp - 06h
    push    cx                          ; bp - 08h
    push    dx                          ; bp - 0ah
    push    si                          ; bp - 0ch
    push    di                          ; bp - 0eh

    ; APIRET  _Pascal DosGetEnv(PSEL pselEnv, PUSHORT pcmdOffset;
    xor     ecx, ecx
    xor     ax, ax
    push    ax                          ; selEnv
    mov     cx, sp
    push    ax                          ; cmdOffset
    mov     dx, sp
    push    ss                          ;
    push    cx                          ; pselEnv
    push    ss                          ;
    push    dx                          ; pcmdOffset
    
%if 1 ; wlink/nasm generates a non-aliased fixup here which results in 16-bit offset with the flat 32-bit selector.
    call far DOS16GETENV
%else
    ; convert flat pointer to a far pointer using the tiled algorithm.
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     eax, g_pfnDos16GetEnv wrt FLAT
    movzx   eax, word [eax + 2]                     ; High word of the flat address (in DATA32).
    shl     ax, 3
    or      ax, 0007h
    mov     dx, DATA16
    mov     ds, dx
    mov     [NAME(g_fpfnDos16GetEnv) + 2], ax        ; Update the selector (in DATA16).
    ; do the call
    call far [NAME(g_fpfnDos16GetEnv)]
%endif

    mov     esp, ecx
    mov     ax, [esp]

    lea     sp, [bp - 0eh]
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ds
    pop     es
    pop     bp
    ret
ENDPROC FS_INIT_GET_ENV

%endif

;;
; 16-bit ring-0 init routine.
;
; Called from various entrypoints likely to be the first to be invoked.
;
GLOBALNAME VBoxFSR0Init16Bit
    DEBUG_STR16 'VBoxFS: VBoxFSR0Init16Bit - enter'
    push    ds
    push    es
    push    fs
    push    gs
    push    esi
    push    edi
    push    ebp
    mov     ebp, esp
    and     sp, 0fffch

    ;
    ; Only try once.
    ;
    mov     ax, DATA16
    mov     ds, ax
    mov     byte [NAME(g_fDoneRing0)], 1

    ;
    ; Try attach to the VBoxGuest driver.
    ;
    mov     bx, NAME(g_szVBoxGuestName)
    mov     di, NAME(g_VBoxGuestAttachDD)
    mov     dl, DevHlp_AttachDD
    call far [NAME(g_fpfnDevHlp)]
    jc      .attach_attempt_done

    push    seg NAME(g_VBoxGuestIDC)
    push    NAME(g_VBoxGuestIDC)
    call far [NAME(g_VBoxGuestAttachDD) + 6]
.attach_attempt_done:

    ;
    ; Get a far pointer to LogPrintf() routine from the DOSHLP segment
    ;
    push    ds
    mov     ax, 100h                   ; DOSHLP selector
    mov     ds, ax
    xor     eax, eax
    cmp     dword [eax], 342F534Fh     ; check for QSINIT / os4ldr signature ('OS/4')
    je      .qsinit
    cmp     dword [eax], 41435241h     ; check for Arca OS loader signature ('ARCA')
    jne     .no_qsinit
.qsinit:
    add     eax, 4                     ; next dword
    mov     dx, [eax]
    cmp     dx, 30h                    ; check for QSINIT / os4ldr version
    jb      .no_qsinit
    mov     ax, [eax + 2 * 17h]
    mov     edx, 100h                  ; DOSHLP selector
    shl     edx, 16
    mov     dx, ax
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     eax, NAME(g_fLogPrint wrt FLAT)
    mov     byte [eax], 1
    mov     ax, DATA16
    mov     ds, ax
    mov     [NAME(g_fpLog_printf)], edx
.no_qsinit:
    pop     ds
    
%ifndef DONT_LOCK_SEGMENTS
    ;
    ; Lock the two 16-bit segments.
    ;
    push    DATA16
    call far FSH_FORCENOSWAP
    push    CODE16
    call far FSH_FORCENOSWAP
    ; Wonder if this'll work if wlink could mark the two segments as ALIASed...
    ;push DATA32
    ;call far FSH_FORCENOSWAP
    ;push TEXT32
    ;call far FSH_FORCENOSWAP
%endif

    ;
    ; Do 32-bit ring-0 init.
    ;
    ;jmp far dword NAME(VBoxFSR0Init16Bit_32) wrt FLAT
    db      066h
    db      0eah
    dd      NAME(VBoxFSR0Init16Bit_32) ;wrt FLAT
    dw      TEXT32 wrt FLAT
segment TEXT32
GLOBALNAME VBoxFSR0Init16Bit_32
    mov     ax, DATA32 wrt FLAT
    mov     ds, ax
    mov     es, ax

    call    KernThunkStackTo32
    call    NAME(VBoxFSR0Init)
    call    KernThunkStackTo16

    ;jmp far dword NAME(VBoxFSR0Init16Bit_16) wrt CODE16
    db      066h
    db      0eah
    dw      NAME(VBoxFSR0Init16Bit_16) wrt CODE16
    dw      CODE16
segment CODE16
GLOBALNAME VBoxFSR0Init16Bit_16

    mov     esp, ebp
    pop     ebp
    pop     edi
    pop     esi
    pop     gs
    pop     fs
    pop     es
    pop     ds
    DEBUG_STR16 'VBoxFS: VBoxFSR0Init16Bit - leave'
    ret
ENDPROC VBoxFSR0Init16Bit


%ifdef DEBUG
;;
; print the string which offset is in AX (it's in the data segment).
; @uses AX
;
GLOBALNAME dbgstr16
    push    ds
    push    ebx
    push    edx

    mov     bx, ax
    mov     dx, 0504h                   ; RTLOG_DEBUG_PORT
    mov     ax, DATA16
    mov     ds, ax

.next:
    mov     al, [bx]
    or      al, al
    jz      .done
    inc     bx
    out     dx, al
    jmp     .next

.done:
    pop     edx
    pop     ebx
    pop     ds
    ret
ENDPROC dbgstr16
%endif
