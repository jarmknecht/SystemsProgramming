#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

int main (int argc, char* argv[]) {
	int fd; //Holds file descriptor
	struct stat sb; //Holds stats of the file
	char *buf; //Holds what mmap returns pointer to the mapped region
	size_t size; //Holds the offset size of the file from the stats struct

	
	fd = open("hello.txt", O_RDWR, NULL); //Opens hello.txt for reading and writing
	if (fd < 0) { //if fd is < 0 open failed
		fprintf(stderr, "Could not open file\n");
		exit(1);
	}
	if (fstat(fd, &sb) < 0) { //Calls fstat which returns info about a file and stores that info in sb
		//if fstat failed -1 will be returned
		fprintf(stderr, "Could not fstat file\n");
		exit(1);
	}
	size = sb.st_size; //Gets the total size in bytes of the file descriptor fd

	/*
	* Tells kernel to create a new area of virtual memory where
	* pages can be written and read and it is a shared object between processes containing
	* size bytes. Buf will hold the address of the new area created
	*/
	buf = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

	if (buf < 0) { //Mmap failed!
		fprintf(stderr, "Could not mmap\n");
		exit(1);
	}

	printf("%s", buf); //Reads the virtual memory area and prints it to the console
	buf[0] = 'J'; //Takes buf and writes J as the first letter in the buffer
	printf("%s", buf); //Reads the virtual memory area and prints it to the console

	if (munmap(buf, size) < 0) { //unmaps the virtual memory area that is pointed to by buf and consists of size bytes
		//if munmap fails it returns -1
		fprintf(stderr, "Could not unmap\n");
		exit (1);
	}

	if (fd != -1) { //a file needs to be closed
		if (close(fd) < 0) { //closes the text file
			//closing the file failed
			fprintf(stderr, "Could not close file\n");
			exit(1);
		}
	}
	return 0;
}