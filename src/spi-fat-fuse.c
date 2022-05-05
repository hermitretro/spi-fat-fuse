/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2021       Hermit Retro Products <alligator.descartes@hermitretro.com>

 This file is part of spi-fat-fuse.

     spi-fat-fuse is free software: you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation, either version 3 of the License, or
     (at your option) any later version.

     spi-fat-fuse is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with spi-fat-fuse.  If not, see <https://www.gnu.org/licenses/>.

*/

/** @file
 *
 * SPI bit-banged FAT filesystem using high-level API
 *
 * ## Source code ##
 * \include spi-fat-fuse.c
 */

#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>

#include "bcm2835.h"
#include "ff.h"

/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
static struct options {
	const char *filename;
	int show_help;
} options;

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("--name=%s", filename),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

/** Persistent filesystem handle */
static FATFS *fatfs = NULL;

#define LAZY_MOUNT \
    int lmrv = _lazy_mount(); \
    if ( lmrv != 0 ) { \
        return lmrv; \
    }

/**
 * Rename files starting with '.' into starting with '_', e.g., macOS
 * resource forks
 *
 * Returns: 0 = fail, 1 = success
 */
int renameHidden( const char *inpath, char *outpath, size_t outpathsz ) {

    int i;

    if ( outpath == NULL || outpathsz == 0 ) {
        return 0;
    }

    strncpy( outpath, inpath, outpathsz );
    for ( i = 0 ; i < strlen( outpath ) ; i++ ) {
        if ( outpath[i] == '.' ) {
            if ( i > 0 ) {
                if ( outpath[i - 1] == '/' ) {
                    outpath[i] = '_';
                }
            }
        }
    }

    return 1;
}


static int FRESULT_TO_OSCODE( int res ) {
    switch ( res ) {
        case FR_OK: {
            return 0;
        }
        case FR_DISK_ERR: {
            return -EINTR;
        }
        case FR_INT_ERR: {
            return -ENOMEM;
        }
        case FR_NOT_READY: {
            return -EINTR;
        }
        case FR_NO_FILE: {
            return -ENOENT;
        }
        case FR_NO_PATH: {
            return -ENOENT;
        }
        case FR_INVALID_NAME: {
            return -ENOENT;
        }
        case FR_DENIED: {
            return -EACCES;
        }
        case FR_EXIST: {
            return -EACCES;
        }
        case FR_INVALID_OBJECT: {
            return -ENOENT;
        }
        case FR_WRITE_PROTECTED: {
            return -EACCES;
        }
        case FR_INVALID_DRIVE: {
            return -EACCES;
        }
        case FR_NOT_ENABLED: {
            return -ENOSPC;
        }
        case FR_NO_FILESYSTEM: {
            return -ENODEV;
        }
        case FR_MKFS_ABORTED: {
            return -ENODEV;
        }
        case FR_TIMEOUT: {
            return -EACCES;
        }
        case FR_LOCKED: {
            return -EACCES;
        }
        case FR_NOT_ENOUGH_CORE: {
            return -ENAMETOOLONG;
        }
        case FR_TOO_MANY_OPEN_FILES: {
            return -ENFILE;
        }
        default: {
            return -ENOENT;
        }
    }

    return ENOENT;
}

/** Timestamp conversion */
unsigned long unixTimestampToFATTimestamp( struct tm *unixTime ) {

    unsigned long fatTimestamp =
        ( ((unixTime->tm_year - 80) << 25) | 
          ((unixTime->tm_mon + 1) << 21) |
          (unixTime->tm_mday << 16) |
          (unixTime->tm_hour << 11) |
          (unixTime->tm_min << 5) |
          unixTime->tm_sec >> 1
        );

    return fatTimestamp;
}

void FATTimestampToDateTime( unsigned long fatTimestamp, unsigned short *fdate, unsigned short *ftime ) {

    if ( fdate == NULL || ftime == NULL ) {
        return;
    }

    *fdate = (fatTimestamp & 0xffff0000) >> 16;
    *ftime = fatTimestamp & 0x0000ffff;
}

