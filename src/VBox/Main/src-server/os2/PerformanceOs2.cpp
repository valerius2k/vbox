/* $Id$ */

/** @file
 *
 * VBox OS/2-specific Performance Classes implementation.
 */

/*
 * Copyright (C) 2008-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#define OS2EMX_PLAIN_CHAR

#define  INCL_BASE
#include <os2.h>

#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <malloc.h>

#undef __STRICT_ANSI__
#include <stdlib.h>

#define CMD_KI_RDCNT 0x63

static __inline__ unsigned long long rdtsc(void)
{
    unsigned long long int x;
    __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
    return x;
}

typedef struct CPUUTIL_ {
  ULONGLONG             ullTime;        // Time stamp.
  ULONGLONG             ullIdle;        // Idle time.
  ULONGLONG             ullBusy;        // Busy time.
  ULONGLONG             ullIntr;        // Interrupt time.
} CPUUTIL, *PCPUUTIL;

typedef QSPREC   *PQSPREC;
typedef QSPTRREC *PQSPTRREC;

#define INCR_BUF_SIZE		(1024 * 10)

#include "Performance.h"

namespace pm {

class CollectorOS2 : public CollectorHAL
{
public:
    virtual int getHostCpuLoad(ULONG *user, ULONG *kernel, ULONG *idle);
    virtual int getHostCpuMHz(ULONG *mhz);
    virtual int getHostMemoryUsage(ULONG *total, ULONG *used, ULONG *available);
    virtual int getProcessCpuLoad(RTPROCESS process, ULONG *user, ULONG *kernel);
    virtual int getProcessMemoryUsage(RTPROCESS process, ULONG *used);

    virtual int getRawHostCpuLoad(uint64_t *user, uint64_t *kernel, uint64_t *idle);
    virtual int getRawProcessCpuLoad(RTPROCESS process, uint64_t *user, uint64_t *kernel, uint64_t *total);

    virtual int getDiskListByFs(const char *name, DiskList& listUsage, DiskList& listLoad);
    virtual int getHostFilesystemUsage(const char *name, ULONG *total, ULONG *used, ULONG *available);
    virtual int getHostDiskSize(const char *name, uint64_t *size);
    virtual int getRawHostDiskLoad(const char *name, uint64_t *disk_ms, uint64_t *total_ms);
    virtual int getRawHostNetworkLoad(const char *name, uint64_t *rx, uint64_t *tx);

protected:
    BOOL    getSysState(ULONG ulFlags, ULONG *pcbPtrRec, PVOID *ppPtrRec);
    PQSPREC getProcessRec(ULONG pid);
};


CollectorHAL *createHAL()
{
    return new CollectorOS2();
}

int CollectorOS2::getHostCpuLoad(ULONG *user, ULONG *kernel, ULONG *idle)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorOS2::getRawHostCpuLoad(uint64_t *user, uint64_t *kernel, uint64_t *idle)
{
    CPUUTIL cpu_util;
    double ts_val, idle_val, busy_val, intr_val;

    DosPerfSysCall(CMD_KI_RDCNT, (ULONG) &cpu_util, 0, 0);

    ts_val   = cpu_util.ullTime;
    idle_val = cpu_util.ullIdle;
    busy_val = cpu_util.ullBusy;
    intr_val = cpu_util.ullIntr;

    /* assume interrupt time ~= kernel mode time */
    *user    = (uint64_t)busy_val - intr_val;
    *idle    = (uint64_t)idle_val;
    *kernel  = (uint64_t)intr_val;

    return VINF_SUCCESS;
}

int CollectorOS2::getHostCpuMHz(ULONG *mhz)
{
    ULONG ulTmrFreq;
    ULONGLONG qwTmrTime1, qwTmrTime2;
    ULONGLONG tics1, tics2, interval;
    ULONGLONG ullInterval, freqMHz;

    // RTC timer frequency
    DosTmrQueryFreq((PULONG)&ulTmrFreq);

    DosEnterCritSec();

    tics1 = rdtsc(); // get starting tics count
    DosTmrQueryTime((QWORD *)&qwTmrTime1); // get RTC timer tics
    DosSleep(30);    // wait for 30 msec
    DosTmrQueryTime((QWORD *)&qwTmrTime2); // get RTC timer tics
    tics2 = rdtsc(); // get ending tics count

    DosExitCritSec();

    ullInterval = qwTmrTime2 - qwTmrTime1;
    interval = tics2 - tics1;
    
    /** @todo: Howto support more than one CPU? */
    *mhz = ( interval / ( (0x1e6 * ullInterval / ulTmrFreq) ) ) / 1000000;

    return VINF_SUCCESS;
}

