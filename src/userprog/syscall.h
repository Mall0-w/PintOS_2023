#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdint.h>
#include <stdbool.h>

void syscall_init (void);

int sys_write(uint8_t* base_addr);
int sys_exit(uint8_t* base_addr);

int get_user (const uint8_t *uaddr);
bool put_user (uint8_t *udst, uint8_t byte);

#endif /* userprog/syscall.h */
