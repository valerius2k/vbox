/** @file
 * Shared Clipboard: OS/2 PM host.
 */

/*
 * Copyright (C) 2006-2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#define INCL_DOSSEMAPHORES
#define INCL_WINCLIPBOARD
#define INCL_WINERRORS
#define INCL_WINTIMER
#define INCL_WINATOM
#include <os2.h>

//#include <windows.h>

// unicode clipboard
#include <uclip.h>

// for wcslen()
#include <wchar.h>

#define LOG_ENABLE_FLOW
#define LOG_ENABLED

#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/thread.h>
#include <process.h>

#include "VBoxClipboard.h"

#define dprintf Log

static char gachWindowClassName[] = "VBoxSharedClipboardClass";

enum { CBCHAIN_TIMEOUT = 5000 /* ms */ };

struct _VBOXCLIPBOARDCONTEXT
{
    HWND    hwnd;
    //HWND    hwndNextInChain;

    HAB     hab;

    ULONG   timerRefresh;
    //UINT     timerRefresh;

    bool     fFmtAnnouncePending;
    //bool     fCBChainPingInProcess;

    RTTHREAD thread;
    bool volatile fTerminate;

    HEV hRenderEvent;
    //HANDLE hRenderEvent;

    VBOXCLIPBOARDCLIENTDATA *pClient;
};

/* Only one client is supported. There seems to be no need for more clients. */
static VBOXCLIPBOARDCONTEXT g_ctx;


#ifdef LOG_ENABLED
void vboxClipboardDump(const void *pv, size_t cb, uint32_t u32Format)
{
    if (u32Format & VBOX_SHARED_CLIPBOARD_FMT_UNICODETEXT)
    {
        Log(("DUMP: VBOX_SHARED_CLIPBOARD_FMT_UNICODETEXT:\n"));

        if (pv && cb)
        {
            Log(("%ls\n", pv));
        }
        else
        {
            Log(("%p %d\n", pv, cb));
        }
    }
    else if (u32Format & VBOX_SHARED_CLIPBOARD_FMT_BITMAP)
    {
        dprintf(("DUMP: VBOX_SHARED_CLIPBOARD_FMT_BITMAP\n"));
    }
    else if (u32Format & VBOX_SHARED_CLIPBOARD_FMT_HTML)
    {
        Log(("DUMP: VBOX_SHARED_CLIPBOARD_FMT_HTML:\n"));

        if (pv && cb)
        {
            Log(("%s\n", pv));
        }
        else
        {
            Log(("%p %d\n", pv, cb));
        }
    }
    else
    {
        dprintf(("DUMP: invalid format %02X\n", u32Format));
    }
}
#else
#define vboxClipboardDump(__pv, __cb, __format) do { NOREF(__pv); NOREF(__cb); NOREF(__format); } while (0)
#endif /* LOG_ENABLED */

static void vboxClipboardGetData (uint32_t u32Format, const void *pvSrc, uint32_t cbSrc,
                                  void *pvDst, uint32_t cbDst, uint32_t *pcbActualDst)
{
    dprintf (("vboxClipboardGetData.\n"));

    *pcbActualDst = cbSrc;

    LogFlow(("vboxClipboardGetData cbSrc = %d, cbDst = %d\n", cbSrc, cbDst));

    if (cbSrc > cbDst)
    {
        /* Do not copy data. The dst buffer is not enough. */
        return;
    }

    memcpy (pvDst, pvSrc, cbSrc);

    vboxClipboardDump(pvDst, cbSrc, u32Format);

    return;
}

static int vboxClipboardReadDataFromClient (VBOXCLIPBOARDCONTEXT *pCtx, uint32_t u32Format)
{
    ULONG ulResetCnt = 0;

    Assert(pCtx->pClient);
    Assert(pCtx->pClient->data.pv == NULL && pCtx->pClient->data.cb == 0 && pCtx->pClient->data.u32Format == 0);

    LogFlow(("vboxClipboardReadDataFromClient u32Format = %02X\n", u32Format));

    DosResetEventSem (pCtx->hRenderEvent, &ulResetCnt);
    //ResetEvent (pCtx->hRenderEvent);

    vboxSvcClipboardReportMsg (pCtx->pClient, VBOX_SHARED_CLIPBOARD_HOST_MSG_READ_DATA, u32Format);

    DosWaitEventSem(pCtx->hRenderEvent, SEM_INDEFINITE_WAIT);
    //WaitForSingleObject(pCtx->hRenderEvent, INFINITE);

    LogFlow(("vboxClipboardReadDataFromClient wait completed\n"));

    return VINF_SUCCESS;
}

