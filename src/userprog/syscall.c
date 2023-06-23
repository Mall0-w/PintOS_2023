#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <stddef.h>
#include "kernel/stdio.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "threads/synch.h"

/*Mapping each syscall to their respective function*/
static int (*syscall_handlers[])(const uint8_t* stack) = {
  [SYS_HALT]syscall_halt,
  [SYS_EXIT]syscall_exit,
  [SYS_EXEC]syscall_exec,
  [SYS_WAIT]syscall_wait,
  [SYS_CREATE]syscall_create,
  [SYS_REMOVE]syscall_remove,
  [SYS_OPEN]syscall_open, 
  [SYS_FILESIZE]syscall_filesize,
  [SYS_READ]syscall_read,
  [SYS_WRITE]syscall_write,
  [SYS_SEEK]syscall_seek,
  [SYS_TELL]syscall_tell,
  [SYS_CLOSE]syscall_close
};

struct lock file_lock;

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
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

/*copy data of size size to dst_ from usrc_.  return false if an error occured, otherwise true*/
bool copy_in (void* dst_, const void* usrc_, size_t size){
  ASSERT (dst_ != NULL || size == 0);
  ASSERT (usrc_ != NULL || size == 0);
  
  int curr;
  uint8_t* dst = dst_;
  const uint8_t* usrc = usrc_;
  if(!is_user_vaddr(usrc) || !is_user_vaddr(usrc + size)){
    printf("invalid vaddr\n");
    return false;
  }  

  for(; size > 0; size--, dst++, usrc++){
    int curr = get_user(usrc);
    if(curr == -1){
      printf("segfault\n");
      return false;
    }
    *dst = curr;
  }
  return true;
}

static void
syscall_handler (struct intr_frame *f) 
{ 
  unsigned interupt_number;
  //copy in interrupt number, exit if error occured
  if(!copy_in(&interupt_number, f->esp, sizeof(interupt_number))){
    printf("invalid copy");
    thread_exit();
  }

  printf ("system call!\n");
  //if interrupt number is valid, call its function and grab return code
  if(interupt_number < sizeof(syscall_handlers) / sizeof(syscall_handlers[0])){
    //setting return code to code given by respective handler
    f->eax = syscall_handlers[interupt_number](f->esp + sizeof(unsigned));
  }else{
    //otherwise return code is -1
    f->eax = -1;
  }
  thread_exit();
}

void syscall_halt (uint8_t* stack){
  return;
}

void syscall_exit(uint8_t* stack){
  return;
}

int syscall_exec(uint8_t* stack){
  return -1;
}

int syscall_wait(uint8_t* stack){
  return -1;
}

bool syscall_create(uint8_t* stack){
  const char* file_name;
  unsigned inital_size;
  uint8_t* curr_pos;
  //copy in arguments
  if(!copy_in(&file_name, stack, sizeof(char*)))
    return false;
  curr_pos += sizeof(char*);

  if(!copy_in(&inital_size, stack, sizeof(unsigned)))
    return false;
  
  lock_acquire(&file_lock);
  bool success = filesys_create(file_name, inital_size);
  lock_release(&file_lock);
  
  return success;
}

bool syscall_remove(uint8_t* stack){
  const char* file_name;
  if(!copy_in(&file_name, stack, sizeof(char*)))
    return false;
  lock_acquire(&file_lock);
  bool success = filesys_remove(file_name);
  lock_release(&file_lock);
  return success;
}

int syscall_open(uint8_t* stack){
  //copy filename froms stack
  char* file_name;
  if (!copy_in(&file_name, stack, sizeof(char*)))
    return -1;
  //open file
  lock_acquire(&file_lock);
  struct file* f = filesys_open(file_name);
  lock_release(&file_lock);
  if(f == NULL){
    return -1;
  }
  
  //allocate memeory for a fd for the file
  struct process_file* new_file = malloc(sizeof(struct process_file));
  struct thread* t = thread_current();
  //asign its fd
  new_file->fd = t->curr_fd+1;
  new_file->file = f;
  t->curr_fd++; 
  //add the file to process' list of files
  list_push_front(&t->opened_files, &new_file->elem);
  //assign a fd and return it
  return new_file->fd;
}

int syscall_filesize(uint8_t* stack){
  int fd;
  if(!copy_in(&fd, stack, sizeof(int)))
    return 0;
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  if(f == NULL){
    lock_release(&file_lock);
    return 0;
  }
  int size = (int)file_length(f);
  lock_release(&file_lock);
  return size;
}

int syscall_read(uint8_t* stack){
  int fd;
  void* buffer;
  unsigned size;
  uint8_t* curr_pos = stack;
  //collect args
  if(!copy_in(&fd, curr_pos, sizeof(int)))
    return -1;
  curr_pos += sizeof(int);
  if(!copy_in(&buffer, stack, sizeof(void*)))
    return -1;
  curr_pos += sizeof(void*);
  if(!copy_in(&size, stack, sizeof(unsigned)))
    return -1;
  
  //acquire lock, find file and read
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  if(f == NULL){
    lock_release(&file_lock);
    return -1;
  }
  int size = file_read(f->file, buffer, (off_t)size);
  lock_release(&file_lock);
  return size;
}

int syscall_write(uint8_t* stack){
  uint8_t* curr_address = stack;
  //int fd = *((int*) curr_address);
  int fd;
  char* buffer;
  int size;

  //copy in respective arguments, checking for valid addresses
  if(!copy_in(&fd, (int*)curr_address, sizeof(int)))
    return -1;
  curr_address += sizeof(int);
  if(!copy_in(&buffer, (char**)curr_address,sizeof(char*)))
    return -1;
  //char* buffer = *((char**)curr_address);
  curr_address += sizeof(char*);
  if(!copy_in(&size, (int*)curr_address, sizeof(int)))
    return -1;

  //printf("fd %d, buffer %s, size %d\n", fd, buffer, size);  
  
  //if to stdout, just put to the buffer
  if(fd == STDOUT_FILENO){
    putbuf(buffer, size);
    return size;
  }
  //TODO: figure out how to handle different file descriptors
  return 0;
}

void syscall_seek(uint8_t* stack){
  int fd;
  unsigned position;
  uint8_t* curr_pos = stack;
  if(!copy_in(&fd, curr_pos, sizeof(int)))
    return;
  
  curr_pos += sizeof(int);

  if(!copy_in(&position, curr_pos, sizeof(unsigned)))
    return;
  
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  if(f == NULL){
    lock_release(&file_lock);
    return;
  }
  file_seek(f->file, position);
  lock_release(&file_lock);

  return;
}

unsigned syscall_tell(uint8_t* stack){
  int fd;
  if(!copy_in(&fd, stack, sizeof(int)))
    return 0;
  struct thread* t = thread_current();
  
  lock_acquire(&file_lock);
  struct process_file* f = find_file(t, fd);
  if(f == NULL){
    lock_release(&file_lock);
    return 0;
  }
  unsigned tell = (unsigned)file_tell(f->file);
  lock_release(&file_lock);
  return tell;
}

void syscall_close(uint8_t* stack){
  int fd;
  if(!copy_in(&fd, stack, sizeof(int)))
    return;
  
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  close_proc_file(f, true);
  return;
}

void close_proc_file(struct process_file* f, bool release_lock){
  ASSERT(f != NULL);
  if(file_lock.holder != thread_current())
    lock_acquire(&file_lock);
  file_close(f->file);
  list_remove(&f->elem);
  free(f);
  if(release_lock)
    lock_release(&file_lock);
  return;
}