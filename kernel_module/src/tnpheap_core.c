//////////////////////////////////////////////////////////////////////
//                             North Carolina State University
//
//
//
//                             Copyright 2016
//
////////////////////////////////////////////////////////////////////////
//
// This program is free software; you can redistribute it and/or modify it
// under the terms and conditions of the GNU General Public License,
// version 2, as published by the Free Software Foundation.
//
// This program is distributed in the hope it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
//
////////////////////////////////////////////////////////////////////////
//
//   Author:  Hung-Wei Tseng
//
//   Description:
//     Skeleton of NPHeap Pseudo Device
//
////////////////////////////////////////////////////////////////////////

#include "tnpheap_ioctl.h"

#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/list.h>


typedef struct TNP_object_store {
	struct list_head iter_ptrs;  // kernel's list structure 
	struct mutex resource_lock;
	__u64 offset;
	__u64 version;
} tnp_struct;

struct list_head glob_iters;  		// global next and prev pointers of linked list
struct miscdevice tnpheap_dev;
struct mutex global_lock;
struct mutex commit_lock;

// Print nodes of linked list
void list_print(void) {
    struct list_head *position = NULL;
    printk("\nPrinting contents of the linked list:\n");
    list_for_each(position, &glob_iters) {
        printk("Version:%llu,\nOffset:%llu\n\n",((tnp_struct *)position)->version,\
            ((tnp_struct *)position)->offset);
    }
}

// Searches for the desired object-id
tnp_struct *get_object(__u64 offset) {
    
    struct list_head *position = NULL;
    tnp_struct *res = NULL;
    printk("Search the list for offset: %llu using list_for_each()\n", offset);
    list_for_each(position, &glob_iters){
        res = (tnp_struct *) position;
        if(res->offset == offset) {
            printk(" offset: %llu found in list\n", offset);
            return res;
        }
    }
    printk("%llu not found in list\n", offset);
    return NULL;
}

// Inserts a Node at tail of Doubly linked list
tnp_struct *insert_object(__u64 offset) {
    
    tnp_struct *new = NULL;
    printk("Inside insert_object for offset: %llu\n", offset);
    new = (tnp_struct*)kmalloc(sizeof(tnp_struct),GFP_KERNEL);
    memset(new, 0, sizeof(tnp_struct));
    INIT_LIST_HEAD(&new->iter_ptrs);
    mutex_init(&new->resource_lock);
    new->offset = offset;
    new->version=1; //initial version number
    list_add_tail(&new->iter_ptrs, &glob_iters);
    printk("Leaving insert_object \n");
    //list_print();
    return new;
}

// Deletes an entry from the list 
void delete_object(__u64 offset) {
	
	struct list_head *position = NULL ;
    struct list_head *temp_store = NULL;
	printk("deleting the list using list_for_each_safe()\n");	
	list_for_each_safe(position, temp_store, &glob_iters) {
		if(((tnp_struct *)position)->offset == offset) {
			list_del(position);
		    kfree(position);
		    return;	
		}
	}	 
}

// Deletes the entire linked-list
void delete_list(void) {
    
    struct list_head *position = NULL; 
    struct list_head *temp_store = NULL;
    printk("deleting the whole linked-list data structure\n");  
    list_for_each_safe(position, temp_store, &glob_iters) {
            list_del(position);
            kfree(position);	
    }	
    //myobjectlist->head_of_list->next = myobjectlist->head_of_list->prev;
}

__u64 tnpheap_get_version(struct tnpheap_cmd __user *user_cmd)
{
    struct tnpheap_cmd cmd;
    tnp_struct* tnpheap_store;
    tnpheap_store = NULL;
    __u64 offset;
    if (copy_from_user(&cmd, user_cmd, sizeof(cmd)))
    {
        return -1 ;
    }    
    offset = cmd.offset;
    mutex_lock(&global_lock);
    tnpheap_store = get_object(offset);
    if(tnpheap_store == NULL){
        tnpheap_store = insert_object(offset);
        //failure condition and unlock not handled
    }
    cmd.version = tnpheap_store->version;    
    mutex_unlock(&global_lock);
    return cmd.version;
}

__u64 tnpheap_start_tx(struct tnpheap_cmd __user *user_cmd)
{
    struct tnpheap_cmd cmd;
    static __u64 t_id = 0; //global transaction id
    static struct mutex tid_lock; //lock to modify transaction id
    __u64 ret=0;
    if (copy_from_user(&cmd, user_cmd, sizeof(cmd)))
    {
        return -1 ;
    }    
    if (t_id == 0){
        mutex_init(&tid_lock); //initialize mutex
    }
    mutex_lock(&tid_lock);
    t_id++;
    ret = t_id; //may need to change
    mutex_unlock(&tid_lock);
    cmd.version = ret;
    copy_to_user((void __user *) user_cmd, &cmd, sizeof(cmd));

    return ret;
}

__u64 tnpheap_commit(struct tnpheap_cmd __user *user_cmd)
{
    tnp_struct* tnpheap_store;
    struct tnpheap_cmd cmd;
    __u64 ret=0;
    if (copy_from_user(&cmd, user_cmd, sizeof(cmd)))
    {
        return -1 ;
    }
    if(cmd.version!=0){
        if(cmd.version ==1){
            //commit lock
            mutex_lock(&commit_lock);
        }
        else if(cmd.version==2){
            //commit unlock
            mutex_unlock(&commit_lock);
        }
        return 0;
    }

    mutex_lock(&global_lock);
    
    tnpheap_store = get_object(cmd.offset);
    if (tnpheap_store==NULL){
        mutex_unlock(&global_lock);
        return -1;
    }

    tnpheap_store->version++;
    mutex_unlock(&global_lock);
    return ret;
}

__u64 tnpheap_ioctl(struct file *filp, unsigned int cmd,
                                unsigned long arg)
{
    switch (cmd) {
    case TNPHEAP_IOCTL_START_TX:
        return tnpheap_start_tx((void __user *) arg);
    case TNPHEAP_IOCTL_GET_VERSION:
        return tnpheap_get_version((void __user *) arg);
    case TNPHEAP_IOCTL_COMMIT:
        return tnpheap_commit((void __user *) arg);
    default:
        return -ENOTTY;
    }
}

static const struct file_operations tnpheap_fops = {
    .owner                = THIS_MODULE,
    .unlocked_ioctl       = tnpheap_ioctl,
};

struct miscdevice tnpheap_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "tnpheap",
    .fops = &tnpheap_fops,
};

static int __init tnpheap_module_init(void)
{
    int ret = 0;
    if ((ret = misc_register(&tnpheap_dev)))
        printk(KERN_ERR "Unable to register \"npheap\" misc device\n");
    else
        printk(KERN_ERR "\"npheap\" misc device installed\n");

    INIT_LIST_HEAD(&glob_iters);
    mutex_init(&global_lock);
    mutex_init(&commit_lock);

    return ret;
}

static void __exit tnpheap_module_exit(void)
{
    delete_list();
    mutex_destroy(&global_lock);
    mutex_destroy(&commit_lock);
    misc_deregister(&tnpheap_dev);
    return;
}

MODULE_AUTHOR("Hung-Wei Tseng <htseng3@ncsu.edu>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
module_init(tnpheap_module_init);
module_exit(tnpheap_module_exit);
