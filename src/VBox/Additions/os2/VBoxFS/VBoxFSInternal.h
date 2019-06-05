/** $Id$ */
/** @file
 * VBoxFS - OS/2 Shared Folder IFS, Internal Header.
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

#ifndef ___VBoxFSInternal_h___
#define ___VBoxFSInternal_h___


#define INCL_DOSINFOSEG
#define INCL_BASE
#define INCL_ERROR
#include <os2.h>
#include <os2ddk/bsekee.h>
#include <os2ddk/devhlp.h>
#include <os2ddk/unikern.h>
#include <os2ddk/fsd.h>
#undef RT_MAX

#include <VBox/VBoxGuestLibSharedFolders.h>

#include <iprt/types.h>
#include <iprt/assert.h>

#define MIN_EA_SIZE 128
#define MAX_EA_SIZE 65536UL

#define FIL_QUERYALLEAS        4   /* Level 4, return all EA's         */
#define FIL_QUERYFULLNAME      5   /* Level 5, return fully qualified  */
                                   /*   name of file                   */
#define FIL_NAMEISVALID        6   /* Level 6, check validity of       */

#define MIN(a, b) ((a < b) ? a : b)

#define ERROR_VOLUME_NOT_MOUNTED 0xEE00

#define INFO_RETRIEVE		0x00

void log(const char *fmt, ...);

#pragma pack(2)
typedef struct _FILEFNDBUF                 /* findbuf */
{
    FDATE   fdateCreation;
    FTIME   ftimeCreation;
    FDATE   fdateLastAccess;
    FTIME   ftimeLastAccess;
    FDATE   fdateLastWrite;
    FTIME   ftimeLastWrite;
    ULONG   cbFile;
    ULONG   cbFileAlloc;
    USHORT  attrFile;
    UCHAR   cchName;
    CHAR    achName[1]; 
    //CHAR    achName[CCHMAXPATHCOMP];
} FILEFNDBUF;
typedef FILEFNDBUF *PFILEFNDBUF;

typedef struct _FILEFNDBUF3 {   /* findbuf3 */
    FDATE   fdateCreation;
    FTIME   ftimeCreation;
    FDATE   fdateLastAccess;
    FTIME   ftimeLastAccess;
    FDATE   fdateLastWrite;
    FTIME   ftimeLastWrite;
    ULONG   cbFile;
    ULONG   cbFileAlloc;
    USHORT  attrFile;
    UCHAR   cchName;
    CHAR    achName[1]; 
    //CHAR    achName[CCHMAXPATHCOMP]; /* initial room for zero terminator */
} FILEFNDBUF3;
typedef FILEFNDBUF3 *PFILEFNDBUF3;

typedef struct _FILEFNDBUF3L     /* findbuf */
{
    FDATE  fdateCreation;
    FTIME  ftimeCreation;
    FDATE  fdateLastAccess;
    FTIME  ftimeLastAccess;
    FDATE  fdateLastWrite;
    FTIME  ftimeLastWrite;
    LONGLONG  cbFile;
    LONGLONG  cbFileAlloc;
    ULONG  attrFile;
    UCHAR  cchName;
    CHAR   achName[1]; 
    //CHAR   achName[CCHMAXPATHCOMP];
} FILEFNDBUF3L;
typedef FILEFNDBUF3L *PFILEFNDBUF3L;

typedef struct _FILEFNDBUF2    /* findbuf2 */
{
    FDATE  fdateCreation;
    FTIME  ftimeCreation;
    FDATE  fdateLastAccess;
    FTIME  ftimeLastAccess;
    FDATE  fdateLastWrite;
    FTIME  ftimeLastWrite;
    ULONG  cbFile;
    ULONG  cbFileAlloc;
    USHORT attrFile;
    ULONG  cbList;
    UCHAR  cchName;
    CHAR   achName[1]; 
    //CHAR   achName[CCHMAXPATHCOMP];
} FILEFNDBUF2;
typedef FILEFNDBUF2 *PFILEFNDBUF2;

typedef struct _FILEFNDBUF4L                /* findbuf4l */
{
    FDATE    fdateCreation;
    FTIME    ftimeCreation;
    FDATE    fdateLastAccess;
    FTIME    ftimeLastAccess;
    FDATE    fdateLastWrite;
    FTIME    ftimeLastWrite;
    LONGLONG cbFile;
    LONGLONG cbFileAlloc;
    ULONG    attrFile;                    /* widened field */
    ULONG    cbList;
    UCHAR    cchName;
    CHAR    achName[1]; 
    //CHAR     achName[CCHMAXPATHCOMP];
} FILEFNDBUF4L;
typedef FILEFNDBUF4L  *PFILEFNDBUF4L;

typedef struct _ProcInfo
{
    USHORT usPid;
    USHORT usUid;
    USHORT usPdb;
} PROCINFO, *PPROCINFO;

#pragma pack()

typedef struct _CWD
{
    SHFLHANDLE handle;
    VBGLSFMAP map;
} CWD, *PCWD;

