/* $Id$ */
/** @file
 * Skeleton main module.
 */

/*
 * Copyright (C) 2010-2015 Oracle Corporation
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/ExtPack/ExtPack.h>

#include <VBox/err.h>
#include <VBox/version.h>
#include <VBox/vmm/cfgm.h>
#include <iprt/string.h>
#include <iprt/param.h>
#include <iprt/path.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to the extension pack helpers. */
static PCVBOXEXTPACKHLP g_pHlp;


// /**
//  * @interface_method_impl{VBOXEXTPACKREG,pfnInstalled}
//  */
// static DECLCALLBACK(void) vboxSkeletonExtPack_Installed(PCVBOXEXTPACKREG pThis, VBOXEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);
// /**
//  * @interface_method_impl{VBOXEXTPACKREG,pfnUninstall}
//  */
// static DECLCALLBACK(int)  vboxSkeletonExtPack_Uninstall(PCVBOXEXTPACKREG pThis, VBOXEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);
//
// /**
//  * @interface_method_impl{VBOXEXTPACKREG,pfnVirtualBoxReady}
//  */
// static DECLCALLBACK(void)  vboxSkeletonExtPack_VirtualBoxReady(PCVBOXEXTPACKREG pThis, VBOXEXTPACK_IF_CS(IVirtualBox) *pVirtualBox);
//
// /**
//  * @interface_method_impl{VBOXEXTPACKREG,pfnUnload}
//  */
// static DECLCALLBACK(void) vboxSkeletonExtPack_Unload(PCVBOXEXTPACKREG pThis);
// /**
//  * @interface_method_impl{VBOXEXTPACKREG,pfnVMCreated}
//  */
// static DECLCALLBACK(int)  vboxSkeletonExtPack_VMCreated(PCVBOXEXTPACKREG pThis, VBOXEXTPACK_IF_CS(IVirtualBox) *pVirtualBox, IMachine *pMachine);
//
// /**
//  * @interface_method_impl{VBOXEXTPACKREG,pfnVMConfigureVMM}
//  */
// static DECLCALLBACK(int)  vboxSkeletonExtPack_VMConfigureVMM(PCVBOXEXTPACKREG pThis, IConsole *pConsole, PVM pVM);
//
// /**
//  * @interface_method_impl{VBOXEXTPACKREG,pfnVMPowerOn}
//  */
// static DECLCALLBACK(int)  vboxSkeletonExtPack_VMPowerOn(PCVBOXEXTPACKREG pThis, IConsole *pConsole, PVM pVM);
// /**
//  * @interface_method_impl{VBOXEXTPACKREG,pfnVMPowerOff}
//  */
// static DECLCALLBACK(void) vboxSkeletonExtPack_VMPowerOff(PCVBOXEXTPACKREG pThis, IConsole *pConsole, PVM pVM);
// /**
//  * @interface_method_impl{VBOXEXTPACKREG,pfnVMPowerOff}
//  */
// static DECLCALLBACK(void) vboxSkeletonExtPack_QueryObject(PCVBOXEXTPACKREG pThis, PCRTUUID pObjectId);


static const VBOXEXTPACKREG g_vboxSkeletonExtPackReg =
{
    VBOXEXTPACKREG_VERSION,
    /* .pfnInstalled =      */  NULL,
    /* .pfnUninstall =      */  NULL,
    /* .pfnVirtualBoxReady =*/  NULL,
    /* .pfnConsoleReady =   */  NULL,
    /* .pfnUnload =         */  NULL,
    /* .pfnVMCreated =      */  NULL,
    /* .pfnVMConfigureVMM = */  NULL,
    /* .pfnVMPowerOn =      */  NULL,
    /* .pfnVMPowerOff =     */  NULL,
    /* .pfnQueryObject =    */  NULL,
    VBOXEXTPACKREG_VERSION
};


/** @callback_method_impl{FNVBOXEXTPACKREGISTER}  */
extern "C" DECLEXPORT(int) VBoxExtPackRegister(PCVBOXEXTPACKHLP pHlp, PCVBOXEXTPACKREG *ppReg, PRTERRINFO pErrInfo)
{
    /*
     * Check the VirtualBox version.
     */
    if (!VBOXEXTPACK_IS_VER_COMPAT(pHlp->u32Version, VBOXEXTPACKHLP_VERSION))
        return RTErrInfoSetF(pErrInfo, VERR_VERSION_MISMATCH,
                             "Helper version mismatch - expected %#x got %#x",
                             VBOXEXTPACKHLP_VERSION, pHlp->u32Version);
    if (   VBOX_FULL_VERSION_GET_MAJOR(pHlp->uVBoxFullVersion) != VBOX_VERSION_MAJOR
        || VBOX_FULL_VERSION_GET_MINOR(pHlp->uVBoxFullVersion) != VBOX_VERSION_MINOR)
        return RTErrInfoSetF(pErrInfo, VERR_VERSION_MISMATCH,
                             "VirtualBox version mismatch - expected %u.%u got %u.%u",
                             VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR,
                             VBOX_FULL_VERSION_GET_MAJOR(pHlp->uVBoxFullVersion),
                             VBOX_FULL_VERSION_GET_MINOR(pHlp->uVBoxFullVersion));

    /*
     * We're good, save input and return the registration structure.
     */
    g_pHlp = pHlp;
    *ppReg = &g_vboxSkeletonExtPackReg;

    return VINF_SUCCESS;
}

