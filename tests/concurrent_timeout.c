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
#include <pthread.h>
#include "../timed-messaging.h"

//Use here the max_msg_size param used when the module was inserted (4096 default value)
#define MAX_MSG_SIZE 4096

int filed;

void *reader(void *arg){

	pthread_t id;
	id = pthread_self();
	
	//Read the written messages
	char to_read[MAX_MSG_SIZE];
	int ret;
	while (1) {
		ret = read(filed, to_read, MAX_MSG_SIZE);
		if (ret >= 0){
			printf("Message red by %lu thread: %s\n", id, to_read);
		} else {
			if (errno == ETIME){
				printf("%lu thread timeout expired wihtout reading any message\n", id);
			} else if (errno == ENOMSG) {
				printf("%lu thread no message found\n", id);
				return;
			} else {
				printf("Unexpected behaviour, read returned: %d\n", ret);
				exit(EXIT_FAILURE);
			}
		}					
	}

	return;
}

void *writer(void *arg){

	pthread_t id;
	id = pthread_self();

	char to_write[MAX_MSG_SIZE];
	sprintf(to_write, "Message by %lu thread\n", id);

	//Write the message
	int ret;
	ret = write(filed, to_write, strlen(to_write) + 1);
	if (ret == -1){
		fprintf(stderr, "Error in write");
		return(EXIT_FAILURE);	
	}

	return;
}

int main(int argc, char *argv[]){

	printf("Concurrent threads with timeout test: multiple readers and writers, with timeouts (read and/or write can be deferred)\n");

	int ret;
	
	if (argc != 8){
		fprintf(stderr, "Usage: sudo %s <pathname> <major> <minor> <writer_num> <reader_num> <recv_timeout> <send_timeout>\n", argv[0]);
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
	filed = open(argv[1], O_RDWR);
	if (filed == -1){
		fprintf(stderr, "Error opening file\n");
		return(EXIT_FAILURE);
	}
	printf("Open succesfull\n");

	//Set send and recv timeouts
	unsigned long recv_timeout, send_timeout;
	recv_timeout = strtoul(argv[6], NULL, 0);
	send_timeout = strtoul(argv[7], NULL, 0);
	printf("recv_timeout: %lu , send_timeout: %lu\n", recv_timeout, send_timeout);
	ret = ioctl(filed, SET_RECV_TIMEOUT, (unsigned long) recv_timeout);
	if (ret == -1) {
		fprintf(stderr, "ioctl() failed\n");
		return(EXIT_FAILURE);
	}
	
	ret = ioctl(filed, SET_SEND_TIMEOUT, (unsigned long) send_timeout);
	if (ret == -1) {
		fprintf(stderr, "ioctl() failed\n");
		return(EXIT_FAILURE);
	}

	//Get writers and readers number
	int writer_num, reader_num, i;
	writer_num = strtoul(argv[4], NULL, 0);
	reader_num = strtoul(argv[5], NULL, 0);

	//Create writers
	pthread_t writers_tid[writer_num];
	for (i = 0; i < writer_num; i++){
		if(pthread_create(&writers_tid[i], NULL, writer, NULL)){
			fprintf(stderr, "Error in creating writer threads\n");
			return(EXIT_FAILURE);
		}
	}

	//Create readers
	pthread_t readers_tid[reader_num];
	for (i = 0; i < reader_num; i++){
		if(pthread_create(&readers_tid[i], NULL, reader, NULL)){
			fprintf(stderr, "Error in creating reader threads\n");
			return(EXIT_FAILURE);
		}
	}

	while(1);
	return (EXIT_SUCCESS);
	
}