int CollectorOS2::getHostMemoryUsage(ULONG *total, ULONG *used, ULONG *available)
{
    ULONG buf[3];

    DosQuerySysInfo(QSV_TOTPHYSMEM, QSV_TOTAVAILMEM, buf, 3 * sizeof(ULONG));

    *total = buf[0] / 1024;
    *used  = buf[1] / 1024;
    *available = buf[2] / 1024;

    return VINF_SUCCESS;
}

int CollectorOS2::getRawProcessCpuLoad(RTPROCESS process, uint64_t *user, uint64_t *kernel, uint64_t *total)
{

    PQSPREC pQSPRec = getProcessRec(process);

    if (pQSPRec == NULL)
        return VERR_INTERNAL_ERROR;

    *user   = pQSPRec->pThrdRec->usertime;
    *kernel = pQSPRec->pThrdRec->systime;

    /* to avoid division by zero */
    if (*user == 0)
        *user ++;

    /* to avoid division by zero */
    if (*kernel == 0)
        *kernel ++;

    *total = *user + *kernel;

    return VINF_SUCCESS;
}

int CollectorOS2::getProcessCpuLoad(RTPROCESS process, ULONG *user, ULONG *kernel)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorOS2::getProcessMemoryUsage(RTPROCESS process, ULONG *used)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorOS2::getDiskListByFs(const char *name, DiskList& listUsage, DiskList& listLoad)
{
    char disknum;
    FSQBUFFER2 fsq;
    ULONG cb = sizeof(fsq);
    char fsname[3] = "d:";
    int local = 0;

    if (!strcmp(name, "/"))
    {
        // if name == "/lalala/qwe/rty" then use fsname == %UNIXROOT%
        char *unixroot = getenv("UNIXROOT");

        if (unixroot == NULL)
            return VERR_NOT_FOUND;

        // set UNIXROOT driveletter
        fsname[0] = toupper(*unixroot);
    }
    else
    {
        // otherwise, if it is d:/lalala/qwe/rty, then use fsname == d:
        fsname[0] = toupper(*name);
    }

    DosQueryFSAttach(fsname, 0, FSAIL_QUERYNAME, &fsq, (PULONG)&cb);

    // local drive
    if (fsq.iType == FSAT_LOCALDRV)
        local = 1;

    if (toupper(*fsname) == 'A' || toupper(*fsname) == 'B')
        local = 0;
    
    listUsage.push_back(RTCString(fsname));

    if (local)
        listLoad.push_back(RTCString(fsname));

    return VINF_SUCCESS;
}

int CollectorOS2::getHostFilesystemUsage(const char *name, ULONG *total, ULONG *used, ULONG *available)
{
    char disknum;
    FSALLOCATE fsa;

    if (!strcmp(name, "/"))
    {
        // if name == "/lalala/qwe/rty" then use fsname == %UNIXROOT%
        char *unixroot = getenv("UNIXROOT");

        if (unixroot == NULL)
            return VERR_NOT_FOUND;

        // set UNIXROOT driveletter
        disknum = toupper(*unixroot) - 'A' + 1;
    }
    else
    {
        // otherwise, if it is d:/lalala/qwe/rty, then use fsname == d:
        disknum = toupper(*name) - 'A' + 1;
    }

    DosQueryFSInfo(disknum, FSIL_ALLOC, &fsa, sizeof(fsa));

    // unit in kilobytes
    ULONG unit = fsa.cSectorUnit * fsa.cbSector / (1024);

    *total     = fsa.cUnit * unit / 1024;
    *available = fsa.cUnitAvail * unit / 1024;
    *used      = *total - *available;

    return VINF_SUCCESS;
}

