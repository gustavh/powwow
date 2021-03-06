/*
 *  powwow-movieplay.c  --  replay powwow movies or convert them into ASCII
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

void millisec_sleep(msec)
long msec;
{
    struct timeval t;
    t.tv_sec = msec / 1000;
    t.tv_usec = (msec % 1000) * 1000;
    select(0, NULL, NULL, NULL, &t);
}

int main(argc, argv)
int argc; char *argv[];
{
    FILE *infile, *outfile;
    char buf[4096];
    int i, play = 0;

    if (strstr(argv[0], "powwow-movieplay"))
	play = 1;
    else if (!strstr(argv[0], "powwow-movie2ascii")) {
	fprintf(stderr, "Please run this program as \"powwow-movieplay\" or \"powwow-movie2ascii\"\n");
	return 1;
    }

    if (play) {
	if (argc == 2) {
	    infile = fopen(argv[1], "rb");
	    outfile = stdout;
	    if (infile == NULL) {
		fprintf(stderr, "Error opening input file \"%s\"\n", argv[1]);
		return 1;
	    }
	} else {
	    infile = stdin;
	    outfile = stdout;
	}
    } else {
	if (argc == 3) {
	    infile = fopen(argv[1], "rb");
	    outfile = fopen(argv[2], "wb");
	    if (infile == NULL) {
		fprintf(stderr, "Error opening input file \"%s\"\n", argv[1]);
		return 1;
	    }
	    if (outfile == NULL) {
		fprintf(stderr, "Error opening output file \"%s\"\n", argv[2]);
		return 1;
	    }
	} else {
	    fprintf(stderr, "Usage: %s [infile [outfile]]\n", argv[0]);
	    return 1;
	}
    }

    while (fgets(buf, 4096, infile) != NULL) {
	i = strlen(buf);
	if (i > 0 && buf[i-1] == '\n')
	    buf[i-1] = '\0';
	if (!strncmp(buf, "sleep ", 6)) {
	    if (play)
		millisec_sleep(atoi(buf + 6));
	}
	else if (!strncmp(buf, "line ", 5))
	    fprintf(outfile, "%s\n", buf + 5);
	else if (!strncmp(buf, "prompt ", 7))
	    fprintf(outfile, "%s", buf + 7);
	else {
	    fprintf(stderr, "Syntax error in line:\n%s\n", buf);
	    return 1;
	}
	fflush(outfile);
    }
    if (feof(infile)) {
	fprintf(outfile, "\n");
	return 0;
    } else {
	fprintf(stderr, "Error reading file\n");
	return 1;
    }
}

