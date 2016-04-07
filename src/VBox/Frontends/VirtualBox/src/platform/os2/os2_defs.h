/* $Id: VBoxHlp.h 3 2015-07-31 15:39:00Z dmik $ */
/** @file
 * VBox Qt GUI - Declaration of OS/2-specific helpers that require to reside in a DLL.
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

#ifndef __os2_defs_h
#define __os2_defs_h

// for not redefining these in os2.h
#undef RT_MAX
#undef LONG
#undef ULONG
#undef BOOL

#define  INCL_BASE
#define  INCL_WININPUT
#include <os2.h> 

// undef OS/2 types and define them IPRT way again
#undef LONG
#undef ULONG
#undef BOOL

#include <VBox/com/defs.h>

#define RT_MAX(Value1, Value2) ( (Value1) >= (Value2) ? (Value1) : (Value2) )

#define BOOL    PRBool
#define LONG    PRInt32
#define ULONG   PRUint32

#define  UM_PREACCEL_CHAR WM_USER

#define WM_KEYDOWN               0x0100
#define WM_KEYUP                 0x0101
#define WM_SYSKEYDOWN            0x0104
#define WM_SYSKEYUP              0x0105

/* Extra virtual keys returned by UIHotKeyEditor::virtualKey() */
#define VK_LSHIFT   VK_USERFIRST + 0
#define VK_LCTRL    VK_USERFIRST + 1
#define VK_LWIN     VK_USERFIRST + 2
#define VK_RWIN     VK_USERFIRST + 3
#define VK_WINMENU  VK_USERFIRST + 4
#define VK_FORWARD  VK_USERFIRST + 5
#define VK_BACKWARD VK_USERFIRST + 6

#endif
