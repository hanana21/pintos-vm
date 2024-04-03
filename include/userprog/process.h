#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct file_info
{
	struct file		*file;
	off_t			ofs;
	size_t			read_bytes;
	size_t			page_read_bytes;
	size_t			page_zero_bytes;
};


tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
struct thread *get_child_process(int pid);

bool mmap_load_segment (struct file *file, off_t ofs, uint8_t *upage, size_t length, bool writable);
void do_munmap(void *addr);

bool lazy_load_segment (struct page *page, void *aux);

#endif /* userprog/process.h */
