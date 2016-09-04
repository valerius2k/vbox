/* $Id$ */
/** @file
 * IPRT - File I/O, POSIX, Part 1.
 */

/*
 * Copyright (C) 2006-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_FILE

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#ifdef _MSC_VER
# include <io.h>
# include <stdio.h>
#else
# include <unistd.h>
# include <sys/time.h>
#endif
#ifdef RT_OS_LINUX
# include <sys/file.h>
#endif

#if defined(RT_OS_OS2)
# define  OS2EMX_PLAIN_CHAR
# define  INCL_BASE
# define  INCL_DOSDEVIOCTL
# include <os2.h>
# include <stdio.h>
# define  IOCTL_CDROMDISK2         0x82
# define  CDROMDISK_READDATA       0x76
# define  CDROMDISK_WRITEDATA      0x56
# define  CDROMDISK2_FEATURES      0x63
# define  CDROMDISK2_DRIVELETTERS  0x60
# define  FEATURE_EXECMD_SUPPORT   0x00000004L
# include <ctype.h> // for tolower
# if (!defined(__INNOTEK_LIBC__) || __INNOTEK_LIBC__ < 0x006)
#  include <io.h>
# endif
#endif
#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD)
# include <sys/disk.h>
#endif
#ifdef RT_OS_SOLARIS
# include <stropts.h>
# include <sys/dkio.h>
# include <sys/vtoc.h>
#endif /* RT_OS_SOLARIS */

#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include "internal/file.h"
#include "internal/fs.h"
#include "internal/path.h"



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Default file permissions for newly created files. */
#if defined(S_IRUSR) && defined(S_IWUSR)
# define RT_FILE_PERMISSION  (S_IRUSR | S_IWUSR)
#else
# define RT_FILE_PERMISSION  (00600)
#endif


RTDECL(bool) RTFileExists(const char *pszPath)
{
    bool fRc = false;
    char const *pszNativePath;
    int rc = rtPathToNative(&pszNativePath, pszPath, NULL);
    if (RT_SUCCESS(rc))
    {
        struct stat s;
        fRc = !stat(pszNativePath, &s)
            && S_ISREG(s.st_mode);

        rtPathFreeNative(pszNativePath, pszPath);
    }

    LogFlow(("RTFileExists(%p={%s}): returns %RTbool\n", pszPath, pszPath, fRc));
    return fRc;
}

#ifdef RT_OS_OS2
// check if the driveletter is cdrom one
static bool rtOs2IsCdRom(const char *pszFilename)
{
    typedef struct _CDROMDeviceMap
    {
        USHORT usDriveCount;
        USHORT usFirstLetter;
    } CDROMDeviceMap;

    HFILE hf;
    ULONG ulAction = 0;
    CDROMDeviceMap CDMap;
    ULONG ulParamSize = sizeof(ulAction);
    ULONG ulDataSize = sizeof(CDROMDeviceMap);
    char driveName[3] = { '?', ':', '\0' };

    if (DosOpen("\\DEV\\CD-ROM2$", &hf, &ulAction, 0, FILE_NORMAL,
                OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
                OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, NULL))
        return false;

    DosDevIOCtl(hf, IOCTL_CDROMDISK2, CDROMDISK2_DRIVELETTERS,
                NULL, 0, &ulParamSize,
                (PVOID)&CDMap, sizeof(CDROMDeviceMap), &ulDataSize);
    DosClose(hf);

    for (int drv = CDMap.usFirstLetter; drv < CDMap.usFirstLetter + CDMap.usDriveCount; drv++)
    {
        driveName[0] = 'A' + drv;
        if (!stricmp(driveName, pszFilename))
            return true;
    }

    return false;
}

static int rtOs2ExtraFileOpen(PRTFILE pFile, const char *pszFilename, uint64_t fOpen, int fOpenMode, int fMode)
{
    int    fh;
    APIRET rc;
    ULONG  dummy;

    errno = 0;
    (*pFile)->rawfs = 0;

    // in case someone appended the default
    // path to a drive letter, truncate path
    // (otherwise we'll get n:\dir\w: or n:/dir/w:)
    // and it will trap trying to open it
    int len = strlen(pszFilename);

    // access via the raw filesystem
    if ( !strncmp(pszFilename, "\\\\.\\", 4) )
        (*pFile)->rawfs = 1;
    // handle d:\path\to\w: or d:\path\to/w: case (truncate path)
    else if (len >= 3 &&
        pszFilename[len - 1] == ':' &&
        (pszFilename[len - 3] == '\\' || pszFilename[len - 3] == '/'))
        pszFilename += len - 2;

     // a special case of opening the drive letter
     // in OPEN_FLAGS_DASD mode (logical disk or CDROM or floppy)
     // otherwise, opening "w:" causes VERR_IS_A_DIRECTORY error
    if ( (*pFile)->rawfs || 
         (tolower(pszFilename[0]) >= 'a' &&
         tolower(pszFilename[0]) <= 'z' &&
         pszFilename[1] == ':' &&
         pszFilename[2] == '\0') )
    {
        // logical disk
        ULONG  ulAction;
        ULONG  sig;
        ULONG  parmlen = sizeof(sig);
        ULONG  datalen = sizeof(sig);
        ULONG  fsOpenMode = OPEN_SHARE_DENYNONE | OPEN_FLAGS_NOINHERIT | OPEN_FLAGS_FAIL_ON_ERROR;

        if (! (*pFile)->rawfs)
            fsOpenMode |= OPEN_FLAGS_DASD;

        switch (fOpen & RTFILE_O_ACCESS_MASK)
        {
            case RTFILE_O_READ:
                fsOpenMode |= OPEN_ACCESS_READONLY;
                break;
            case RTFILE_O_WRITE:
                fsOpenMode |= OPEN_ACCESS_WRITEONLY;
                break;
            case RTFILE_O_READWRITE:
                fsOpenMode |= OPEN_ACCESS_READWRITE;
                break;
            default:
                AssertMsgFailed(("RTFileOpen received an invalid RW value, fOpen=%#llx\n", fOpen));
                return VERR_INVALID_PARAMETER;
        }

        rc = DosOpenL(pszFilename, (PHFILE)&fh,
                      &ulAction, 0, FILE_NORMAL,
                      OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW,
                      fsOpenMode, NULL);

        if (rc == 0)
        {
            if (! fh)
                return VERR_MEDIA_NOT_PRESENT;

            if (rtOs2IsCdRom(pszFilename))
            {
                // identify CDROM driver
                sig = ('C') | ('D' << 8) | ('9' << 16) | ('9' << 24);

                if ( (rc = DosDevIOCtl(fh, IOCTL_CDROMDISK, CDROMDISK_GETDRIVER,
                                       &sig, sizeof(sig), NULL,
                                       &sig, sizeof(sig), NULL)) ||
                      sig != (('C') | ('D' << 8) | ('0' << 16) | ('1' << 24)) )
                    return VERR_MEDIA_NOT_PRESENT;

                RTFileFromNative(pFile, fh);
                (*pFile)->type = FILE_TYPE_CD;

                // lock drive
                DosDevIOCtl(fh, IOCTL_DISK, DSK_LOCKDRIVE,
                            &dummy, sizeof(dummy), NULL,
                            &dummy, sizeof(dummy), NULL);

                return VINF_SUCCESS;
            }

            RTFileFromNative(pFile, fh);

            if (! strncmp(pszFilename, "\\\\.\\Physical_Disk", 17))
            {
                (*pFile)->rawfs = 1;

                // lock drive
                DosDevIOCtl(fh, IOCTL_PHYSICALDISK, PDSK_LOCKPHYSDRIVE,
                            &dummy, sizeof(dummy), NULL,
                            &dummy, sizeof(dummy), NULL);
            }
            else
            {
                (*pFile)->type = FILE_TYPE_DASD;

                // lock drive
                DosDevIOCtl(fh, IOCTL_DISK, DSK_LOCKDRIVE,
                            &dummy, sizeof(dummy), NULL,
                            &dummy, sizeof(dummy), NULL);
            }

            return VINF_SUCCESS;
        }

        return RTErrConvertFromOS2(rc);
    }
    else if (pszFilename[0] <= '9' &&
             pszFilename[0] >= '0' &&
             pszFilename[1] == ':' &&
             pszFilename[2] == '\0')
    {
        // physical disk number
        rc = DosPhysicalDisk(INFO_GETIOCTLHANDLE,
                             (PHFILE)&fh,
                             2L,
                             (PSZ)pszFilename,
                             strlen((PSZ)pszFilename) + 1);

        if (! rc)
        {
            if (! fh)
                return VERR_MEDIA_NOT_PRESENT;

            RTFileFromNative(pFile, fh);
            (*pFile)->type = FILE_TYPE_RAW;

            // lock drive
            DosDevIOCtl(fh, IOCTL_PHYSICALDISK, PDSK_LOCKPHYSDRIVE,
                        &dummy, sizeof(dummy), NULL,
                        &dummy, sizeof(dummy), NULL);

            return VINF_SUCCESS;
        }

        return RTErrConvertFromOS2(rc);
    }
    else
    {
        fh = open(pszFilename, fOpenMode, fMode);
        RTFileFromNative(pFile, fh);
        return -1;
    }

    return VINF_SUCCESS;
}

