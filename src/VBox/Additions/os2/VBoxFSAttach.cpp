/** $Id: VBoxFS.cpp 161 2018-04-10 00:36:59Z valerius $ */
/** @file
 * VBoxFS - OS/2 Shared Folders, the FS and FSD level IFS EPs
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

#define  OS2EMX_PLAIN_CHAR
#define  INCL_DOSFILEMGR
#define  INCL_DOSERRORS
#include <os2.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    APIRET rc;

    if (argc == 1)
    {
        printf("attach [d: [<sharename> | /delete]]\n\n");
        return 0;
    }

    if (argc == 3 && !stricmp(argv[2], "/delete"))
        rc = DosFSAttach(argv[1], "VBOXFS", NULL, 0, FS_DETACH);
    else
    {
        if (argc == 2)
            rc = DosFSAttach(argv[1], "VBOXFS", NULL, 0, FS_ATTACH);
        else
            rc = DosFSAttach(argv[1], "VBOXFS", argv[2], strlen(argv[2]) + 1, FS_ATTACH);
    }

    switch (rc)
    {
      case ERROR_INVALID_FSD_NAME:
             puts ("Error: IFS=VBOXFS.IFS not loaded in CONFIG.SYS");
             return 1;

      case ERROR_INVALID_PATH:
             puts ("Error: Invalid drive letter");
             return 1;

      case ERROR_ALREADY_ASSIGNED:
             puts ("Error: This drive letter is already in use");
             return 1;
 
      case NO_ERROR:
             puts ("OK");
             return 0;

      default:
             puts ("DosFSAttach failed");
             return 1;
    }

    return 0;
}
