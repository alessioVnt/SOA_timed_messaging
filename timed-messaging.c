
#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/sched.h>	
#include <linux/pid.h>		/* For pid types */
#include <linux/tty.h>		/* For the tty declarations */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/param.h>
#include <linux/wait.h>
#include "timed-messaging.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alessio Vintari");

#define MODNAME "timed_messaging"
#define DEVICE_NAME "timed_messaging_dev"  /* Device file name in /dev/ - not mandatory  */

#define MAX_MSG_SIZE_DEFAULT 4096 // One page
#define MAX_STORAGE_SIZE_DEFAULT 65536

//Reconfigurable module params 
static unsigned int max_message_size = MAX_MSG_SIZE_DEFAULT;
module_param(max_message_size, uint, S_IRUGO | S_IWUSR);
static unsigned int max_storage_size = MAX_STORAGE_SIZE_DEFAULT;
module_param(max_storage_size, uint, S_IRUGO | S_IWUSR);

static int Major;           

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)	MAJOR(session->f_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)	MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_dentry->d_inode->i_rdev)
#endif

#define MINORS 8
struct object_state objects[MINORS]; //Array of minors


//OPEN 
static int dev_open(struct inode *inode, struct file *file) {

	//Get minor
	int minor;
	minor = get_minor(file);

	//Check if minor is valid
	if(minor >= MINORS){
		return -ENODEV;
	}

	
	//Allocate and init session
	struct session *session;
	session = kmalloc(sizeof(struct session), GFP_KERNEL);
	if (session == NULL) {
		return -ENOMEM;
	}
	mutex_init(&(session->session_mtx));
	session->send_timeout = 0;
	session->recv_timeout = 0;
	INIT_LIST_HEAD(&(session->list_node));
	//Allocate concurrency managed workqueue
	session->wq = alloc_workqueue("deferred-write-wq", WQ_MEM_RECLAIM, 0);
	if (session->wq == NULL){
		kfree(session);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&(session->deferred_writes));

	//Link session to private data area of file struct
	file->private_data = (void *)session;

	//Insert session in minor's sessions list
	mutex_lock(&(objects[minor].operation_synchronizer));
	list_add_tail(&(session->list_node), &(objects[minor].sessions));
	mutex_unlock(&(objects[minor].operation_synchronizer));

	printk("%s: device file successfully opened for object with minor %d\n",MODNAME,minor);
	return 0;

}


//RELEASE
static int dev_release(struct inode *inode, struct file *file) {

	int minor;
	minor = get_minor(file);

	struct session *session;
	session = (struct session *) file->private_data;
	
	//flush and destroy the wq
	flush_workqueue(session->wq);
	destroy_workqueue(session->wq);

	//Delete session from the session's list
	mutex_lock(&(objects[minor].operation_synchronizer));
	list_del(&(session->list_node));
	mutex_unlock(&(objects[minor].operation_synchronizer));

	printk("%s: device file closed\n",MODNAME);
	return 0;

}

//Function to store a new message in the fifo message queue (will be the deferred work)
static int __store_message(struct object_state *the_object, char *kbuf, size_t len){
	
	//Check if the remaining storage of the current minor is enough
	if (the_object->size + len > max_storage_size){
		kfree(kbuf);
		return -ENOSPC;
	}
	
	//Allocate the new message struct
	struct message *to_store;
	to_store = kmalloc(sizeof(struct message), GFP_KERNEL);
	if (to_store == NULL){
		kfree(kbuf);
		return -ENOMEM;
	}
	
	to_store->content = kbuf;
	to_store->size = len;
	INIT_LIST_HEAD(&(to_store->list_node));
	list_add_tail(&(to_store->list_node), &(the_object->fifo_mq));

	//Increase total size of the minor
	the_object->size += len;

	//Awake one of the timeout readers
	struct timeout_reader *t_read;
	t_read = list_first_entry_or_null(&(the_object->timeout_readers), struct timeout_reader, list_node);
	if (t_read != NULL){
		t_read->msg_available = 1;
		list_del(&(t_read->list_node));
		//Only the interruptible tasks in wich the condition is true will wake up (in this case all the timeout readers int the current minor)
		wake_up_interruptible(&(the_object->wait_q));
	}
	
	//If there is no error it will store exactly len bytes
	return len;
}

