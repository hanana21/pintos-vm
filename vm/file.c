/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "threads/init.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	// printf("file swap in\n");
	struct file_page *file_page = &page->file;
	// page->va = (void *)((uint64_t)page->va | PTE_P);

	void *aux = (void *)((uint64_t)file_page->aux);
	lazy_load_segment (page, aux);
	return true;
}

/* Swap out the page by writeback contents to the file. */
/*	내용을 다시 파일에 기록하여 swap out합니다. 
	먼저 페이지가 dirty  인지 확인하는 것이 좋습니다. 
	더럽지 않으면 파일의 내용을 수정할 필요가 없습니다. 
	페이지를 교체한 후에는 페이지의 더티 비트를 꺼야 합니다. */
static bool
file_backed_swap_out (struct page *page) {
	// printf("file swap out %p\n", page->va);
	struct	file_page *file_page = &page->file;

	if (file_page->aux) {
		struct file_info *f_i = (struct file_info *)page->uninit.aux;
		// printf("!!!%d\n", f_i->page_read_bytes);
		// if (pml4_is_dirty(thread_current()->pml4, pg_round_down (page->va)) || pml4_is_dirty(base_pml4, page->frame->kva))
		if (pml4_is_dirty(thread_current()->pml4, pg_round_down (page->va)))
			file_write_at (f_i->file, pg_round_down (page->frame->kva), f_i->page_read_bytes, f_i->ofs);
	}

	pml4_set_dirty(thread_current()->pml4, pg_round_down (page->va), 0);

	pml4_clear_page(thread_current()->pml4, pg_round_down (page->va));
	// pml4_set_dirty(base_pml4, page->frame->kva, 0);

	// page->va = (void *)((uint64_t)page->va & ~PTE_P);

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	if (file_page->aux) {
		struct file_info *f_i = (struct file_info *)page->file.aux;
		if (pml4_is_dirty(thread_current()->pml4, page->va))
			file_write_at (f_i->file, page->frame->kva, f_i->page_read_bytes, f_i->ofs);
		free(file_page->aux);
	}

	pml4_clear_page(thread_current()->pml4, pg_round_down(page->va));

	if (page->frame) {
		list_remove(&page->frame->f_elem);
		free (page->frame);
	}
}

// /* Do the mmap */
// void *
// do_mmap (void *addr, size_t length, int writable,
// 		struct file *file, off_t offset) {
// }

// /* Do the munmap */
// void
// do_munmap (struct page *page) {
// 	if (page_get_type(page) == VM_FILE) {
// 		struct file_info *file_info = (struct file_info *)page->file.aux;
// 		printf("read bytes: %d\n", (int)file_info->read_bytes);
// 	}
// }