static int rtOs2ExtraFileSeek(RTFILE hFile, int64_t offSeek, unsigned uMethod, off_t *poffActual)
{
    static const unsigned aSeekRecode[] =
    {
        SEEK_SET,
        SEEK_CUR,
        SEEK_END,
    };

    #pragma pack(1)

    /* parameter packet */
    struct {
        UCHAR command;
        UCHAR drive;
    } parm;

    struct {
        UCHAR command;
    } parmphys;

    #pragma pack()

    /* data packet      */
    BIOSPARAMETERBLOCK bpb;
    DEVICEPARAMETERBLOCK dpb;
    ULONG parm2 = ('C') | ('D' << 8) | ('0' << 16) | ('1' << 24);
    ULONG parmlen2 = sizeof(parm2);
    ULONG data2 = 0;
    ULONG datalen2 = sizeof(data2);
    int rc = NO_ERROR;

    LONGLONG cb;
    ULONG cbSectors;
    ULONG parmlen;
    ULONG datalen;

    if (hFile->type == FILE_TYPE_NORMAL)
    {
        rc = lseek(RTFileToNative(hFile), (off_t)offSeek, aSeekRecode[uMethod]);

        if (rc < 0)
            rc = RTErrConvertFromErrno(errno);
        else
        {
            hFile->off = rc;
            rc = 0;
        }
    }
    else
    {
        switch (uMethod)
        {
            case RTFILE_SEEK_BEGIN:
                hFile->off = offSeek;
                break;

            case RTFILE_SEEK_CURRENT:
                hFile->off += offSeek;
                break;

            case RTFILE_SEEK_END:
                switch (hFile->type)
                {
                    case FILE_TYPE_CD:
                        if ( (rc = DosDevIOCtl(RTFileToNative(hFile),
                                               IOCTL_CDROMDISK, CDROMDISK_DEVICESTATUS,
                                               &parm2, parmlen2, &parmlen2,
                                               &data2, datalen2, &datalen2)) )
                            break;

                        if (data2 & (1 << 11))
                        {
                            // disk does not present
                            rc = ERROR_UNCERTAIN_MEDIA;
                            break;
                        }

                        parm2 = ('C') | ('D' << 8) | ('0' << 16) | ('1' << 24);

                        if ( (rc = DosDevIOCtl(RTFileToNative(hFile),
                                               IOCTL_CDROMDISK, CDROMDISK_GETVOLUMESIZE,
                                               &parm2, parmlen2, &parmlen2,
                                               &data2, datalen2, &datalen2)) )
                            break;

                        // return disk size in bytes
                        hFile->off = data2 << 11; // multiply by sector size (2048)
                        rc = NO_ERROR;
                        break;

                    case FILE_TYPE_DASD:
                        parmlen = sizeof(parm);
                        datalen = sizeof(bpb);

                        memset(&bpb, 0, sizeof(bpb));

                        parm.command = 1; // Return the BPB for the media currently in the drive
                        parm.drive = 0;   // unused

                        if ( (rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_DISK, DSK_GETDEVICEPARAMS,
                                               &parm, parmlen, &parmlen,
                                               &bpb, datalen, &datalen)) )
                            break;

                        hFile->off = bpb.cCylinders
                                   * bpb.cHeads
                                   * bpb.usSectorsPerTrack;
                        hFile->off <<= 9; // multiply by sector size (512)
                        break;

                    case FILE_TYPE_RAW:
                        parmlen = sizeof(parmphys);
                        datalen = sizeof(dpb);

                        memset(&dpb, 0, sizeof(dpb));

                        parmphys.command = 1;

                        if ( (rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_PHYSICALDISK, PDSK_GETPHYSDEVICEPARAMS,
                                               &parmphys, parmlen, &parmlen,
                                               &dpb, datalen, &datalen)) )
                            break;

                        hFile->off = dpb.cCylinders
                                   * dpb.cHeads
                                   * dpb.cSectorsPerTrack;
                        hFile->off <<= 9; // multiply by sector size (512)
                        break;

                    default:
                        AssertMsgFailed(("Incorrect handle type!\n"));
                }
                hFile->off -= offSeek;
                break;

            default:
                AssertMsgFailed(("Incorrect seek type!\n"));
        }
    }
    *poffActual = hFile->off;
    rc = RTErrConvertFromOS2(rc);
    return rc;
}

