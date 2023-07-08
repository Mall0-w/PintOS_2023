#ifndef VM_FRAME_H
#define VM_FRAME_H

struct frame {
  void *frame_addr;
  struct page *page;
};

void frame_init (void);

struct frame frame_get (void);

void frame_free (struct frame *f);

#endif /*vm/frame.h*/