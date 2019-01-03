/*
Jonathan Armknecht
CS 324 Sec 2
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
	FILE *fp = NULL;
	char line[1024];
	int sleepTime = 0;
	const char *envVar = NULL;

	if (argc > 1) {
		if (strcmp(argv[1], "-") == 0) {
			fp = stdin;
		}
		else {
			fp = fopen(argv[1], "r");
			if (fp == NULL) {
				printf("Error! Could not open file\n");
				exit(-1);
			}
		}
	}
	else {
		fp = stdin;
	}

	fprintf(stderr, "Process ID: %d\n", getpid());

	while(fgets(line, 150, fp)) {
		fprintf(stdout, "%s", line);
		envVar = getenv("SLOWCAT_SLEEP_TIME");
		if (envVar == NULL) {
			sleepTime = 1;
		}
		else {
			sleepTime = atoi(envVar);
		}
		sleep(sleepTime);
	}

	if (fp != NULL) {
		fclose(fp);
		fp = NULL;
	}

	return 0;
}