#include <npheap/tnpheap_ioctl.h>
#include <npheap/npheap.h>
#include <npheap.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <malloc.h>
#include <string.h>


typedef struct list_iterator {
    struct list_iterator *next;
} list_iter;


typedef struct Transactional_Buffer{
    list_iter   l_head;
    void        *buffer;
    __u64       offset;
    __u64       version;
    __u64       size;
} t_buff;

list_iter head_buff;

void list_print1(void) {
	list_iter *position = NULL;
	t_buff *buff_obj = NULL;
	position = (&head_buff)->next;
	while(position != NULL) {
		buff_obj = (t_buff *) position;
		printf("\nPID is = %d,\nBuffer is = %p,\nOffset is = %llu,", getpid(), buff_obj->buffer, buff_obj->offset);
		printf("\nVersion is = %llu,\nSize is = %llu", buff_obj->version, buff_obj->size);
		position = position->next;
	}
}

t_buff *insert_object1(__u64 offset, __u64 size) {
// Node creation starts here
    t_buff *new = NULL;
    new = (t_buff *) malloc(sizeof(t_buff));
    if (new == NULL) {
        return NULL;
    }
    memset(new, 0, sizeof(t_buff));
    new->size = size;
    new->offset = offset;
    new->buffer = malloc(size);
    if(new->buffer == NULL) {
        free(new);
        return NULL;
    }
    memset(new->buffer, 0, size);

// Node Insertion starts here
    if(head_buff.next==NULL) {
        new->l_head.next = NULL;
        head_buff.next = &(new->l_head);
        return new;
    }
    list_iter *position = NULL;
    position = head_buff.next;
    while(position->next != NULL) {
        position = position->next;
    }
    position->next = &(new->l_head);
    new->l_head.next = NULL;
    return new;
}

t_buff *get_object1(list_iter *head_pos, __u64 offset) {
    list_iter *position = NULL;
    if (head_pos == NULL) {
        return NULL;
    }
    position = head_pos->next;
    while (position != NULL) {
        if (((t_buff *) position)->offset == offset) {
            return ((t_buff *) position);
        }
        position = position->next;
    }
    return NULL;
}

list_iter *delete_object1(void) {

    list_iter *position = NULL;
    if (&(head_buff) == NULL || head_buff.next == NULL) {
        return NULL;
    }
    position = head_buff.next;
    head_buff.next = position->next;
    position->next = NULL;
    return position;
}

__u64 tnpheap_get_version(int npheap_dev, int tnpheap_dev, __u64 offset)
{
    struct tnpheap_cmd  cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.offset = offset;
    ioctl(tnpheap_dev, TNPHEAP_IOCTL_GET_VERSION, &cmd);
    return(cmd.version);
}

int tnpheap_handler(int sig, siginfo_t *si)
{
    return 0;
}


void *tnpheap_alloc(int npheap_dev, int tnpheap_dev, __u64 offset, __u64 size)
{
    t_buff* store = NULL;
    __u64 present_version = 0;
    static int  init = 1;

    if (init==1){
        head_buff.next = NULL;
        init=0;
    }
    size = ((size + getpagesize() - 1) / getpagesize())*getpagesize();
    if((getpagesize()/size)>=1){
        size=getpagesize();
    }
    else size=2*getpagesize();

    store = get_object1(&head_buff, offset);
    if(store==NULL){
        store = insert_object1(offset, size);
        if(store==NULL)
            return NULL;
    }
    if (store->buffer != NULL){
        free(store->buffer);
        store->buffer = NULL;
    }

    store->buffer = malloc (size);
    if (!store->buffer)
        return NULL;

    memset (store->buffer, 0, size);
    store->size = size;
    
    present_version = tnpheap_get_version(npheap_dev, tnpheap_dev, offset);
    if (!present_version)
        return NULL;
    store->version = present_version;
    return store->buffer;    
}

__u64 tnpheap_start_tx(int npheap_dev, int tnpheap_dev)
{
    struct tnpheap_cmd  cmd;
    memset (&cmd, 0, sizeof(cmd));
    ioctl(tnpheap_dev, TNPHEAP_IOCTL_START_TX, &cmd);
    return cmd.version;
}

int tnpheap_commit(int npheap_dev, int tnpheap_dev)
{
    struct tnpheap_cmd cmd;
    t_buff *store = NULL;
    list_iter *node = NULL;
    char *data = NULL;
    __u64 old_size = 0;
    __u64 new_size = 0;
    int canCommit = 1;
    //take commit lock
    memset (&cmd, 0, sizeof(cmd));
    cmd.version = 1;
    ioctl (tnpheap_dev, TNPHEAP_IOCTL_COMMIT, &cmd);

    node = (&head_buff)->next;
    while (node != NULL){
        store = (t_buff *) node;
        if (store->version != tnpheap_get_version (npheap_dev, tnpheap_dev, 
                                                     store->offset)) {
            canCommit = 0;
            break;
        }
        node = node->next;
    }
    if (canCommit != 1){
        memset (&cmd, 0, sizeof(cmd));
        cmd.version = 2;
        ioctl(tnpheap_dev, TNPHEAP_IOCTL_COMMIT, &cmd);
        for(;;){
            node = delete_object1();
            if (node==NULL)
                break;
            store = (t_buff *)node;
            free(store->buffer);
            store->buffer = NULL;
            free(store);
            store = NULL;
        }

        return 1;
    }
    node = (&head_buff)->next;
    while(node != NULL){
        store = (t_buff *) node;
        old_size = npheap_getsize(npheap_dev, store->offset);
        if(old_size){
            if (old_size < store->size){
                /* More pages need to be allocated to NP Heap */
                npheap_delete(npheap_dev, store->offset);
                old_size = store->size;
            }

            if (old_size > store->size) {
                new_size = store->size;
            }
            else new_size = old_size;
        }
        else{
            old_size = store->size;
            new_size = store->size;
        }

        data = (char *)npheap_alloc(npheap_dev, store->offset, old_size);
        if(data==NULL)
            goto remove_node;

        memset(data, 0, old_size);
        memcpy(data, store->buffer, new_size);
        memset (&cmd, 0, sizeof(cmd));
        cmd.offset = store->offset;
        if(ioctl (tnpheap_dev, TNPHEAP_IOCTL_COMMIT, &cmd))
        {
            goto remove_node;
        }
//change this
remove_node:
        node = delete_object1();
        if (node!=NULL){
            store = (t_buff *) node;
            free(store->buffer);
            store->buffer = NULL;
            free(store);
            store = NULL;
        }
        node = (&head_buff)->next;
    }

    memset (&cmd, 0, sizeof(cmd));
    cmd.version = 2;
    ioctl(tnpheap_dev, TNPHEAP_IOCTL_COMMIT, &cmd);
    return 0;
}