static int rtOs2ExtraFileRead(RTFILE hFile, void *pvBuf, size_t cbToRead, ssize_t *pcbRead)
{
    #pragma pack(1)

    // parameter packet
    struct ReadData_param {
        ULONG       ID_code;                // 'CD01'
        UCHAR       address_mode;           // Addressing format of start_sector:
                                            //  00 - Logical Block format
                                            //  01 - Minutes/Seconds/Frame format
        USHORT      transfer_count;         // Numbers of sectors to read.
                                            //  Must  be non zero
        ULONG       start_sector;           // Starting sector number of the read operation
        UCHAR       reserved;               // Reserved. Must be 0
        UCHAR       interleave_size;        // Not used. Must be 0
        UCHAR       interleave_skip_factor; // Not used. Must be 0
    } parm;
    ULONG parmlen = sizeof(parm);

    #define SECTORSIZE      2048

    struct ReadData_data {
        UCHAR        data_area[SECTORSIZE];
    } data;

    ULONG datalen = sizeof(data);
    uint32_t cbRead32;

    /* parameter packet */
    struct PARM {
        unsigned char command;
        unsigned char drive;
    } parm2;

    #pragma pack()

    /* data packet      */
    BIOSPARAMETERBLOCK bpb;
    ULONG parmlen2 = sizeof(parm2);
    ULONG datalen2 = sizeof(bpb);
    int32_t rc = 0;

    parm2.command = 1;
    parm2.drive = 0;

    struct {
        unsigned char command;
    } parmphys;
    DEVICEPARAMETERBLOCK dpb;
    ULONG parmphyslen = sizeof(parmphys);
    ULONG dataphyslen = sizeof(dpb);

    parmphys.command = 0;

    if (hFile->type == FILE_TYPE_DASD || hFile->type == FILE_TYPE_CD)
    {
        if ( (rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_DISK, DSK_GETDEVICEPARAMS,
                               &parm2, parmlen2, &parmlen2, &bpb, datalen2, &datalen2)) )
            return RTErrConvertFromOS2(rc);
    }
    else if (hFile->type == FILE_TYPE_RAW)
    {
        if ( (rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_PHYSICALDISK, PDSK_GETPHYSDEVICEPARAMS,
                               &parmphys, parmphyslen, &parmphyslen, &dpb, dataphyslen, &dataphyslen)) )
            return RTErrConvertFromOS2(rc);
    }

    #define BUFSIZE 0x4000
    #define SECSIZE 512

    ULONG parmlen4;

    //if (hFile->type == FILE_TYPE_DASD || hFile->type == FILE_TYPE_CD)
    //    parmlen4 = sizeof(TRACKLAYOUT) + sizeof(USHORT) * 2 * (bpb.usSectorsPerTrack - 1);
    //else if (hFile->type == FILE_TYPE_RAW)
    //    parmlen4 = sizeof(TRACKLAYOUT) + sizeof(USHORT) * 2 * (dpb.cSectorsPerTrack - 1);

    char trkbuf[sizeof(TRACKLAYOUT) + sizeof(USHORT) * 2 * 255];
    PTRACKLAYOUT ptrk = (PTRACKLAYOUT)trkbuf;
    ULONG datalen4 = BUFSIZE;
    USHORT cyl, head, sec, n;
    ssize_t cbRead = 0;

    if (hFile->rawfs)
    {
        rc = DosRead(RTFileToNative(hFile), pvBuf, cbToRead, (PULONG)&cbRead);
        rc = RTErrConvertFromOS2(rc);
    }
    else
    {
        switch (hFile->type)
        {
            case FILE_TYPE_NORMAL:
                cbRead = read(RTFileToNative(hFile), pvBuf, cbToRead);

                if (cbRead < 0)
                    rc = RTErrConvertFromErrno(errno);
                else
                    rc = VINF_SUCCESS;
                break;

            case FILE_TYPE_CD:
                AssertReturn(!(hFile->off % SECTORSIZE), VERR_INVALID_PARAMETER);
                cbRead32 = (cbToRead > SECTORSIZE) ? SECTORSIZE : (uint32_t)cbToRead;
                AssertReturn(!(cbToRead % SECTORSIZE), VERR_INVALID_PARAMETER);

                memset(&parm, 0, sizeof(parm));
                memset(&data, 0, sizeof(data));

                parm.ID_code = ('C') | ('D' << 8) | ('0' << 16) | ('1' << 24);
                parm.address_mode = 0; // lba

                do
                {
                    parm.transfer_count = 1;
                    parm.start_sector = (ULONG)(hFile->off / SECTORSIZE);

                    rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_CDROMDISK, CDROMDISK_READDATA,
                                     &parm, parmlen, &parmlen, &data, datalen, &datalen);

                    rc = RTErrConvertFromOS2(rc);

                    if (RT_SUCCESS(rc))
                        memcpy((char *)pvBuf, &data, cbRead32);
                    else
                        break;

                    hFile->off    += cbRead32;
                    cbRead        += cbRead32;
                    cbToRead      -= cbRead32;
                    pvBuf         = (uint8_t *)pvBuf + cbRead32;

                } while ((cbToRead > 0) && RT_SUCCESS(rc));
                break;

            case FILE_TYPE_DASD:
                memset((char *)trkbuf, 0, sizeof(trkbuf));

                for (int i = 0; i < bpb.usSectorsPerTrack; i++)
                {
                    ptrk->TrackTable[i].usSectorNumber = i + 1;
                    ptrk->TrackTable[i].usSectorSize = SECSIZE;
                }

                do
                {
                    cbRead32 = (cbToRead > BUFSIZE) ? BUFSIZE : (uint32_t)cbToRead;
                    parmlen4 = sizeof(TRACKLAYOUT) + sizeof(USHORT) * 2 * (bpb.usSectorsPerTrack - 1);
                    datalen4 = BUFSIZE;

                    uint64_t off = hFile->off;
                    off += (uint64_t)bpb.cHiddenSectors * SECSIZE;

                    cyl = off / (SECSIZE * bpb.cHeads * bpb.usSectorsPerTrack);
                    head = (off / SECSIZE - bpb.cHeads * bpb.usSectorsPerTrack * cyl) / bpb.usSectorsPerTrack;
                    sec = off / SECSIZE - bpb.cHeads * bpb.usSectorsPerTrack * cyl - head * bpb.usSectorsPerTrack;

                    if (sec + cbRead32 / SECSIZE > bpb.usSectorsPerTrack)
                        cbRead32 = (bpb.usSectorsPerTrack - sec) * SECSIZE;

                    n = cbRead32 / SECSIZE;

                    ptrk->bCommand = 1;
                    ptrk->usHead = head;
                    ptrk->usCylinder = cyl;
                    ptrk->usFirstSector = sec;
                    ptrk->cSectors = n;

                    rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_DISK, DSK_READTRACK,
                                     trkbuf, parmlen4, &parmlen4, pvBuf, datalen4, &datalen4);

                    rc = RTErrConvertFromOS2(rc);

                    if (RT_FAILURE(rc))
                        break;

                    hFile->off    += cbRead32;
                    cbToRead      -= cbRead32;
                    cbRead        += cbRead32;
                    pvBuf         = (uint8_t *)pvBuf + cbRead32;

                } while ((cbToRead > 0) && RT_SUCCESS(rc));
                break;

            case FILE_TYPE_RAW:
                memset((char *)trkbuf, 0, sizeof(trkbuf));

                for (int i = 0; i < dpb.cSectorsPerTrack; i++)
                {
                    ptrk->TrackTable[i].usSectorNumber = i + 1;
                    ptrk->TrackTable[i].usSectorSize = SECSIZE;
                }

                do
                {
                    cbRead32 = (cbToRead > BUFSIZE) ? BUFSIZE : (uint32_t)cbToRead;
                    parmlen4 = sizeof(TRACKLAYOUT) + sizeof(USHORT) * 2 * (dpb.cSectorsPerTrack - 1);
                    datalen4 = BUFSIZE;

                    cyl = hFile->off / (SECSIZE * dpb.cHeads * dpb.cSectorsPerTrack);
                    head = (hFile->off / SECSIZE - dpb.cHeads * dpb.cSectorsPerTrack * cyl) / dpb.cSectorsPerTrack;
                    sec = hFile->off / SECSIZE - dpb.cHeads * dpb.cSectorsPerTrack * cyl - head * dpb.cSectorsPerTrack;

                    if (sec + cbRead32 / SECSIZE > dpb.cSectorsPerTrack)
                        cbRead32 = (dpb.cSectorsPerTrack - sec) * SECSIZE;

                    n = cbRead32 / SECSIZE;

                    ptrk->bCommand = 1;
                    ptrk->usHead = head;
                    ptrk->usCylinder = cyl;
                    ptrk->usFirstSector = sec;
                    ptrk->cSectors = n;

                    rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_PHYSICALDISK, PDSK_READPHYSTRACK,
                                     trkbuf, parmlen4, &parmlen4, (void *)pvBuf, datalen4, &datalen4);

                    rc = RTErrConvertFromOS2(rc);

                    if (RT_FAILURE(rc))
                        break;

                    hFile->off += cbRead32;
                    cbRead     += cbRead32;
                    cbToRead   -= cbRead32;
                    pvBuf      =  (uint8_t *)pvBuf + cbRead32;

                } while ((cbToRead > 0) && RT_SUCCESS(rc));
                break;

            default:
                rc = VERR_NOT_SUPPORTED;
        }
    }
    *pcbRead = cbRead;
    return rc;
}

