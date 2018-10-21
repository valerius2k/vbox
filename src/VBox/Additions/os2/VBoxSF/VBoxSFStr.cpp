/** $Id: VBoxSF.cpp 14 2015-11-29 02:15:22Z dmik $ */
/** @file
 * VBoxSF - OS/2 Shared Folders, the FS and FSD level IFS EPs
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
#include "VBoxSFInternal.h"

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/mem.h>

#include <string.h>

UconvObj uconv = {0};

PSHFLSTRING make_shflstring(const char* const s)
{
    int len = strlen(s);
    if (len > 0xFFFE)
    {
        log(("vboxsf: make_shflstring: string too long\n"));
        return NULL;
    }

    PSHFLSTRING rv = (PSHFLSTRING)RTMemAllocZ(sizeof(SHFLSTRING) + len);
    if (!rv)
        return NULL;

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
        memcpy(rv, s, sizeof(SHFLSTRING) + s->u16Length);
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

APIRET APIENTRY vboxsfStrFromUtf8(char *dst, char *src,
                                  ULONG len, ULONG srclen)
{
    APIRET  rc = NO_ERROR;
    ULONG   newlen;
    //UniChar buf_ucs2[CCHMAXPATHCOMP];
    PRTUTF16 pwsz;
    static  char initted = 0;

    if (! initted)
    {
        KernCreateUconvObject(1208, &uconv);
        initted = 1;
    }

    //memset(buf_ucs2, 0, sizeof(buf_ucs2));

    //rc = KernStrToUcs(&uconv,
    //                  buf_ucs2,
    //                  src,
    //                  2 * CCHMAXPATHCOMP,
    //                  srclen);

    //if (rc)
    //    log("KernStrToUcs returned %lu\n", rc);
    RTStrToUtf16(src, &pwsz);

    rc = KernStrFromUcs(0,
                        dst,
                        pwsz,
                        CCHMAXPATHCOMP,
                        RTStrUniLen(src));

    if (rc)
        log("KernStrFromUcs returned %lu\n", rc);

    RTMemFree(pwsz);
    return rc;
}

APIRET APIENTRY vboxsfStrToUtf8(char *dst, char *src)
{
    APIRET  rc = NO_ERROR;
    ULONG   srclen;
    ULONG   newlen;
    UniChar buf_ucs2[CCHMAXPATHCOMP];
    static  char initted = 0;

    if (! initted)
    {
        KernCreateUconvObject(1208, &uconv);
        initted = 1;
    }

    srclen = strlen(src);

    rc = KernStrToUcs(0,
                      buf_ucs2,
                      src,
                      2 * CCHMAXPATHCOMP,
                      srclen);

    if (rc)
        log("KernStrToUcs returned %lu\n", rc);

    rc = KernStrFromUcs(&uconv,
                        dst,
                        buf_ucs2,
                        CCHMAXPATHCOMP,
                        2 * CCHMAXPATHCOMP);

    if (rc)
        log("KernStrFromUcs returned %lu\n", rc);

    return rc;
}

int tolower (int c)
{
    if (c >= 'A' && c <= 'Z')
        return (c + ('a' - 'A'));

    return c;
}

char *strncpy(char *dst, char *src, int len)
{
    char *ret;

    ret = dst;
    for( ;len; --len ) {
        if( *src == '\0' ) 
            break;
        *dst++ = *src++;
    }
    while( len != 0 ) {
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

    if( s2[0] == '\0' ) {
        return( (char *)s1 );
    } else if( s2[1] == '\0' ) {
        return( strchr( s1, s2[0] ) );
    }
    end_of_s1 = (char *)memchr( s1, '\0', ~0 );
    s2len = strlen( (char *)s2 );
    for( ;; ) {
        s1len = end_of_s1 - s1;
        if( s1len < s2len )
            break;
        s1 = (char *)memchr( s1, *s2, s1len );  /* find start of possible match */

        if( s1 == 0 )
            break;
        if( memeq( (void *)s1, (void *)s2, s2len ) )
            return( (char *)s1 );
        ++s1;
    }
    return( 0 );
}

int stricmp( const char *s, const char *t )
{
    for( ; tolower(*s) == tolower(*t); s++, t++ )
        if( *s == '\0' )
            return( 0 );
    return( tolower(*s) - tolower(*t) );
}
