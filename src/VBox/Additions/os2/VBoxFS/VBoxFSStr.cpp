/** $Id: VBoxFS.cpp 14 2015-11-29 02:15:22Z dmik $ */
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEFAULT
#include "VBoxFSInternal.h"

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/utf16.h>
#include <iprt/string.h>
#include <iprt/mem.h>

#include <string.h>

extern VBGLSFCLIENT g_clientHandle;
extern ULONG        g_fHideLFN;
extern ULONG        g_Cp;

UconvObj cp_uconv = {0};
UconvObj utf8_uconv = {0};
UniChar *starter_table = NULL;
static char initted = 0;

PSHFLSTRING make_shflstring(const char* const s)
{
    int len = strlen(s);

    if (len > 0xFFFE)
    {
        log(("make_shflstring: string too long\n"));
        return NULL;
    }

    PSHFLSTRING rv = (PSHFLSTRING)RTMemAllocZ(sizeof(SHFLSTRING) + len);

    if (! rv)
    {
        return NULL;
    }

    rv->u16Length = len;
    rv->u16Size = len + 1;
    strcpy((char *)rv->String.utf8, s);
    return rv;
}

void free_shflstring(PSHFLSTRING s)
{
    RTMemFree(s);
}

PSHFLSTRING clone_shflstring(PSHFLSTRING s)
{
    PSHFLSTRING rv = (PSHFLSTRING)RTMemAllocZ(sizeof(SHFLSTRING) + s->u16Length);

    if (rv)
    {
        memcpy(rv, s, sizeof(SHFLSTRING) + s->u16Length);
    }

    return rv;
}

PSHFLSTRING concat_shflstring_cstr(PSHFLSTRING s1, const char* const s2)
{
    size_t s2len = strlen(s2);

    PSHFLSTRING rv = (PSHFLSTRING)RTMemAllocZ(sizeof(SHFLSTRING) + s1->u16Length + s2len);

    if (rv)
    {
        memcpy(rv, s1, sizeof(SHFLSTRING) + s1->u16Length);
        RTStrCat((char *)rv->String.utf8, s2len + s1->u16Length, s2);
        rv->u16Length += s2len;
        rv->u16Size += s2len;
    }

    return rv;
}

PSHFLSTRING concat_cstr_shflstring(const char* const s1, PSHFLSTRING s2)
{
    size_t s1len = strlen(s1);

    PSHFLSTRING rv = (PSHFLSTRING)RTMemAllocZ(sizeof(SHFLSTRING) + s1len + s2->u16Length);

    if (rv)
    {
        strcpy((char *)rv->String.utf8, s1);
        RTStrCat((char *)rv->String.utf8, s1len + s2->u16Length, (char *)s2->String.utf8);
        rv->u16Length = s1len + s2->u16Length;
        rv->u16Size = rv->u16Length + 1;
    }

    return rv;
}

PSHFLSTRING build_path(PSHFLSTRING dir, const char* const name)
{
    log(("*** build_path(%p, %p)\n", dir, name));

    if (!dir || !name)
        return NULL;

    size_t len = dir->u16Length + strlen(name) + 1;

    PSHFLSTRING rv = (PSHFLSTRING)RTMemAllocZ(sizeof(SHFLSTRING) + len);

    if (rv)
    {
        strcpy((char *)rv->String.utf8, (char *)dir->String.utf8);
        RTStrCat((char *)rv->String.utf8, len, "/");
        RTStrCat((char *)rv->String.utf8, len, name);
        rv->u16Length = len;
        rv->u16Size = rv->u16Length + 1;
    }

    return rv;
}

void vboxfsCPInit(void)
{
    uint32_t cpg = 0;
    int16_t cp = 0;
    struct SAS *pSAS;
    struct SAS_info_section *pSas_info_data;
    struct CDIB *cdib;
    struct CDIB_codepage_section *cpsec;
    APIRET rc;

    if (g_Cp)
    {
        cp = g_Cp;
    }

    /* Get the 1st user codepage from prepared codepages list */
    /* pSAS = (struct SAS *)KernSelToFlat(0x70 << 16);
    pSas_info_data = (struct SAS_info_section *)((char *)pSAS + pSAS->SAS_info_data);
    cdib = (struct CDIB *)KernSelToFlat(pSas_info_data->SAS_info_CDIB << 16);
    cpsec = (struct CDIB_codepage_section *)((char *)cdib + cdib->CDIB_codepage_ptr);

    if (cpsec->CDIB_cp_length >= sizeof(struct CDIB_cp_id_section))
    {
        cp = cpsec->CDIB_cp_first_id.CDIB_cp_id;
    } */

    log("*** Codepage: %u\n", cp);

    rc = KernCreateUconvObject(cp, &cp_uconv);

    /* Get starter table address */
    starter_table = (UniChar *)KernSelToFlat((ULONG)cp_uconv.starter);

    log("*** starter_table: %lx\n", starter_table);

    KernCreateUconvObject(1208, &utf8_uconv);
}

