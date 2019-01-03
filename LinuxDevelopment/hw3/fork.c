#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/wait.h>

int main(int argc, char *argv[]) {
	int pid;
	FILE *fp;
	FILE *stream;
	int myPipe[2];
	char line[1024];
	char *newenviron[] = { NULL };

	if (pipe(myPipe) == -1) {
		fprintf(stderr, "Could not pipe()");
		exit(1);
	}

	printf("Starting program; process has pid %d\n", getpid());

	fp = fopen("fork-output.txt", "w");
	if (fp == NULL) {
		printf("Error! Couldn't write to file\n");
		exit(1);
	}
	fprintf(fp, "BEFORE FORK\n");
	fflush(fp);
	if ((pid = fork()) < 0) {
		fprintf(stderr, "Could not fork()");
		exit(1);
	}

	/* BEGIN SECTION A */
	printf("Section A;  pid %d\n", getpid());
	fprintf(fp, "SECTION A\n");
	//sleep(30);

	/* END SECTION A */
	if (pid == 0) {
		/* BEGIN SECTION B */
		printf("Section B\n");
		fprintf(fp, "SECTION B\n");
		close(myPipe[0]); //Close reading end of the pipe
		stream = fdopen(myPipe[1], "w"); //Open the write end of pipe and write to it
		fputs("hello from Section B\n", stream);
		
		if (stream != NULL) {
			fclose(stream);
		}
		//sleep(30);
		//sleep(30);
		printf("Section B done sleeping\n");

		dup2(fileno(fp), STDOUT_FILENO);
		if (fp != NULL) {
			fclose(fp);
		}
		//THIS CODE RUNS THE EXEC.C code
		printf("Program \"%s\" has pid %d. Sleeping.\n", argv[0], getpid());
		//sleep(30);

		if (argc <= 1) {
			printf("No program to exec.  Exiting...\n");
			exit(0);
		}

		printf("Running exec of \"%s\"\n", argv[1]);
		execve(argv[1], &argv[1], newenviron);
		printf("End of program \"%s\".\n", argv[0]);
		//ADDED EXEC.C code ends here
		exit(0);

		/* END SECTION B */
	} else {
		/* BEGIN SECTION C */

		printf("Section C\n");
		fprintf(fp, "SECTION C\n");
		waitpid(-1, NULL, 0);
		close(myPipe[1]); //Close the write end of the pipe
		stream = fdopen(myPipe[0], "r"); //Read from read end of pipe
		if (fgets(line, 250, stream)) {
			fprintf(stdout, "%s", line);
		}
		if (stream != NULL) {
			fclose(stream);
		}
		//sleep(30);
		//sleep(30);
		printf("Section C done sleeping\n");

		if (fp != NULL) {
			fclose(fp);
		}
		exit(0);

		/* END SECTION C */
	}
	/* BEGIN SECTION D */

	printf("Section D\n");
	fprintf(fp, "SECTION D\n");
	//sleep(30);

	/* END SECTION D */
}
