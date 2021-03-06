/** @file
 * Inline routines for Watcom C.
 */

/*
 * Copyright (C) 2010-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


extern unsigned inp(unsigned port);
extern unsigned outp(unsigned port, unsigned value);
extern unsigned inpw(unsigned port);
extern unsigned outpw(unsigned port, unsigned value);
#pragma intrinsic(inp,outp,inpw,outpw)
#define inb(p)      inp(p)
#define outb(p, v)  outp(p, v)
#define inw(p)      inpw(p)
#define outw(p, v)  outpw(p, v)

extern  uint8_t     read_byte(uint16_t seg, uint16_t offset);
extern  uint16_t    read_word(uint16_t seg, uint16_t offset);
extern  uint32_t    read_dword(uint16_t seg, uint16_t offset);
extern  void        write_byte(uint16_t seg, uint16_t offset, uint8_t data);
extern  void        write_word(uint16_t seg, uint16_t offset, uint16_t data);
extern  void        write_dword(uint16_t seg, uint16_t offset, uint32_t data);

void int_enable(void);
#pragma aux int_enable = "sti" modify exact [] nomemory;

void int_disable(void);
#pragma aux int_disable = "cli" modify exact [] nomemory;

void int_enable_hlt_disable(void);
#pragma aux int_enable_hlt_disable = \
    "sti" \
    "hlt" \
    "cli" \
    modify exact [] nomemory;

uint16_t int_query(void);
#pragma aux int_query =     \
    "pushf"                 \
    "pop    ax"             \
    value [ax] modify exact [ax] nomemory;

void int_restore(uint16_t old_flags);
#pragma aux int_restore =   \
    "push   ax"             \
    "popf"                  \
    parm [ax] modify exact [] nomemory;

void halt(void);
#pragma aux halt = "hlt" modify exact [] nomemory;

void halt_forever(void);
#pragma aux halt_forever =  \
    "forever:"              \
    "hlt"                   \
    "jmp forever"           \
    modify exact [] nomemory aborts;

#ifdef __386__

void rep_movsb(void __far *d, void __far *s, int nbytes);
#pragma aux rep_movsb =     \
    "push   ds"             \
    "mov    ds, dx"         \
    "rep    movsb"          \
    "pop    ds"             \
    parm [es edi] [dx esi] [ecx];

#else

void rep_movsb(void __far *d, void __far *s, int nbytes);
#pragma aux rep_movsb =     \
    "push   ds"             \
    "mov    ds, dx"         \
    "rep    movsb"          \
    "pop    ds"             \
    parm [es di] [dx si] [cx];

#endif

void rep_movsw(void __far *d, void __far *s, int nwords);
#pragma aux rep_movsw =     \
    "push   ds"             \
    "mov    ds, dx"         \
    "rep    movsw"          \
    "pop    ds"             \
    parm [es di] [dx si] [cx];

#ifndef __386__

char __far *rep_insb(char __far *buffer, unsigned nbytes, unsigned port);
#pragma aux rep_insb = ".286" "rep insb" parm [es di] [cx] [dx] value [es di] modify exact [cx di];

char __far *rep_insw(char __far *buffer, unsigned nwords, unsigned port);
#pragma aux rep_insw = ".286" "rep insw" parm [es di] [cx] [dx] value [es di] modify exact [cx di];

char __far *rep_insd(char __far *buffer, unsigned ndwords, unsigned port);
#pragma aux rep_insd = ".386" "rep insd" parm [es di] [cx] [dx] value [es di] modify exact [cx di];

char __far *rep_outsb(char __far *buffer, unsigned nbytes, unsigned port);
#pragma aux rep_outsb = ".286" "rep outs dx,byte ptr es:[si]" parm [es si] [cx] [dx] value [es si] modify exact [cx si];

char __far *rep_outsw(char __far *buffer, unsigned nwords, unsigned port);
#pragma aux rep_outsw = ".286" "rep outs dx,word ptr es:[si]" parm [es si] [cx] [dx] value [es si] modify exact [cx si];

char __far *rep_outsd(char __far *buffer, unsigned ndwords, unsigned port);
#pragma aux rep_outsd = ".386" "rep outs dx,dword ptr es:[si]" parm [es si] [cx] [dx] value [es si] modify exact [cx si];

uint16_t swap_16(uint16_t val);
#pragma aux swap_16 = "xchg ah,al" parm [ax] value [ax] modify exact [ax] nomemory;

uint32_t swap_32(uint32_t val);
#pragma aux swap_32 =   \
    "xchg   ah, al"     \
    "xchg   dh, dl"     \
    "xchg   ax, dx"     \
    parm [dx ax] value [dx ax] modify exact [dx ax] nomemory;

uint64_t swap_64(uint64_t val);
#pragma aux swap_64 =   \
    "xchg   ah, al"     \
    "xchg   bh, bl"     \
    "xchg   ch, cl"     \
    "xchg   dh, dl"     \
    "xchg   ax, dx"     \
    "xchg   bx, cx"     \
    parm [ax bx cx dx] value [ax bx cx dx] modify exact [ax bx cx dx] nomemory;

#endif