static void vboxClipboardChanged (VBOXCLIPBOARDCONTEXT *pCtx)
{
    LogFlow(("vboxClipboardChanged\n"));

    if (pCtx->pClient == NULL)
    {
        Log(("return\n"));
        return;
    }

    /* Query list of available formats and report to host. */
    HAB hab = g_ctx.hab;
    //LogFlow(("hab=%x\n", hab));

    //if (OpenClipboard (pCtx->hwnd))
    if ( WinOpenClipbrd (hab) )
    {
        Log(("WinOpenClipbrd\n"));
        uint32_t u32Formats = 0;

        ULONG format = 0;
        //UINT format = 0;

        //while ((format = EnumClipboardFormats (format)) != 0)
        while ((format = WinEnumClipbrdFmts (hab, format)) != 0)
        {
            LogFlow(("vboxClipboardChanged format %#x\n", format));

            switch (format)
            {
                //case CF_UNICODETEXT:
                case UCLIP_CF_TEXT:
                //case UCLIP_CF_UNICODETEXT:
                //case UCLIP_CF_OEMTEXT:
                //case UCLIP_CF_DSPTEXT:
                    u32Formats |= VBOX_SHARED_CLIPBOARD_FMT_UNICODETEXT;
                    break;

                //case CF_DIB:
                case UCLIP_CF_BITMAP:
                //case UCLIP_CF_DSPBITMAP:
                    u32Formats |= VBOX_SHARED_CLIPBOARD_FMT_BITMAP;
                    break;

                default:
                    if (format >= 0xC000)
                    {
                        ATOM atom = (ATOM)format;
                        char szFormatName[256];
                        //TCHAR szFormatName[256];

                        ULONG cActual = WinQueryAtomName ( WinQuerySystemAtomTable(), atom, (PSZ)szFormatName, sizeof(szFormatName) );
                        //int cActual = GetClipboardFormatName(format, szFormatName, sizeof(szFormatName)/sizeof (TCHAR));

                        if (cActual)
                        {
                            if (strcmp (szFormatName, "HTML Format") == 0)
                            {
                                u32Formats |= VBOX_SHARED_CLIPBOARD_FMT_HTML;
                            }
                        }
                    }
                    break;
            }
        }

        //CloseClipboard ();
        WinCloseClipbrd (hab);
        Log(("WinCloseClipbrd\n"));

        if (u32Formats)
        {
            LogFlow(("vboxClipboardChanged u32Formats %02X\n", u32Formats));

            vboxSvcClipboardReportMsg (pCtx->pClient, VBOX_SHARED_CLIPBOARD_HOST_MSG_FORMATS, u32Formats);
        }
    }
}

/* Add ourselves into the chain of cliboard listeners */
static void addToCBChain (VBOXCLIPBOARDCONTEXT *pCtx)
{
    LogFlow(("addToCBChain\n"));
    HAB hab = g_ctx.hab;
    ////WinSetClipbrdOwner (hab, pCtx->hwnd);
    BOOL ret = WinSetClipbrdViewer (hab, pCtx->hwnd);
    LogFlow(("WinSetClipbrdViewer ret=%x, err=%x\n", ret, WinGetLastError(hab)));
    //pCtx->hwndNextInChain = SetClipboardViewer (pCtx->hwnd);
}

/* Remove ourselves from the chain of cliboard listeners */
static void removeFromCBChain (VBOXCLIPBOARDCONTEXT *pCtx)
{
    LogFlow(("removeFromCBChain\n"));
    HAB hab = g_ctx.hab;
    //ChangeClipboardChain (pCtx->hwnd, pCtx->hwndNextInChain);
    ////WinSetClipbrdOwner (hab, NULLHANDLE);
    BOOL ret = WinSetClipbrdViewer (hab, NULLHANDLE);
    LogFlow(("WinSetClipbrdViewer ret=%x, err=%x\n", ret, WinGetLastError(hab)));
    //pCtx->hwndNextInChain = NULL;
}

/* Callback which is invoked when we have successfully pinged ourselves down the
 * clipboard chain.  We simply unset a boolean flag to say that we are responding.
 * There is a race if a ping returns after the next one is initiated, but nothing
 * very bad is likely to happen. */
//VOID CALLBACK CBChainPingProc(HWND hwnd, UINT uMsg, ULONG_PTR dwData, LRESULT lResult)
/* void CBChainPingProc(HWND hwnd, ULONG uMsg, PULONG dwData, MRESULT lResult)
{
    (void) hwnd;
    (void) uMsg;
    (void) lResult;
    printf("%s\n", __FUNCTION__);
    VBOXCLIPBOARDCONTEXT *pCtx = (VBOXCLIPBOARDCONTEXT *)dwData;
    pCtx->fCBChainPingInProcess = FALSE;
} */