void FATTimestampToUNIX( unsigned long fatTimestamp, struct tm *unixTimestamp ) {

    if ( unixTimestamp == NULL ) {
        return;
    }

    unixTimestamp->tm_year = ((fatTimestamp & 0xfe000000) >> 25) + 80;
    unixTimestamp->tm_mon = ((fatTimestamp & 0x1e00000) >> 21) - 1;
    unixTimestamp->tm_mday = (fatTimestamp & 0x1f0000) >> 16;
    unixTimestamp->tm_hour = (fatTimestamp & 0xf800) >> 11;
    unixTimestamp->tm_min = (fatTimestamp & 0x7e0) >> 5;
    unixTimestamp->tm_sec = (fatTimestamp & 0x1f) >> 1;  
}

/** Populates the passed in struct */
void FATDateTimeToUNIX( WORD fdate, WORD ftime, struct tm *unixTimestamp ) {
   
    if ( unixTimestamp == NULL ) {
        return;
    }

    unsigned long fatTimestamp = ((unsigned long)fdate << 16) | ftime;
    return FATTimestampToUNIX( fatTimestamp, unixTimestamp );
}

/** Returns the current time as a FAT timestamp */
static unsigned long nowAsFATTimestamp() {

    time_t lnow = time( NULL );
    struct tm *ltime = localtime( &lnow );
    return unixTimestampToFATTimestamp( ltime );
}

static void *spi_fat_fuse_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg)
{
	(void) conn;
	cfg->auto_cache = 1;
    cfg->attr_timeout = 3600;

    bcm2835_init();

    fatfs = NULL;

	return NULL;
}

static int _lazy_mount() {

//    printf( "_lazy_mount: in: fatfs: %X\n", fatfs ); 

    /** Lazy filesystem mount */
    if ( fatfs == NULL ) {

//        printf( "volume unmounted.... mounting...\n" );

        fatfs = malloc( sizeof( FATFS ) );
        if ( fatfs == NULL ) {
//            printf( "_lazy_mount: failed to mount\n" );
            return FRESULT_TO_OSCODE( FR_DISK_ERR );
        }
        memset( fatfs, 0, sizeof( FATFS ) );

        FRESULT res = f_mount( fatfs, "", 0 );
        if ( res != FR_OK ) {
//            printf( "f_mount failed: %d\n", res );
        fatfs = NULL;
            return FRESULT_TO_OSCODE( res );
        }
    } else {
//      printf( "_lazy_mount: already mounted\n" );
    }

//    printf( "_lazy_mount: out: fatfs: %X\n", fatfs );

    return FRESULT_TO_OSCODE( FR_OK );
}

static int spi_fat_fuse_getattr( const char *path, struct stat *stbuf,
			         struct fuse_file_info *fi ) {

    (void) fi;
    FRESULT res;
    FILINFO finfo;

    memset(stbuf, 0, sizeof(struct stat));

    printf( "getattr: %s\n", path );

    LAZY_MOUNT

    if ( strcmp( path, "/" ) == 0 ) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    /** Demangle the path for hidden files */
    char lpath[255];
    renameHidden( path, lpath, 255 ); 

    /**
     * Retry loop. Depending on the access rate to the underlying card
     * the f_stat() call can transiently fail
     */
    int ntries = 0;
    int maxtries = 1;
fstat_retry:
    res = f_stat( lpath, &finfo );
    if ( res != FR_OK ) {
        printf( "f_stat failed: %d\n", res );
        if ( ntries >= maxtries ) {
            return FRESULT_TO_OSCODE( res );
        } else {
            ntries++;
            bcm2835_delay( 50 );
            goto fstat_retry;
        }
    }

    if ( (finfo.fattrib & AM_DIR) == AM_DIR ) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else {
        stbuf->st_size = finfo.fsize;
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
    }

	return 0;
}

static int spi_fat_fuse_setxattr( const char *arg1, const char *arg2, const char *arg3, size_t arg4, int arg5 ) {
    printf( "setxattr\n" );

    LAZY_MOUNT

    return 0;
}

static int spi_fat_fuse_mkdir( const char *path, mode_t mode ) {

    LAZY_MOUNT

    FRESULT res = f_mkdir( path );

    return FRESULT_TO_OSCODE( res );
}

