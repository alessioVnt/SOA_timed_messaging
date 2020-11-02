#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <errno.h>

int i;
char buff[4096];
#define DATA "ciao a tutti\n"
#define SIZE strlen(DATA)

void * the_thread(void* path){

	char* device;
	int fd, ret;
	pthread_t id;
	id = pthread_self();

	device = (char*)path;
	sleep(1);

	printf("opening device %s\n",device);
	fd = open(device,O_RDWR);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}
	printf("device %s successfully opened\n",device);
	for(i=0;i<1000;i++){
		ret = write(fd,DATA,SIZE);
		if (ret == -1){
			fprintf(stderr, "Thread: %lu write() failed: %s\n", id, strerror(errno));
			return NULL;
		}
	} 
	return NULL;

}
int main(int argc, char** argv){

     int ret;
     int major;
     int minors;
     char *path;
     pthread_t tid;

     if(argc<4){
	printf("useg: prog pathname major minors");
	return -1;
     }

     path = argv[1];
     major = strtol(argv[2],NULL,10);
     minors = strtol(argv[3],NULL,10);
     printf("creating %d minors for device %s with major %d\n",minors,path,major);

     for(i=0;i<minors;i++){
	sprintf(buff,"mknod %s%d c %d %i\n",path,i,major,i);
	system(buff);
	sprintf(buff,"%s%d",path,i);
	pthread_create(&tid,NULL,the_thread,strdup(buff));
     }

     pause();
     return 0;

}
