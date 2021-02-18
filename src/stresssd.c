/*----------------------------------------------------------------------*/
/* Foolproof FatFs sample project for AVR              (C)ChaN, 2014    */
/*----------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef USE_WIRINGPI
#include <wiringPi.h>
#else
#include "bcm2835.h"
#endif
#include <unistd.h>

#include "ff.h"		/* Declarations of FatFs API */

FATFS FatFs;		/* FatFs work area needed for each volume */
FIL Fil;			/* File object needed for each open file */

int nfiles = 0;
int ndirs = 0;

FILINFO fileinfo[255];
FILINFO *currentFileInfo;
int nfileinfo = 0;

const char *expectedFilenames[] = {
    "SPOTLI~1",
    "FSEVEN~1",
    "3DDEAT~1.TZX",
    "AMAUROTE.TZX",
    "GLIDER~1.TZX",
    "_GLIDE~1.TZX",
    "STARGLD1.TAP",
    "_STARG~1.TAP",
    "SABREW~1.TAP",
    "_SABRE~1.TAP",
    "STARGL~1.TZX",
    "_STARG~1.TZX",
    "STARGL~2.TZX",
    "_STARG~2.TZX",
    "STARQU~1.TZX",
    "_STARQ~1.TZX",
    "JETPAC.TAP",
    "MANICM~1.TZX",
    "PSSST.TAP",
    "LORDSMID.TAP",
    "_LORDS~1.TAP",
    "FULLTHR1.TAP",
    "_FULLT~1.TAP",
    "ATICATAC.TAP",
    "_ATICA~1.TAP",
    "TANKDUEL.TAP",
    "_TANKD~1.TAP",
    "GRNBERET.TAP",
    "_GRNBE~1.TAP",
    "FEUD.TAP",
    "_FEU~1.TAP",
    "INFO"
};

FRESULT scan_files( char *path, UINT quiet ) {
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

int main (void)
{
	//UINT bw;
	FRESULT fr;

#ifdef USE_WIRINGPI
	if ( wiringPiSetup() == 0 ) {
		printf( "wiringPi init'd ok\n" );
	}
    piHiPri( 99 );
#else
    bcm2835_init();
#endif

	f_mount(&FatFs, "", 0);		/* Give a work area to the default drive */

#ifdef PANTS
	fr = f_open(&Fil, "newfile.txt", FA_WRITE | FA_CREATE_ALWAYS);	/* Create a file */
	if (fr == FR_OK) {
		f_write(&Fil, "It works!\r\n", 11, &bw);	/* Write data to the file */
		fr = f_close(&Fil);							/* Close the file */
		if (fr == FR_OK && bw == 11) {		/* Lights green LED if data written well */
			printf( "ok\n" );
		}
		return 0;
	} else {
		printf( "it doesn't work: %d\n", fr );
	}
#endif

    int quiet = 1;

    int niterations = 10000;
    int nmatches = 0;
    int nmismatches = 0;
    int ncorruptions = 0;
    int i = 0, j = 0;

    for ( i = 0 ; i < niterations ; i++ ) {
	    fr = scan_files( "/", 1 );
	    if ( fr == FR_OK ) {
		    printf( "file scan[%d] ok: %d dirs, %d files\n", i, ndirs, nfiles );

            if ( ndirs == 5 && nfiles == 32 ) {
                printf( "-> scan_files ok\n" );

                /** Check filenames */
                UINT hasCorruption = 0;
                for ( j = 0 ; j < 32 ; j++ ) {
                    if ( strcmp( expectedFilenames[j], fileinfo[j].fname ) != 0 ) {
                        printf( "filename fail[%d]: '%s' != expected '%s'\n", j, fileinfo[j].fname, expectedFilenames[j] );
                        hasCorruption = 1;
                        ncorruptions++;
                    }
                }
                if ( hasCorruption ) {
                    printf( "!! corrupt filenames\n" );
                } else {
                    printf( "-> correct filenames\n" );
                }

                if ( !quiet ) {
                    for ( j = 0 ; j < nfileinfo ; j++ ) {
                        printf( "%s, %d\n", fileinfo[j].fname, ((fileinfo[j].fattrib & AM_DIR) == AM_DIR) );
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

    fr = f_open( &Fil, "/GLIDER~1.TZX", FA_READ );
	if ( fr == FR_OK ) {
		printf( "fopen ok\n" );

		unsigned char *buf = malloc( 256 * 1024 );
		UINT bytesRead = 0;
		fr = f_read( &Fil, buf, 256 * 1024, &bytesRead );
		if ( fr == FR_OK ) {
			printf( "fread ok: %d bytes read\n", bytesRead );
		} else {
			printf( "fread failed: %d\n", fr );
		}

		fr = f_close( &Fil );
		if ( fr == FR_OK ) {
			printf( "fclose ok\n" );
		} else {
			printf( "fclose failed\n" );
		}
	} else {
		printf( "fopen failed: %d\n", fr );
	}

	return 1;
}

