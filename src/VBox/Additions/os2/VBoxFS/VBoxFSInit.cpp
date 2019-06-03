/* $Id$ */
/** @file
 * VBoxFS - OS/2 Shared Folders, Initialization.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 * Copyright (c) 2015-2018 Valery V. Sedletski <_valerius-no-spam@mail.ru>
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
#define LOG_GROUP LOG_GROUP_DEFAULT
#include "VBoxFSInternal.h"

#include <VBox/VBoxGuestLib.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
/* from VBoxFSA.asm */
extern RTFAR16 g_fpfnDevHlp;
extern VBOXGUESTOS2IDCCONNECT g_VBoxGuestIDC;
extern uint32_t g_u32Info;
/* from sys0.asm and the linker/end.lib. */
extern char _text, _etext, _data, _end;
extern VBGLSFCLIENT g_clientHandle;
//extern USHORT    g_selGIS;
extern PGINFOSEG g_pGIS;
//extern USHORT    g_selEnv;
//char            *g_pEnv = NULL;
extern ULONG KernSISData;
extern ULONG     g_fpLog_printf;
void             LogPrintf(char *fmt, ...);
RT_C_DECLS_END


#if 0

/*!
  @brief         Gets an environment variable by name

  @param         pszName       variable name
  @param         ppszValue     pointer to returned pointer variable (to an environment variable)

  @return
    NO_ERROR                   if env. var found successfully
    ERROR_ENVVAR_NOT_FOUND     env. var. not found
*/

APIRET APIENTRY doScanEnv(PCSZ  pszName,
                          PSZ  *ppszValue)
{
  char varname[CCHMAXPATH];
  int  i;
  char *p, *q, *env;

  /* get the environment */
  env = g_pEnv;

  /* search for needed env variable */
  log("env=%s\n", env);
  for (p = env; *p; p += strlen(p) + 1)
  {
    // move until '=' sign is encountered
    for (i = 0, q = p; *q && *q != '=' && i < CCHMAXPATH - 1; q++, i++) ;

    /* copy to name buffer  */
    strncpy(varname, p, i);
    /* add ending zero byte */
    varname[i] = '\0';

    log("var[%d]=%s\n", i, varname);
    if (! stricmp(varname, (const char *)pszName))
    {
      /* variable found */
      *ppszValue = (PSZ)(q + 1);

      return NO_ERROR;
    }
  }

  log("none found!\n");
  return ERROR_ENVVAR_NOT_FOUND;
}

#endif


/**
 * 32-bit Ring-0 init routine.
 *
 * This is called the first time somebody tries to use the IFS.
 * It will initialize IPRT, Vbgl and whatever else is required.
 *
 * The caller will do the necessary AttachDD and calling of the 16 bit
 * IDC to initialize the g_VBoxGuestIDC global. Perhaps we should move
 * this bit to VbglInitClient? It's just that it's so much simpler to do it
 * while we're on the way here...
 *
 */
DECLASM(void) VBoxFSR0Init(void)
{
    APIRET rc;

    log("VBoxFSR0Init: g_fpfnDevHlp=%lx u32Version=%RX32 u32Session=%RX32 pfnServiceEP=%p g_u32Info=%u (%#x)\n",
         g_fpfnDevHlp, g_VBoxGuestIDC.u32Version, g_VBoxGuestIDC.u32Session, g_VBoxGuestIDC.pfnServiceEP, g_u32Info, g_u32Info);

    /*
     * Start by initializing IPRT.
     */
    if (    g_VBoxGuestIDC.u32Version == VMMDEV_VERSION
        &&  VALID_PTR(g_VBoxGuestIDC.u32Session)
        &&  VALID_PTR(g_VBoxGuestIDC.pfnServiceEP))
    {
#if 0 // ???
        int rc = RTR0Init(0);

        if (RT_SUCCESS(rc))
        {
            rc = VbglInitClient();

            if (RT_SUCCESS(rc))
            {
#endif // ???
#ifndef DONT_LOCK_SEGMENTS
        /*
         * Lock the 32-bit segments in memory.
         */
        static KernVMLock_t s_Text32, s_Data32;

        rc = KernVMLock(VMDHL_LONG,
                        &_text, (uintptr_t)&_etext - (uintptr_t)&_text,
                        &s_Text32, (KernPageList_t *)-1, NULL);

        AssertMsg(rc == NO_ERROR, ("locking text32 failed, rc=%d\n"));

        rc = KernVMLock(VMDHL_LONG | VMDHL_WRITE,
                        &_data, (uintptr_t)&_end - (uintptr_t)&_data,
                        &s_Data32, (KernPageList_t *)-1, NULL);
        AssertMsg(rc == NO_ERROR, ("locking text32 failed, rc=%d\n"));
#endif

        /* Initialize VBox subsystem. */
        rc = VbglR0SfInit();

        if (RT_FAILURE(rc))
        {
            log("VBOXFS: %s: ERROR while initializing VBox subsystem (%Rrc)!\n", __FUNCTION__, rc);
            return;
        }

        /* Connect the HGCM client */
        RT_ZERO(g_clientHandle);

        rc = VbglR0SfConnect(&g_clientHandle); //// hang on VBox 5.0.14

        if (RT_FAILURE(rc))
        {
            log("VBOXFS: %s: ERROR while connecting to host (%Rrc)!\n", __FUNCTION__, rc);
            VbglR0SfTerm();
            return;
        }

        rc = VbglR0SfSetUtf8(&g_clientHandle);

        if (RT_FAILURE(rc))
        {
            log("VBOXFS: VbglR0SfSetUtf8 failed. rc=%d\n", rc);
            return;
        }

        /* Initialize Global Infoseg pointer */
        g_pGIS = (PGINFOSEG)&KernSISData;
        //ULONG ulPtr = g_selGIS;
        //ulPtr <<= 16;
        //g_pGIS = (PGINFOSEG)KernSelToFlat(ulPtr);
        
        /* Initialize Global Environment pointer */
        //ulPtr = g_selEnv;
        //ulPtr <<= 16;
        //g_pEnv = (char *)KernSelToFlat(ulPtr);
        
#ifdef DUMP_ENV
        char *env = g_pEnv;
        int i, j;

        /* Dump the env. */
        for (i = 0; i < 0x2000 / 16; i++)
        {
            log("%08lx  ", 16 * i);

            for (j = 0; j < 16; j++)
            {
                log("%02x ", env[16 * i + j] & 0xff);
            }

            log("  ");

            for (j = 0; j < 16; j++)
            {
                log("%c", env[16 * i + j] & 0xff);
            }

            log("\n");
        }
#endif

        log("VBoxFSR0Init: completed successfully\n");
        return;
    }
    else
    {
        log("VBoxFS: Failed to connect to VBoxGuest.sys.\n");
    }
}
