/**
 * Stress test program for SDMM/FatFS handling
 *
 * Copyright (c)2021 Hermit Retro Products Ltd. <https://hermitretro.com>
 *
 * This file is part of spi-fat-fuse.
 * 
 *     spi-fat-fuse is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.
 * 
 *     spi-fat-fuse is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with spi-fat-fuse.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bcm2835.h"
#include "ff.h"		/* Declarations of FatFs API */

FATFS fatfs;		/* FatFs work area needed for each volume */

int nfiles = 0;
int ndirs = 0;

/** Size of each test file in bytes */
#define RANDOMBUFSZ (48 * 1024)

#define MAXFILES 32
FILINFO fileinfo[MAXFILES];
FILINFO *currentFileInfo;
int nfileinfo = 0;

char expectedFilenames[MAXFILES][255] = { 0 };
uint64_t expectedChecksums[MAXFILES] = { 0 };

FRESULT scan_files( const char *path, UINT quiet ) {
	DIR dir;
	FRESULT res;

    nfiles = ndirs = 0;
    currentFileInfo = &fileinfo[0];
    nfileinfo = 0;

	if ( ( res = f_opendir( &dir, path ) ) == FR_OK ) {
		res = f_readdir( &dir, currentFileInfo );
        if ( res != FR_OK ) {
            printf( "f_readdir failed: %d\n", res );
            goto cleanup;
        }
		while ( res == FR_OK && currentFileInfo->fname[0] ) {
		    if ( currentFileInfo->fattrib & AM_DIR ) {
                if ( !quiet ) {
		            printf( ">>> %s\n", currentFileInfo->fname );
                }
                ndirs++;
			} else {
                if ( !quiet ) {
			        printf( "%s\n", currentFileInfo->fname );
                }
                nfiles++;
			}
            nfileinfo++;
            currentFileInfo++;

			res = f_readdir( &dir, currentFileInfo );
            if ( res != FR_OK ) {
                printf( "f_readdir failed: %d\n", res );
                goto cleanup;
            }
		}

cleanup:
        res = f_closedir( &dir );
        if ( res != FR_OK ) {
            printf( "f_closedir() failed: %d\n", res );
        } else {
            if ( !quiet ) {
                printf( "f_closedir() ok\n" );
            }
        }
	} else {
		printf( "f_opendir failed: %d\n", res );
		return res;
	}

	return FR_OK;
}

/**
 * Create X test files named '0000.DAT' with random data. Also store the
 * total of the contents.
 *
 * Returns: number of files created
 */
int create_test_files( const char *parentdir, int nfilesToCreate ) {

    FRESULT res;
    FIL fp;
    char filename[255];

    int i, j;

    unsigned char randombuf[RANDOMBUFSZ];
    unsigned char *randombufptr = randombuf;

    /** Create a test directory */
    res = f_mkdir( parentdir );
    if ( res == FR_OK ) {
        printf( "mkdir ok\n" );
    } else {
        printf( "mkdir failed: %d\n", res );
        return 1;
    }

    /** Create the files within the test directory */
    int nfilesCreated = 0;
    for ( i = 0 ; i < nfilesToCreate ; i++ ) {
        sprintf( filename, "%04d.DAT", i );
        strcpy( expectedFilenames[i], filename );

        char path[255];
        sprintf( path, "%s/%s", parentdir, filename );

	    res = f_open( &fp, path, (FA_WRITE | FA_CREATE_ALWAYS) );
	    if ( res == FR_OK ) {
            /** Fill the file with random data */
            FILE *devu = fopen( "/dev/urandom", "rb" );
            randombufptr = randombuf;
            for ( j = 0 ; j < RANDOMBUFSZ ; j++ ) {
                int rv = fread( randombufptr, 1, 1, devu );
                if ( rv != 1 ) {
                    printf( "random read failed. data will be junk\n" );
                    break;
                }
                expectedChecksums[i] += *randombufptr;
                randombufptr++; 
            }

            UINT bw;
		    res = f_write( &fp, randombuf, RANDOMBUFSZ, &bw );
            if ( res == FR_OK ) {
                if ( bw == RANDOMBUFSZ ) {
                    printf( "write ok\n" );
                } else {
                    printf( "write operation ok but wrong data size written: %d (should be %d)\n", bw, RANDOMBUFSZ );
                }
            } else {
                printf( "write failed: %d\n", res );
            }

            fclose( devu );

		    res = f_close( &fp );
		    if ( res == FR_OK ) {
			    printf( "fclose ok\n" );
                nfilesCreated++;
	    	} else {
                printf( "fclose failed\n" );
            }
	    } else {
		    printf( "it doesn't work: %d\n", res );
            break;
	    }
    }

    return nfilesCreated;
}

