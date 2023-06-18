#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void syscall_init (void);

bool copy_in (void* dst_, const void* usrc_, size_t size);

int syscall_halt (uint8_t* stack);
int syscall_exit(uint8_t* stack);
int syscall_exec(uint8_t* stack);
int syscall_wait(uint8_t* stack);
int syscall_create(uint8_t* stack);
int syscall_remove(uint8_t* stack);
int syscall_open(uint8_t* stack); 
int syscall_filesize(uint8_t* stack);
int syscall_read(uint8_t* stack);
int syscall_write(uint8_t* stack);
int syscall_seek(uint8_t* stack);
int syscall_tell(uint8_t* stack);
int syscall_close(uint8_t* stack);

#endif /* userprog/syscall.h */
