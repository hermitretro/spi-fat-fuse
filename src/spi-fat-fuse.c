/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2021  Alligator Descartes <alligator.descartes@hermitretro.com>

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

static void *spi_fat_fuse_init(struct fuse_conn_info *conn,
			struct fuse_config *cfg)
{
	(void) conn;
	cfg->auto_cache = 1;
    cfg->attr_timeout = 3600;

    bcm2835_init();

    FATFS *fatfs = malloc( sizeof( FATFS ) );
    if ( fatfs == NULL ) {
        return NULL;
    }
    memset( fatfs, 0, sizeof( FATFS ) );

    FRESULT res = f_mount( fatfs, "", 0 );
    if ( res != FR_OK ) {
        printf( "f_mount failed: %d\n", res );
        fuse_get_context()->private_data = NULL;
    }

    fuse_get_context()->private_data = (void *)fatfs;

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

static int spi_fat_fuse_opendir( const char *path, struct fuse_file_info *fi ) {

    FRESULT res;
    DIR *dir;
    
    dir = malloc( sizeof( DIR ) );
    memset( dir, 0, sizeof( DIR ) );

    res = f_opendir( dir, path );
    if ( res != FR_OK ) {
        fprintf( stderr, "f_opendir failed: %d\n", res );
        fi->fh = 0;
    } else {
        fi->fh = (uint64_t)dir;
    }

    return FRESULT_TO_OSCODE( res );
}

static int spi_fat_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
    FRESULT res;
    FRESULT rv = FR_OK;

    DIR *dir = NULL;

    int hasRDP = ((flags & FUSE_READDIR_PLUS) == FUSE_READDIR_PLUS);

    int i;

    printf( "readdir: offset: %lld, readdirplus: %d\n", offset, hasRDP );

    dir = (DIR *)fi->fh; 
    if ( dir == NULL ) {
        return -ENOENT;
    }

    unsigned int nfileinfo = offset;
    FILINFO finfo;
    memset( &finfo, 0, sizeof( FILINFO ) );

    struct stat st;
    memset( &st, 0, sizeof( st ) );
    if ( offset == 0 ) {
        nfileinfo++;    /** readdirplus offset needs to start at 1.. */
    }

    printf( "nfileinfo: %d\toffset: %d\n", nfileinfo, offset );

    /** Add default directories */
    if ( offset == 0 ) {
        st.st_mode = S_IFDIR | 0755;
        st.st_nlink = 2;
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
        rv = res;
        goto readdir_cleanup;
    }

    while ( res == FR_OK && finfo.fname[0] ) {
        memset( &st, 0, sizeof( st ) );
        if ( (finfo.fattrib & AM_DIR) == AM_DIR ) {
            st.st_mode = S_IFDIR | 0755;
            st.st_nlink = 2;
            st.st_size = 0;
        } else {
            st.st_size = finfo.fsize;
            st.st_mode = S_IFREG | 0644;
            st.st_nlink = 1;
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
            f_seekdir( dir, -1 );
            break;
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

    return FRESULT_TO_OSCODE( res );
}

static int spi_fat_fuse_open(const char *path, struct fuse_file_info *fi)
{
    FRESULT res;
    int i;

    printf( "fuse_open: %s (mode %d)\n", path, fi->flags );

    int mode = FA_READ;
    if ( (fi->flags & O_ASYNC) == O_ASYNC ) {
        mode = FA_READ;
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

static const struct fuse_operations spi_fat_fuse_oper = {
	.init       = spi_fat_fuse_init,
	.getattr	= spi_fat_fuse_getattr,
    .opendir    = spi_fat_fuse_opendir,
	.readdir	= spi_fat_fuse_readdir,
    .releasedir = spi_fat_fuse_releasedir,
	.open		= spi_fat_fuse_open,
	.read		= spi_fat_fuse_read,
    .release    = spi_fat_fuse_release
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