int remove_test_files( const char *parentdir ) {

    FRESULT res;
    FILINFO lfileinfo;
    int i;
    char filename[255];

    res = f_stat( parentdir, &lfileinfo );
    printf( "f_stat: %d\n", res );
    if ( res == FR_OK ) {
        /** Something exists.... */
        if ( (lfileinfo.fattrib & AM_DIR) == AM_DIR ) {
            /** ...and is a directory.. */
            /** Remove all the files first... */
            res = scan_files( parentdir, 0 );
            if ( res == FR_OK ) {
                for ( i = 0 ; i < nfileinfo ; i++ ) {
                    printf( "removing[%d]: %s\n", i, fileinfo[i].fname );
                    sprintf( filename, "%s/%s", parentdir, fileinfo[i].fname );
                    res = f_unlink( filename );
                    if ( res == FR_OK ) {
                        printf( "unlinked[%d]: %s\n", i, filename );
                    } else {
                        printf( "failed to unlink[%d]: %s -> %d\n", i, filename, res );
                        return 1;
                    }
                }

                /** Finally, unlink the parent directory.. */
                res = f_unlink( parentdir );
                if ( res == FR_OK ) {
                    printf( "parentdir unlink ok\n" );
                } else {
                    printf( "parentdir unlink failed: %d\n", res );
                    return 1;
                }
            } else {
                printf( "failed to scan files for removal: %d\n", res );
                return 1;
            }
        } else {
            /** ...it's a file */
            res = f_unlink( parentdir );
            if ( res == FR_OK ) {
                printf( "unlink ok\n" );
            } else {
                printf( "unlink failed: %d\n", res );
                return 1;
            }
        }
    } else {
        printf( "failed to stat parent directory for removal: %d\n", res );
        if ( res == FR_NO_FILE ) {
            printf( "...but this is ok because it can't be found\n" );
            return 0;
        } else {
            printf( "...f_stat failed for a more severe reason: %d\n", res );
            if ( res == FR_NOT_READY ) {
                printf( "...SD card not ready\nThis is possibly due to a previously incomplete run\nRetry in 60 seconds\n" );
            }
        }
        return 1;
    }

    return 0;
}