//static LRESULT CALLBACK vboxClipboardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
static MRESULT EXPENTRY vboxClipboardWndProc(HWND hwnd, ULONG msg, MPARAM wParam, MPARAM lParam)
{
    LogFlow(("vboxClipboardWndProc\n"));

    MRESULT rc = 0;
    //LRESULT rc = 0;

    VBOXCLIPBOARDCONTEXT *pCtx = &g_ctx;

    HAB hab = pCtx->hab;
    //LogFlow(("hab=%x\n", hab));

    switch (msg)
    {
        /* case WM_CHANGECBCHAIN:
        {
            Log(("WM_CHANGECBCHAIN\n"));

            HWND hwndRemoved = (HWND)wParam;
            HWND hwndNext    = (HWND)lParam;

            if (hwndRemoved == pCtx->hwndNextInChain)
            {
                // The window that was next to our in the chain is being removed.
                // Relink to the new next window.
                //
                pCtx->hwndNextInChain = hwndNext;
            }
            else
            {
                if (pCtx->hwndNextInChain)
                {
                    // Pass the message further.
                    DWORD_PTR dwResult;
                    rc = SendMessageTimeout(pCtx->hwndNextInChain, WM_CHANGECBCHAIN, wParam, lParam, 0, CBCHAIN_TIMEOUT, &dwResult);
                    if (!rc)
                        rc = (LRESULT)dwResult;
                }
            }
        } break; */

        case WM_DRAWCLIPBOARD:
        {
            Log(("WM_DRAWCLIPBOARD\n"));
            //Log(("WM_DRAWCLIPBOARD next %p\n", pCtx->hwndNextInChain));

            //if (GetClipboardOwner () != hwnd)
            if ( !pCtx->fFmtAnnouncePending && WinQueryClipbrdOwner(hab) != hwnd )
            {
                /* Clipboard was updated by another application. */
                vboxClipboardChanged (pCtx);
            }

            //if (pCtx->hwndNextInChain)
            //{
                /* Pass the message to next windows in the clipboard chain. */
                //DWORD_PTR dwResult;
                //rc = SendMessageTimeout(pCtx->hwndNextInChain, msg, wParam, lParam, 0, CBCHAIN_TIMEOUT, &dwResult);
                //if (! rc )
                //    rc = dwResult;

                //rc = WinSendMsg (pCtx->hwndNextInChain, msg, wParam, lParam);
            //}
        } break;

        case WM_TIMER:
        {
            HWND hViewer = WinQueryClipbrdViewer(hab);
            //LogFlow(("WinQueryClipbrdViewer: hViewer=%x, err=%x\n", hViewer, WinGetLastError(hab)));
            //HWND hViewer = GetClipboardViewer();

            /* Re-register ourselves in the clipboard chain if our last ping
             * timed out or there seems to be no valid chain. */
            if (!hViewer) // || pCtx->fCBChainPingInProcess)
            {
                removeFromCBChain(pCtx);
                addToCBChain(pCtx);
            }
            /* Start a new ping by passing a dummy WM_CHANGECBCHAIN to be
             * processed by ourselves to the chain. */
            //pCtx->fCBChainPingInProcess = TRUE;
            ////hViewer = WinQueryClipbrdViewer(hab);
            //hViewer = GetClipboardViewer();
            //if (hViewer)
            //    SendMessageCallback(hViewer, WM_CHANGECBCHAIN, (WPARAM)pCtx->hwndNextInChain, (LPARAM)pCtx->hwndNextInChain, CBChainPingProc, (ULONG_PTR) pCtx);
        } break;

        case WM_CLOSE:
        {
            /* Do nothing. Ignore the message. */
        } break;

        //case WM_RENDERFORMAT:
        case WM_RENDERFMT:
        {
            /* Insert the requested clipboard format data into the clipboard. */
            uint32_t u32Format = 0;

            ULONG format = (ULONG)wParam;
            //UINT format = (UINT)wParam;

            Log(("WM_RENDERFMT %d\n", format));
            //Log(("WM_RENDERFORMAT %d\n", format));

            if (! WinOpenClipbrd(hab) ) break;
            Log(("WinOpenClipbrd\n"));

            switch (format)
            {
                //case CF_UNICODETEXT:
                case UCLIP_CF_TEXT:
                //case UCLIP_CF_UNICODETEXT:
                //case UCLIP_CF_OEMTEXT:
                //case UCLIP_CF_DSPTEXT:
                    u32Format |= VBOX_SHARED_CLIPBOARD_FMT_UNICODETEXT;
                    break;

                //case CF_DIB:
                case UCLIP_CF_BITMAP:
                //case UCLIP_CF_DSPBITMAP:
                    u32Format |= VBOX_SHARED_CLIPBOARD_FMT_BITMAP;
                    break;

                default:
                    if (format >= 0xC000)
                    {
                        ATOM atom = (ATOM)format;
                        char szFormatName[256];
                        //TCHAR szFormatName[256];

                        ULONG cActual = WinQueryAtomName ( WinQuerySystemAtomTable(), atom, (PSZ)szFormatName, sizeof(szFormatName) );
                        //int cActual = GetClipboardFormatName(format, szFormatName, sizeof(szFormatName)/sizeof (TCHAR));

                        if (cActual)
                        {
                            if (strcmp (szFormatName, "HTML Format") == 0)
                            {
                                u32Format |= VBOX_SHARED_CLIPBOARD_FMT_HTML;
                            }
                        }
                    }
                    break;
            }

            if (u32Format == 0 || pCtx->pClient == NULL)
            {
                /* Unsupported clipboard format is requested. */
                Log(("WM_RENDERFMT unsupported format requested or client is not active.\n"));

                WinEmptyClipbrd (hab);
                //EmptyClipboard ();
            }
            else
            {
                int vboxrc = vboxClipboardReadDataFromClient (pCtx, u32Format);

                dprintf(("vboxClipboardReadDataFromClient vboxrc = %d\n", vboxrc));

                if (   RT_SUCCESS (vboxrc)
                    && pCtx->pClient->data.pv != NULL
                    && pCtx->pClient->data.cb > 0
                    && pCtx->pClient->data.u32Format == u32Format)
                {
                    PVOID pMem;
                    APIRET ret = DosAllocSharedMem (&pMem, NULL, 
                                     pCtx->pClient->data.cb, 
                                     PAG_WRITE|PAG_COMMIT|OBJ_GIVEABLE);
                    //HANDLE hMem = GlobalAlloc (GMEM_DDESHARE | GMEM_MOVEABLE, pCtx->pClient->data.cb);

                    dprintf(("pMem %p\n", pMem));

                    //if (hMem)
                    if (! ret)
                    {
                        //void *pMem = GlobalLock (hMem);

                        dprintf(("pMem %p, size %d\n", pMem, pCtx->pClient->data.cb));
                        //dprintf(("pMem %p, GlobalSize %d\n", pMem, GlobalSize (hMem)));

                        if (pMem)
                        {
                            Log(("WM_RENDERFMT setting data\n"));
                            //Log(("WM_RENDERFORMAT setting data\n"));

                            if (pCtx->pClient->data.pv)
                            {
                                memcpy (pMem, pCtx->pClient->data.pv, pCtx->pClient->data.cb);

                                RTMemFree (pCtx->pClient->data.pv);
                                pCtx->pClient->data.pv        = NULL;
                            }

                            pCtx->pClient->data.cb        = 0;
                            pCtx->pClient->data.u32Format = 0;

                            /* The memory must be unlocked before inserting to the Clipboard. */
                            //GlobalUnlock (hMem);

                            // Clear old clipboard contents
                            WinEmptyClipbrd (hab);

                            /* 'hMem' contains the host clipboard data.
                             * size is 'cb' and format is 'format'.
                             */
                            BOOL hClip = WinSetClipbrdData (hab, (ULONG)pMem, format, CFI_POINTER);
                            //HANDLE hClip = SetClipboardData (format, hMem);

                            dprintf(("vboxClipboardHostEvent hClip %p\n", hClip));

                            if (hClip)
                            {
                                /* The hMem ownership has gone to the system. Nothing to do. */
                                WinCloseClipbrd(hab);
                                Log(("WinCloseClipbrd\n"));
                                DosFreeMem (pMem);
                                break;
                            }
                        }

                        DosFreeMem (pMem);
                        //GlobalFree (hMem);
                    }
                }

                RTMemFree (pCtx->pClient->data.pv);
                pCtx->pClient->data.pv        = NULL;
                pCtx->pClient->data.cb        = 0;
                pCtx->pClient->data.u32Format = 0;

                /* Something went wrong. */
                WinEmptyClipbrd (hab);
                //EmptyClipboard ();
            }
            WinCloseClipbrd(hab);
            Log(("WinCloseClipbrd\n"));

        } break;

        //case WM_RENDERALLFORMATS:
        case WM_RENDERALLFMTS:
        {
            Log(("WM_RENDERALLFMTS\n"));
            //Log(("WM_RENDERALLFORMATS\n"));

            /* Do nothing. The clipboard formats will be unavailable now, because the
             * windows is to be destroyed and therefore the guest side becomes inactive.
             */
            //if (OpenClipboard (hwnd))
            if ( WinOpenClipbrd (hab) )
            {
                Log(("WinOpenClipbrd\n"));
                WinEmptyClipbrd(hab);
                //EmptyClipboard();

                WinCloseClipbrd(hab);
                //CloseClipboard();
                Log(("WinCloseClipbrd\n"));
            }
        } break;

        case WM_USER:
        {
            if (pCtx->pClient == NULL || pCtx->pClient->fMsgFormats)
            {
                /* Host has pending formats message. Ignore the guest announcement,
                 * because host clipboard has more priority.
                 */
                break;
            }

            /* Announce available formats. Do not insert data, they will be inserted in WM_RENDER*. */
            uint32_t u32Formats = (uint32_t)lParam;

            Log(("WM_USER u32Formats = %02X\n", u32Formats));

            pCtx->fFmtAnnouncePending = TRUE;

            //if (OpenClipboard (hwnd))
            if ( WinOpenClipbrd (hab) )
            {
                Log(("WinOpenClipbrd\n"));
                BOOL ret = WinSetClipbrdOwner (hab, hwnd);

                Log(("WinSetClipbrdOwner ret=%x, err=%x, hwnd=%x, pCtx->hwnd=%x\n", 
                          ret, WinGetLastError(hab), hwnd, pCtx->hwnd));

                ////WinEmptyClipbrd(hab);
                //EmptyClipboard();

                //Log(("WM_USER emptied clipboard\n"));

                BOOL hClip = NULL;
                //HANDLE hClip = NULL;

                if (u32Formats & VBOX_SHARED_CLIPBOARD_FMT_UNICODETEXT)
                {
                    dprintf(("window proc WM_USER: VBOX_SHARED_CLIPBOARD_FMT_UNICODETEXT\n"));

                    hClip = WinSetClipbrdData (hab, NULL, UCLIP_CF_TEXT, CFI_POINTER);
                    //if (! hClip) Log(("hClip == 0, err: %lx\n", WinGetLastError(hab)));
                    //hClip = SetClipboardData (CF_UNICODETEXT, NULL);
                }

                if (u32Formats & VBOX_SHARED_CLIPBOARD_FMT_BITMAP)
                {
                    dprintf(("window proc WM_USER: VBOX_SHARED_CLIPBOARD_FMT_BITMAP\n"));

                    hClip = WinSetClipbrdData (hab, NULL, UCLIP_CF_BITMAP, CFI_HANDLE);
                    //if (! hClip) Log(("hClip == 0, err: %lx\n", WinGetLastError(hab)));
                    //hClip = SetClipboardData (CF_DIB, NULL);
                }

                if (u32Formats & VBOX_SHARED_CLIPBOARD_FMT_HTML)
                {
                    ATOM format = WinAddAtom ( WinQuerySystemAtomTable(), (PCSZ)"HTML Format" );
                    //UINT format = RegisterClipboardFormat ("HTML Format");

                    dprintf(("window proc WM_USER: VBOX_SHARED_CLIPBOARD_FMT_HTML 0x%04X\n", format));

                    if (format != 0)
                    {
                        hClip = WinSetClipbrdData (hab, NULL, format, CFI_POINTER);
                        //hClip = SetClipboardData (format, NULL);
                    }
                }

                WinCloseClipbrd(hab);
                //CloseClipboard();
                Log(("WinCloseClipbrd\n"));

                pCtx->fFmtAnnouncePending = FALSE;

                dprintf(("window proc WM_USER: hClip %d, err %lx\n", hClip, WinGetLastError(hab)));
                //dprintf(("window proc WM_USER: hClip %p, err %d\n", hClip, GetLastError ()));
            }
            else
            {
                dprintf(("window proc WM_USER: failed to open clipboard\n"));
            }

            //WinSetClipbrdOwner (hab, NULLHANDLE);
        } break;

        default:
        {
            Log(("WM_ %p\n", msg));
            rc = WinDefWindowProc (hwnd, msg, wParam, lParam);
            //rc = DefWindowProc (hwnd, msg, wParam, lParam);
        }
    }

    Log(("WM_ rc %d\n", rc));
    return rc;
}

