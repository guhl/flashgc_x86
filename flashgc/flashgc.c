/*
 *  Copyright (C) 2011  skorgon
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  The vital parts of this program are copied from gfree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "gopt.h"
#include "gcgen.h"

#define SDCARDDEV	"/dev/block/mmcblk1"
#define DEFCIDPATH	"/sys/block/mmcblk1/device/cid"

int writePartition(uint8_t* buf, int bufSize, const char* pPartition);
int backupMbr(const char* pPartition, const char* pBackupFile);
void printHelp(const char* exec);
int reverseCid(const char* cidFile, uint8_t* cid);

int main(int argc, const char* argv[])
{
	int restore = 0;
	int ret = 0;
	int backup = 0;
	char* backupFile;
	uint8_t cid[0x18];
	char* s_cid;
	char* cidPath;
        char* outPath;
	uint8_t* gcbuf;
	int gcSize = 0;

	void* options = gopt_sort(&argc, argv, gopt_start(
				gopt_option('h', 0, gopt_shorts('h'), gopt_longs("help")),
				gopt_option('c', GOPT_ARG, gopt_shorts('c'), gopt_longs("cid-file")),
                gopt_option('f', GOPT_ARG, gopt_shorts('f'), gopt_longs("cid-forward")),
                gopt_option('e', GOPT_ARG, gopt_shorts('e'), gopt_longs("cid-reverse")),
				gopt_option('o', GOPT_ARG, gopt_shorts('o'), gopt_longs("out")),
				gopt_option('b', GOPT_ARG, gopt_shorts('b'), gopt_longs("backupfile")),
				gopt_option('d', 0, gopt_shorts('d'), gopt_longs("dumpgc")),
				gopt_option('r', GOPT_ARG, gopt_shorts('r'), gopt_longs("restore"))
				));

	/* print help text and exit if help option is given */
	if (gopt(options, 'h')) {
		printHelp(argv[0]);
		goto cleanup;
	}

	/* restore backup if restore option is given */
	if (gopt_arg(options, 'r', &backupFile)) {
		restore = 1;
	} else if (gopt_arg(options, 'b', &backupFile)) {
		/* Get backup file name from command line option or set default */
		backup = 1;
	}

	if (!gopt_arg(options, 'o', &outPath))
		outPath = SDCARDDEV;

	if (gopt_arg(options, 'f', &s_cid)) {
		/* Get normal CID from option and reverse it */
		size_t size;
		size = strlen(s_cid);
		if(size != 32) {
			printf("Error: CID must be a 18 character string. Length of specified string: %d\n", (int)size);
			exit(1);
	    	}
		int i;
		for (i=0; i<31; i+=2){
			cid[i]=s_cid[30-i];
			cid[i+1]=s_cid[31-i];
		}
        cid[32] = 0;
	} else if (gopt_arg(options, 'e', &s_cid)) {
       	/* Get reverse CID from option */
		size_t size;
		size = strlen(s_cid);
		if(size != 32) {
			printf("Error: reverse CID must be a 18 character string. Length of specified string: %d\n", (int)size);
			exit(1);
	    	}
			int i;
            for (i=0; i<32; i++) {
            	cid[i] = s_cid[i];
            }
            cid[32] = 0;
	} else if (gopt_arg(options, 'c', &cidPath)) {
       	/* Get CID from file */
       	if ((ret = reverseCid(cidPath, cid)))
       		goto cleanup;
	} else {
       	/* Get sdcard CID */
       	cidPath = DEFCIDPATH;
       	if ((ret = reverseCid(cidPath, cid)))
       		goto cleanup;
	}
	printf("Reverse CID: %s\n", (char *) cid);


	if (restore) {
		printf("Restoring backup image from \"%s\".\n", backupFile);
		/* Actual a restore!! */
		if (backupMbr(backupFile, outPath)) {
			printf("ERROR: Restoring the backup failed. SD-card may be corrupted.\n");
			ret = -1;
			goto cleanup;
		}
		printf("SUCCESS: Backup restored.\n");
	} else {		
		/* Generate the gold card image */
		genGc(cid, 0, &gcbuf, &gcSize);

		/* Dump gc image if requested */
		if (gopt(options, 'd')) {
			printf("Dumping gold-card image to file \"goldcard.img\".\n");
			FILE* fout = fopen("goldcard.img", "wb");
			if (fout == NULL) {
				printf("ERROR: Unable to open file \"goldcard.img\".\n");
			} else {
				int i;
				for (i = 0; i < gcSize; i++)
					fputc(gcbuf[i], fout);
				fclose(fout);
			}
		}

		/* Create backup of sdcard MBR */
		if (backup) {
			printf("Backing up sd-card MBR to file \"%s\".\n", backupFile);
			if (backupMbr(outPath, backupFile)) {
				printf("ERROR: Backing up the sd-card's MBR failed.\n");
				ret = -1;
				goto cleanup;
			}
		}

		/* Write gold-card to sd-card */
		printf("Making sd-card golden.\n");
		if (writePartition(gcbuf, gcSize, outPath)) {
			printf
			    ("ERROR: Writing to sd-card failed. SD-card may be corrupted. :(\n");
			ret = -1;
			goto cleanup;
		}
		printf("SUCCESS: SD-card is golden.\n");
	}