static void __store_message_deferred(struct work_struct *the_work){
	
	//Get delayed_work struct through container_of
	struct delayed_work *delayed_work;
	delayed_work = container_of( the_work, struct delayed_work, work);

	//Get deferred_write struct through container_of
	struct deferred_write *def_write;
	def_write = container_of( delayed_work, struct deferred_write, delayed_work);

	//Remove the deferred write from the list
	mutex_lock(&(def_write->session->session_mtx));
	list_del(&(def_write->list_node));
	mutex_unlock(&(def_write->session->session_mtx));

	//Store the message in the fifo mq
	int ret;
	mutex_lock(&(objects[def_write->minor].operation_synchronizer));
	ret = __store_message(&objects[def_write->minor], def_write->deferred_msg, def_write->size);
	mutex_unlock(&(objects[def_write->minor].operation_synchronizer));

	//Deallocate the deferred write struct
	kfree(def_write);
	return;
}

//WRITE 
static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off) {
	
	//Get minor
	int minor = get_minor(filp);
	int ret;
	struct object_state *the_object;

	the_object = objects + minor;
	printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

	//Get the session
	struct session *session;
	session = (struct session *)filp->private_data;

	//Check that the message size is correct
	if(len > max_message_size){
		return -EMSGSIZE; //Message too big
	}

	//Create a kernel buffer
	char *kbuf;
	kbuf = kmalloc(len, GFP_KERNEL);
	if (kbuf == NULL) {
		return -ENOMEM;
	}

	//Copy the message in the kernel buffer (if copy_from_user doesn't copy everything (ret != 0), adjust the value of len variable)
	ret = copy_from_user(kbuf, buff, len);
	len = len - ret;

	//Check wether ther is a send timeout or not
	mutex_lock(&(session->session_mtx));
	if (session->send_timeout > 0){
		//There's a send timeout, allocate and initialize the deferred write structure and enqueue the deferred work in the wq
		struct deferred_write *def_write;
		def_write = kmalloc(sizeof(struct deferred_write), GFP_KERNEL);
		if (def_write == NULL){
			kfree(kbuf);
			mutex_unlock(&(session->session_mtx));
			return -ENOMEM;		
		}
		def_write->minor = minor;
		def_write->session = session;
		def_write->deferred_msg = kbuf;
		def_write->size = len;
		INIT_DELAYED_WORK(&(def_write->delayed_work), __store_message_deferred);
		INIT_LIST_HEAD(&(def_write->list_node));
		
		list_add_tail(&(def_write->list_node), &(session->deferred_writes));
		//mutex_unlock(session->session_mtx);
		queue_delayed_work(session->wq, &(def_write->delayed_work), session->send_timeout);
		mutex_unlock(&(session->session_mtx));
		return 0;
	}

	mutex_unlock(&(session->session_mtx));

	//Immediate store
	mutex_lock(&(objects[minor].operation_synchronizer));
	ret = __store_message(&objects[minor], kbuf, len);
	mutex_unlock(&(objects[minor].operation_synchronizer));
	return ret;

}

static int __deliver_message(struct object_state *the_object, struct message *to_deliver, char *ubuf, size_t len){

	int ret;
	
	//Check the message len
	if (len > to_deliver->size) len = to_deliver->size;
	
	//Actual delivery of the message (copy_to_user can return a value != 0, in that case returns the number of bytes truly red)
	ret = copy_to_user(ubuf, to_deliver->content, len);
	
	list_del(&(to_deliver->list_node));
	the_object->size -= to_deliver->size;
	kfree(to_deliver->content);
	kfree(to_deliver);
	return len - ret;
}