DECLCALLBACK(int) VBoxClipboardThread (RTTHREAD ThreadSelf, void *pInstance)
{
    /* Create a window and make it a clipboard viewer. */
    LogFlow(("VBoxClipboardThread\n"));

    int rc = VINF_SUCCESS;

    VBOXCLIPBOARDCONTEXT *pCtx = &g_ctx;

    //HINSTANCE hInstance = (HINSTANCE)GetModuleHandle (NULL);
    HAB hab = WinInitialize(0);

    if ( hab == NULLHANDLE ) return 1;

    pCtx->hab = hab;

    Log(("WinInitialize\n"));

    HMQ hmq = WinCreateMsgQueue(hab, 0);

    if ( hmq == NULLHANDLE ) return 1;

    Log(("WinCreateMsgQueue\n"));

    /* Register the Window Class. */
    //WNDCLASS wc;

    //wc.style         = CS_NOCLOSE;
    //wc.lpfnWndProc   = vboxClipboardWndProc;
    //wc.cbClsExtra    = 0;
    //wc.cbWndExtra    = 0;
    //wc.hInstance     = hInstance;
    //wc.hIcon         = NULL;
    //wc.hCursor       = NULL;
    //wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);
    //wc.lpszMenuName  = NULL;
    //wc.lpszClassName = gachWindowClassName;

    //ATOM atomWindowClass = RegisterClass (&wc);

    rc = WinRegisterClass(hab, 
           (PCSZ)gachWindowClassName,
           vboxClipboardWndProc,
           0, 
           0);

    //if (atomWindowClass == 0)
    if ( rc == FALSE )
    {
       Log(("Failed to register window class\n"));
       rc = VERR_NOT_SUPPORTED;
    }
    else
    {
       Log(("WinRegisterClass\n"));

        /* Create the window. */
       pCtx->hwnd = WinCreateWindow (HWND_DESKTOP,
                                    (PCSZ)gachWindowClassName, (PCSZ)gachWindowClassName, 0,
                                    -200, -200, 100, 100, NULLHANDLE, HWND_TOP,
                                    0, NULL, NULL);

        //pCtx->hwnd = CreateWindowEx (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        //                             gachWindowClassName, gachWindowClassName,
        //                             WS_POPUPWINDOW,
        //                             -200, -200, 100, 100, NULL, NULL, hInstance, NULL);

        if (pCtx->hwnd == NULL)
        {
            Log(("Failed to create window, lasterr: 0x%lx\n", WinGetLastError(hab)));
            rc = VERR_NOT_SUPPORTED;
        }
        else
        {
            pCtx->fFmtAnnouncePending = FALSE;

            Log(("WinCreateWindow\n"));
            //LogFlow(("WinCreateWindow: hwnd=%x, err=%x\n", pCtx->hwnd, WinGetLastError(hab)));

            WinSetWindowPos(pCtx->hwnd, HWND_TOP, -200, -200, 0, 0,
                         SWP_DEACTIVATE | SWP_HIDE | SWP_NOREDRAW);

            Log(("WinSetWindowPos\n"));
            //LogFlow(("WinSetWindowPos: hwnd=%x, err=%x\n", pCtx->hwnd, WinGetLastError(hab)));

            //SetWindowPos(pCtx->hwnd, HWND_TOPMOST, -200, -200, 0, 0,
            //             SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOSIZE);

            addToCBChain(pCtx);

            Log(("addToCBChain\n"));

            pCtx->timerRefresh = WinStartTimer(hab, pCtx->hwnd, 0, 10 * 1000);
            //pCtx->timerRefresh = SetTimer(pCtx->hwnd, 0, 10 * 1000, NULL);

            Log(("WinStartTimer, pCtx->timerRefresh=%ld\n", pCtx->timerRefresh));
            //LogFlow(("WinStartTimer: hwnd=%x, err=%x\n", pCtx->hwnd, WinGetLastError(hab)));

            QMSG msg;
            //MSG msg;

            while ( WinGetMsg(hab, &msg, NULL, 0, 0) && !pCtx->fTerminate )
                WinDispatchMsg(hab, &msg);

            Log(("WinGetMsg/WinDispatchMsg loop finished\n"));

            //while (GetMessage(&msg, NULL, 0, 0) && !pCtx->fTerminate)
            //{
            //    TranslateMessage(&msg);
            //    DispatchMessage(&msg);
            //}
        }
    }

    if (pCtx->hwnd)
    {
        removeFromCBChain(pCtx);

        Log(("removeFromCBChain\n"));

        if (pCtx->timerRefresh)
        {
            WinStopTimer(hab, pCtx->hwnd, pCtx->timerRefresh);

            Log(("WinStopTimer\n"));
            //LogFlow(("WinStopTimer: hwnd=%x, err=%x\n", pCtx->hwnd, WinGetLastError(hab)));
        }

        //if (pCtx->timerRefresh)
        //    KillTimer(pCtx->hwnd, 0);

        WinDestroyWindow (pCtx->hwnd);
        //DestroyWindow (pCtx->hwnd);

        Log(("WinDestroyWindow\n"));
        //LogFlow(("WinDestroyWindow: hwnd=%x, err=%x\n", pCtx->hwnd, WinGetLastError(hab)));

        pCtx->hwnd = NULL;
    }

    //if (atomWindowClass != 0)
    //{
        //UnregisterClass (gachWindowClassName, hInstance);
        //atomWindowClass = 0;
    //}

    WinDestroyMsgQueue(hmq);

    Log(("WinDestroyMsgQueue\n"));

    WinTerminate(hab);

    Log(("WinTerminate\n"));

    return 0;
}