static int rtOs2ExtraFileWrite(RTFILE hFile, const void *pvBuf, size_t cbToWrite, ssize_t *pcbWritten)
{
    #pragma pack(1)

    // parameter packet
    struct WriteData_param {
        ULONG       ID_code;                // 'CD01'
        UCHAR       address_mode;           // Addressing format of start_sector:
                                            //  00 - Logical Block format
                                            //  01 - Minutes/Seconds/Frame format
        USHORT      transfer_count;         // Numbers of sectors to read.
                                            //  Must  be non zero
        ULONG       start_sector;           // Starting sector number of the read operation
        UCHAR       reserved;               // Reserved. Must be 0
        UCHAR       interleave_size;        // Not used. Must be 0
        UCHAR       interleave_skip_factor; // Not used. Must be 0
    } parm;
    ULONG parmlen = sizeof(parm);

    #define SECTORSIZE      2048

    // data packet
    struct WriteData_data {
        UCHAR       sector_data[SECTORSIZE]; // Sector to be written to the disk
    } data;

    ULONG datalen, data_offset;
    uint32_t cbWrite32;

    datalen = sizeof(data);

    /* parameter packet */
    struct {
        unsigned char command;
        unsigned char drive;
    } parm2;

    #pragma pack()

    /* data packet      */
    BIOSPARAMETERBLOCK bpb;
    ULONG parmlen2 = sizeof(parm2);
    ULONG datalen2 = sizeof(bpb);
    int32_t rc = 0;

    parm2.command = 1;
    parm2.drive = 0;

    struct {
        unsigned char command;
    } parmphys;
    DEVICEPARAMETERBLOCK dpb;
    ULONG parmphyslen = sizeof(parmphys);
    ULONG dataphyslen = sizeof(dpb);

    parmphys.command = 0;

    if (hFile->type == FILE_TYPE_DASD || hFile->type == FILE_TYPE_CD)
    {
        if ( (rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_DISK, DSK_GETDEVICEPARAMS,
                               &parm2, parmlen2, &parmlen2, &bpb, datalen2, &datalen2)) )
            return RTErrConvertFromOS2(rc);
    }
    else if (hFile->type == FILE_TYPE_RAW)
    {
        if ( (rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_PHYSICALDISK, PDSK_GETPHYSDEVICEPARAMS,
                               &parmphys, parmphyslen, &parmphyslen, &dpb, dataphyslen, &dataphyslen)) )
            return RTErrConvertFromOS2(rc);
    }

    #define BUFSIZE 0x4000
    #define SECSIZE 512

    ULONG parmlen4;

    //if (hFile->type == FILE_TYPE_DASD || hFile->type == FILE_TYPE_CD)
    //    parmlen4 = sizeof(TRACKLAYOUT) + sizeof(USHORT) * 2 * (bpb.usSectorsPerTrack - 1);
    //else if (hFile->type == FILE_TYPE_RAW)
    //    parmlen4 = sizeof(TRACKLAYOUT) + sizeof(USHORT) * 2 * (dpb.cSectorsPerTrack - 1);

    char trkbuf[sizeof(TRACKLAYOUT) + sizeof(USHORT) * 2 * 255];
    PTRACKLAYOUT ptrk = (PTRACKLAYOUT)trkbuf;
    ULONG datalen4 = BUFSIZE;
    USHORT cyl, head, sec, n;
    ssize_t cbWritten = 0;

    if (hFile->rawfs)
    {
        rc = DosWrite(RTFileToNative(hFile), pvBuf, cbToWrite, (PULONG)&cbWritten);
        rc = RTErrConvertFromOS2(rc);
    }
    else
    {
        switch (hFile->type)
        {
            case FILE_TYPE_NORMAL:
                cbWritten = write(RTFileToNative(hFile), pvBuf, cbToWrite);

                if (cbWritten < 0)
                    rc = RTErrConvertFromErrno(errno);
                else
                    rc = VINF_SUCCESS;
                break;

            case FILE_TYPE_CD:
                AssertReturn(!(hFile->off % SECTORSIZE), VERR_INVALID_PARAMETER);
                cbWrite32 = (cbToWrite > SECTORSIZE) ? SECTORSIZE : (uint32_t)cbToWrite;
                AssertReturn(!(cbToWrite % SECTORSIZE), VERR_INVALID_PARAMETER);

                memset(&parm, 0, sizeof(parm));
                memset(&data, 0, sizeof(data));

                parm.ID_code = ('C') | ('D' << 8) | ('0' << 16) | ('1' << 24);
                parm.address_mode = 0; // lba

                do
                {
                    parm.transfer_count = 1;
                    parm.start_sector = (ULONG)(hFile->off / SECTORSIZE);

                    memcpy(&data, (char *)pvBuf, cbWrite32);

                    rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_CDROMDISK, CDROMDISK_WRITEDATA,
                                     &parm, parmlen, &parmlen, &data, datalen, &datalen);

                    rc = RTErrConvertFromOS2(rc);

                    if (RT_FAILURE(rc))
                        break;

                    hFile->off += cbWrite32;
                    cbWritten  += cbWrite32;
                    cbToWrite  -= cbWrite32;
                    pvBuf      =  (uint8_t *)pvBuf + cbWrite32;

                } while ((cbToWrite > 0) && RT_SUCCESS(rc));
                break;

            case FILE_TYPE_DASD:
                memset((char *)trkbuf, 0, sizeof(trkbuf));

                for (int i = 0; i < bpb.usSectorsPerTrack; i++)
                {
                    ptrk->TrackTable[i].usSectorNumber = i + 1;
                    ptrk->TrackTable[i].usSectorSize = SECSIZE;
                }

                do
                {
                    cbWrite32 = (cbToWrite > BUFSIZE) ? BUFSIZE : (uint32_t)cbToWrite;
                    parmlen4 = sizeof(TRACKLAYOUT) + sizeof(USHORT) * 2 * (bpb.usSectorsPerTrack - 1);
                    datalen4 = BUFSIZE;

                    uint64_t off = hFile->off;
                    off += (uint64_t)bpb.cHiddenSectors * SECSIZE;

                    cyl = off / (SECSIZE * bpb.cHeads * bpb.usSectorsPerTrack);
                    head = (off / SECSIZE - bpb.cHeads * bpb.usSectorsPerTrack * cyl) / bpb.usSectorsPerTrack;
                    sec = off / SECSIZE - bpb.cHeads * bpb.usSectorsPerTrack * cyl - head * bpb.usSectorsPerTrack;

                    if (sec + cbWrite32 / SECSIZE > bpb.usSectorsPerTrack)
                        cbWrite32 = (bpb.usSectorsPerTrack - sec) * SECSIZE;

                    n = cbWrite32 / SECSIZE;

                    ptrk->bCommand = 1;
                    ptrk->usHead = head;
                    ptrk->usCylinder = cyl;
                    ptrk->usFirstSector = sec;
                    ptrk->cSectors = n;

                    rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_DISK, DSK_WRITETRACK,
                                     trkbuf, parmlen4, &parmlen4, (void *)pvBuf, datalen4, &datalen4);

                    rc = RTErrConvertFromOS2(rc);

                    if (RT_FAILURE(rc))
                        break;

                    hFile->off += cbWrite32;
                    cbWritten  += cbWrite32;
                    cbToWrite  -= cbWrite32;
                    pvBuf      =  (uint8_t *)pvBuf + cbWrite32;

                } while ((cbToWrite > 0) && RT_SUCCESS(rc));
                break;

            case FILE_TYPE_RAW:
                memset((char *)trkbuf, 0, sizeof(trkbuf));

                for (int i = 0; i < dpb.cSectorsPerTrack; i++)
                {
                    ptrk->TrackTable[i].usSectorNumber = i + 1;
                    ptrk->TrackTable[i].usSectorSize = SECSIZE;
                }

                do
                {
                    cbWrite32 = (cbToWrite > BUFSIZE) ? BUFSIZE : (uint32_t)cbToWrite;
                    parmlen4 = sizeof(TRACKLAYOUT) + sizeof(USHORT) * 2 * (dpb.cSectorsPerTrack - 1);
                    datalen4 = BUFSIZE;

                    cyl = hFile->off / (SECSIZE * dpb.cHeads * dpb.cSectorsPerTrack);
                    head = (hFile->off / SECSIZE - dpb.cHeads * dpb.cSectorsPerTrack * cyl) / dpb.cSectorsPerTrack;
                    sec = hFile->off / SECSIZE - dpb.cHeads * dpb.cSectorsPerTrack * cyl - head * dpb.cSectorsPerTrack;

                    if (sec + cbWrite32 / SECSIZE > dpb.cSectorsPerTrack)
                        cbWrite32 = (dpb.cSectorsPerTrack - sec) * SECSIZE;

                    n = cbWrite32 / SECSIZE;

                    ptrk->bCommand = 1;
                    ptrk->usHead = head;
                    ptrk->usCylinder = cyl;
                    ptrk->usFirstSector = sec;
                    ptrk->cSectors = n;

                    rc = DosDevIOCtl(RTFileToNative(hFile), IOCTL_PHYSICALDISK, PDSK_WRITEPHYSTRACK,
                                     trkbuf, parmlen4, &parmlen4, (void *)pvBuf, datalen4, &datalen4);

                    rc = RTErrConvertFromOS2(rc);

                    if (RT_FAILURE(rc))
                        break;

                    hFile->off += cbWrite32;
                    cbWritten  += cbWrite32;
                    cbToWrite  -= cbWrite32;
                    pvBuf      =  (uint8_t *)pvBuf + cbWrite32;

                } while ((cbToWrite > 0) && RT_SUCCESS(rc));
                break;

            default:
                rc = VERR_NOT_SUPPORTED;
        }
    }
    *pcbWritten = cbWritten;
    return rc;
}
#endif

