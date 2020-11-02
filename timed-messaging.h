#include <linux/ioctl.h>

//Define of ioctl commands

#define TIMED_MSG_MAGIC 'k' 
#define SET_SEND_TIMEOUT _IO(TIMED_MSG_MAGIC, 0)
#define SET_RECV_TIMEOUT _IO(TIMED_MSG_MAGIC, 1)
#define REVOKE_DELAYED_MESSAGES _IO(TIMED_MSG_MAGIC, 2)



#ifdef __KERNEL__

//Struct for minors info
//Tiemeout reads are managed through a wait event queue
struct object_state{
	unsigned int size;
	struct mutex operation_synchronizer;
	struct list_head fifo_mq;
	struct list_head sessions;
	wait_queue_head_t wait_q;
	struct list_head timeout_readers;
};

//Struct for messages
struct message{
	char *content;
	unsigned int size;
	struct list_head list_node;
};

//Struct for session info
//Uses a concurrency managed workqueue for deferred writes
struct session{
	unsigned long send_timeout; //In jiffies
	unsigned long recv_timeout; //In jiffies
	struct list_head list_node;
	struct mutex session_mtx;
	struct workqueue_struct *wq;
	struct list_head deferred_writes;
};

//Deferred write struct
//Delayed work is embedded here 
struct deferred_write {
	int minor;
	struct session *session;
	char *deferred_msg;
	unsigned int size;
	struct delayed_work delayed_work;
	struct list_head list_node;
};

//Reader with recv_timeout != 0 struct
struct timeout_reader {
	int flush;
	int msg_available;
	struct list_head list_node;
};

/*Open: 
	-Gets minor num and checks if it's valid
	-Allocates and initialize the session struct
	-Links the session struct to the private_data field of the file struct and to the session's list of the current minor*/
static int dev_open(struct inode *, struct file *);

/*Release:
	-Flushes and destroys the work queue
	-Deletes the session from the minor's sessions list
	-Called upon close() if the file structure is not referred by other processes */
static int dev_release(struct inode *, struct file *);

/*Write:
	-If there is a send_timeout, enqueues the deferred write in the concurrency managed work queue*/
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

/*Read:
	-If there is a recv_timeout the thread is put in the wait event queue
	-Resume running after a timeout, or when one of the conditions is met (or on signal) (when wake_up_interruptible is called, only threads that meet the conditions in the event queue will be woken up)*/
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);

/*ioctl:
	-SET_SEND_TIMEOUT: set the current session send timeout to the given param
	-SET_RECV_TIMEOUT: set the current session recv timeout to the given param
	-REVOKE_DELAYED_MESSAGES: revokes dealyed writes not yet stored (deferred writes are stored in a list linked to the session struct)*/
static long dev_ioctl(struct file *, unsigned int, unsigned long);

/*Flush:
	-Awake all the timeout readers
	-Revoke all deferred writes*/
static int dev_flush(struct file *, fl_owner_t id);

#endif
