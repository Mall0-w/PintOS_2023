#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void syscall_init (void);

bool copy_in (void* dst_, const void* usrc_, size_t size);

void syscall_halt (uint8_t* stack);             /*Handler for SYS_HALT*/
void syscall_exit(uint8_t* stack);             /*Handler for SYS_EXIT*/
int syscall_exec(uint8_t* stack);             /*Handler for SYS_EXEC*/
int syscall_wait(uint8_t* stack);             /*Handler for SYS_WAIT*/
bool syscall_create(uint8_t* stack);             /*Handler for SYS_CREATE*/
bool syscall_remove(uint8_t* stack);             /*Handler for SYS_REMOVE*/
int syscall_open(uint8_t* stack);              /*Handler for SYS_OPEN*/
int syscall_filesize(uint8_t* stack);             /*Handler for SYS_FILESIZE*/
int syscall_read(uint8_t* stack);             /*Handler for SYS_READ*/
int syscall_write(uint8_t* stack);             /*Handler for SYS_WRITE*/
void syscall_seek(uint8_t* stack);             /*Handler for SYS_SEEK*/
unsigned syscall_tell(uint8_t* stack);             /*Handler for SYS_TELL*/
void syscall_close(uint8_t* stack);             /*Handler for SYS_CLOSE*/

/*Function that closes a file for a process, release filesys lock if release_lock is true*/
void close_proc_file(struct process_file* f, bool release_lock);

#endif /* userprog/syscall.h */