APIRET APIENTRY vboxfsStrToUpper(char *src, int len, char *dst)
{
    APIRET rc;
    UniChar buf_ucs2[CCHMAXPATHCOMP + 1];
    PRTUTF16 pwsz;
    int srclen;

    if (! initted)
    {
        vboxfsCPInit();
        initted = 1;
    }

    srclen = strlen(src);

    rc = KernStrToUcs(0,
                      buf_ucs2,
                      src,
                      2 * CCHMAXPATHCOMP + 2,
                      CCHMAXPATHCOMP + 1);
                      //srclen);

    if (rc)
    {
        log("KernStrToUcs returned %lu\n", rc);
    }

    pwsz = (PRTUTF16)buf_ucs2;

    pwsz = RTUtf16ToUpper(pwsz);

    rc = KernStrFromUcs(0,
                        dst,
                        pwsz,
                        CCHMAXPATHCOMP,
                        2 * CCHMAXPATHCOMP + 2);

    if (rc)
    {
        log("KernStrFromUcs returned %lu\n", rc);
    }

    return rc;
}

APIRET APIENTRY vboxfsStrToLower(char *src, int len, char *dst)
{
    APIRET rc;
    UniChar buf_ucs2[CCHMAXPATHCOMP + 1];
    PRTUTF16 pwsz;
    int srclen;

    if (! initted)
    {
        vboxfsCPInit();
        initted = 1;
    }

    srclen = strlen(src);

    rc = KernStrToUcs(0,
                      buf_ucs2,
                      src,
                      2 * CCHMAXPATHCOMP + 2,
                      CCHMAXPATHCOMP + 1);
                      //srclen);

    if (rc)
    {
        log("KernStrToUcs returned %lu\n", rc);
    }

    pwsz = (PRTUTF16)buf_ucs2;

    pwsz = RTUtf16ToLower(pwsz);

    rc = KernStrFromUcs(0,
                        dst,
                        pwsz,
                        CCHMAXPATHCOMP,
                        2 * CCHMAXPATHCOMP + 2);

    if (rc)
    {
        log("KernStrFromUcs returned %lu\n", rc);
    }

    return rc;
}

APIRET APIENTRY vboxfsStrFromUtf8(char *dst, char *src, ULONG len)
{
    APIRET  rc = NO_ERROR;
    ULONG   newlen;
    PRTUTF16 pwsz;

    if (! initted)
    {
        vboxfsCPInit();
        initted = 1;
    }

    RTStrToUtf16(src, &pwsz);

    rc = KernStrFromUcs(0,
                        dst,
                        pwsz,
                        CCHMAXPATHCOMP + 1,
                        //len,
                        2 * CCHMAXPATHCOMP + 2);
                        //RTStrUniLen(src));

    if (rc)
    {
        log("KernStrFromUcs returned %lu\n", rc);
    }

    RTMemFree(pwsz);

    return rc;
}

APIRET APIENTRY vboxfsStrToUtf8(char *dst, char *src)
{
    APIRET  rc = NO_ERROR;
    ULONG   srclen;
    ULONG   newlen;
    UniChar buf_ucs2[CCHMAXPATHCOMP + 1];

    if (! initted)
    {
        vboxfsCPInit();
        initted = 1;
    }

    srclen = strlen(src);

    rc = KernStrToUcs(0,
                      buf_ucs2,
                      src,
                      2 * CCHMAXPATHCOMP + 2,
                      CCHMAXPATHCOMP + 1);
                      //srclen);

    if (rc)
    {
        log("KernStrToUcs returned %lu\n", rc);
    }

    rc = KernStrFromUcs(&utf8_uconv,
                        dst,
                        buf_ucs2,
                        CCHMAXPATHCOMP + 1,
                        2 * CCHMAXPATHCOMP + 2);

    if (rc)
    {
        log("KernStrFromUcs returned %lu\n", rc);
    }

    return rc;
}

