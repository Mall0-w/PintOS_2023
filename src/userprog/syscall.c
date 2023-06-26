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
#include "threads/malloc.h"

/*Mapping each syscall to their respective function*/
static int (*handlers[])(const uint8_t* stack) = {
  [SYS_HALT]halt,
  [SYS_EXIT]syscall_exit,
  [SYS_EXEC]exec,
  [SYS_WAIT]wait,
  [SYS_CREATE]create,
  [SYS_REMOVE]remove,
  [SYS_OPEN]open, 
  [SYS_FILESIZE]filesize,
  [SYS_READ]read,
  [SYS_WRITE]write,
  [SYS_SEEK]seek,
  [SYS_TELL]tell,
  [SYS_CLOSE]close
};

/*Lock used to handle filesys concurrency*/
struct lock file_lock;

static void syscall_handler (struct intr_frame *);


void
syscall_init (void) 
{
  //have to init lock before register, since register calls syscall handler
  lock_init(&file_lock);
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

void
get_args (uint8_t *stack, int argc, int *argv) {
  int *next_arg;
  for (int i = 0; i < argc; i++) {
    next_arg = stack + i + i;
    argv[i] = *next_arg;
  }
}

static void
syscall_handler (struct intr_frame *f) 
{ 
  unsigned interupt_number;
  //copy in interrupt number, exit if error occured
  if(!memcpy(&interupt_number, f->esp, sizeof(interupt_number))){
    proc_exit(-1);
  }
  //if interrupt number is valid, call its function and grab return code
  if(interupt_number < sizeof(handlers) / sizeof(handlers[0])){
    //setting return code to code given by respective handler
    f->eax = handlers[interupt_number](f->esp + sizeof(unsigned));
  }else{
    //otherwise return code is -1
    f->eax = -1;
    proc_exit(-1);
  }
  // thread_exit();
}

/*handler for SYS_HALT*/
int halt (const uint8_t* stack){
  shutdown_power_off();
  return -1;
}
/*HANDLER FOR SYS_EXIT*/
int syscall_exit(const uint8_t* stack){
  int status;
  if(!memcpy(&status, stack, sizeof(int)))
    status = -1;
  thread_exit();
  return status;
}

void proc_exit(int status){
  struct thread* curr = thread_current();
  curr->exit_code = status;
  thread_exit();
}

/*HANLDER FOR SYS_EXEC*/
int exec(const uint8_t* stack){
  tid_t tid;
  int argv[1];
  get_args((uint8_t*)stack, 1, argv);
  char* cmd_line = argv[0];
  tid = process_execute(cmd_line);
  return tid;
}

/*Handler for SYS_WAIT*/
int wait(const uint8_t* stack){
  int argv[1];
  get_args((uint8_t*)stack, 1, argv);
  int pid = argv[0];
  int status = process_wait(pid);
  return status;
}

/*handler for SYS_CREATE*/
int create(const uint8_t* stack){
  //get stack args
  const char* file_name;
  unsigned inital_size;
  uint8_t* curr_pos = stack;
  //copy in arguments
  if(!memcpy(&file_name, curr_pos, sizeof(char*)))
    return false;
  curr_pos += sizeof(char*);

  if(!memcpy(&inital_size, curr_pos, sizeof(unsigned)))
    return false;
  //acquire lock and call filesys_create
  lock_acquire(&file_lock);
  bool success = filesys_create(file_name, inital_size);
  lock_release(&file_lock);
  
  return (int)success;
}

/*Handler for SYS_REMOVE*/
int remove(const uint8_t* stack){
  //get the file name
  const char* file_name;
  if(!memcpy(&file_name, stack, sizeof(char*)))
    return false;
  //acquire lock, remove then release
  lock_acquire(&file_lock);
  bool success = filesys_remove(file_name);
  lock_release(&file_lock);
  return (int)success;
}

/*Handler for SYS_OPEn*/
int open(const uint8_t* stack){
  //copy filename from stack
  char* file_name;
  if (!memcpy(&file_name, stack, sizeof(char*)))
    return -1;
  //open file
  lock_acquire(&file_lock);
  struct file* f = filesys_open(file_name);
  if(f == NULL){
    return -1;
  }

  
  //allocate memeory for a fd for the file
  struct process_file* new_file = malloc(sizeof(struct process_file));
  struct thread* t = thread_current();
  //asign its fd, its file, and increment thread's fd counter
  new_file->fd = t->curr_fd+1;
  new_file->file = f;
  t->curr_fd++; 
  //add the file to process' list of files
  list_push_front(&t->opened_files, &new_file->elem);
  //release the lock for the file system
  lock_release(&file_lock);
  //return assigned fd
  return new_file->fd;
}

/*Handler for SYS_FILESIZE*/
int filesize(const uint8_t* stack){
  //copy in fd
  int fd;
  if(!memcpy(&fd, stack, sizeof(int)))
    return 0;
  //acquire lock, file from fd, then get size
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  if(f == NULL){
    lock_release(&file_lock);
    return 0;
  }
  int size = (int)file_length(f->file);
  //release lock and return size
  lock_release(&file_lock);
  return size;
}

/*Handler for SYS_READ*/
int read(const uint8_t* stack){
  int fd;
  void* buffer;
  int size;
  uint8_t* curr_pos = stack;
  //collect args
  if(!memcpy(&fd, curr_pos, sizeof(int)))
    return -1;
  curr_pos += sizeof(int);
  if(!memcpy(&buffer, curr_pos, sizeof(void*)))
    return -1;
  curr_pos += sizeof(void*);
  if(!memcpy(&size, curr_pos, sizeof(unsigned)))
    return -1;
  
  //acquire lock, find file and read
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  if(f == NULL){
    lock_release(&file_lock);
    return -1;
  }
  size = (int)file_read(f->file, buffer, (off_t)size);
  lock_release(&file_lock);
  return size;
}

/*Handler for SYS_WRITE*/
int write(const uint8_t* stack){
  uint8_t* curr_address = stack;
  //int fd = *((int*) curr_address);
  int fd;
  char* buffer;
  int size;

  //copy in respective arguments, checking for valid addresses
  if(!memcpy(&fd, (int*)curr_address, sizeof(int)))
    return -1;
  curr_address += sizeof(int);
  if(!memcpy(&buffer, (char**)curr_address,sizeof(char*)))
    return -1;
  //char* buffer = *((char**)curr_address);
  curr_address += sizeof(char*);
  if(!memcpy(&size, (int*)curr_address, sizeof(int)))
    return -1;
  
  //if to stdout, just put to the buffer
  if(fd == STDOUT_FILENO){
    putbuf(buffer, size);
    return size;
  }
  //acquire lock for filesys and find file
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  if(f == NULL){
    lock_release(&file_lock);
    return -1;
  }
  //write to file, then release lock
  size = (int)file_write(f->file, buffer, size);
  lock_release(&file_lock);
  return size;
  
}

/*Handler for SYS_SEEK*/
int seek(const uint8_t* stack){
  //copy in args
  int fd;
  unsigned position;
  uint8_t* curr_pos = stack;
  if(!memcpy(&fd, curr_pos, sizeof(int)))
    return -1;
  
  curr_pos += sizeof(int);

  if(!memcpy(&position, curr_pos, sizeof(unsigned)))
    return -1;
  
  //acquire lock and find file
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  if(f == NULL){
    lock_release(&file_lock);
    return -1;
  }
  //call seek then release lock
  file_seek(f->file, position);
  lock_release(&file_lock);

  return 1;
}

/*Hanlder for SYS_TELL*/
int tell(const uint8_t* stack){
  //copy in args
  int fd;
  if(!memcpy(&fd, stack, sizeof(int)))
    return 0;
  struct thread* t = thread_current();
  //acquire lock and file
  lock_acquire(&file_lock);
  struct process_file* f = find_file(t, fd);
  if(f == NULL){
    lock_release(&file_lock);
    return 0;
  }
  //get tell and release lock
  unsigned tell = (unsigned)file_tell(f->file);
  lock_release(&file_lock);
  return (int)tell;
}

//Handler for SYS_CLOSE
int close(const uint8_t* stack){
  //copy in args
  int fd;
  if(!memcpy(&fd, stack, sizeof(int)))
    return -1;
  //acquire lock and file
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  //call handler for closing process file based on process_file
  close_proc_file(f, true);
  return 1;
}

/*Function that takes a process_file f and closes it
if release_lock is true it will release file_lock*/
void close_proc_file(struct process_file* f, bool release_lock){
  ASSERT(f != NULL);
  //check if already holding file lock, if not acquire it
  if(file_lock.holder != thread_current())
    lock_acquire(&file_lock);
  //close the file and remove it from the thread's files
  file_close(f->file);
  list_remove(&f->elem);
  //free the process_file that was allocated on create
  free(f);
  //release lock if set to true
  if(release_lock)
    lock_release(&file_lock);
  return;
}