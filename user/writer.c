#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>
#include "../timed-messaging.h"

#define MAX_MESSAGE_SIZE 128

int main(int argc, char *argv[]){

	int filed, ret;
	char msg[MAX_MESSAGE_SIZE];
	
	if (argc != 3){
		fprintf(stderr, "Usage: sudo %s <filename> <send_timeout>\n", argv[0]);
		return(EXIT_FAILURE);
	}

	printf("pid: %d\n", getpid());

	//Open file
	filed = open(argv[1], O_RDWR);
	if (filed == -1){
		fprintf(stderr, "open() failed: %s\n", strerror(errno));
		return(EXIT_FAILURE);
	}

	// Set the send timeout
	unsigned long send_timeout;
	send_timeout = strtoul(argv[2], NULL, 0);
	ret = ioctl(filed, SET_SEND_TIMEOUT, send_timeout);
	if (ret == -1) {
		fprintf(stderr, "ioctl() failed: %s\n", strerror(errno));
		return(EXIT_FAILURE);
	}

	while (1) {
		fputc('>', stdout);
		fflush(stdout);
		if (!fgets(msg, MAX_MESSAGE_SIZE, stdin)) {
			fprintf(stderr, "fgets() failed\n");
			return(EXIT_FAILURE);
		}
		msg[strlen(msg)-1] = '\0';
		
		if (strcmp(msg, "CLOSE") == 0) {
			close(filed);
			printf("File descriptor closed\n");
			return(EXIT_SUCCESS);
		}

		//Write into the device file
		ret = write(filed, msg, strlen(msg) + 1);
		if (ret == -1) {
			fprintf(stderr, "write() failed: %s\n", strerror(errno));	
		} else {
			fprintf(stderr, "write() returned %d\n", ret);		
		}
	}
	
}