/*
 * Public platform dependent functions.
 */
int vboxClipboardInit (void)
{
    LogFlow(("vboxClipboardInit\n"));
    int rc = VINF_SUCCESS;

    DosCreateEventSem(NULL, &g_ctx.hRenderEvent, 0, FALSE);
    //g_ctx.hRenderEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    rc = RTThreadCreate (&g_ctx.thread, VBoxClipboardThread, NULL, 65536,
                         RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "SHCLIP");

    if (RT_FAILURE (rc))
    {
        DosCloseEventSem (g_ctx.hRenderEvent);
        //CloseHandle (g_ctx.hRenderEvent);
    }

    return rc;
}

void vboxClipboardDestroy (void)
{
    LogFlow(("vboxClipboardDestroy\n"));

    /* Set the termination flag and ping the window thread. */
    ASMAtomicWriteBool (&g_ctx.fTerminate, true);

    if (g_ctx.hwnd)
    {
        WinPostMsg (g_ctx.hwnd, WM_CLOSE, 0, 0);
        Log(("WinPostMsg\n"));
        //LogFlow(("WinPostMsg: hwnd=%x, err=%x\n", g_ctx.hwnd, WinGetLastError(g_ctx.hab)));
        //PostMessage (g_ctx.hwnd, WM_CLOSE, 0, 0);
    }

    DosCloseEventSem (g_ctx.hRenderEvent);
    Log(("%s: DosCloseEventSem\n", __FUNCTION__));
    //CloseHandle (g_ctx.hRenderEvent);

    /* Wait for the window thread to terminate. */
    RTThreadWait (g_ctx.thread, RT_INDEFINITE_WAIT, NULL);

    g_ctx.thread = NIL_RTTHREAD;
}