static int spi_fat_fuse_rmdir( const char *path ) {

    LAZY_MOUNT

    FRESULT res = f_rmdir( path );

    return FRESULT_TO_OSCODE( res );
}

static int spi_fat_fuse_opendir( const char *path, struct fuse_file_info *fi ) {

    FRESULT res;
    DIR *dir;
    int rv = 0;

    LAZY_MOUNT
    
    dir = malloc( sizeof( DIR ) );
    memset( dir, 0, sizeof( DIR ) );

    res = f_opendir( dir, path );
    if ( res != FR_OK ) {
        fprintf( stderr, "f_opendir failed: %d\n", res );
        fi->fh = 0;
    } else {
        fi->fh = (uint64_t)dir;
    }

    rv = FRESULT_TO_OSCODE( res );

    return rv;
}

static int spi_fat_fuse_readdir( const char *path, void *buf, 
                                 fuse_fill_dir_t filler,
			                     off_t offset, struct fuse_file_info *fi,
			                     enum fuse_readdir_flags flags )
{
    FRESULT res;
    FRESULT rv = FR_OK;

    DIR *dir = NULL;

    int hasRDP = ((flags & FUSE_READDIR_PLUS) == FUSE_READDIR_PLUS);

    int i;

    printf( "readdir: offset: %lld, readdirplus: %d\n", offset, hasRDP );

    LAZY_MOUNT

    dir = (DIR *)fi->fh; 
    if ( dir == NULL ) {
        return -ENOENT;
    }

    unsigned int nfileinfo = offset;
    FILINFO finfo;
    memset( &finfo, 0, sizeof( FILINFO ) );

    struct stat st;
    memset( &st, 0, sizeof( st ) );
    nfileinfo++;    /** readdirplus offset needs to start at 1.. */

    printf( "nfileinfo: %d\toffset: %d\n", nfileinfo, offset );

    /** Add default directories */
    if ( offset == 0 ) {
        st.st_mode = S_IFDIR | 0755;
        st.st_nlink = 2;
        st.st_ino = 0xffffffff; /** Needs to be set otherwise these directories won't add in plus mode. This value corresponds to FUSE_UNKNOWN_INO in fuse.c */
        if ( filler( buf, ".", &st, nfileinfo++, FUSE_FILL_DIR_PLUS ) ) {
            printf( "failed to inject .\n" );
        }
        if ( filler( buf, "..", &st, nfileinfo++, FUSE_FILL_DIR_PLUS ) ) {
            printf( "failed to inject ..\n" );
        }
    }

    res = f_readdir( dir, &finfo );
    if ( res != FR_OK ) {
        fprintf( stderr, "f_readdir failed: %d\n", res );
        if ( res == FR_DISK_ERR ) {
            /** SD card has probably been ejected */
            printf( "card has probably been ejected. invalidate filesystem for remounting\n" );
            if ( fatfs != NULL ) {
                free( fatfs );
                fatfs = NULL;
            }
        }
            
        rv = res;
        goto readdir_cleanup;
    }

