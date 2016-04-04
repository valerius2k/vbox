/* $Id$ */
/** @file
 * Main - NetIf, OS/2 implementation.
 */

/*
 * Copyright (C) 2008-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */



/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN

#include <sys/socket.h>
#include <sys/sockio.h>
#include <netinet/in.h>
#include <net/route.h>
#include <net/if.h>
#include <arpa/inet.h>

#undef __STRICT_ANSI__
#include <stdlib.h>

#include <iprt/err.h>
#include <list>

#include "HostNetworkInterfaceImpl.h"
#include "netif.h"
#include "Logging.h"


static int getInterfaceInfo(int sock, char *pszName, PNETIFINFO pInfo)
{
    const  char *type, *prev_type;
    int    i, num;
    struct ifmib ifmib;

    // get network interface list
    if ( os2_ioctl( sock, SIOSTATIF42, (caddr_t)&ifmib,
                    sizeof(struct ifmib) ) == -1 )
        return VERR_INTERNAL_ERROR;

    for (i = 0; i < ifmib.ifNumber; i++)
    {
        struct ifreq Req;

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

        RT_ZERO(Req);
        RTStrCopy(Req.ifr_name, sizeof(Req.ifr_name), pszName);

        switch (ifmib.iftable[i].iftType)
        {
            case HT_ETHER:    // Ethernet
            case HT_ISO88023: // CSMA CD
            case HT_ISO88025: // Token ring
                pInfo->enmMediumType = NETIF_T_ETHERNET;
                break;    
            default:
                pInfo->enmMediumType = NETIF_T_UNKNOWN;
        }
        
        /* Generate UUID from name and MAC address. */
        RTUUID uuid;
        RTUuidClear(&uuid);
        memcpy(&uuid, pszName, RT_MIN(sizeof(pszName), sizeof(uuid)));
        uuid.Gen.u8ClockSeqHiAndReserved = (uuid.Gen.u8ClockSeqHiAndReserved & 0x3f) | 0x80;
        uuid.Gen.u16TimeHiAndVersion = (uuid.Gen.u16TimeHiAndVersion & 0x0fff) | 0x4000;
        memcpy(uuid.Gen.au8Node, &ifmib.iftable[i].iftPhysAddr, sizeof(uuid.Gen.au8Node));
        pInfo->Uuid = uuid;
        
        memcpy(&pInfo->MACAddress, ifmib.iftable[i].iftPhysAddr, sizeof(pInfo->MACAddress));

        if (ioctl(sock, SIOCGIFADDR, &Req) >= 0)
            memcpy(pInfo->IPAddress.au8,
                   &((struct sockaddr_in *)&Req.ifr_ifru.ifru_addr)->sin_addr.s_addr,
                   sizeof(pInfo->IPAddress.au8));

        if (ioctl(sock, SIOCGIFNETMASK, &Req) >= 0)
            memcpy(pInfo->IPNetMask.au8,
                   &((struct sockaddr_in *)&Req.ifr_ifru.ifru_addr)->sin_addr.s_addr,
                   sizeof(pInfo->IPNetMask.au8));

        if (ioctl(sock, SIOCGIFFLAGS, &Req) >= 0)
            pInfo->enmStatus = Req.ifr_ifru.ifru_flags & IFF_UP ? NETIF_S_UP : NETIF_S_DOWN;

        /*
         * Don't even try to get speed for non-Ethernet interfaces, it only
         * produces errors.
         */
        if (pInfo->enmMediumType == NETIF_T_ETHERNET)
            pInfo->uSpeedMbits = ifmib.iftable[i].iftSpeed;
        else
            pInfo->uSpeedMbits = 0;
    }

    return VINF_SUCCESS;
}


/**
 * Obtain the name of the interface used for default routing.
 *
 * NOTE: There is a copy in Devices/Network/testcase/tstIntNet-1.cpp.
 *
 * @returns VBox status code.
 *
 * @param   pszName     The buffer of IFNAMSIZ+1 length where to put the name.
 */
