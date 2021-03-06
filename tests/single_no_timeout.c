#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include "../timed-messaging.h"

//Use here the max_msg_size param used when the module was inserted (4096 default value)
#define MAX_MSG_SIZE 4096

int main(int argc, char *argv[]){

	printf("Single no timeout test: 1 reader and 1 writer, no timeouts (immediate write immediate read)\n");

	int ret;
	
	if (argc != 5){
		fprintf(stderr, "Usage: sudo %s <pathname> <major> <minor> <message>\n", argv[0]);
		return(EXIT_FAILURE);
	}

	unsigned int major, minor;
	major = strtoul(argv[2], NULL, 0);
	minor = strtoul(argv[3], NULL, 0);

	//Create a chdev with given major and minor
	ret = mknod(argv[1], S_IFCHR, makedev(major, minor));
	if (ret == -1){
		fprintf(stderr, "Unable to create chdev\n");
		return(EXIT_FAILURE);
	}
	printf("Chdev successfully created!\n");

	//Open file
	int filed;
	filed = open(argv[1], O_RDWR);
	if (filed == -1){
		fprintf(stderr, "Error opening file\n");
		return(EXIT_FAILURE);
	}
	printf("Open succesfull\n");

	printf("Writing message\n");
	//Write the message
	ret = write(filed, argv[4], strlen(argv[4]) + 1);
	if (ret == -1){
		fprintf(stderr, "Error in write");
		return(EXIT_FAILURE);	
	}
	printf("message: %s successfully written\n", argv[4]);

	printf("Reading message\n");
	//Read the written message
	char to_read[MAX_MSG_SIZE];
	ret = read(filed, to_read, MAX_MSG_SIZE);
	if (ret > 0){
		printf("Message red: %s\n", to_read);
		return(EXIT_SUCCESS);
	} else {
		printf("Unexpected behaviour, read returned: %d\n", ret);
		return(EXIT_FAILURE);
	}

	
}