int CollectorOS2::getHostDiskSize(const char *name, uint64_t *size)
{
    char disknum;
    FSALLOCATE fsa;

    if (!strcmp(name, "/"))
    {
        // if name == "/lalala/qwe/rty" then use fsname == %UNIXROOT%
        char *unixroot = getenv("UNIXROOT");

        if (unixroot == NULL)
            return VERR_NOT_FOUND;

        // set UNIXROOT driveletter
        disknum = toupper(*unixroot) - 'A' + 1;
    }
    else
    {
        // otherwise, if it is d:/lalala/qwe/rty, then use fsname == d:
        disknum = toupper(*name) - 'A' + 1;
    }

    DosQueryFSInfo(disknum, FSIL_ALLOC, &fsa, sizeof(fsa));

    // unit in kilobytes
    ULONGLONG unit = fsa.cSectorUnit * fsa.cbSector;

    *size      = fsa.cUnit * unit;

    return VINF_SUCCESS;
}

int CollectorOS2::getRawHostDiskLoad(const char *name, uint64_t *disk_ms, uint64_t *total_ms)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorOS2::getRawHostNetworkLoad(const char *name, uint64_t *rx, uint64_t *tx)
{
    int sock = -1;
    struct ifmib ifmib;
    char pszName[20];
    int i;

    sock = socket( AF_INET, SOCK_RAW, 0 );

    if (sock == -1)
        return VERR_INTERNAL_ERROR;

    if ( os2_ioctl( sock, SIOSTATIF42, (caddr_t)&ifmib,
                  sizeof(struct ifmib) ) == -1 )
        return VERR_INTERNAL_ERROR;

    for (i = 0; i < ifmib.ifNumber; i++)
    {
        int ifindex = ifmib.iftable[i].iftIndex;

        if (ifindex >=0 && ifindex <= 9)
        {
            // lanX
            strcpy(pszName, "lan");
            itoa(ifindex, pszName + strlen(pszName), 10);
        }
        else if (strstr(ifmib.iftable[i].iftDescr, "back"))
        {
            // loopback
            strcpy(pszName, "lo");
        }
        else if (strstr(ifmib.iftable[i].iftDescr, "ace ppp"))
        {
            // pppX
            strcpy(pszName, "ppp");
            itoa(ifindex, pszName + strlen(pszName), 10);
        }
        else if (strstr(ifmib.iftable[i].iftDescr,"ace sl"))
        {
            // slX
            strcpy(pszName, "sl");
            itoa(ifindex, pszName + strlen(pszName), 10);
        }
        else if (strstr(ifmib.iftable[i].iftDescr,"ace dod"))
        {
            // dodX
            strcpy(pszName, "dod");
            itoa(ifindex, pszName + strlen(pszName), 10);
        }
        else
        {
            // unknown
            strcpy(pszName, "unk");
            itoa(ifindex, pszName + strlen(pszName), 10);
        }


        if (!strcmp(pszName, name))
        {
            *rx = ifmib.iftable[i].iftInOctets;
            *tx = ifmib.iftable[i].iftOutOctets;
            break;
        }
    }

    soclose(sock);

    return VINF_SUCCESS;
}

BOOL CollectorOS2::getSysState(ULONG ulFlags, ULONG *pcbPtrRec, PVOID *ppPtrRec)
{
    APIRET rc;

    do
    {
        if (*pcbPtrRec && *ppPtrRec)
        {
            rc = DosQuerySysState(ulFlags, 0, 0, 0,
                                  *ppPtrRec, *pcbPtrRec);

            if (rc == NO_ERROR)
                break;

            if (*ppPtrRec)
                free(*ppPtrRec);

            *ppPtrRec = NULL;

            if (rc != ERROR_BUFFER_OVERFLOW)
                return FALSE;
        }
        else
            *pcbPtrRec = 0;

        *pcbPtrRec += INCR_BUF_SIZE;
        *ppPtrRec  =  malloc(*pcbPtrRec);

        if (*ppPtrRec == NULL)
            return FALSE;    

    }
    while (TRUE);

    return TRUE;
}


PQSPREC CollectorOS2::getProcessRec(ULONG pid)
{
    ULONG   cbQSPRec = 0;
    PQSPREC pQSPRec  = NULL;

    if (! getSysState(QS_PROCESS, &cbQSPRec, (PVOID *)&pQSPRec) )
        return NULL;

    pQSPRec = (PQSPREC)(((PQSPTRREC)pQSPRec)->pProcRec);

    while ( pQSPRec->RecType == 1)
    {
        if (pQSPRec->pid == pid)
            return pQSPRec;

        pQSPRec = (PQSPREC)( (PBYTE)(pQSPRec->pThrdRec) +
                             ( pQSPRec->cTCB * sizeof(QSTREC) ) );
    }

    return NULL;
}

}
