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
#include "devices/input.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "vm/page.h"

/*Mapping each syscall to their respective function*/
static int (*handlers[])(uint8_t* stack) = {
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
  [SYS_CLOSE]close,
  [SYS_MMAP]mmap,
  [SYS_MUNMAP]munmap
};

/*Lock used to handle filesys concurrency*/
struct lock file_lock;

struct lock error_lock;

bool raised_error = false;

static void syscall_handler (struct intr_frame *);
struct mmap_file* find_mmap_file(struct thread *t, mapid_t mapid);


void
syscall_init (void) 
{
  //have to init lock before register, since register calls syscall handler
  lock_init(&file_lock);
  lock_init(&error_lock);
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
 

/*copy data of size size to dst_ from usrc_.  
return false if an error occured, otherwise true*/
bool copy_in (void* dst_, const void* usrc_, size_t size){
  ASSERT (dst_ != NULL || size == 0);
  ASSERT (usrc_ != NULL || size == 0);
  
  struct thread* curr = thread_current();
  uint8_t* dst = dst_;
  const uint8_t* usrc = usrc_;
  if(!is_user_vaddr(usrc) || !is_user_vaddr(usrc + size) || usrc == NULL){
    return false;
  }
  // Checks if page exists to a mapped physical memory for each byte
  for(int i = 0; i <= 3; i++){
    if(pagedir_get_page(curr->pagedir, usrc + i) == NULL){
      return false;
    }
  }


  for(; size > 0; size--, dst++, usrc++){
    int curr = get_user(usrc);
    if(curr == -1){
      return false;
    }
    *dst = curr;
  }
  return true;
}

/*Function used to determine if a pointer and its following range btres 
are valid addresses for syscalls*/
bool valid_esp(void* ptr, int range){
  struct thread* curr = thread_current();
  for(int i = 0; i <= range; i++){
    if(ptr == NULL) {
      return false;
    }
    if(!is_user_vaddr(ptr + i) || ptr == NULL){
      return false;
    }
    if(pagedir_get_page(curr->pagedir, ptr + i) == NULL){
      return false;
    }
  }
  return true;
}


static void
syscall_handler (struct intr_frame *f) 
{ 
  unsigned interrupt_number;
  
  // Check if the 4 bytes is in user virtual address space and has an existing
  // page associated with it, if all good put it in interrupt_number
  if(!valid_esp((void*) f->esp, 3))
    proc_exit(-1);
  
  //copy in the interrupt number from the stack
  if(!copy_in(&interrupt_number, f->esp, sizeof(interrupt_number))) {
    proc_exit(-1);
  }

  //if interrupt number is valid, call its function and grab return code
  if(interrupt_number < sizeof(handlers) / sizeof(handlers[0])){
    //setting return code to code given by respective handler
    f->eax = handlers[interrupt_number](f->esp + sizeof(unsigned));
    //if an important error occured, resest the flag and exit
    if(raised_error){
      lock_acquire(&error_lock);
      raised_error = false;
      lock_release(&error_lock);
      proc_exit(-1);
    }
  }else{
    //otherwise return code is -1
    f->eax = -1;
    proc_exit(-1);
  }
}

/*handler for SYS_HALT*/
int halt (uint8_t* stack UNUSED){
  shutdown_power_off();
  return -1;
}
/*HANDLER FOR SYS_EXIT*/
int syscall_exit(uint8_t* stack){
  int status;
  if(!copy_in(&status, stack, sizeof(int)))
    status = -1;
  proc_exit(status);
  return status;
}

void proc_exit(int status){
  struct thread* cur = thread_current();
  cur->exit_code = status;
  struct child_process *child = find_child_from_id(cur->tid, &cur->parent->child_processes);
  child->exit_code = status;
  if (status == -1) {
    child->is_alive = false;
  }
  thread_exit();
}

/*HANLDER FOR SYS_EXEC*/
int exec(uint8_t* stack){
  struct thread* cur = thread_current();
  tid_t pid;
  char* cmd_line;
  if(!copy_in(&cmd_line, stack, sizeof(char*))) {
    lock_acquire(&error_lock);
    raised_error = true;
    lock_release(&error_lock);
    return -1;
  }
  // Checks if page exists to a mapped physical memory for every byte in cmd_line
  if(!valid_esp((void*)cmd_line, 3)){
  // Checks if page exists to a mapped physical memory 
  // for every byte in cmd_line
    lock_acquire(&error_lock);
    raised_error = true;
    lock_release(&error_lock);
    return -1;
  }

  pid = process_execute(cmd_line);
  struct child_process *child = find_child_from_id(pid, &cur->child_processes);
  sema_down(&child->t->exec_sema);
  if (!child->load_success) {
    return -1;
  }
  return pid;
}

/*Handler for SYS_WAIT*/
int wait(uint8_t* stack){
  tid_t pid;
  if(!copy_in(&pid, stack, sizeof(tid_t)))
    return -1;
  int status = process_wait(pid);
  return status;
}

/*handler for SYS_CREATE*/
int create(uint8_t* stack){
  //get stack args
  const char* file_name;
  unsigned inital_size;
  uint8_t* curr_pos = stack;
  //copy in arguments
  if(!copy_in(&file_name, curr_pos, sizeof(char*)))
    return -1;
  curr_pos += sizeof(char*);

  if(!copy_in(&inital_size, curr_pos, sizeof(unsigned)))
    return -1;
  
  //checking for null filename or invalid ptr
  if((int*) file_name == NULL || !valid_esp((void*)file_name, 0)) {
    lock_acquire(&error_lock);
    raised_error = true;
    lock_release(&error_lock);
    return -1;
  }

  //acquire lock and call filesys_create
  lock_acquire(&file_lock);
  bool success = filesys_create(file_name, inital_size);
  lock_release(&file_lock);
  
  return (int)success;
}

/*Handler for SYS_REMOVE*/
int remove(uint8_t* stack){
  //get the file name
  const char* file_name;
  if(!copy_in(&file_name, stack, sizeof(char*)))
    return false;
  //acquire lock, remove then release
  lock_acquire(&file_lock);
  bool success = filesys_remove(file_name);
  lock_release(&file_lock);
  return (int)success;
}

/*Handler for SYS_OPEn*/
int open(uint8_t* stack){
  //copy filename from stack
  char* file_name;
  if (!copy_in(&file_name, stack, sizeof(char*)))
    return -1;

  // Checks if the file name goes into unmapped memory
  if((int*) file_name == NULL || !valid_esp((void*)file_name, 3)){
    lock_acquire(&error_lock);
    raised_error = true;
    lock_release(&error_lock);
    return -1;
  }
  
  //open file
  lock_acquire(&file_lock);
  struct file* f = filesys_open(file_name);
  if(f == NULL){
    return -1;
  }
  bool is_exe = is_file_exe(f);
  if(is_exe){
    //deny writing if its an exe
    file_deny_write(f);
  }
  
  //allocate memeory for a fd for the file
  struct process_file* new_file = malloc(sizeof(struct process_file));
  if(new_file == NULL){
    file_close(f);
    lock_release(&file_lock);
    return -1;
  }
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
int filesize(uint8_t* stack){
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
int read(uint8_t* stack){
  int fd;
  void* buffer;
  unsigned size;
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

  if(!valid_esp((void*)buffer, 0)) {
    lock_acquire(&error_lock);
    raised_error = true;
    lock_release(&error_lock);
    return -1;
  }

  if(fd == STDOUT_FILENO){
    return -1;
  }else if(fd == STDIN_FILENO){
    return (int) input_getc();
  }

  //acquire lock, find file and read
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  if(f == NULL){
    lock_release(&file_lock);
    return -1;
  }
  int read_size = file_read(f->file, buffer, (off_t) size);
  lock_release(&file_lock);
  return read_size;
}

/*Handler for SYS_WRITE*/
int write(uint8_t* stack){
  uint8_t* curr_address = stack;
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

  // Checks if the buffer goes into unmapped memory
  if(!valid_esp((void*)buffer, 0)) {
    lock_acquire(&error_lock);
    raised_error = true;
    lock_release(&error_lock);
    return -1;
  }
  

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

  int write_size = 0;
  if(is_file_exe(f->file)){
    //if file is an executable, check if what we're writing will change 
    // the contents, if not then pretend to write
    char read_buffer[size];
    int read_size = file_read(f->file,read_buffer,size);
    if(read_size == size && strcmp(read_buffer, buffer) == 0){
      write_size = size;
    }
  }else{
    //write to file, then release lock
    write_size = (int)file_write(f->file, buffer, size);
  }

  lock_release(&file_lock);
  return write_size;
  
}

/*Handler for SYS_SEEK*/
int seek(uint8_t* stack){
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
int tell(uint8_t* stack){
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
int close(uint8_t* stack){
  //copy in args
  int fd;
  if(!copy_in(&fd, stack, sizeof(int)))
    return -1;

  if(fd == STDIN_FILENO || fd == STDOUT_FILENO)
    return -1;
  //acquire lock and file
  lock_acquire(&file_lock);
  struct process_file* f = find_file(thread_current(), fd);
  if(f == NULL){
    lock_release(&file_lock);
    return -1;
  } 
    
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

mapid_t mmap(uint8_t* stack) {
  struct thread *cur = thread_current();
  int fd;
  void *addr;
  uint8_t* curr_pos = stack;
  if(!copy_in(&fd, curr_pos, sizeof(int)))
    return -1;
  curr_pos += sizeof(int);
  if(!copy_in(&addr, curr_pos, sizeof(void*)))
    return -1;

  // Checks if the buffer goes into unmapped memory
  if(fd == 0 || fd == 1 || !valid_esp((void*)addr, 0)) {
    lock_acquire(&error_lock);
    raised_error = true;
    lock_release(&error_lock);
    return -1;
  }

  lock_acquire(&file_lock);

  struct process_file* f = find_file(cur, fd);

  if(f == NULL || file_length(f) == 0){
    lock_release(&file_lock);
    return -1;
  }

  size_t offset;
  void *cur_addr;

  for (size_t i = 0; i < file_length(f); i++) {
    offset = i * PGSIZE;
    cur_addr = addr + offset;
    if (sup_pt_find(&cur->spt, cur_addr)) {
      lock_release(&file_lock);
      return -1;
    }
  }

  for (size_t i = 0; i < file_length(f); i++) {
    offset = i * PGSIZE;
    cur_addr = addr + offset;
    
    size_t page_read_bytes = PGSIZE < file_length(f) - offset ? PGSIZE : file_length(f) - offset;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    sup_pt_insert(&cur->spt, FILE_ORIGIN, cur_addr, f, offset, true, page_read_bytes, page_zero_bytes);
  }
  
  mapid_t mapid;
  if (!list_empty(&cur->mmap_files)) {
    mapid = list_entry(list_back(&cur->mmap_files), struct mmap_file, mmap_elem)->id + 1;
  } else {
    mapid = 1;
  }

  struct mmap_file *mmap_f = (struct mmap_file *) malloc(sizeof(struct mmap_file));
  mmap_f->id = mapid;
  mmap_f->file = f;
  mmap_f->addr = addr;
  list_push_back(&cur->mmap_files, &mmap_f->mmap_elem);

  lock_release(&file_lock);
  return mapid;
}

bool munmap(uint8_t* stack) {
  struct thread *cur = thread_current();
  mapid_t mapid;
  if(!copy_in(&mapid, stack, sizeof(int)))
    return -1;
  struct mmap_file *mmap_f = find_mmap_file(cur, mapid);

  if (mmap_f == NULL) return false;

  lock_acquire(&file_lock);

  size_t offset;
  void *cur_addr;

  for (size_t i = 0; i < file_length(mmap_f->file); i++) {
    offset = i * PGSIZE;
    cur_addr = mmap_f->addr + offset;
    size_t page_read_bytes = PGSIZE < file_length(mmap_f->file) - offset ? PGSIZE : file_length(mmap_f->file) - offset;
  }
}

struct mmap_file*
find_mmap_file(struct thread *t, mapid_t mapid) {
  ASSERT (t != NULL);
  struct list_elem *e;
  if (! list_empty(&t->mmap_files)) {
    for(e = list_begin(&t->mmap_files);
        e != list_end(&t->mmap_files); e = list_next(e))
    {
      struct mmap_file *mmap_f = list_entry(e, struct mmap_file, mmap_elem);
      if(mmap_f->id == mapid) {
        return mmap_f;
      }
    }
  }
  return NULL;
}