    while ( res == FR_OK && finfo.fname[0] ) {
        memset( &st, 0, sizeof( st ) );

        /** Timestamp */
        struct tm unixTimestamp;
        FATDateTimeToUNIX( finfo.fdate, finfo.ftime, &unixTimestamp );
        time_t ltime = mktime( &unixTimestamp );

        if ( (finfo.fattrib & AM_DIR) == AM_DIR ) {
            st.st_mode = S_IFDIR | 0755;
            st.st_nlink = 2;
            st.st_size = 0;
        } else {
            st.st_size = finfo.fsize;
            st.st_mode = S_IFREG | 0644;
            st.st_nlink = 1;

            /** Compute blocks used for 'ls -l' total */
            st.st_blocks = (finfo.fsize / 512);
            if ( (finfo.fsize % 512) != 0 ) {
                st.st_blocks++;
            }
            st.st_blksize = 512;

            /** Times */
            if ( ltime != -1 ) {
            	st.st_atim.tv_sec = ltime;
            	st.st_mtim.tv_sec = ltime;
            	st.st_ctim.tv_sec = ltime;
            }
        }

        /**
         * Hidden files are translated to begin with '_'. Rename them on
         * the fly to start with '.' to behave as per a UNIX hidden file.
         * These are demangled in open() and getattr()
         */
        if ( finfo.fname[0] == '_' ) {
            finfo.fname[0] = '.';
        }

        if ( filler( buf, finfo.fname, &st, nfileinfo, FUSE_FILL_DIR_PLUS ) ) {
            /** We need to rewind the readdir call here... */
            int lrv = f_seekdir( dir, -1 );
            if ( lrv != FR_OK ) {
                printf( "seekdir failed: %d\n", lrv );
            }
            goto readdir_cleanup;
        }

        nfileinfo++;

        res = f_readdir( dir, &finfo );
        if ( res != FR_OK ) {
            printf( "f_readdir failed: %d\n", res );
            rv = res;
            goto readdir_cleanup;
        }
    }

readdir_cleanup:
	return FRESULT_TO_OSCODE( rv );
}

static int spi_fat_fuse_releasedir( const char *path, struct fuse_file_info *fi ) {

    FRESULT res;
    DIR *dir = NULL;

    dir = (DIR *)fi->fh;
    if ( dir == NULL ) {
        return -ENOENT;
    }

    res = f_closedir( dir );
    if ( res != FR_OK ) {
        printf( "f_closedir() failed: %d\n", res );
    }

    free( dir );
    fi->fh = 0;

    return FRESULT_TO_OSCODE( res );
}

static int spi_fat_fuse_open(const char *path, struct fuse_file_info *fi)
{
    FRESULT res;
    int i;

    printf( "fuse_open: %s (mode %d)\n", path, fi->flags );

    int mode = FA_READ | FA_WRITE;
    if ( (fi->flags & O_ASYNC) == O_ASYNC ) {
        mode = FA_READ;
    } else {
      if ( (fi->flags & O_CREAT) == O_CREAT ) {
          printf( "open: create mode\n" );
          mode = FA_WRITE | FA_CREATE_NEW;
      }
    }

    FIL *fp = malloc( sizeof( FIL ) );
    if ( fp == NULL ) {
        return ENOENT;
    }

    /**
     * If the filename starts with a '.', translate it to '_'
     */
    char lpath[255];
    renameHidden( path, lpath, 255 );

    res = f_open( fp, lpath, mode );
    if ( res != FR_OK ) {
        printf( "f_open failed: %d\n", res );
        fi->fh = 0;
        return FRESULT_TO_OSCODE( res );
    } else {
        fi->fh = (uint64_t)fp;
    }

	return 0;
}

static int spi_fat_fuse_release( const char *path, struct fuse_file_info *fi)
{
    FRESULT res;

    FIL *fp = (FIL *)fi->fh;
    if ( fp == NULL ) {
        return ENOENT;
    }

    res = f_close( fp );
    if ( res != FR_OK ) {
        printf( "f_close failed: %d\n", res );
    }

    return FRESULT_TO_OSCODE( res );
}

static int spi_fat_fuse_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	(void) fi;
    FRESULT res;
    UINT bread = 0;

    printf( "fuse_read: %s -> %d bytes (%lld offset)\n", path, size, offset );

    FIL *fp = (FIL *)fi->fh;
    if ( fp == NULL ) {
        return ENOENT;
    }

    res = f_lseek( fp, offset );
    if ( res != FR_OK ) {
        printf( "failed to f_seek(): %d\n", res );
        return FRESULT_TO_OSCODE( res );
    }

    res = f_read( fp, buf, size, &bread );
    if ( res != FR_OK ) {
        printf( "failed to f_read(): %d\n", res );
        return FRESULT_TO_OSCODE( res );
    }

	return bread;
}

