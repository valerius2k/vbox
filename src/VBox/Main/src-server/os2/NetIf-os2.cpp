/* $Id$ */
/** @file
 * Main - NetIfList, OS/2 implementation.
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

#include <iprt/err.h>
#include <list>

#include "HostNetworkInterfaceImpl.h"
#include "netif.h"

int NetIfList(std::list <ComObjPtr<HostNetworkInterface> > &list)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfEnableStaticIpConfig(VirtualBox *pVBox, HostNetworkInterface * pIf, ULONG aOldIp, ULONG aNewIp, ULONG aMask)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfEnableStaticIpConfigV6(VirtualBox *pVBox, HostNetworkInterface * pIf, IN_BSTR aOldIPV6Address,
                                IN_BSTR aIPV6Address, ULONG aIPV6MaskPrefixLength)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfEnableDynamicIpConfig(VirtualBox *pVBox, HostNetworkInterface * pIf)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfDhcpRediscover(VirtualBox *pVBox, HostNetworkInterface * pIf)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfGetState(const char *pcszIfName, NETIFSTATUS *penmState)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfGetLinkSpeed(const char *pcszIfName, uint32_t *puMbits)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfCreateHostOnlyNetworkInterface(VirtualBox *pVirtualBox,
                                        IHostNetworkInterface **aHostNetworkInterface,
                                        IProgress **aProgress,
                                        const char *pcszName)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfGetConfig(HostNetworkInterface * pIf, NETIFINFO *pInfo)
{
    return VERR_NOT_IMPLEMENTED;
}

int NetIfRemoveHostOnlyNetworkInterface(VirtualBox *pVirtualBox, IN_GUID aId,
                                        IProgress **aProgress)
{
    return VERR_NOT_IMPLEMENTED;
}