//READ
static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {

	//Get the minor
	int minor = get_minor(filp);
	int ret;
	struct object_state *the_object;

	the_object = objects + minor;
	printk("%s: somebody called a read on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

	//Get the session
	struct session *session;
	session = (struct session *)filp->private_data;

	mutex_lock(&(objects[minor].operation_synchronizer));

	//Get the first entry of the fifo message queue or null
	struct message *to_deliver;
	to_deliver = list_first_entry_or_null(&(the_object->fifo_mq),
				       struct message, list_node);

	//Check wethere there is a message to deliver
	if (to_deliver != NULL) {
		ret = __deliver_message(&(objects[minor]), to_deliver, buff, len);
		mutex_unlock(&(objects[minor].operation_synchronizer));
		return ret;
	}

	//The fifo message queue was empty -> check if there is a timeout
	mutex_unlock(&(objects[minor].operation_synchronizer));
	
	unsigned long timeout;
	mutex_lock(&(session->session_mtx));
	timeout = session->recv_timeout;
	mutex_unlock(&(session->session_mtx));
	
	//No timeout and no message in the mq
	if (timeout == 0) {
		return -ENOMSG;
	}
	
	//Create a timeout reader and linking to the minor
	struct timeout_reader *t_read;
	t_read = kmalloc(sizeof(struct timeout_reader), GFP_KERNEL);
	if (t_read == NULL){
		return -ENOMEM;
	}
	//Init fields
	t_read->flush = 0;
	t_read->msg_available = 0;
	INIT_LIST_HEAD(&(t_read->list_node));
	
	//Link to the minor's list of timeout readers
	mutex_lock(&(objects[minor].operation_synchronizer));
	list_add_tail(&(t_read->list_node), &(objects[minor].timeout_readers));
	mutex_unlock(&(objects[minor].operation_synchronizer));

	//Wait for messages till timeout expires or signal arrives
	while(timeout != 0){
		//Params are respectively: the wait_queue_head, events, time to sleep
		ret = wait_event_interruptible_timeout(objects[minor].wait_q, t_read->flush || t_read->msg_available, timeout);
		
		//A signal has been received
		if (ret == -ERESTARTSYS){
			//If one of the two events happened while the signal was received, the timeout reader has already been removed from the list
			if(t_read->flush != 1 && t_read->msg_available != 1){
				mutex_lock(&(objects[minor].operation_synchronizer));
				list_del(&(t_read->list_node));
				mutex_unlock(&(objects[minor].operation_synchronizer));
			}
			kfree(t_read);
			return ret;
			
		}
		//Woke up because flush was called
		if(t_read->flush){
			kfree(t_read);
			return -ECANCELED;		
		}
		//Woke up because of timeout and no messages available
		if(ret == 0){
			mutex_lock(&(objects[minor].operation_synchronizer));
			list_del(&(t_read->list_node));
			mutex_unlock(&(objects[minor].operation_synchronizer));
			kfree(t_read);
			return -ETIME;
		}
		
		//Try reading the message from the minor's fifo mq
		mutex_lock(&(objects[minor].operation_synchronizer));
		to_deliver = list_first_entry_or_null(&(objects[minor].fifo_mq), struct message, list_node);
		//No message to read, return to sleep
		if (to_deliver == NULL){
			t_read->msg_available = 0;
			list_add_tail(&(t_read->list_node), &(objects[minor].timeout_readers));
			mutex_unlock(&(objects[minor].operation_synchronizer));
			timeout = ret;
		} else {
			//Message found
			kfree(t_read);
			ret = __deliver_message(&(objects[minor]), to_deliver, buff, len);
			mutex_unlock(&(objects[minor].operation_synchronizer));
			return ret;
		}

	}

}

//Remove al the deferred writes still not stored
static void __revoke_delayed_messages(struct session *session){
	
	struct list_head *pos, *temp;
	struct deferred_write *def_write;

	//Use the safe version when deleting nodes or pos would become unvalid after first execution of body	
	list_for_each_safe(pos, temp, &(session->deferred_writes)) {
		def_write = list_entry(pos, struct deferred_write, list_node);
		//Cancel the delayed work and if successfull delete the current entry and deallcoate mem
		if (cancel_delayed_work(&(def_write->delayed_work))){
			list_del(&(def_write->list_node));
			kfree(def_write->deferred_msg);
			kfree(def_write);
		}
	}
}


//IOCTL
//1 ms == 0.1 jiffies -> 1 jiffy == 10 ms
static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {

	int minor = get_minor(filp);
	struct object_state *the_object;

	the_object = objects + minor;

	printk("%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u \n",MODNAME,get_major(filp),get_minor(filp),command);

	struct session *session;
	session = (struct session *)filp->private_data;

	switch(command) {
		case SET_SEND_TIMEOUT: 
			mutex_lock(&(session->session_mtx));
			session->send_timeout = (param *HZ) / 1000;
			mutex_unlock(&(session->session_mtx));
			break;
		case SET_RECV_TIMEOUT:
			mutex_lock(&(session->session_mtx));
			session->recv_timeout = (param *HZ) / 1000;
			mutex_unlock(&(session->session_mtx));
			break;
		case REVOKE_DELAYED_MESSAGES:
			mutex_lock(&(session->session_mtx));
			__revoke_delayed_messages(session);
			mutex_unlock(&(session->session_mtx));
			break;
		default:
			printk("Invalid command, did you mean SET_SEND_TIMEOUT, SET_RECV_TIMEOUT, or REVOKE_DELAYED_MESSAGES ?");
			return -ENOTTY;
	}
	return 0;

}

static int dev_flush(struct file *filp, fl_owner_t id){
	int minor = get_minor(filp);
	struct list_head *pos;
	struct session *session;
	
	mutex_lock(&(objects[minor].operation_synchronizer));
	//Revoke messages that are not yet delivered in every session (not using safe version since not deleting the sessions)
	list_for_each(pos, &(objects[minor].sessions)){
		session = list_entry(pos, struct session, list_node);
		mutex_lock(&(session->session_mtx));
		__revoke_delayed_messages(session);
		mutex_unlock(&(session->session_mtx));
	}

	//Unlock readers
	struct list_head *t_read_pos, *temp;
	struct timeout_reader *t_read;
	//Go through the list of timeout reader and set flush flag to 1
	//Using list for each safe since timeout_readers are going to be deleted from the list
	list_for_each_safe(t_read_pos, temp, &(objects[minor].timeout_readers)) {
		t_read = list_entry(t_read_pos, struct timeout_reader, list_node);
		t_read->flush = 1;
		list_del(&(t_read->list_node));
		//Only the interruptible tasks in wich the condition is true will wake up (in this case all the timeout readers int the current minor)
		wake_up_interruptible(&(objects[minor].wait_q));
	}

	mutex_unlock(&(objects[minor].operation_synchronizer));
	return 0;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.write = dev_write,
	.read = dev_read,
	.open =  dev_open,
	.release = dev_release,
	.unlocked_ioctl = dev_ioctl,
	.flush = dev_flush

};



//INIT
int init_module(void) {

	int i;

	//initialize the drive internal state
	for(i=0;i<MINORS;i++){
		objects[i].size = 0;
		mutex_init(&(objects[i].operation_synchronizer));
		INIT_LIST_HEAD(&(objects[i].fifo_mq));
		INIT_LIST_HEAD(&(objects[i].sessions));
		init_waitqueue_head(&(objects[i].wait_q));
		INIT_LIST_HEAD(&(objects[i].timeout_readers));
	}
	
	//Register driver
	Major = __register_chrdev(0, 0, 256, DEVICE_NAME, &fops);

	if (Major < 0) {
	  printk("%s: registering device failed\n",MODNAME);
	  return Major;
	}

	printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n",MODNAME, Major);

	return 0;

}

//END called before module removal
void cleanup_module(void) {

	struct list_head *ptr;
	struct list_head *tmp;
	struct message *msg;

	int i;
	//Delete all messages in the fifo mq
	for(i=0;i<MINORS;i++){
		list_for_each_safe(ptr, tmp, &(objects[i].fifo_mq)) {
			msg = list_entry(ptr, struct message, list_node);
			list_del(&(msg->list_node));
			kfree(msg->content);
			kfree(msg);
		}
	}

	unregister_chrdev(Major, DEVICE_NAME);

	printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n",MODNAME, Major);

	return;

}
