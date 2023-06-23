#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void syscall_init (void);

bool copy_in (void* dst_, const void* usrc_, size_t size);

void syscall_halt (uint8_t* stack);
void syscall_exit(uint8_t* stack);
int syscall_exec(uint8_t* stack);
int syscall_wait(uint8_t* stack);
bool syscall_create(uint8_t* stack);
bool syscall_remove(uint8_t* stack);
int syscall_open(uint8_t* stack); 
int syscall_filesize(uint8_t* stack);
int syscall_read(uint8_t* stack);
int syscall_write(uint8_t* stack);
void syscall_seek(uint8_t* stack);
unsigned syscall_tell(uint8_t* stack);
void syscall_close(uint8_t* stack);

void close_proc_file(struct process_file* f, bool release_lock);

#endif /* userprog/syscall.h */