static int spi_fat_fuse_write( const char *path, const char *buf, size_t size,
                               off_t offset, struct fuse_file_info *fi )
{
    (void) fi;
    FRESULT res;
    UINT bwrite = 0;

    printf( "fuse_write: %s -> %d bytes (%lld offset)\n", path, size, offset );

    FIL *fp = (FIL *)fi->fh;
    if ( fp == NULL ) {
        return ENOENT;
    }

    res = f_lseek( fp, offset );
    if ( res != FR_OK ) {
        printf( "failed to f_seek(): %d\n", res );
        return FRESULT_TO_OSCODE( res );
    }

    res = f_write( fp, buf, size, &bwrite );
    if ( res != FR_OK ) {
        printf( "failed to f_write(): %d\n", res );
        return FRESULT_TO_OSCODE( res );
    }

    return bwrite;
}

static int spi_fat_fuse_create( const char *name, mode_t mode, struct fuse_file_info *fi ) {
    return spi_fat_fuse_open( name, fi );
}

static int spi_fat_fuse_unlink( const char *path ) {

    FRESULT res;

    printf( "fuse_unlink: %s\n", path );

    res = f_unlink( path );
    return FRESULT_TO_OSCODE( res );
}

static int spi_fat_fuse_flush( const char *path, struct fuse_file_info *fi ) {

    LAZY_MOUNT

    FRESULT res = FR_OK;

    FIL *fp = (FIL *)fi->fh;
    if ( fp == NULL ) {
        return ENOENT;
    }

    res = f_sync( fp );

    return FRESULT_TO_OSCODE( res );
}

static int spi_fat_fuse_utimens( const char *path, const struct timespec tv[2], struct fuse_file_info *fi ) {

    /** Convert 'now' FAT timestamp */
    /** tv doesn't appear to contain anything useful... */
    unsigned long fatTimestamp = nowAsFATTimestamp();

    WORD fldate, fltime;
    FATTimestampToDateTime( fatTimestamp, &fldate, &fltime );

    FILINFO finfo;
    finfo.fdate = fldate;
    finfo.ftime = fltime;

    FRESULT res = f_utime( path, &finfo );

    return FRESULT_TO_OSCODE( res );
}

static int spi_fat_fuse_chmod( const char *path, mode_t mode, struct fuse_file_info *fi ) {
    /** NOP */
    return FRESULT_TO_OSCODE( FR_OK );
}

static int spi_fat_fuse_chown( const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi ) {
    /** NOP */
    return FRESULT_TO_OSCODE( FR_OK );
}

static int spi_fat_fuse_truncate( const char *path, off_t offset, struct fuse_file_info *fi ) {
    /** NOP */
    return FRESULT_TO_OSCODE( FR_OK );
}

static const struct fuse_operations spi_fat_fuse_oper = {

    .init           = spi_fat_fuse_init,
    .flush          = spi_fat_fuse_flush,
    .getattr        = spi_fat_fuse_getattr,
    .setxattr       = spi_fat_fuse_setxattr,
    .opendir        = spi_fat_fuse_opendir,
    .readdir        = spi_fat_fuse_readdir,
    .releasedir     = spi_fat_fuse_releasedir,
    .open           = spi_fat_fuse_open,
    .read           = spi_fat_fuse_read,
    .write          = spi_fat_fuse_write,
    .create         = spi_fat_fuse_create,
    .unlink         = spi_fat_fuse_unlink,
    .truncate       = spi_fat_fuse_truncate,
    .release        = spi_fat_fuse_release,
    .utimens        = spi_fat_fuse_utimens,
    .chmod          = spi_fat_fuse_chmod,
    .chown          = spi_fat_fuse_chown,
    .mkdir          = spi_fat_fuse_mkdir,
    .rmdir          = spi_fat_fuse_rmdir
};

static void show_help(const char *progname)
{
	printf("usage: %s [options] <mountpoint>\n\n", progname);
}

int main(int argc, char *argv[])
{
	int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	/* Set defaults -- we have to use strdup so that
	   fuse_opt_parse can free the defaults if other
	   values are specified */
	options.filename = strdup("spifat");

	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;

	/* When --help is specified, first print our own file-system
	   specific help text, then signal fuse_main to show
	   additional help (by adding `--help` to the options again)
	   without usage: line (by setting argv[0] to the empty
	   string) */
	if (options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	}

	ret = fuse_main(args.argc, args.argv, &spi_fat_fuse_oper, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
