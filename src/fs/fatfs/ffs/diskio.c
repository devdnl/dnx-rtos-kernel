/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

#if FF_MIN_SS != FF_MAX_SS
#error "diskio.c supports fixed sector size only (FF_MIN_SS == FF_MAX_SS)"
#endif

#define SECTOR_SIZE		FF_MAX_SS


/*-----------------------------------------------------------------------*/
/* Convert errno to disk result                                          */
/*-----------------------------------------------------------------------*/

static DRESULT errno_2_dresult (int err)
{
        switch (err) {
        case ESUCC: return RES_OK;
        case EROFS: return RES_WRPRT;
        default:    return RES_ERROR;
        }
}



/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	kfile_t *pdrv		/* Physical drive nmuber to identify the drive */
)
{
        UNUSED_ARG1(pdrv);
        return 0;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	kfile_t *pdrv				/* Physical drive nmuber to identify the drive */
)
{
        UNUSED_ARG1(pdrv);
	return 0;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	kfile_t *pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
        const size_t len = (size_t)count * SECTOR_SIZE;
        size_t rdcnt = 0;

        int err = sys_fseek(pdrv, (i64_t)(sector * SECTOR_SIZE), SEEK_SET);
        if (!err) {
                err = sys_fread(buff, len, &rdcnt, pdrv);
                if (!err && (rdcnt != len)) {
                        err = EIO;
                }
        }

        return errno_2_dresult(err);
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	kfile_t *pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
        const size_t len = (size_t)count * SECTOR_SIZE;
        size_t wrcnt = 0;

        int err = sys_fseek(pdrv, (i64_t)(sector * SECTOR_SIZE), SEEK_SET);
        if (!err) {
                err = sys_fwrite(buff, len, &wrcnt, pdrv);
                if (!err && (wrcnt != len)) {
                        err = EIO;
                }
        }

        return errno_2_dresult(err);
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	kfile_t *pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
        UNUSED_ARG1(buff);

        switch (cmd) {
        case CTRL_SYNC: {
                /* data written by FatFs must reach the medium */
                int err = sys_fflush(pdrv);
                return errno_2_dresult((err == ENOTSUP) ? ESUCC : err);
        }

        default:
                return RES_PARERR;
        }
}