typedef struct _FILEBUF
{
    SHFLHANDLE handle;
    PSHFLSTRING path;
    VBGLSFMAP map;
    bool tmp;
} FILEBUF, *PFILEBUF;

/**
 * VBoxFS Volume Parameter Structure.
 *
 * @remark  Overlays the 36 byte VPFSD structure (fsd.h).
 */
typedef struct VBOXSFVP
{
    uint32_t u32Dummy;
    VBGLSFMAP  map;
    char *pszShareName;
    char szLabel[12];
} VBOXSFVP;
AssertCompile(sizeof(VBOXSFVP) <= sizeof(VPFSD));
/** Pointer to a VBOXSFVP struct. */
typedef VBOXSFVP *PVBOXSFVP;


/**
 * VBoxFS Current Directory Structure.
 *
 * @remark  Overlays the 8 byte CDFSD structure (fsd.h).
 */
typedef struct VBOXSFCD
{
    uint32_t u32Dummy;
    PCWD cwd;
} VBOXSFCD;
AssertCompile(sizeof(VBOXSFCD) <= sizeof(CDFSD));
/** Pointer to a VBOXSFCD struct. */
typedef VBOXSFCD *PVBOXSFCD;


/**
 * VBoxFS System File Structure.
 *
 * @remark  Overlays the 30 byte SFFSD structure (fsd.h).
 */
typedef struct VBOXSFFSD
{
    /** Self pointer for quick 16:16 to flat translation. */
    struct VBOXSFFSD *pSelf;
    PFILEBUF filebuf;
} VBOXSFFSD;
AssertCompile(sizeof(VBOXSFFSD) <= sizeof(SFFSD));
/** Pointer to a VBOXSFFSD struct. */
typedef VBOXSFFSD *PVBOXSFFSD;

typedef struct _FINDBUF
{
    SHFLHANDLE handle;
    PSHFLDIRINFO buf, bufpos;
    PSHFLSTRING path;
    uint32_t num_files;
    uint32_t index;
    uint32_t len;
    bool has_more_files;
    char *pDir;
    uint32_t bAttr;
    uint32_t bMustAttr;
    VBGLSFMAP  map;
    bool tmp;
} FINDBUF, *PFINDBUF;

/**
 * VBoxFS File Search Structure.
 *
 * @remark  Overlays the 24 byte FSFSD structure (fsd.h).
 */
typedef struct VBOXFSFSD
{
    /** Self pointer for quick 16:16 to flat translation. */
    struct VBOXSFFS *pSelf;
    PFINDBUF pFindBuf;
} VBOXFSFSD;
AssertCompile(sizeof(VBOXFSFSD) <= sizeof(FSFSD));
/** Pointer to a VBOXSFFS struct. */
typedef VBOXFSFSD *PVBOXFSFSD;

PSHFLSTRING make_shflstring(const char* const s);
void free_shflstring(PSHFLSTRING s);
PSHFLSTRING clone_shflstring(PSHFLSTRING s);
PSHFLSTRING concat_shflstring_cstr(PSHFLSTRING s1, const char* const s2);
PSHFLSTRING concat_cstr_shflstring(const char* const s1, PSHFLSTRING s2);
PSHFLSTRING build_path(PSHFLSTRING dir, const char* const name);
APIRET APIENTRY vboxfsStrFromUtf8(char *dst, char *src, ULONG len, ULONG srclen);
APIRET APIENTRY vboxfsStrToUtf8(char *dst, char *src);
APIRET APIENTRY parseFileName(const char *pszPath, PCDFSI pcdfsi,
                              char *pszParsedPath, int *cbParsedPath,
                              VBGLSFMAP *map, bool *tmp);

APIRET APIENTRY doScanEnv(PCSZ  pszName,
                          PSZ  *ppszValue);
RT_C_DECLS_BEGIN

/* IFS Helpers */
APIRET APIENTRY FSH32_GETVOLPARM(USHORT hVPB, PVPFSI *pvpfsi, PVPFSD *pvpfsd);
APIRET APIENTRY FSH32_QSYSINFO(USHORT index, char *pData, USHORT cbData);
APIRET APIENTRY FSH32_PROBEBUF(ULONG operation, char *pData, ULONG cbData);
APIRET APIENTRY FSH32_WILDMATCH(char *pPat, char *pStr);

RT_C_DECLS_END

APIRET GetProcInfo(PPROCINFO pProcInfo, USHORT usSize);
bool IsDosSession(void);

uint32_t VBoxToOS2Attr(uint32_t fMode);
uint32_t OS2ToVBoxAttr(uint32_t attr);

USHORT vbox_err_to_os2_err(int rc);

APIRET GetEmptyEAS(PEAOP pEAOP);

int tolower (int c);
char *strrchr(const char *cp, int ch);
char *strncpy(char *dst, char *src, size_t len);
int stricmp(const char *s, const char *t);
char *strstr(const char *s1, const char *s2);
char *strcat(char *dst, const char *app);
char *stpcpy(char *dst, const char *src);


#endif
