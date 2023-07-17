#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "userprog/process.h"

void syscall_init (void);

bool copy_in (void* dst_, const void* usrc_, size_t size);

int halt ( uint8_t* stack);             /*Handler for SYS_HALT*/
int syscall_exit( uint8_t* stack);             /*Handler for SYS_EXIT*/
int exec( uint8_t* stack);             /*Handler for SYS_EXEC*/
int wait( uint8_t* stack);             /*Handler for SYS_WAIT*/
int create( uint8_t* stack);             /*Handler for SYS_CREATE*/
int remove( uint8_t* stack);             /*Handler for SYS_REMOVE*/
int open( uint8_t* stack);              /*Handler for SYS_OPEN*/
int filesize( uint8_t* stack);             /*Handler for SYS_FILESIZE*/
int read( uint8_t* stack);             /*Handler for SYS_READ*/
int write( uint8_t* stack);             /*Handler for SYS_WRITE*/
int seek( uint8_t* stack);             /*Handler for SYS_SEEK*/
int tell( uint8_t* stack);             /*Handler for SYS_TELL*/
int close( uint8_t* stack);             /*Handler for SYS_CLOSE*/
mapid_t mmap( uint8_t* stack);             /*Handler for SYS_MMAP*/
void munmap( uint8_t* stack);             /*Handler for SYS_MUNMAP*/

/*Function that closes a file for a process, release filesys lock if release_lock is true*/
void close_proc_file(struct process_file* f, bool release_lock);

void proc_exit(int status);  /*Function used to exit process with statuscode status*/

bool valid_esp(void *ptr, int range); /*Function used to check if a stack pointer is valid*/


#endif /* userprog/syscall.h */
