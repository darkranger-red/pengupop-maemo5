/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |     ___|    ____| |    \    PSPDEV Open Source Project.
#-----------------------------------------------------------------------
#
# $Id: bin2c.c 210 2005-06-18 17:52:59Z mrbrown $
*/

/*
 *  Copyright (c) 2005  adresd
 *  Copyright (c) 2005  Marcus R. Brown
 *  Copyright (c) 2005  James Forshaw
 *  Copyright (c) 2005  John Kelley
 *  Copyright (c) 2005  Jesper Svennevid
 *  All rights reserved.
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The names of the authors may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 *  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 *  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

unsigned char *buffer;

int main(int argc, char *argv[])
{
	int fd_size;
	FILE *source,*dest;
	int i;

	if(argc != 4) {
		printf("bin2c - from bin2s By Sjeep\n"
			   "Usage: bin2c infile outfile label\n\n");
		return 1;
	}

	if((source=fopen( argv[1], "rb")) == NULL) {
		printf("Error opening %s for reading.\n",argv[1]);
		return 1;
	}

	fseek(source,0,SEEK_END);
	fd_size = ftell(source);
	fseek(source,0,SEEK_SET);

	buffer = malloc(fd_size);
	if(buffer == NULL) {
		printf("Failed to allocate memory.\n");
		return 1;
	}

	if(fread(buffer,1,fd_size,source) != fd_size) {
		printf("Failed to read file.\n");
		return 1;
	}
	fclose(source);

	if((dest = fopen(argv[2],"w+")) == NULL) {
		printf("Failed to open/create %s.\n",argv[2]);
		return 1;
	}
	
	fprintf(dest, "#ifndef __%s__\n", argv[3]);
	fprintf(dest, "#define __%s__\n\n", argv[3]);
	fprintf(dest, "static unsigned int size_%s = %d;\n", argv[3], fd_size);
	fprintf(dest, "static unsigned char %s[] __attribute__((aligned(16))) = {", argv[3]);

	for(i=0;i<fd_size;i+=1) {
		if((i % 16) == 0) fprintf(dest, "\n\t");
		fprintf(dest, "0x%02x, ", buffer[i]);
	}

	fprintf(dest, "\n};\n\n#endif\n");

	fclose(dest);

	return 0;
}
