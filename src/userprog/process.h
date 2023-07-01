#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct process_file{
    int fd;                     /*File descriptor for process' file*/
    struct file* file;          /*pointer to file for process*/
    struct list_elem elem;      /*list elem used to keep track of opened files*/
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct process_file* find_file(struct thread* t, int fd);   /*Function used to find a file of given fd under thread t*/

bool is_file_exe(struct file* f);

#endif /* userprog/process.h */