bool IsDBCSLead(UCHAR uch)
{
    if (! initted)
    {
        vboxfsCPInit();
        initted = 1;
    }

    if (starter_table)
    {
        return ( starter_table[ uch ] == 2 );
    }

    return false;
}

static char rgValidChars[]="01234567890 ABCDEFGHIJKLMNOPQRSTUVWXYZ!#$%&'()-_@^`{}~";

APIRET APIENTRY MakeShortName(ULONG ulFileNo, char *pszLongName, char *pszShortName)
{
    ULONG ulLongName;
    char *pLastDot;
    char *pFirstDot;
    char *p;
    ULONG usIndex;
    char  szShortName[12];
    char  szFileName[25];
    char *pszUpper;
    APIRET rc;

    ulLongName = LONGNAME_OFF;
    memset(szShortName, 0x20, 11);
    szShortName[11] = 0;

    /*
       Uppercase the longname
    */
    usIndex = strlen(pszLongName) + 5;
    pszUpper = (char *)RTMemAlloc(usIndex);
    if (!pszUpper)
        return LONGNAME_ERROR;

    rc = vboxfsStrToUpper(pszLongName, usIndex, pszUpper);
    if (rc)
    {
        RTMemFree(pszUpper);
        return LONGNAME_ERROR;
    }

    /* Skip all leading dots */
    p = pszUpper;
    while (*p == '.')
        p++;

    pLastDot  = strrchr(p, '.');
    pFirstDot = strchr(p, '.');

    if (!pLastDot)
        pLastDot = pszUpper + strlen(pszUpper);
    if (!pFirstDot)
        pFirstDot = pLastDot;

    /*
       Is the name a valid 8.3 name ?
    */
    if ((!strcmp(pszLongName, pszUpper) || IsDosSession()) &&
        pFirstDot == pLastDot &&
        pLastDot - pszUpper <= 8 &&
        strlen(pLastDot) <= 4)
    {
        char *r = pszUpper;

        if (*r != '.')
        {
            while (*r)
            {
                if (*r < 128 && !strchr(rgValidChars, *r) && *r != '.')
                break;
                r++;
            }
        }

        if (!(*r))
        {
            memset(szShortName, 0x20, sizeof szShortName);
            szShortName[11] = 0;
            memcpy(szShortName, pszUpper, pLastDot - pszUpper);
            if (*pLastDot)
                memcpy(szShortName + 8, pLastDot + 1, strlen(pLastDot + 1));

            memcpy(pszShortName, szShortName, 11);
            RTMemFree(pszUpper);

            r = szShortName + 7;
            while (*r == ' ') r--;
            r++;
            memset(szFileName, 0, sizeof(szFileName));
            memcpy(szFileName, szShortName, r - szShortName);
            if (memcmp(szShortName + 8, "   ", 3))
            {
                szFileName[r - szShortName] = '.';
                memcpy(szFileName + (r - szShortName) + 1, szShortName + 8, 3);
            }
            strcpy(pszShortName, szFileName);
            return ulLongName;
        }
    }

#if 0
    if (IsDosSession())
    {
        RTMemFree(pszUpper);
        return LONGNAME_ERROR;
    }
#endif

    ulLongName = LONGNAME_OK;

    if (pLastDot - pszUpper > 8)
        ulLongName = LONGNAME_MAKE_UNIQUE;

    szShortName[11] = 0;

    usIndex = 0;
    p = pszUpper;
    while (usIndex < 11)
    {
        if (!(*p))
            break;

        if (usIndex == 8 && p <= pLastDot)
        {
            if (p < pLastDot)
                ulLongName = LONGNAME_MAKE_UNIQUE;
            if (*pLastDot)
                p = pLastDot + 1;
            else
                break;
        }

        while (*p == 0x20)
        {
            ulLongName = LONGNAME_MAKE_UNIQUE;
            p++;
        }
        if (!(*p))
            break;

        if (*p >= 128)
        {
            szShortName[usIndex++] = *p;
        }
        else if (*p == '.')
        {
            /*
               Skip all dots, if multiple dots are in the
               name create an unique name
             */
            if (p == pLastDot)
                usIndex = 8;
            else
                ulLongName = LONGNAME_MAKE_UNIQUE;
        }
        else if (strchr(rgValidChars, *p))
            szShortName[usIndex++] = *p;
        else
        {
            szShortName[usIndex++] = '_';
            ulLongName = LONGNAME_MAKE_UNIQUE;
        }
        p++;
    }
    if (strlen(p))
        ulLongName = LONGNAME_MAKE_UNIQUE;

    RTMemFree(pszUpper);

    p = szShortName;
    for( usIndex = 0; usIndex < 8; usIndex++ )
    {
        if( IsDBCSLead( p[ usIndex ]))
            usIndex++;
    }

    if( usIndex > 8 )
        p[ 7 ] = 0x20;

    p = szShortName + 8;
    for( usIndex = 0; usIndex < 3; usIndex++ )
    {
        if( IsDBCSLead( p[ usIndex ]))
            usIndex++;
    }

    if( usIndex > 3 )
        p[ 2 ] = 0x20;

    if (ulLongName == LONGNAME_MAKE_UNIQUE)
    {
        USHORT usNum;
        char szNumber[18];
        ULONG ulCluster;
        char *q;
        USHORT usPos1, usPos2;

        for (usPos1 = 8; usPos1 > 0; usPos1--)
        {
            if (szShortName[usPos1 - 1] != 0x20)
                break;
        }

        for (usNum = 1; usNum < 32000; usNum++)
        {
            memset(szFileName, 0, sizeof szFileName);
            memcpy(szFileName, szShortName, 8);

            /*
               Find last blank in filename before dot.
             */

            memset(szNumber, 0, sizeof szNumber);

            itoa(ulFileNo, szNumber, 16);

            q = szNumber;
            while (*q)
            {
                *q = (char)toupper(*q);
                q++;
            }

            usPos2 = 7 - (strlen(szNumber));
            if (usPos1 && usPos1 < usPos2)
                usPos2 = usPos1;

            for( usIndex = 0; usIndex < usPos2; usIndex++ )
            {
                if( IsDBCSLead( szShortName[ usIndex ]))
                    usIndex++;
            }

            if( usIndex > usPos2 )
                usPos2--;

            strcpy(szFileName + usPos2, "~");
            strcat(szFileName, szNumber);

            if (memcmp(szShortName + 8, "   ", 3))
            {
                strcat(szFileName, ".");
                memcpy(szFileName + strlen(szFileName), szShortName + 8, 3);
                p = szFileName + strlen(szFileName);
                while (q > szFileName && *(q-1) == 0x20)
                    q--;
                *q = 0;
            }
            // if DirEntry number is specified, use it as NNN in file~NNN.ext,
            // no need to try over all values < 32000
            break;
        }
        if (ulFileNo < 32000)
        {
            q = strchr(szFileName, '.');
#if 0
            if (q && q - szFileName < 8)
                memcpy(szShortName, szFileName, q - szFileName);
            else
                memccpy(szShortName, szFileName, 0, 8 );
        }
#else
        if( !q )
            q = szFileName + strlen( szFileName );

        memcpy(szShortName, szFileName, q - szFileName);
        memset( szShortName + ( q - szFileName ), 0x20, 8 - ( q - szFileName ));
#endif
        }
        else
        {
            return LONGNAME_ERROR;
        }
    }

    strcpy(pszShortName, szFileName);
    return ulLongName;
}