int vboxClipboardConnect (VBOXCLIPBOARDCLIENTDATA *pClient, bool)
{
    LogFlow(("vboxClipboardConnect\n"));

    if (g_ctx.pClient != NULL)
    {
        /* One client only. */
        return VERR_NOT_SUPPORTED;
    }

    pClient->pCtx = &g_ctx;

    pClient->pCtx->pClient = pClient;

    /* Sync the host clipboard content with the client. */
    vboxClipboardSync (pClient);

    return VINF_SUCCESS;
}

int vboxClipboardSync (VBOXCLIPBOARDCLIENTDATA *pClient)
{
    /* Sync the host clipboard content with the client. */
    LogFlow(("vboxClipboardSync\n"));
    ////vboxClipboardChanged (pClient->pCtx);
    return VINF_SUCCESS;
}

void vboxClipboardDisconnect (VBOXCLIPBOARDCLIENTDATA *pClient)
{
    LogFlow(("vboxClipboardDisconnect\n"));
    g_ctx.pClient = NULL;
}

void vboxClipboardFormatAnnounce (VBOXCLIPBOARDCLIENTDATA *pClient, uint32_t u32Formats)
{
    /*
     * The guest announces formats. Forward to the window thread.
     */
    LogFlow(("vboxClipboardFormatAnnounce: WinPostMsg\n"));
    if (u32Formats)
        WinPostMsg (pClient->pCtx->hwnd, WM_USER, 0, (MPARAM)u32Formats);
    else
        Log(("u32Formats=0!\n"));
    //LogFlow(("WinPostMsg: hwnd=%x, err=%x\n", pClient->pCtx->hwnd, WinGetLastError(g_ctx.hab)));
    //PostMessage (pClient->pCtx->hwnd, WM_USER, 0, u32Formats);
}