int main (void)
{
	FRESULT res;

    if ( bcm2835_init() ) {
        printf( "bcm2835 init ok\n" );
    } else {
        printf( "bcm2835 failed to init. fatal\n" );
        exit( 1 );
    }

	res = f_mount( &fatfs, "", 1 );
    if ( res != FR_OK ) {
        printf( "failed to mount drive: %d\n", res );
        printf( "Is the SD card formatted as FAT (not FAT32 or ExFAT)\n" );
        printf( "Try ejecting the card and re-inserting it\n" );
    } else {
        printf( "drive mounted ok\n" );
    }

    /** Remove any stale test files */
    int rv = remove_test_files( "/STRESSSD" );
    if ( rv ) {
        printf( "failed to remove test files satisfactorially...\n" );
        exit( 1 );
    } else {
        printf( "removed test files ok...\n" );
    }

    /** Create the test files */
    nfiles = create_test_files( "/STRESSSD", MAXFILES );
    printf( "created %d test files. expected: %d\n", nfiles, MAXFILES );

    if ( nfiles != MAXFILES ) {
        printf( "failed to create expected number of files\n" );
        exit( 1 );
    }

    /** Scan the directory and check the file integrity */
    int quiet = 1;

    int niterations = 10;
    int nmatches = 0;
    int nmismatches = 0;
    int ncorruptions = 0;
    int i = 0, j = 0, k = 0;

    for ( i = 0 ; i < niterations ; i++ ) {
	    res = scan_files( "/STRESSSD", quiet );
	    if ( res == FR_OK ) {
		    printf( "file scan[%d] ok: %d dirs, %d files\n", i, ndirs, nfiles );

            if ( ndirs == 0 && nfiles == MAXFILES ) {
                printf( "-> scan_files ok\n" );

                /** Check filenames haven't corrupted */
                UINT hasCorruption = 0;
                for ( j = 0 ; j < MAXFILES ; j++ ) {
                    if ( strcmp( expectedFilenames[j], fileinfo[j].fname ) != 0 ) {
                        printf( "filename fail[%d]: '%s' != expected '%s'\n", j, fileinfo[j].fname, expectedFilenames[j] );
                        hasCorruption = 1;
                        ncorruptions++;
                    }

                    if ( hasCorruption ) {
                        printf( "!! corrupt filename\n" );
                        printf( "!! skipping file contents check\n" );
                    } else {
                        printf( "-> correct filenames\n" );

                        /** Check file sizes */
                        if ( fileinfo[j].fsize != RANDOMBUFSZ ) {
                            printf( "!! corrupt file size: %d != %d\n", fileinfo[j].fsize, RANDOMBUFSZ );
                        } else {
                            printf( "filesize check ok\n" );

                            /** Check file contents checksum */
                            FIL fp;
                            uint64_t checksum = 0;
                            char path[255];
                            sprintf( path, "/STRESSSD/%s", fileinfo[j].fname );
                            res = f_open( &fp, path, FA_READ );
                            if ( res != FR_OK ) {
                                printf( "failed to open file for integrity check: %d\n", res );
                            } else {
                                unsigned char buf[RANDOMBUFSZ];
                                unsigned char *bufptr = buf;

                                UINT br = 0;
                                for ( k = 0 ; k < RANDOMBUFSZ ; k++ ) {
                                    res = f_read( &fp, bufptr, 1, &br );
                                    if ( res != FR_OK && br != 1 ) {
                                        printf( "failed to read file: %d\n", res );
                                    } else {
                                        checksum += *bufptr;
                                    }
                
                                    bufptr++;
                                } 

                                res = f_close( &fp );
                                if ( res != FR_OK ) {
                                    printf( "failed to close file after integrity check: %d\n", res );
                                }

                                if ( checksum == expectedChecksums[j] ) {
                                    printf( "file integrity check passed: %lld == %lld\n", checksum, expectedChecksums[j] );
                                } else {
                                    printf( "!! file integrity check failed : %lld got != %lld expected\n", checksum, expectedChecksums[j] );
                                }
                            }
                        }
                    }
                }

                if ( !quiet ) {
                    for ( j = 0 ; j < nfileinfo ; j++ ) {
                        printf( "%s, %d, %lld\n", fileinfo[j].fname, fileinfo[j].fsize, expectedChecksums[j] );
                    }
                }
                nmatches++;
            } else {
                printf( "!! scan_files mismatch\n" );
                nmismatches++;
            }
	    } else {
		    printf( "file scan[%d] failed\n", i );
	    }

//        sleep( 1 );
    }

    printf( "Scan Results: %d matches, %d mismatches, %d corruptions, %d total\n", nmatches, nmismatches, ncorruptions, niterations );

    /** Tidy up */
    remove_test_files( "/STRESSSD" );

    res = f_mount( NULL, "", 0 );
    if ( res == FR_OK ) {
        printf( "unmounted volume ok\n" );
    } else {
        printf( "failed to unmount volume: %d\n", res );
    }

	return 1;
}

