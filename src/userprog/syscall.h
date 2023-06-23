#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "userprog/process.h"

void syscall_init (void);

bool copy_in (void* dst_, const void* usrc_, size_t size);

int syscall_halt (const uint8_t* stack);             /*Handler for SYS_HALT*/
int syscall_exit(const uint8_t* stack);             /*Handler for SYS_EXIT*/
int syscall_exec(const uint8_t* stack);             /*Handler for SYS_EXEC*/
int syscall_wait(const uint8_t* stack);             /*Handler for SYS_WAIT*/
int syscall_create(const uint8_t* stack);             /*Handler for SYS_CREATE*/
int syscall_remove(const uint8_t* stack);             /*Handler for SYS_REMOVE*/
int syscall_open(const uint8_t* stack);              /*Handler for SYS_OPEN*/
int syscall_filesize(const uint8_t* stack);             /*Handler for SYS_FILESIZE*/
int syscall_read(const uint8_t* stack);             /*Handler for SYS_READ*/
int syscall_write(const uint8_t* stack);             /*Handler for SYS_WRITE*/
int syscall_seek(const uint8_t* stack);             /*Handler for SYS_SEEK*/
int syscall_tell(const uint8_t* stack);             /*Handler for SYS_TELL*/
int syscall_close(const uint8_t* stack);             /*Handler for SYS_CLOSE*/

/*Function that closes a file for a process, release filesys lock if release_lock is true*/
void close_proc_file(struct process_file* f, bool release_lock);

#endif /* userprog/syscall.h */