APIRET APIENTRY TranslateName(VBGLSFMAP *map, char *pszPath, char *pszTarget,
                              int cbTarget, ULONG ulTranslate)
{
    SHFLCREATEPARMS params = {0};
    PSHFLSTRING path = NULL;
    char *pwsz = NULL;
    char szShortName[13];
    char *pszLongName = NULL;
    char *pszNumber;
    char *pszUpperName = NULL;
    char *pszUpperPart = NULL;
    char *pszPart;
    char *pszDir = NULL;
    char *p;
    ULONG ulFileNo;
    ULONG ulNum = 0;
    bool fFound = false;
    bool fEnd = false;
    USHORT usMode;
    USHORT usFileAttr;
    APIRET hrc = 0;
    PSHFLDIRINFO bufpos = NULL, buf = NULL;
    int    rc;
    uint32_t len, num_files, index = 0;

    if (g_fHideLFN)
    {
        return ERROR_FILE_NOT_FOUND;
    }

    memset(pszTarget, 0, cbTarget);
    if (strlen(pszPath) >= 2)
    {
        if (pszPath[1] == ':')
        {
            memcpy(pszTarget, pszPath, 2);
            pszTarget += 2;
            pszPath += 2;
        }
    }

    len = 16384;

    buf = (PSHFLDIRINFO)RTMemAlloc(len);

    if (! buf)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto TRANSLATENAME_EXIT;
    }

    pszDir = (char *)RTMemAlloc((size_t)CCHMAXPATHCOMP + 1);
    if (! pszDir)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto TRANSLATENAME_EXIT;
    }
    memset(pszDir, 0, CCHMAXPATHCOMP + 1);

    pszLongName = (char *)RTMemAlloc((size_t)CCHMAXPATHCOMP * 4 + 4);
    if (! pszLongName)
    {
        hrc = ERROR_NOT_ENOUGH_MEMORY;
        goto TRANSLATENAME_EXIT;
    }
    memset(pszLongName, 0, CCHMAXPATHCOMP * 4 + 4);

    pszPart      = pszLongName + CCHMAXPATHCOMP + 1;
    pszUpperPart = pszPart + CCHMAXPATHCOMP + 1;
    pszUpperName = pszUpperPart + CCHMAXPATHCOMP + 1;

    if (ulTranslate == TRANSLATE_AUTO)
    {
        if (IsDosSession())
            ulTranslate = TRANSLATE_SHORT_TO_LONG;
        else
            ulTranslate = TRANSLATE_LONG_TO_SHORT;
    }

    usMode = MODE_SCAN;
    while (usMode != MODE_RETURN && ! fEnd)
    {
        usMode = MODE_SCAN;

        if (*pszPath == '\\')
            *pszTarget++ = *pszPath++;

        if (!strlen(pszPath))
            break;

        p = strchr(pszPath, '\\');
        if (!p)
            p = pszPath + strlen(pszPath);

        if (p - pszPath > CCHMAXPATHCOMP - 1)
        {
            hrc = ERROR_BUFFER_OVERFLOW;
            goto TRANSLATENAME_EXIT;
        }

        memset(pszPart, 0, CCHMAXPATHCOMP + 1);
        memcpy(pszPart, pszPath, p - pszPath);
        vboxfsStrToUpper(pszPart, CCHMAXPATHCOMP + 1, pszUpperPart);
        pszPath = p;

        pszNumber = strchr(pszUpperPart, '~');
        if (pszNumber)
        {
            ulNum = strtol(pszNumber + 1, NULL, 16);
        }

        RT_ZERO(params);

        pwsz = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);

        if (! pwsz)
        {
            hrc = ERROR_NOT_ENOUGH_MEMORY;
            goto TRANSLATENAME_EXIT;
        }

        vboxfsStrToUtf8(pwsz, pszDir);

        path = make_shflstring(pwsz);

        if (! path)
        {
            hrc = ERROR_NOT_ENOUGH_MEMORY;
            goto TRANSLATENAME_EXIT;
        }

        params.Handle = SHFL_HANDLE_NIL;
        params.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW | SHFL_CF_ACCESS_READ;

        rc = VbglR0SfCreate(&g_clientHandle, map, path, &params);

        if (RT_SUCCESS(rc))
        {
            if (params.Result == SHFL_PATH_NOT_FOUND)
            {
                hrc = ERROR_PATH_NOT_FOUND;
                goto TRANSLATENAME_EXIT;
            }

            if (params.Result == SHFL_FILE_EXISTS)
            {
                if (params.Handle != SHFL_HANDLE_NIL)
                {
                    len = 16384;
                }
            }
            else
            {
                log("VbglR0SfCreate: handle is NULL!\n");
                rc = ERROR_PATH_NOT_FOUND;
                goto TRANSLATENAME_EXIT;
            }
        }
        else
        {
            hrc = vbox_err_to_os2_err(rc);
            goto TRANSLATENAME_EXIT;
        }

        memset(pszLongName, 0, CCHMAXPATHCOMP + 1);
        ulFileNo = 1;
        fFound = FALSE;
        while (usMode == MODE_SCAN && ! fEnd)
        {
            char *str2 = NULL;
            PSHFLSTRING path2 = NULL;
            char *pwsz2 = NULL;

            str2 = (char *)RTMemAlloc(CCHMAXPATHCOMP + 1);

            if (! str2)
            {
                hrc = ERROR_NOT_ENOUGH_MEMORY;
                goto TRANSLATENAME_EXIT;
            }

            pwsz2 = (char *)RTMemAlloc(2 * CCHMAXPATHCOMP + 2);

            if (! pwsz2)
            {
                RTMemFree(str2);
                hrc = ERROR_NOT_ENOUGH_MEMORY;
                goto TRANSLATENAME_EXIT;
            }

            strcpy(str2, pszDir);
            strcat(str2, "\\*");

            vboxfsStrToUtf8(pwsz2, str2);

            path2 = make_shflstring(pwsz2);

            if (! path2)
            {
                RTMemFree(str2);
                RTMemFree(pwsz2);
                hrc = ERROR_NOT_ENOUGH_MEMORY;
                goto TRANSLATENAME_EXIT;
            }
         
            num_files = index = 0;
            bufpos = buf;

            rc = VbglR0SfDirInfo(&g_clientHandle, map, params.Handle,
                                 path2, 0, index, &len, buf, &num_files);
            RTMemFree(path2);
            RTMemFree(pwsz2);
            RTMemFree(str2);

            if (rc && rc != VERR_NO_MORE_FILES)
            {
                log("VbglR0SfDirInfo failed: %d\n", rc);
                hrc = vbox_err_to_os2_err(rc);
                goto TRANSLATENAME_EXIT;
            }

            if (rc == VERR_NO_MORE_FILES)
            {
                hrc = ERROR_NO_MORE_FILES;
                goto TRANSLATENAME_EXIT;
            }

            while (usMode == MODE_SCAN && index < num_files && bufpos < buf + len)
            {
                PSHFLDIRINFO file = bufpos;
                char *pszFileName = (char *)file->name.String.utf8;

#ifdef CALL_YIELD
                Yield();
#endif
                vboxfsStrFromUtf8(pszLongName, (char *)pszFileName, CCHMAXPATHCOMP + 1);

                usFileAttr = VBoxToOS2Attr(file->Info.Attr.fMode);

                vboxfsStrToUpper(pszLongName, CCHMAXPATHCOMP + 1, pszUpperName);

                if (ulTranslate == TRANSLATE_LONG_TO_SHORT) /* OS/2 session, translate to DOS */
                {
                    MakeShortName(ulFileNo, pszUpperName, szShortName);
                    if (
                        ( !stricmp(pszUpperName, pszUpperPart) ||
                          !stricmp(szShortName,  pszUpperPart) ) )
                    {
                        strcat(pszTarget, szShortName);
                        pszTarget += strlen(pszTarget);
                        fFound = TRUE;
                    }
                }
                else /* translate from DOS to OS/2 */
                {
                    if ( (pszNumber && ulNum == ulFileNo) ||
                         (! pszNumber && ! stricmp(pszUpperName, pszUpperPart) ) )
                    {
                        strcat(pszTarget, pszLongName);
                        pszTarget += strlen(pszTarget);
                        fFound = TRUE;
                    }
                }

                if (fFound)
                {
                    if (strlen(pszPath))
                    {
                        if (usFileAttr & FILE_DIRECTORY)
                        {
                            usMode = MODE_START;
                            break;
                        }
                        fEnd = true;
                    }
                    usMode = MODE_RETURN;
                    break;
                }
                memset(pszLongName, 0, CCHMAXPATHCOMP + 1);
                ulFileNo++;
                index++;
                bufpos = (PSHFLDIRINFO)((char *)bufpos + offsetof(SHFLDIRINFO, name.String) + file->name.u16Size);
            }
            if (usMode != MODE_SCAN)
                break;
            if (fEnd)
            {
                strcat(pszTarget, pszPart);
            }
        }

        if (*pszDir)
            strcat(pszDir, "\\");

        strcat(pszDir, pszLongName);

        RTMemFree(pwsz); pwsz = NULL;
        RTMemFree(path); path = NULL;
        rc = VbglR0SfClose(&g_clientHandle, map, params.Handle);
        params.Handle = SHFL_HANDLE_NIL;
        hrc = vbox_err_to_os2_err(rc);
    }

