#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct process_file{
    int fd;
    struct file* file;
    struct list_elem elem;
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct process_file* find_file(struct thread* t, int fd);   /*Function used to find a file of given fd under thread t*/

#endif /* userprog/process.h */
