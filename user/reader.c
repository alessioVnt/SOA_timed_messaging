#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../timed-messaging.h"

#define MAX_MESSAGE_SIZE 128

int main(int argc, char *argv[]){
	int filed, ret;
	char msg[MAX_MESSAGE_SIZE];

	if (argc != 3){
		fprintf(stderr, "Usage: sudo %s <filename> <read_timeout>\n", argv[0]);
		return(EXIT_FAILURE);
	}

	printf("pid: %d\n", getpid());

	//Open device file
	filed = open(argv[1], O_RDWR);
	if (filed == -1) {
		fprintf(stderr, "open() failed: %s\n", strerror(errno));
		return(EXIT_FAILURE);
	}

	// Set receive timeout
	unsigned long recv_timeout;
	recv_timeout = strtoul(argv[2], NULL, 0);
	ret = ioctl(filed, SET_RECV_TIMEOUT, recv_timeout);
	if (ret == -1) {
		fprintf(stderr, "ioctl() failed: %s\n", strerror(errno));
		return(EXIT_FAILURE);
	}

	//Read from file
	while (1) {
		sleep(1);
		ret = read(filed, msg, MAX_MESSAGE_SIZE);
		if (ret == -1){
			fprintf(stderr, "read() failed: %s\n", strerror(errno));
		} else {
			printf("read: %s\n", msg);
		}
	}
}
