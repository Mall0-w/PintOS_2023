#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "userprog/process.h"

void syscall_init (void);

bool copy_in (void* dst_, const void* usrc_, size_t size);

int halt (const uint8_t* stack);             /*Handler for SYS_HALT*/
int syscall_exit(const uint8_t* stack);             /*Handler for SYS_EXIT*/
int exec(const uint8_t* stack);             /*Handler for SYS_EXEC*/
int wait(const uint8_t* stack);             /*Handler for SYS_WAIT*/
int create(const uint8_t* stack);             /*Handler for SYS_CREATE*/
int remove(const uint8_t* stack);             /*Handler for SYS_REMOVE*/
int open(const uint8_t* stack);              /*Handler for SYS_OPEN*/
int filesize(const uint8_t* stack);             /*Handler for SYS_FILESIZE*/
int read(const uint8_t* stack);             /*Handler for SYS_READ*/
int write(const uint8_t* stack);             /*Handler for SYS_WRITE*/
int seek(const uint8_t* stack);             /*Handler for SYS_SEEK*/
int tell(const uint8_t* stack);             /*Handler for SYS_TELL*/
int close(const uint8_t* stack);             /*Handler for SYS_CLOSE*/

/*Function that closes a file for a process, release filesys lock if release_lock is true*/
void close_proc_file(struct process_file* f, bool release_lock);

void proc_exit(int status);  /*Function used to exit process with statuscode status*/

bool is_valid_ptr(void *ptr, int range); /*Function used to check if a pointer is valid*/

#endif /* userprog/syscall.h */