TRANSLATENAME_EXIT:
    if ( pszUpperPart && ( strchr(pszUpperPart, '*') || strchr(pszUpperPart, '?') ) )
    {
        hrc = NO_ERROR;
        fFound = true;
        strcat(pszTarget, pszUpperPart);
    }

    if (fEnd)
    {
        strcat(pszTarget, pszPath);
    }

    if (! fFound)
    {
        hrc = ERROR_FILE_NOT_FOUND;
    }

    if (params.Handle)
        VbglR0SfClose(&g_clientHandle, map, params.Handle);
    if (pszLongName)
        RTMemFree(pszLongName);
    if (buf)
        RTMemFree(buf);
    if (pszDir)
        RTMemFree(pszDir);
    if (pwsz)
        RTMemFree(pwsz);
    if (path)
        RTMemFree(path);

    return hrc;
}

int tolower (int c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return (c + ('a' - 'A'));
    }

    return c;
}

int toupper (int c)
{
    if (c >= 'a' && c <= 'z')
    {
        return (c - ('a' - 'A'));
    }

    return c;
}

char *strlwr( char *s )
{
    char *p;

    for (p = s; *p; p++)
    {
        *p = tolower(*p);
    }

    return s;
}

char *strupr( char *s )
{
    char *p;

    for (p = s; *p; p++)
    {
        *p = toupper(*p);
    }

    return s;
}

