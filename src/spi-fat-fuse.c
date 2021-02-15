/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2021  Alligator Descartes <alligator.descartes@hermitretro.com>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
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

static FATFS FatFs;
static FIL fp;

#define MAX_FILES_PER_DIR 65536
static FILINFO fileinfo[MAX_FILES_PER_DIR];
static FILINFO *currentFileInfo;
static int nfileinfo = 0;

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

static void *spi_fat_fuse_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg)
{
	(void) conn;
	cfg->kernel_cache = 1;

    bcm2835_init();

    FRESULT res = f_mount(&FatFs, "", 0);
    if ( res != FR_OK ) {
        printf( "f_mount failed: %d\n", res );
    }

	return NULL;
}

static int spi_fat_fuse_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	(void) fi;
    FRESULT res;
    FILINFO finfo;

	memset(stbuf, 0, sizeof(struct stat));

    printf( "getattr: %s\n", path );

    if ( strcmp( path, "/" ) == 0 ) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    int ntries = 0;
    int maxtries = 1;
fstat_retry:
    res = f_stat( path, &finfo );
    if ( res != FR_OK ) {
        printf( "f_stat failed: %d\n", res );
        if ( ntries > maxtries ) {
            return FRESULT_TO_OSCODE( res );
        } else {
            ntries++;
            delay( 50 );
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

static int spi_fat_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	(void) offset;
	(void) fi;
	(void) flags;

    DIR dir;
    FRESULT res;
    FRESULT rv = FR_OK;

    int i;

    nfileinfo = 0;
    currentFileInfo = &fileinfo[0];

    res = f_opendir( &dir, path );
    if ( res != FR_OK ) {
        fprintf( stderr, "f_opendir failed: %d\n", res );
        return FRESULT_TO_OSCODE( res );
    }

    res = f_readdir( &dir, currentFileInfo );
    if ( res != FR_OK ) {
        fprintf( stderr, "f_readdir failed: %d\n", res );
        rv = res;
        goto readdir_cleanup;
    }

    while ( res == FR_OK && currentFileInfo->fname[0] ) {

        nfileinfo++;
        currentFileInfo++;

        res = f_readdir( &dir, currentFileInfo );
        if ( res != FR_OK ) {
            printf( "f_readdir failed: %d\n", res );
            rv = res;
            goto readdir_cleanup;
        }
    }

readdir_cleanup:
    res = f_closedir( &dir );
    if ( res != FR_OK ) {
        printf( "f_closedir() failed: %d\n", res );
    }

    if ( rv == FR_OK ) {
        currentFileInfo = &fileinfo[0];
        for ( i = 0 ; i < nfileinfo ; i++ ) {
            filler( buf, currentFileInfo->fname, NULL, 0, 0 );
            currentFileInfo++;
        }
    }

	return FRESULT_TO_OSCODE( rv );
}

static int spi_fat_fuse_open(const char *path, struct fuse_file_info *fi)
{
    FRESULT res;

    printf( "fuse_open: %s (mode %d)\n", path, fi->flags );

    int mode = FA_READ;
    if ( (fi->flags & O_ASYNC) == O_ASYNC ) {
        mode = FA_READ;
    }

    res = f_open( &fp, path, mode );
    if ( res != FR_OK ) {
        printf( "f_open failed: %d\n", res );
        return FRESULT_TO_OSCODE( res );
    }

	return 0;
}

static int spi_fat_fuse_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	(void) fi;
    FRESULT res;
    UINT bread = 0;

    printf( "fuse_read: %s -> %d bytes (%lld offset)\n", path, size, offset );

    res = f_lseek( &fp, offset );
    if ( res != FR_OK ) {
        printf( "failed to f_seek(): %d\n", res );
        return FRESULT_TO_OSCODE( res );
    }

    res = f_read( &fp, buf, size, &bread );
    if ( res != FR_OK ) {
        printf( "failed to f_read(): %d\n", res );
        return FRESULT_TO_OSCODE( res );
    }

	return bread;
}

static const struct fuse_operations spi_fat_fuse_oper = {
	.init       = spi_fat_fuse_init,
	.getattr	= spi_fat_fuse_getattr,
	.readdir	= spi_fat_fuse_readdir,
	.open		= spi_fat_fuse_open,
	.read		= spi_fat_fuse_read,
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