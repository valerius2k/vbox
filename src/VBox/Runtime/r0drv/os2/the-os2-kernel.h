/* $Id$ */
/** @file
 * IPRT - Ring-0 Driver, The OS/2 Kernel Headers.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef ___the_os2_kernel_h
#define ___the_os2_kernel_h

#include <iprt/types.h>

#define INCL_ERRORS
#include <os2ddk/bsekee.h>
#undef RT_MAX
#include <os2ddk/devhlp.h>

#define DHGETDOSV_CPUMODE   18
#define DHGETDOSV_PSDFLAGS  19
#define DHGETDOSV_TOTALCPUS 20

#define PSD_INITIALIZED   0x80000000
#define PSD_INSTALLED     0x40000000
#define PSD_ADV_INT_MODE  0x20000000
#define PSD_KERNEL_PIC    0x10000000

RT_C_DECLS_BEGIN

extern PCDOSTABLE   g_pDosTable;
extern PCDOSTABLE2  g_pDosTable2;
extern PGINFOSEG    g_pGIS;
extern RTFAR16      g_fpLIS;

RTR0DECL(void *) RTR0Os2Virt2Flat(RTFAR16 fp);
DECLASM(int) RTR0Os2DHQueryDOSVar(uint8_t iVar, uint16_t iSub, PRTFAR16 pfp);
DECLASM(int) RTR0Os2DHVMGlobalToProcess(ULONG fFlags, PVOID pvR0, ULONG cb, PPVOID ppvR3);
DECLASM(int) RTR0Os2DHYield(void);

#define IAECPU_IN_BARRIER_ON  1
#define IAECPU_OUT_BARRIER_ON 2
#define IAECPU_EXCLUDECURRENT 4

typedef void APIENTRY (*RendCallBack_t)(void*);

APIRET   APIENTRY KernInvokeAtEachCpu(RendCallBack_t CallBack,void *arg,uint32_t flags);
APIRET   APIENTRY KernInvokeAtSpecificCpu(RendCallBack_t CallBack,void *arg,uint32_t CPUnum);
APIRET   APIENTRY KernGetCurrentCpuId(void);
APIRET   APIENTRY KernGetReschedStatus(void);
APIRET   APIENTRY KernGetTCReschedStatus(void);
APIRET   APIENTRY KernGetNumCpus(void);
APIRET   APIENTRY KernYield(void);

extern int32_t  KernInterruptLevel;
extern uint32_t KernKEEVersion;

#pragma pack(1)

struct timespec
{
    int64_t tv_sec;  /* seconds     */
    int32_t tv_nsec; /* nanoseconds */
};

#pragma pack()

void APIENTRY KernClockMonotonicGetTime(struct timespec *res);

RT_C_DECLS_END

#endif