const char __Alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";

char *utoa( unsigned value, char *buffer, int radix )
{
    char     *p = buffer;
    char     *q;
    unsigned    rem;
    unsigned    quot;
    char        buf[34];    // only holds ASCII so 'char' is OK

    buf[0] = '\0';
    q = &buf[1];
    do {
        rem = value % radix;
        quot = value / radix;

        *q = __Alphabet[rem];
        ++q;
        value = quot;
    } while( value != 0 );
    while( (*p++ = (char)*--q) )
        ;
    return( buffer );
}

char *itoa( int value, char *buffer, int radix )
{
    char *p = buffer;

    if( radix == 10 ) {
        if( value < 0 ) {
            *p++ = '-';
            value = - value;
        }
    }
    utoa( value, p, radix );
    return( buffer );
}

char *strncpy(char *dst, char *src, size_t len)
{
    char *ret;

    ret = dst;

    for( ;len; --len )
    {
        if( *src == '\0' ) 
            break;

        *dst++ = *src++;
    }

    while( len != 0 )
    {
        *dst++ = '\0';      /* pad destination string with null chars */
        --len;
    }

    return( ret );
}

char *stpcpy(char *dst, const char *src)
{
    strcpy(dst, src);
    return dst + strlen(src);
}

char *strcat(char *dst, const char *app)
{
    strcpy(dst + strlen(dst), app);
    return dst;
}