static int getDefaultIfaceName(char *pszName)
{
    struct rtentries rtent;
    struct ortentry *rt_table;
    int    rt_count;
    int    sock = -1;
    int    j;

    sock = socket( AF_INET, SOCK_RAW, 0 );

    if (sock == -1)
        return VERR_INTERNAL_ERROR;

    // get routing table
    if ( os2_ioctl( sock, SIOSTATRT, (caddr_t)&rtent,
                  sizeof(struct rtentries) ) == -1 )
        return VERR_INTERNAL_ERROR;

    rt_count = rtent.hostcount + rtent.netcount;
    rt_table = rtent.rttable;

    // search routing table for default route
    for (j = 0; j < rt_count; j++)
    {
        struct sockaddr_in *addr = (struct sockaddr_in *)&(rt_table[j].rt_dst);
        int dest = addr->sin_addr.s_addr;

        addr = (struct sockaddr_in *)&(rt_table[j].rt_gateway);

        // find "0.0.0.0"
        if (! dest)
        {
            // found interface name
            // 
            // Note: struct [o]rtentry contain rt_ifp field of type struct ifnet *
            // but struct ifnet is defined in TCPIPV4 headers only. In newer TCPIP v4.3
            // headers, these declarations are missing for some reason. I see that the
            // interface name is contained in char * field at the start of the structure
            // pointed to by rt_ifp pointer. So, we just do this strange typecast:
            strcpy(pszName, (char *)*(unsigned long *)(rt_table[j].rt_ifp));
            break;
        }
    }

    soclose(sock);

    return VINF_SUCCESS;
}

int NetIfList(std::list <ComObjPtr<HostNetworkInterface> > &list)
{
    int    i, j, num, sock = -1;
    struct ifmib ifmib;
    char   pszName[20];
    char   szDefaultIface[20];
    int    lan = 0;

    sock = socket( AF_INET, SOCK_RAW, 0 );

    if (sock == -1)
        return VERR_INTERNAL_ERROR;

    int rc = getDefaultIfaceName(szDefaultIface);

    if (RT_FAILURE(rc))
    {
        Log(("NetIfList: Failed to find default interface.\n"));
        szDefaultIface[0] = 0;
    }

    NETIFINFO Info;
    RT_ZERO(Info);
    rc = getInterfaceInfo(sock, pszName, &Info);
    if (RT_FAILURE(rc))
        return VERR_INTERNAL_ERROR;

    if (Info.enmMediumType == NETIF_T_ETHERNET)
    {
        ComObjPtr<HostNetworkInterface> IfObj;
        IfObj.createObject();

        HostNetworkInterfaceType_T enmType;
        if (strncmp(pszName, RT_STR_TUPLE("vboxnet")))
            enmType = HostNetworkInterfaceType_Bridged;
        else
            enmType = HostNetworkInterfaceType_HostOnly;

        if (SUCCEEDED(IfObj->init(Bstr(pszName), enmType, &Info)))
        {
            if (strcmp(pszName, szDefaultIface) == 0)
                list.push_front(IfObj);
            else
                list.push_back(IfObj);
        }
    }

    soclose(sock);

    return VINF_SUCCESS;
}

/**
 * Retrieve the physical link speed in megabits per second. If the interface is
 * not up or otherwise unavailable the zero speed is returned.
 *
 * @returns VBox status code.
 *
 * @param   pcszIfName  Interface name.
 * @param   puMbits     Where to store the link speed.
 */
int NetIfGetLinkSpeed(const char *pcszIfName, uint32_t *puMbits)
{
    int    i, num, sock = -1, addr;
    const  char *type, *prev_type;
    char   pszName[20];
    struct ifmib ifmib;
    struct ifreq Req;

    sock = socket( AF_INET, SOCK_RAW, 0 );

    if (sock == -1)
        return VERR_INTERNAL_ERROR;

    // get network interface list
    if (ioctl( sock, SIOSTATIF42, (caddr_t)&ifmib,
                  sizeof(struct ifmib) ) == -1)
        return VERR_INTERNAL_ERROR;

    for (i = 0; i < ifmib.ifNumber; i++)
    {
        prev_type = type;

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

        if (!strcmp(pszName, pcszIfName))
        {
            // interface found, set speed
            *puMbits = ifmib.iftable[i].iftSpeed / 1000000;
            break;
        }
    }

    soclose(sock);

    return VINF_SUCCESS;
}

int NetIfGetConfigByName(PNETIFINFO pInfo)
{
    int rc = VINF_SUCCESS;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return VERR_NOT_IMPLEMENTED;
    rc = getInterfaceInfo(sock, pInfo->szShortName, pInfo);
    soclose(sock);
    return rc;
}
