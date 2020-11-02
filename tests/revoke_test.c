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
#define SEND_TIMEOUT 2000
#define MSG "prova"

int main(int argc, char *argv[]){

	printf("Revoke messages test)\n");

	int ret;
	
	if (argc != 4){
		fprintf(stderr, "Usage: sudo %s <pathname> <major> <minor>\n", argv[0]);
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

	//Set send timeout
	ret = ioctl(filed, SET_SEND_TIMEOUT, SEND_TIMEOUT);
	if (ret == -1) {
		fprintf(stderr, "ioctl() failed\n");
		return(EXIT_FAILURE);
	}

	printf("Writing message\n");
	//Write the message
	ret = write(filed, MSG, strlen(MSG) + 1);
	if (ret == -1){
		fprintf(stderr, "Error in write\n");
		return(EXIT_FAILURE);	
	}
	printf("Message will be written in %u ms\n", SEND_TIMEOUT);

	//Invoke ioctl with REVOKE_DELAYED_MESSAGES command
	printf("Revoking delayed messages");
	ret = ioctl(filed, REVOKE_DELAYED_MESSAGES);
	if (ret == -1) {
		fprintf(stderr, "ioctl() failed\n");
		return(EXIT_FAILURE);
	}
	
	//Sleep for a duration that ensures that if the ioctl did not work a message would be found in the mq
	usleep((2*SEND_TIMEOUT)*1000);

	printf("Reading message\n");
	//Read the written message
	char to_read[MAX_MSG_SIZE];
	ret = read(filed, to_read, MAX_MSG_SIZE);
	if (ret > 0){
		printf("Message was not revoked. Message red: %s\n", to_read);
		return(EXIT_FAILURE);
	} else {
		printf("No message found in the mq as expected\n");
		return(EXIT_SUCCESS);
	}

	
}