RTR3DECL(int) RTFileOpen(PRTFILE pFile, const char *pszFilename, uint64_t fOpen)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pFile, VERR_INVALID_POINTER);
    *pFile = NIL_RTFILE;
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);

#ifdef RT_OS_OS2
    *pFile = (RTFILE)RTMemAllocZ(sizeof(struct RTFILEINT));

    (*pFile)->type = FILE_TYPE_NORMAL;
    (*pFile)->off = 0;
#endif

    /*
     * Merge forced open flags and validate them.
     */
    int rc = rtFileRecalcAndValidateFlags(&fOpen);
    if (RT_FAILURE(rc))
        return rc;
#ifndef O_NONBLOCK
    if (fOpen & RTFILE_O_NON_BLOCK)
    {
        AssertMsgFailed(("Invalid parameters! fOpen=%#llx\n", fOpen));
        return VERR_INVALID_PARAMETER;
    }
#endif

    /*
     * Calculate open mode flags.
     */
    int fOpenMode = 0;
#ifdef O_BINARY
    fOpenMode |= O_BINARY;              /* (pc) */
#endif
#ifdef O_LARGEFILE
    fOpenMode |= O_LARGEFILE;           /* (linux, solaris) */
#endif
#ifdef O_NOINHERIT
    if (!(fOpen & RTFILE_O_INHERIT))
        fOpenMode |= O_NOINHERIT;
#endif
#ifdef O_CLOEXEC
    static int s_fHave_O_CLOEXEC = 0; /* {-1,0,1}; since Linux 2.6.23 */
    if (!(fOpen & RTFILE_O_INHERIT) && s_fHave_O_CLOEXEC >= 0)
        fOpenMode |= O_CLOEXEC;
#endif
#ifdef O_NONBLOCK
    if (fOpen & RTFILE_O_NON_BLOCK)
        fOpenMode |= O_NONBLOCK;
#endif
#ifdef O_SYNC
    if (fOpen & RTFILE_O_WRITE_THROUGH)
        fOpenMode |= O_SYNC;
#endif
#if defined(O_DIRECT) && defined(RT_OS_LINUX)
    /* O_DIRECT is mandatory to get async I/O working on Linux. */
    if (fOpen & RTFILE_O_ASYNC_IO)
        fOpenMode |= O_DIRECT;
#endif
#if defined(O_DIRECT) && (defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD))
    /* Disable the kernel cache. */
    if (fOpen & RTFILE_O_NO_CACHE)
        fOpenMode |= O_DIRECT;
#endif

    /* create/truncate file */
    switch (fOpen & RTFILE_O_ACTION_MASK)
    {
        case RTFILE_O_OPEN:             break;
        case RTFILE_O_OPEN_CREATE:      fOpenMode |= O_CREAT; break;
        case RTFILE_O_CREATE:           fOpenMode |= O_CREAT | O_EXCL; break;
        case RTFILE_O_CREATE_REPLACE:   fOpenMode |= O_CREAT | O_TRUNC; break; /** @todo replacing needs fixing, this is *not* a 1:1 mapping! */
    }
    if (fOpen & RTFILE_O_TRUNCATE)
        fOpenMode |= O_TRUNC;

    switch (fOpen & RTFILE_O_ACCESS_MASK)
    {
        case RTFILE_O_READ:
            fOpenMode |= O_RDONLY; /* RTFILE_O_APPEND is ignored. */
            break;
        case RTFILE_O_WRITE:
            fOpenMode |= fOpen & RTFILE_O_APPEND ? O_APPEND | O_WRONLY : O_WRONLY;
            break;
        case RTFILE_O_READWRITE:
            fOpenMode |= fOpen & RTFILE_O_APPEND ? O_APPEND | O_RDWR   : O_RDWR;
            break;
        default:
            AssertMsgFailed(("RTFileOpen received an invalid RW value, fOpen=%#llx\n", fOpen));
            return VERR_INVALID_PARAMETER;
    }

    /* File mode. */
    int fMode = (fOpen & RTFILE_O_CREATE_MODE_MASK)
              ? (fOpen & RTFILE_O_CREATE_MODE_MASK) >> RTFILE_O_CREATE_MODE_SHIFT
              : RT_FILE_PERMISSION;

    /** @todo sharing! */

    /*
     * Open/create the file.
     */
    char const *pszNativeFilename;
    rc = rtPathToNative(&pszNativeFilename, pszFilename, NULL);
    if (RT_FAILURE(rc))
        return (rc);

    int fh = 0;
