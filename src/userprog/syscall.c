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
#include <string.h>

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
  if(!copy_in(&interupt_number, f->esp, sizeof(interupt_number))){
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
}

/*handler for SYS_HALT*/
int halt (const uint8_t* stack){
  shutdown_power_off();
  return -1;
}
/*HANDLER FOR SYS_EXIT*/
int syscall_exit(const uint8_t* stack){
  int status;
  if(!copy_in(&status, stack, sizeof(int)))
    status = -1;
  proc_exit(status);
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
  if(!is_kernel_vaddr((void*) argv) || !is_user_vaddr((void*) argv))
    return -1;
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
  if(!copy_in(&file_name, curr_pos, sizeof(char*)))
    return 0;
  curr_pos += sizeof(char*);

  if(!copy_in(&inital_size, curr_pos, sizeof(unsigned)))
    return 0;
  
  if(!is_user_vaddr((void*) file_name) || !is_kernel_vaddr((void*) file_name) || file_name == NULL || strnlen(file_name, 128) == 0)
    return 0;

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
  if(!copy_in(&file_name, stack, sizeof(char*)))
    return false;
  if(!is_user_vaddr(file_name) || file_name == NULL || strnlen(file_name, 128) == 0)
    return -1;
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
  if (!copy_in(&file_name, stack, sizeof(char*)))
    return -1;
  if(!is_user_vaddr((void*) file_name) || !is_kernel_vaddr((void*) file_name) || file_name == NULL || strnlen(file_name, 128) == 0)
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
  if(!copy_in(&fd, stack, sizeof(int)))
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
  if(!copy_in(&fd, curr_pos, sizeof(int)))
    return -1;
  curr_pos += sizeof(int);
  if(!copy_in(&buffer, curr_pos, sizeof(void*)))
    return -1;
  curr_pos += sizeof(void*);
  if(!copy_in(&size, curr_pos, sizeof(unsigned)))
    return -1;
  
  if(fd == STDOUT_FILENO || !is_user_vaddr(buffer) || !is_kernel_vaddr(buffer)){
    return -1;
  }

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
  if(!copy_in(&fd, (int*)curr_address, sizeof(int)))
    return -1;
  curr_address += sizeof(int);
  if(!copy_in(&buffer, (char**)curr_address,sizeof(char*)))
    return -1;
  //char* buffer = *((char**)curr_address);
  curr_address += sizeof(char*);
  if(!copy_in(&size, (int*)curr_address, sizeof(int)))
    return -1;
  
  //if to stdout, just put to the buffer
  if(fd == STDOUT_FILENO || !is_user_vaddr(buffer) || !is_kernel_vaddr(buffer)){
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
  if(!copy_in(&fd, curr_pos, sizeof(int)))
    return -1;
  
  curr_pos += sizeof(int);

  if(!copy_in(&position, curr_pos, sizeof(unsigned)))
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
  if(!copy_in(&fd, stack, sizeof(int)))
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
  if(!copy_in(&fd, stack, sizeof(int)))
    return -1;

  if(fd == STDIN_FILENO || fd == STDOUT_FILENO)
    return -1;
  //acquire lock and file
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  if(f == NULL)
    return -1;
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