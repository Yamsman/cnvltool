#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

/*
 * Archive format
 *
 * 0x2B bytes: Magic number
 * 0x01 bytes: Encryption key
 * 0x04 bytes: File count
 * 0x04 bytes: File table position
 * 0x** bytes: File data
 * File table:
 * 	0x27 bytes: Filename
 * 	0x01 bytes: Offset
 * 	0x04 bytes: File position + value of 'offset'
 * 	0x04 bytes: File size + value of 'offset'
 *
 * File table entries contain an 'offset' which is used to obfuscate
 * the file size/file position values. To calculare the original 
 * value, simply subtract the offset.
 *
 * File data is encrypted with XOR using a 1-byte key.
 * This is ignored for .ogg and .png files.
 */

struct HEADER {
	char magic[0x2B];
	unsigned char key;
	unsigned int fcount;
	unsigned int ft_pos;
};

struct FT_ENTRY {
	char fname[0x27];
	char encofs;
	unsigned int fpos;
	unsigned int fsize;
};

void dump(char *ifname, char *dir) {
	FILE *input_f = fopen(ifname, "rb");
	if (!input_f) {
		printf("ERROR: Could not open archive '%s'\n", ifname);
		return;
	}

	//Read header
	struct HEADER head;
	fread(&head, sizeof(struct HEADER), 1, input_f);
	//TODO: verify
	
	//Read file table
	struct FT_ENTRY *ftable = malloc(sizeof(struct FT_ENTRY)*head.fcount);
	fseek(input_f, head.ft_pos, SEEK_SET);
	fread(ftable, sizeof(struct FT_ENTRY), head.fcount, input_f);

	/*
	for (int i=0; i<head.fcount; i++)
		printf("%i\n", ftable[i].encofs);
	*/

	//Change to output directory
	if (mkdir(dir, 0777) && errno != EEXIST) {
		perror("ERROR: Could not create directory: ");
		fclose(input_f);
		return;
	}
	if (chdir(dir)) {
		perror("ERROR: Could not change directory: ");
		fclose(input_f);
		return;
	}
	
	//Dump files from archive
	for (int i=0; i<head.fcount; i++) {
		//Fix obfuscated fpos/fsize
		ftable[i].fpos -= ftable[i].encofs;
		ftable[i].fsize -= ftable[i].encofs;

		//Open output file
		FILE *output_f = fopen(ftable[i].fname, "wb");
		if (!output_f) {
			printf("ERROR: Could not open file %s\n", ftable[i].fname);
			free(ftable);
			return;
		}

		unsigned char *buf = malloc(ftable[i].fsize);
		fseek(input_f, ftable[i].fpos, SEEK_SET);
		fread(buf, ftable[i].fsize, 1, input_f);
		for (int i=0; i<ftable[i].fsize; i++) buf[i] ^= head.key;
		fwrite(buf, ftable[i].fsize, 1, output_f);

		free(buf);
		fclose(output_f);
	}

	free(ftable);
	fclose(input_f);
	return;
}

void pack(char *dname, char *ofname) {
	//Open output file
	FILE *output_f = fopen(ofname, "wb");
	if (!output_f) {
		printf("ERROR: Could not open archive '%s'\n", ofname);
		return;
	}

	//Change directory
	if (chdir(dname)) {
		perror("ERROR: Could not change directory: ");
		return;
	}

	//Allocate space for the file table
	struct FT_ENTRY *ftable = malloc(sizeof(struct FT_ENTRY)*16);
	int ft_count = 0;
	int ft_max = 10;

	//Write dummy header
	struct HEADER head = {0};
	strncpy(head.magic, "PackPlus", 0x2A);
	fwrite(&head, sizeof(struct HEADER), 1, output_f);

	//Query all files in the directory and add their data to the archive
	DIR *dir = opendir(".");
	struct dirent *entry;
	while (entry = readdir(dir)) {
		struct stat st;
		stat(entry->d_name, &st);
		if (!S_ISREG(st.st_mode)) continue;

		//Create file table entry
		int i = ft_count++;
		if (ft_count == ft_max) {
			ft_max *= 2;
			ftable = realloc(ftable, ft_max*sizeof(struct FT_ENTRY));
		}
		memset(ftable[i].fname, 0x00, 0x27);
		strncpy(ftable[i].fname, entry->d_name, 0x26); //TODO: check length
		ftable[i].encofs = 0x00; //Disable obfuscation
		ftable[i].fpos = ftell(output_f);
		ftable[i].fsize = st.st_size;

		//Read file data
		FILE *input_f = fopen(entry->d_name, "rb");
		if (!input_f) {
			printf("ERROR: Could not open file '%s'\n", ofname);
			return;
		}

		unsigned char *buf = malloc(st.st_size);
		fread(buf, st.st_size, 1, input_f);
		fwrite(buf, st.st_size, 1, output_f);

		free(buf);
		fclose(input_f);
	}
	closedir(dir);

	//Write file table to archive
	int ft_pos = ftell(output_f);
	fwrite(ftable, sizeof(struct FT_ENTRY), ft_count, output_f);

	//Rewrite completed header
	head.key = 0x00; //Disable encryption
	head.fcount = ft_count;
	head.ft_pos = ft_pos;
	fseek(output_f, 0, SEEK_SET);
	fwrite(&head, sizeof(struct HEADER), 1, output_f);

	free(ftable);
	fclose(output_f);
	return;
}

int main(int argc, char **argv) {
	if (argc < 4 || (argc == 2 && !strcmp(argv[1], "-h"))) {
		printf("USAGE:\tcnvltool dump [archive] [folder]\tdumps archive to folder\n"
		       "\tcnvltool pack [folder] [archive]\tcreates archive from folder\n");
		return 0;
	}
	
	if (!strcmp(argv[1], "dump")) {
		dump(argv[2], argv[3]);
	} else if (!strcmp(argv[1], "pack")) {
		pack(argv[2], argv[3]);
	} else {
		printf("ERROR: Invalid mode '%s'\n", argv[1]);
	}

	return 0;
}