#define memeq( p1, p2, len )    ( memcmp((p1),(p2),(len)) == 0 )

char *strstr(const char *s1, const char *s2 )
{
    char *end_of_s1;
    int  s1len, s2len;

    if( s2[0] == '\0' )
    {
        return( (char *)s1 );
    }
    else if( s2[1] == '\0' )
    {
        return( strchr( s1, s2[0] ) );
    }

    end_of_s1 = (char *)memchr( s1, '\0', ~0 );
    s2len = strlen( (char *)s2 );

    for(;;)
    {
        s1len = end_of_s1 - s1;

        if( s1len < s2len )
        {
            break;
        }

        s1 = (char *)memchr( s1, *s2, s1len );  /* find start of possible match */

        if( s1 == 0 )
        {
            break;
        }

        if( memeq( (void *)s1, (void *)s2, s2len ) )
        {
            return( (char *)s1 );
        }

        ++s1;
    }

    return( 0 );
}

int stricmp( const char *s, const char *t )
{
    for( ; tolower(*s) == tolower(*t); s++, t++ )
    {
        if( *s == '\0' )
            return( 0 );
    }

    return( tolower(*s) - tolower(*t) );
}

char *strrchr(const char *cp, int ch)
{
    char *save;
    char c;

    for (save = (char *) 0; (c = *cp); cp++)
    {
	    if (c == ch)
	    {
	        save = (char *) cp;
	    }
    }

    return save;
}

int isspace( int c )
{
  switch (c)
    {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
      return 1;
    default:
      break;
    }

  return 0;
}

int isdigit( int c )
{
   return (c <= 0x39 && c >= 0x30) ? 1 : 0;
}

int isalpha(int c)
{
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

char isupper (unsigned char c)
{
    if ( c >= 'A' && c <= 'Z' )
        return 1;
    return 0;
}