cleanup:
	gopt_free(options);
	return ret;
}

/*
 * writePartition function copied from gfree and modified to overwrite only
 * the first 512 bytes of the sd-card at max
 */
int writePartition(uint8_t* buf, int bufSize, const char* pPartition)
{
	int i;
	FILE* fdout;
	int ret = 0;

	printf("Writing gold-card to sd-card (%s) ...\n", pPartition);

	if (bufSize > 512) {
		printf("ERROR: Image exceeds 512 byte boundary.\n");
		return -1;
	}

	fdout = fopen(pPartition, "wb");
	if (fdout == NULL) {
		printf("ERROR: Opening output partition failed.\n");
		return -1;
	}

	//  copy the image to the partition
	for (i = 0; i < bufSize; i++) {
		fputc(buf[i], fdout);
		if (ferror(fdout)) {
			printf("ERROR: Writing to output partition failed.\n");
			ret = 1;
			break;
		}
	}

	if (fclose(fdout) == EOF) {
		printf("ERROR: Closing output partition failed.\n");
		ret = 1;
	}

	return ret;
}

/*
 * backupPartition function copied from gfree and modified to only backup
 * the first 512 bytes of the partition
 */
int backupMbr(const char* pPartition, const char* pBackupFile)
{
	FILE* fdin;
	FILE* fdout;
	char ch;
	int bytec;
	int ret = 0;

	fdin = fopen(pPartition, "rb");
	if (fdin == NULL) {
		printf("ERROR: Opening input file failed.\n");
		return -1;
	}

	fdout = fopen(pBackupFile, "wb");
	if (fdout == NULL) {
		printf("ERROR: Opening output file failed.\n");
		ret = -1;
		goto cleanup2;
	}

//  create a copy of the partition
	bytec = 0;
	while (!feof(fdin) && (bytec < 512)) {
		ch = fgetc(fdin);
		if (ferror(fdin)) {
			printf("ERROR: Reading from input file failed.\n");
			ret = 1;
			goto cleanup1;
		}
		if (!feof(fdin))
			fputc(ch, fdout);
		if (ferror(fdout)) {
			printf("ERROR: Writing to output file failed.\n");
			ret = 1;
			goto cleanup1;
		}
		bytec++;
	}

cleanup1:
	if (fclose(fdout) == EOF) {
		printf("ERROR: Closing output file failed.\n");
		ret = 1;
	}

cleanup2:
	if (fclose(fdin) == EOF) {
		printf("ERROR: Closing input file failed.\n");
		ret = 1;
	}

	return ret;
}

void printHelp(const char* exec)
{
	printf("Usage:\n\t%s [options]\n", exec);
	printf("Options:\n");
	printf("\t--help\t\t\t\tPrint this help message and exit.\n");
	printf("\t--cid <cid>\t\t\tRead CID from file <cid>.\n");
	printf("\t--out <out>\t\t\tWrite to file <out>.\n");
	printf("\t--backupfile <file>\t\tSpecify a path and file for the backup.\n");
	printf("\t--restore <backup image>\tRestore backup image to sd-card.\n");
	printf("\t--dumpgc\t\t\tDump gold-card image to a file.\n");
}

int reverseCid(const char* cidFile, uint8_t* cid)
{
	FILE* fdin;
	char buf[0x18*2 + 1];
	char* ch;
	int i = 0;

	fdin = fopen(cidFile, "r");
	if (fdin == NULL) {
		printf("ERROR: Opening CID file failed.\n");
		return -1;
	}

	fgets(buf, 0x18*2 + 1, fdin);
	ch = buf;
	while ((*ch != '\0') && (*ch != '\n'))
		ch++;

	while(ch > buf) {
		*ch = '\0';
		ch -= 2;
		cid[i++] = strtol(ch, NULL, 16);
	}

	if (fclose(fdin) == EOF) {
		printf("ERROR: Closing CID file failed.\n");
		return -1;
	}

	return 0;
}