#ifdef RT_OS_OS2
    int ret = rtOs2ExtraFileOpen(pFile, pszNativeFilename,
                                 fOpen, fOpenMode, fMode);

    int iErr = errno;
    fh = RTFileToNative(*pFile);

    if (RT_FAILURE(ret) && ret != -1)
        return ret;
#else
    fh = open(pszNativeFilename, fOpenMode, fMode);
    iErr = errno;
#endif

#ifdef O_CLOEXEC
    if (   (fOpenMode & O_CLOEXEC)
        && s_fHave_O_CLOEXEC == 0)
    {
        if (fh < 0 && iErr == EINVAL)
        {
            s_fHave_O_CLOEXEC = -1;
            fh = open(pszNativeFilename, fOpenMode, fMode);
            iErr = errno;
        }
        else if (fh >= 0)
        {
            s_fHave_O_CLOEXEC = fcntl(fh, F_GETFD, 0) > 0 ? 1 : -1;
        }
    }
#endif

    rtPathFreeNative(pszNativeFilename, pszFilename);
    if (fh >= 0)
    {
        iErr = 0;

        /*
         * Mark the file handle close on exec, unless inherit is specified.
         */
        if (    !(fOpen & RTFILE_O_INHERIT)
#ifdef O_NOINHERIT
            &&  !(fOpenMode & O_NOINHERIT)  /* Take care since it might be a zero value dummy. */
#endif
#ifdef O_CLOEXEC
            &&  s_fHave_O_CLOEXEC <= 0
#endif
            )
            iErr = fcntl(fh, F_SETFD, FD_CLOEXEC) >= 0 ? 0 : errno;

        /*
         * Switch direct I/O on now if requested and required.
         */
#if defined(RT_OS_DARWIN) \
 || (defined(RT_OS_SOLARIS) && !defined(IN_GUEST))
        if (iErr == 0 && (fOpen & RTFILE_O_NO_CACHE))
        {
# if defined(RT_OS_DARWIN)
            iErr = fcntl(fh, F_NOCACHE, 1)        >= 0 ? 0 : errno;
# else
            iErr = directio(fh, DIRECTIO_ON)      >= 0 ? 0 : errno;
# endif
        }
#endif

        /*
         * Implement / emulate file sharing.
         *
         * We need another mode which allows skipping this stuff completely
         * and do things the UNIX way. So for the present this is just a debug
         * aid that can be enabled by developers too lazy to test on Windows.
         */
#if 0 && defined(RT_OS_LINUX)
        if (iErr == 0)
        {
            /* This approach doesn't work because only knfsd checks for these
               buggers. :-( */
            int iLockOp;
            switch (fOpen & RTFILE_O_DENY_MASK)
            {
                default:
                AssertFailed();
                case RTFILE_O_DENY_NONE:
                case RTFILE_O_DENY_NOT_DELETE:
                    iLockOp = LOCK_MAND | LOCK_READ | LOCK_WRITE;
                    break;
                case RTFILE_O_DENY_READ:
                case RTFILE_O_DENY_READ | RTFILE_O_DENY_NOT_DELETE:
                    iLockOp = LOCK_MAND | LOCK_WRITE;
                    break;
                case RTFILE_O_DENY_WRITE:
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_NOT_DELETE:
                    iLockOp = LOCK_MAND | LOCK_READ;
                    break;
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ:
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ | RTFILE_O_DENY_NOT_DELETE:
                    iLockOp = LOCK_MAND;
                    break;
            }
            iErr = flock(fh, iLockOp | LOCK_NB);
            if (iErr != 0)
                iErr = errno == EAGAIN ? ETXTBSY : 0;
        }
#endif /* 0 && RT_OS_LINUX */
#if defined(DEBUG_bird) && !defined(RT_OS_SOLARIS)
        if (iErr == 0)
        {
            /* This emulation is incomplete but useful. */
            switch (fOpen & RTFILE_O_DENY_MASK)
            {
                default:
                AssertFailed();
                case RTFILE_O_DENY_NONE:
                case RTFILE_O_DENY_NOT_DELETE:
                case RTFILE_O_DENY_READ:
                case RTFILE_O_DENY_READ | RTFILE_O_DENY_NOT_DELETE:
                    break;
                case RTFILE_O_DENY_WRITE:
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_NOT_DELETE:
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ:
                case RTFILE_O_DENY_WRITE | RTFILE_O_DENY_READ | RTFILE_O_DENY_NOT_DELETE:
                    if (fOpen & RTFILE_O_WRITE)
                    {
                        iErr = flock(fh, LOCK_EX | LOCK_NB);
                        if (iErr != 0)
                            iErr = errno == EAGAIN ? ETXTBSY : 0;
                    }
                    break;
            }
        }
#endif
#ifdef RT_OS_SOLARIS
        /** @todo Use fshare_t and associates, it's a perfect match. see sys/fcntl.h */
#endif

        /*
         * We're done.
         */
        if (iErr == 0)
        {
            RTFileFromNative(pFile, fh);
            LogFlow(("RTFileOpen(%p:{%RTfile}, %p:{%s}, %#llx): returns %Rrc\n",
                     pFile, *pFile, pszFilename, pszFilename, fOpen, rc));
            return VINF_SUCCESS;
        }

        close(fh);
    }
#ifdef RT_OS_OS2
    // it traps in some cases if we free it at this point
    // (in an unsuccessful open case)
    //RTMemFree(*pFile);
#endif
    return RTErrConvertFromErrno(iErr);
}


RTR3DECL(int)  RTFileOpenBitBucket(PRTFILE phFile, uint64_t fAccess)
{
    AssertReturn(   fAccess == RTFILE_O_READ
                 || fAccess == RTFILE_O_WRITE
                 || fAccess == RTFILE_O_READWRITE,
                 VERR_INVALID_PARAMETER);
    return RTFileOpen(phFile, "/dev/null", fAccess | RTFILE_O_DENY_NONE | RTFILE_O_OPEN);
}