int vboxClipboardReadData (VBOXCLIPBOARDCLIENTDATA *pClient, uint32_t u32Format, void *pv, uint32_t cb, uint32_t *pcbActual)
{
    LogFlow(("vboxClipboardReadData: u32Format = %02X\n", u32Format));

    //HANDLE hClip = NULL;
    ULONG hClip = 0;
    HAB hab = g_ctx.hab;
    //LogFlow(("hab=%x\n", hab));

    /*
     * The guest wants to read data in the given format.
     */
    //if (OpenClipboard (pClient->pCtx->hwnd))
    if ( WinOpenClipbrd (hab) )
    {
        Log(("WinOpenClipbrd\n"));

        if (u32Format & VBOX_SHARED_CLIPBOARD_FMT_BITMAP)
        {
            LogFlow(("VBOX_SHARED_CLIPBOARD_FMT_BITMAP\n"));
            hClip = WinQueryClipbrdData(hab, UCLIP_CF_BITMAP); // UCLIP_CF_BITMAP
            //hClip = GetClipboardData (CF_DIB);

            LogFlow(("hClip=%x\n", hClip));

            if (hClip != NULL)
            {
                PVOID lp = (PVOID)hClip;
                //LPVOID lp = GlobalLock (hClip);

                if (lp != NULL)
                {
                    dprintf(("UCLIP_CF_BITMAP\n"));
                    //dprintf(("CF_DIB\n"));

                    ULONG cbSize, flags;
                    APIRET ret = DosQueryMem(lp, &cbSize, &flags);

                    Log(("DosQueryMem\n"));

                    vboxClipboardGetData (VBOX_SHARED_CLIPBOARD_FMT_BITMAP, lp, cbSize,
                                          pv, cb, pcbActual);

                    //vboxClipboardGetData (VBOX_SHARED_CLIPBOARD_FMT_BITMAP, lp, GlobalSize (hClip),
                    //                      pv, cb, pcbActual);

                    //GlobalUnlock(hClip);
                }
                else
                {
                    hClip = NULL;
                }
            }
        }
        else if (u32Format & VBOX_SHARED_CLIPBOARD_FMT_UNICODETEXT)
        {
            LogFlow(("VBOX_SHARED_CLIPBOARD_FMT_UNICODETEXT\n"));
            int   count = 0;
            ULONG fmt = 0, info;

            /* while ( ( fmt = WinEnumClipbrdFmts(hab, fmt) ) != 0)
            {
                hClip = WinQueryClipbrdData(hab, fmt);

                count++;

                Log(("fmt=%x, count=%d, hClip=%x\n", fmt, count, hClip));

                if (hClip)
                {
                    WinQueryClipbrdFmtInfo(hab, fmt, &info);
                    Log(("info=%x\n", info));
                    break;
                }
            } */

            hClip = WinQueryClipbrdData(hab, UCLIP_CF_TEXT); // returns 0 if using UCLIP_CF_UNICODETEXT
            //hClip = GetClipboardData(CF_UNICODETEXT);

            LogFlow(("hClip=%x\n", hClip));

            if (hClip != NULL)
            {
                wchar_t *uniString = (wchar_t *)hClip;
                //LPWSTR uniString = (LPWSTR)GlobalLock (hClip);

                if (uniString != NULL)
                {
                    dprintf(("UCLIP_CF_UNICODETEXT\n"));

                    ULONG cbSize, flags;
                    APIRET ret = DosQueryMem((void *)hClip, &cbSize, &flags);
                    Log(("uniString=%x, DosQueryMem ret=%u, cbSize=%u, flags=%x\n", uniString, ret, cbSize, flags));

                    vboxClipboardGetData (VBOX_SHARED_CLIPBOARD_FMT_UNICODETEXT, uniString, (wcslen (uniString) + 1) * 2,
                                          pv, cb, pcbActual);

                    //vboxClipboardGetData (VBOX_SHARED_CLIPBOARD_FMT_UNICODETEXT, uniString, (lstrlenW (uniString) + 1) * 2,
                    //                      pv, cb, pcbActual);

                    //GlobalUnlock(hClip);
                }
                else
                {
                    hClip = NULL;
                }
            }
        }
        else if (u32Format & VBOX_SHARED_CLIPBOARD_FMT_HTML)
        {
            LogFlow(("VBOX_SHARED_CLIPBOARD_FMT_HTML\n"));

            ATOM format = WinAddAtom ( WinQuerySystemAtomTable(), (PCSZ)"HTML Format" );
            //UINT format = RegisterClipboardFormat ("HTML Format");

            LogFlow(("format=%x\n", format));

            if (format != 0)
            {
                hClip = WinQueryClipbrdData(hab, format);
                //hClip = GetClipboardData (format);

                LogFlow(("hClip=%x\n", hClip));

                if (hClip != NULL)
                {
                    PVOID lp = (PVOID)hClip;
                    //LPVOID lp = GlobalLock (hClip);

                    ULONG cbSize, flags;
                    APIRET ret = DosQueryMem(lp, &cbSize, &flags);

                    Log(("DosQueryMem\n"));

                    //if (lp != NULL)
                    if ( ! ret )
                    {
                        dprintf(("CF_HTML\n"));

                        vboxClipboardGetData (VBOX_SHARED_CLIPBOARD_FMT_HTML, lp, cbSize,
                                              pv, cb, pcbActual);

                        //vboxClipboardGetData (VBOX_SHARED_CLIPBOARD_FMT_HTML, lp, GlobalSize (hClip),
                        //                      pv, cb, pcbActual);

                        //GlobalUnlock(hClip);
                    }
                    else
                    {
                        hClip = NULL;
                    }
                }
            }
        }

        WinCloseClipbrd (hab);
        //CloseClipboard ();
        Log(("WinCloseClipbrd\n"));
    }
    else
    {
        dprintf(("failed to open clipboard\n"));
    }

    if (hClip == NULL)
    {
        /* Reply with empty data. */
        Log(("hClip == NULL, err: %lx\n", WinGetLastError(hab)));
        vboxClipboardGetData (0, NULL, 0,
                              pv, cb, pcbActual);
    }

    return VINF_SUCCESS;
}

void vboxClipboardWriteData (VBOXCLIPBOARDCLIENTDATA *pClient, void *pv, uint32_t cb, uint32_t u32Format)
{
    LogFlow(("vboxClipboardWriteData\n"));
 
    /*
     * The guest returns data that was requested in the WM_RENDERFMT handler.
     */
    Assert(pClient->data.pv == NULL && pClient->data.cb == 0 && pClient->data.u32Format == 0);

    vboxClipboardDump(pv, cb, u32Format);

    if (cb > 0)
    {
        pClient->data.pv = RTMemAlloc (cb);

        if (pClient->data.pv)
        {
            memcpy (pClient->data.pv, pv, cb);
            pClient->data.cb = cb;
            pClient->data.u32Format = u32Format;
        }
    }

    DosPostEventSem(pClient->pCtx->hRenderEvent);
    //SetEvent(pClient->pCtx->hRenderEvent);

    Log(("DosPostEventSem\n"));
}
