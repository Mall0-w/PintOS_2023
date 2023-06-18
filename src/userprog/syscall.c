#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/*Mapping each syscall to their respective function*/
static int (*syscalls[])(const uint8_t* stack) = {
  [SYS_HALT] syscall_halt,
  [SYS_EXIT] syscall_exit,
  [SYS_EXEC] syscall_exec,
  [SYS_WAIT] syscall_wait,
  [SYS_CREATE] syscall_create,
  [SYS_REMOVE] syscall_remove,
  [SYS_OPEN] syscall_open, 
  [SYS_FILESIZE] syscall_filesize,
  [SYS_READ] syscall_read,
  [SYS_WRITE] syscall_write,
  [SYS_SEEK] syscall_seek,
  [SYS_TELL] syscall_tell,
  [SYS_CLOSE] syscall_close
};

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

	

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static void
copy_in (void* dst_, const void* usrc_, size_t size){
  uint8_t* dst = dst_;
  const uint8_t* usrc = usrc_;
  
  for(; size > 0; size--, dst++, usrc++){
    *dst = get_user(usrc);
  }
}

static void
syscall_handler (struct intr_frame *f) 
{ 
  unsigned interupt_number;
  copy_in(&interupt_number, f->esp, sizeof(interupt_number));
  printf ("system call!\n");
  if(interupt_number < sizeof(syscalls) / sizeof(*syscalls)){
    f->eax = syscalls[interupt_number]((uint8_t*)f->esp + sizeof(interupt_number));
  }
  thread_exit();
}

int syscall_halt (uint8_t* stack){
  return -1;
}

int syscall_exit(uint8_t* stack){
  return -1;
}

int syscall_exec(uint8_t* stack){
  return -1;
}

int syscall_wait(uint8_t* stack){
  return -1;
}

int syscall_create(uint8_t* stack){
  return -1;
}

int syscall_remove(uint8_t* stack){
  return -1;
}

int syscall_open(uint8_t* stack){
  return -1;
}

int syscall_filesize(uint8_t* stack){
  return -1;
}

int syscall_read(uint8_t* stack){
  return -1;
}

int syscall_write(uint8_t* stack){
  printf("write called\n");
  return -1;
}

int syscall_seek(uint8_t* stack){
  return -1;
}

int syscall_tell(uint8_t* stack){
  return -1;
}

int syscall_close(uint8_t* stack){
  return -1;
}