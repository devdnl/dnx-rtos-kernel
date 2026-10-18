/*------------------------------------------------------------------------*/
/* Sample Code of OS Dependent Functions for FatFs                        */
/* (C)ChaN, 2018                                                          */
/*------------------------------------------------------------------------*/


#include "ff.h"
#include "fs/fs.h"

#if FF_USE_LFN == 3	/* Dynamic memory allocation */

/*------------------------------------------------------------------------*/
/* Allocate a memory block                                                */
/*------------------------------------------------------------------------*/

void* ff_memalloc (	/* Returns pointer to the allocated memory block (null if not enough core) */
	UINT msize		/* Number of bytes to allocate */
)
{
        void *ptr = NULL;
        sys_malloc(msize, &ptr);
        return ptr;
}


/*------------------------------------------------------------------------*/
/* Free a memory block                                                    */
/*------------------------------------------------------------------------*/

void ff_memfree (
	void* mblock	/* Pointer to the memory block to free (nothing to do if null) */
)
{
        if (mblock) {
                sys_free(&mblock);
        }
}

#endif



#if FF_FS_REENTRANT	/* Mutal exclusion */

/*------------------------------------------------------------------------*/
/* Create a Synchronization Object                                        */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount() function to create a new
/  synchronization object for the volume, such as semaphore and mutex.
/  When a 0 is returned, the f_mount() function fails with FR_INT_ERR.
*/

//const osMutexDef_t Mutex[FF_VOLUMES];	/* Table of CMSIS-RTOS mutex */


int ff_cre_syncobj (	/* 1:Function succeeded, 0:Could not create the sync object */
	BYTE vol,			/* Corresponding volume (logical drive number) */
	FF_SYNC_t* sobj		/* Pointer to return the created sync object */
)
{
        UNUSED_ARG1(vol);

        kmtx_t *mutex = NULL;
        int err = sys_mutex_create(KMTX_TYPE_NORMAL, &mutex);
        *sobj = mutex;

        return (!err and (mutex != NULL));
}


/*------------------------------------------------------------------------*/
/* Delete a Synchronization Object                                        */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount() function to delete a synchronization
/  object that created with ff_cre_syncobj() function. When a 0 is returned,
/  the f_mount() function fails with FR_INT_ERR.
*/

int ff_del_syncobj (	/* 1:Function succeeded, 0:Could not delete due to an error */
	FF_SYNC_t sobj		/* Sync object tied to the logical drive to be deleted */
)
{
        return sys_mutex_destroy(sobj) == ESUCC;
}


/*------------------------------------------------------------------------*/
/* Request Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on entering file functions to lock the volume.
/  When a 0 is returned, the file function fails with FR_TIMEOUT.
*/

int ff_req_grant (	/* 1:Got a grant to access the volume, 0:Could not get a grant */
	FF_SYNC_t sobj	/* Sync object to wait */
)
{
        return sys_mutex_lock(sobj, FF_FS_TIMEOUT) == ESUCC;
}


/*------------------------------------------------------------------------*/
/* Release Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on leaving file functions to unlock the volume.
*/

void ff_rel_grant (
	FF_SYNC_t sobj	/* Sync object to be signaled */
)
{
        sys_mutex_unlock(sobj);
}

#endif


/*------------------------------------------------------------------------*/
/* Get FAT time                                                           */
/*------------------------------------------------------------------------*/
//                24                16                 8                 0
// +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+
// |Y|Y|Y|Y|Y|Y|Y|M| |M|M|M|D|D|D|D|D| |h|h|h|h|h|m|m|m| |m|m|m|s|s|s|s|s|
// +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+
//  \___________/\________/\_________/ \________/\____________/\_________/
//     year        month       day      hour       minute        second
//
// Timestamp is stored in UTC. The same interpretation is used when reading
// (time_fat2unix() in fatfs.c). Range of FAT format is 1980..2107, time out
// of range (e.g. RTC not set) is clamped.

DWORD get_fattime (void)
{
        time_t t = 0;
        struct tm tm;

        memset(&tm, 0, sizeof(tm));

        if (sys_gettime(&t) != ESUCC) {
                t = 0;
        }

        sys_gmtime_r(&t, &tm);

        if (tm.tm_year < 80) {
                tm.tm_year = 80;
                tm.tm_mon  = 0;
                tm.tm_mday = 1;
                tm.tm_hour = 0;
                tm.tm_min  = 0;
                tm.tm_sec  = 0;

        } else if (tm.tm_year > 207) {
                tm.tm_year = 207;
                tm.tm_mon  = 11;
                tm.tm_mday = 31;
                tm.tm_hour = 23;
                tm.tm_min  = 59;
                tm.tm_sec  = 59;
        }

        if (tm.tm_sec > 59) {   /* leap second */
                tm.tm_sec = 59;
        }

        DWORD fattime = ((DWORD)(tm.tm_year - 80) << 25)
                      | ((DWORD)(tm.tm_mon + 1)   << 21)
                      | ((DWORD)tm.tm_mday        << 16)
                      | ((DWORD)tm.tm_hour        << 11)
                      | ((DWORD)tm.tm_min         << 5)
                      | ((DWORD)tm.tm_sec         >> 1);

        return fattime;
}