RTR3DECL(int)  RTFileClose(RTFILE hFile)
{
    if (hFile == NIL_RTFILE)
        return VINF_SUCCESS;
#ifdef RT_OS_OS2
    // lock drive
    ULONG  dummy;
    APIRET rc;

    if (hFile->type == FILE_TYPE_DASD || hFile->type == FILE_TYPE_CD)
    {
        rc = DosDevIOCtl(RTFileToNative(hFile),
                         IOCTL_DISK, DSK_UNLOCKDRIVE,
                         &dummy, sizeof(dummy), NULL,
                         &dummy, sizeof(dummy), NULL);
    }
    else if (hFile->type == FILE_TYPE_RAW)
    {
        DosDevIOCtl(RTFileToNative(hFile),
                    IOCTL_PHYSICALDISK, PDSK_UNLOCKPHYSDRIVE,
                    &dummy, sizeof(dummy), NULL,
                    &dummy, sizeof(dummy), NULL);
    }

    if (! hFile->rawfs && hFile->type == FILE_TYPE_RAW)
    {
        // physical disk handle
        HFILE fh = RTFileToNative(hFile);
        DosPhysicalDisk(INFO_FREEIOCTLHANDLE,
                        NULL, 
                        0L, 
                        &fh,
                        2L);
    }
    else
#endif
    if (close(RTFileToNative(hFile)) == 0)
    {
#ifdef RT_OS_OS2
        RTMemFree(hFile);
#endif
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTFileFromNative(PRTFILE pFile, RTHCINTPTR uNative)
{
#ifndef RT_OS_OS2
    AssertCompile(sizeof(uNative) == sizeof(*pFile));
#endif
    if (uNative < 0)
    {
        AssertMsgFailed(("%p\n", uNative));
        *pFile = NIL_RTFILE;
        return VERR_INVALID_HANDLE;
    }
#ifdef RT_OS_OS2
    (*pFile)->hFile = uNative;
#else
    *pFile = (RTFILE)uNative;
#endif
    return VINF_SUCCESS;
}


RTR3DECL(RTHCINTPTR) RTFileToNative(RTFILE hFile)
{
    AssertReturn(hFile != NIL_RTFILE, -1);
#ifdef RT_OS_OS2
    return (intptr_t)hFile->hFile;
#else
    return (intptr_t)hFile;
#endif
}


RTFILE rtFileGetStandard(RTHANDLESTD enmStdHandle)
{
    int fd;
    switch (enmStdHandle)
    {
        case RTHANDLESTD_INPUT:  fd = 0; break;
        case RTHANDLESTD_OUTPUT: fd = 1; break;
        case RTHANDLESTD_ERROR:  fd = 2; break;
        default:
            AssertFailedReturn(NIL_RTFILE);
    }

    struct stat st;
    int rc = fstat(fd, &st);
    if (rc == -1)
        return NIL_RTFILE;
    return (RTFILE)(intptr_t)fd;
}


RTR3DECL(int)  RTFileDelete(const char *pszFilename)
{
    char const *pszNativeFilename;
    int rc = rtPathToNative(&pszNativeFilename, pszFilename, NULL);
    if (RT_SUCCESS(rc))
    {
        if (unlink(pszNativeFilename) != 0)
            rc = RTErrConvertFromErrno(errno);
        rtPathFreeNative(pszNativeFilename, pszFilename);
    }
    return rc;
}

RTR3DECL(int)  RTFileSeek(RTFILE hFile, int64_t offSeek, unsigned uMethod, uint64_t *poffActual)
{
    static const unsigned aSeekRecode[] =
    {
        SEEK_SET,
        SEEK_CUR,
        SEEK_END,
    };

    /*
     * Validate input.
     */
    if (uMethod > RTFILE_SEEK_END)
    {
        AssertMsgFailed(("Invalid uMethod=%d\n", uMethod));
        return VERR_INVALID_PARAMETER;
    }

    /* check that within off_t range. */
    if (    sizeof(off_t) < sizeof(offSeek)
        && (    (offSeek > 0 && (unsigned)(offSeek >> 32) != 0)
            ||  (offSeek < 0 && (unsigned)(-offSeek >> 32) != 0)))
    {
        AssertMsgFailed(("64-bit search not supported\n"));
        return VERR_NOT_SUPPORTED;
    }

    off_t offCurrent;
#ifdef RT_OS_OS2
    int ret = rtOs2ExtraFileSeek(hFile, offSeek, uMethod, &offCurrent);
#else
    offCurrent = lseek(RTFileToNative(hFile), (off_t)offSeek, aSeekRecode[uMethod]);
    int ret = VINF_SUCCESS;

    if (ret < 0)
        ret = RTErrConvertFromErrno(errno);
#endif
    if (offCurrent != ~0)
    {
        if (poffActual)
            *poffActual = (uint64_t)offCurrent;
        return VINF_SUCCESS;
    }
    return ret;
}

RTR3DECL(int)  RTFileRead(RTFILE hFile, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    if (cbToRead <= 0)
        return VINF_SUCCESS;

    /*
     * Attempt read.
     */
    ssize_t cbRead;
#ifdef RT_OS_OS2
    if ((rtOs2ExtraFileRead(hFile, pvBuf, cbToRead, &cbRead) == VINF_SUCCESS) && (cbRead >= 0))
#else
    cbRead = read(RTFileToNative(hFile), pvBuf, cbToRead);
    if (cbRead >= 0)
#endif
    {
        if (pcbRead)
            /* caller can handle partial read. */
            *pcbRead = cbRead;
        else
        {
            /* Caller expects all to be read. */
            while ((ssize_t)cbToRead > cbRead)
            {
                ssize_t cbReadPart;
                int rc;
#ifdef RT_OS_OS2
                if (((rc = rtOs2ExtraFileRead(hFile, (char *)pvBuf + cbRead, cbToRead - cbRead,
                                           &cbReadPart)) != VINF_SUCCESS) || (cbReadPart <= 0))
                {
                    if (cbReadPart == 0)
                        return VERR_EOF;
                    return rc;
                }
#else
                cbReadPart = read(RTFileToNative(hFile), (char*)pvBuf + cbRead, cbToRead - cbRead);
                if (cbReadPart <= 0)
                {
                    if (cbReadPart == 0)
                        return VERR_EOF;
                    return RTErrConvertFromErrno(errno);
                }
#endif
                cbRead += cbReadPart;
            }
        }
        return VINF_SUCCESS;
    }

    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int)  RTFileWrite(RTFILE hFile, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    if (cbToWrite <= 0)
        return VINF_SUCCESS;

    /*
     * Attempt write.
     */
    ssize_t cbWritten;
#ifdef RT_OS_OS2
    if ((rtOs2ExtraFileWrite(hFile, pvBuf, cbToWrite, &cbWritten) == VINF_SUCCESS) && (cbWritten >= 0))
#else
    cbWritten = write(RTFileToNative(hFile), pvBuf, cbToWrite);
    if (cbWritten >= 0)
#endif
    {
        if (pcbWritten)
            /* caller can handle partial write. */
            *pcbWritten = cbWritten;
        else
        {
            /* Caller expects all to be write. */
            while ((ssize_t)cbToWrite > cbWritten)
            {
                ssize_t cbWrittenPart;
                int rc;
#ifdef RT_OS_OS2
                if (((rc = rtOs2ExtraFileWrite(hFile, (char *)pvBuf + cbWritten, cbToWrite - cbWritten,
                                            &cbWrittenPart)) != VINF_SUCCESS) || (cbWrittenPart <= 0))
                    return rc;
#else
                cbWrittenPart = write(RTFileToNative(hFile), (const char *)pvBuf + cbWritten, cbToWrite - cbWritten);
                if (cbWrittenPart <= 0)
                    return RTErrConvertFromErrno(errno);
#endif
                cbWritten += cbWrittenPart;
            }
        }
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int)  RTFileSetSize(RTFILE hFile, uint64_t cbSize)
{
    /*
     * Validate offset.
     */
    if (    sizeof(off_t) < sizeof(cbSize)
        &&  (cbSize >> 32) != 0)
    {
        AssertMsgFailed(("64-bit filesize not supported! cbSize=%lld\n", cbSize));
        return VERR_NOT_SUPPORTED;
    }

#if defined(_MSC_VER) || (defined(RT_OS_OS2) && (!defined(__INNOTEK_LIBC__) || __INNOTEK_LIBC__ < 0x006))
    if (chsize(RTFileToNative(hFile), (off_t)cbSize) == 0)
#else
    /* This relies on a non-standard feature of FreeBSD, Linux, and OS/2
     * LIBC v0.6 and higher. (SuS doesn't define ftruncate() and size bigger
     * than the file.)
     */
    if (ftruncate(RTFileToNative(hFile), (off_t)cbSize) == 0)
#endif
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTFileGetSize(RTFILE hFile, uint64_t *pcbSize)
{
    /*
     * Ask fstat() first.
     */
    struct stat st;
    if (!fstat(RTFileToNative(hFile), &st))
    {
        *pcbSize = st.st_size;
        if (   st.st_size != 0
#if defined(RT_OS_SOLARIS)
            || (!S_ISBLK(st.st_mode) && !S_ISCHR(st.st_mode))
#elif defined(RT_OS_FREEBSD)
            || !S_ISCHR(st.st_mode)
#else
            || !S_ISBLK(st.st_mode)
#endif
            )
            return VINF_SUCCESS;

        /*
         * It could be a block device.  Try determin the size by I/O control
         * query or seek.
         */
#ifdef RT_OS_DARWIN
        uint64_t cBlocks;
        if (!ioctl(RTFileToNative(hFile), DKIOCGETBLOCKCOUNT, &cBlocks))
        {
            uint32_t cbBlock;
            if (!ioctl(RTFileToNative(hFile), DKIOCGETBLOCKSIZE, &cbBlock))
            {
                *pcbSize = cBlocks * cbBlock;
                return VINF_SUCCESS;
            }
        }
        /* must be a block device, fail on failure. */

#elif defined(RT_OS_SOLARIS)
        struct dk_minfo MediaInfo;
        if (!ioctl(RTFileToNative(hFile), DKIOCGMEDIAINFO, &MediaInfo))
        {
            *pcbSize = MediaInfo.dki_capacity * MediaInfo.dki_lbsize;
            return VINF_SUCCESS;
        }
        /* might not be a block device. */
        if (errno == EINVAL || errno == ENOTTY)
            return VINF_SUCCESS;

#elif defined(RT_OS_FREEBSD)
        off_t cbMedia = 0;
        if (!ioctl(RTFileToNative(hFile), DIOCGMEDIASIZE, &cbMedia))
        {
            *pcbSize = cbMedia;
            return VINF_SUCCESS;
        }
        /* might not be a block device. */
        if (errno == EINVAL || errno == ENOTTY)
            return VINF_SUCCESS;

#else
        /* PORTME! Avoid this path when possible. */
        uint64_t offSaved;
        int rc = RTFileSeek(hFile, 0, RTFILE_SEEK_CURRENT, &offSaved);
        if (RT_SUCCESS(rc))
        {
            rc = RTFileSeek(hFile, 0, RTFILE_SEEK_END, pcbSize);
            int rc2 = RTFileSeek(hFile, offSaved, RTFILE_SEEK_BEGIN, NULL);
            if (RT_SUCCESS(rc))
                return rc2;
        }
#endif
    }
    return RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTFileGetMaxSizeEx(RTFILE hFile, PRTFOFF pcbMax)
{
    /*
     * Save the current location
     */
    uint64_t offOld;
    int rc = RTFileSeek(hFile, 0, RTFILE_SEEK_CURRENT, &offOld);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Perform a binary search for the max file size.
     */
    uint64_t offLow  =       0;
    uint64_t offHigh = 8 * _1T; /* we don't need bigger files */
    /** @todo Unfortunately this does not work for certain file system types,
     * for instance cifs mounts. Even worse, statvfs.f_fsid returns 0 for such
     * file systems. */
    //uint64_t offHigh = INT64_MAX;
    for (;;)
    {
        uint64_t cbInterval = (offHigh - offLow) >> 1;
        if (cbInterval == 0)
        {
            if (pcbMax)
                *pcbMax = offLow;
            return RTFileSeek(hFile, offOld, RTFILE_SEEK_BEGIN, NULL);
        }

        rc = RTFileSeek(hFile, offLow + cbInterval, RTFILE_SEEK_BEGIN, NULL);
        if (RT_FAILURE(rc))
            offHigh = offLow + cbInterval;
        else
            offLow  = offLow + cbInterval;
    }
}


RTR3DECL(bool) RTFileIsValid(RTFILE hFile)
{
    if (hFile != NIL_RTFILE)
    {
        int fFlags = fcntl(RTFileToNative(hFile), F_GETFD);
        if (fFlags >= 0)
            return true;
    }
    return false;
}


RTR3DECL(int)  RTFileFlush(RTFILE hFile)
{
#ifdef RT_OS_OS2
    // Access to a physical disk is not mediated by
    // a filesystem cache, so it is synchronous
    if (hFile->type != FILE_TYPE_RAW)
#endif
    if (fsync(RTFileToNative(hFile)))
        return RTErrConvertFromErrno(errno);
    return VINF_SUCCESS;
}


RTR3DECL(int) RTFileIoCtl(RTFILE hFile, unsigned long ulRequest, void *pvData, unsigned cbData, int *piRet)
{
    NOREF(cbData);
    int rc = ioctl(RTFileToNative(hFile), ulRequest, pvData);
    if (piRet)
        *piRet = rc;
    return rc >= 0 ? VINF_SUCCESS : RTErrConvertFromErrno(errno);
}


RTR3DECL(int) RTFileSetMode(RTFILE hFile, RTFMODE fMode)
{
    /*
     * Normalize the mode and call the API.
     */
    fMode = rtFsModeNormalize(fMode, NULL, 0);
    if (!rtFsModeIsValid(fMode))
        return VERR_INVALID_PARAMETER;

    if (fchmod(RTFileToNative(hFile), fMode & RTFS_UNIX_MASK))
    {
        int rc = RTErrConvertFromErrno(errno);
        Log(("RTFileSetMode(%RTfile,%RTfmode): returns %Rrc\n", hFile, fMode, rc));
        return rc;
    }
    return VINF_SUCCESS;
}


RTDECL(int) RTFileSetOwner(RTFILE hFile, uint32_t uid, uint32_t gid)
{
    uid_t uidNative = uid != NIL_RTUID ? (uid_t)uid : (uid_t)-1;
    AssertReturn(uid == uidNative, VERR_INVALID_PARAMETER);
    gid_t gidNative = gid != NIL_RTGID ? (gid_t)gid : (gid_t)-1;
    AssertReturn(gid == gidNative, VERR_INVALID_PARAMETER);

    if (fchown(RTFileToNative(hFile), uidNative, gidNative))
        return RTErrConvertFromErrno(errno);
    return VINF_SUCCESS;
}


RTR3DECL(int) RTFileRename(const char *pszSrc, const char *pszDst, unsigned fRename)
{
    /*
     * Validate input.
     */
    AssertMsgReturn(VALID_PTR(pszSrc), ("%p\n", pszSrc), VERR_INVALID_POINTER);
    AssertMsgReturn(VALID_PTR(pszDst), ("%p\n", pszDst), VERR_INVALID_POINTER);
    AssertMsgReturn(*pszSrc, ("%p\n", pszSrc), VERR_INVALID_PARAMETER);
    AssertMsgReturn(*pszDst, ("%p\n", pszDst), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fRename & ~RTPATHRENAME_FLAGS_REPLACE), ("%#x\n", fRename), VERR_INVALID_PARAMETER);

    /*
     * Take common cause with RTPathRename.
     */
    int rc = rtPathPosixRename(pszSrc, pszDst, fRename, RTFS_TYPE_FILE);

    LogFlow(("RTDirRename(%p:{%s}, %p:{%s}, %#x): returns %Rrc\n",
             pszSrc, pszSrc, pszDst, pszDst, fRename, rc));
    return rc;
}

