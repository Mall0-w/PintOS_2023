#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

/* Array of system calls, maps the system call enum
to a function that takes in the base address of the arguments
and returns an int (the return code of the handler)*/
static int (*syscalls[])(const uint8_t *arg_base) =
{
  [SYS_EXIT] sys_exit,
  [SYS_WRITE] sys_write,
};

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

	
/*Gets the interrupt code of the given interrupt,
  returns false if unsafe memory acess or error occured,
  otherwise returns true and puts error code int code*/
static bool
get_interrupt_code(const uint8_t *user_address, int* code){
  
  //check to make sure that the user address is valid and so is its offset after
  //the code is also valid, if not return fasle
  if(!is_user_vaddr(user_address) || !is_user_vaddr(user_address+4))
    return false;
  //intalize variables for all four bytes of memory and set using virtual address
  int byte_1 = get_user(user_address);
  int byte_2 = get_user(user_address+1);
  int byte_3 = get_user(user_address+2);
  int byte_4 = get_user(user_address+3);

  //check that all bytes didn't encounter an error while calling get_user
  if(byte_1 == -1 || byte_2 == -1 || byte_3 == -1 || byte_4 == -1)
    return false;
  
  //if all bytes were accessed correctly, concat them all to make an unsigned 32 bit integer
  //according to lectures and such, the integer we want should be
  // [byte_4][byte_3][byte_2][byte_1] (because its a stack and not a queue), so need to bit shift accordingly
  //also need to make sure to cast to a 8bit unsigned int
  // do this by bit shifting byte_4 24, byte_3, 16, byte_2 8, leaving byte_1
  *code = (uint8_t)byte_4 << 24 | (uint8_t)byte_3 << 16 | (uint8_t)byte_2 << 8 | (uint8_t)byte_1;
  //everything went off well, so return true for a success
  return true;
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //declaring int to map to interrupt code
  //stored as an unsigned 32 bit int
  unsigned int interrupt_code;
  //get the interrupt code from the frame
  
  
  printf ("system call!\n");
  if(!get_interrupt_code(f->esp, &interrupt_code))
    thread_exit ();
  
  //if code is valid, check to map it to the appropriate handler
  //using sizeof so don't have to keep manually changing a variable for number of syscalls,
  //instead divide the size of the entire array by size of indiv element (first element)
  if(interrupt_code >= 0 && interrupt_code < sizeof syscalls / sizeof (* syscalls)
    && syscalls[interrupt_code] != NULL){
      //if valid, execute the appropriate handler and put its returned code into the
      //frame's eax (the part of the frame that handles the return value), passing the rest
      //of the stack after the interrupt code as its parameters
      f->eax = syscalls[interrupt_code] ((uint8_t *) f->esp + sizeof(int));
  }else{
    //if not vaild code, then pass -1 to raise an error
    f->eax = -1;
  }

}

static int sys_exit(uint8_t* base_addr){
  return -1;
}

static int sys_write(uint8_t* base_addr){
  return -1;
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
