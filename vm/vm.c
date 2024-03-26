/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "lib/kernel/hash.h"
#include "threads/mmu.h"
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

static void perror(char * name, char * error);
// static void set_offset(void *va, int64_t offset);
// static int64_t find_offset(void *va);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	type = type & 0b11;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		struct page *page = (struct page *)calloc(sizeof(struct page), 1);
		if (page == NULL) {
			perror("vm_alloc_page_with_initializer", "calloc");
			goto err;
		}

		if (writable)
			upage = (void *)((uint64_t)upage | PTE_W);

		switch (type) {
			case VM_ANON:
				uninit_new(page, upage, init, type, aux, anon_initializer);
				break ;
			case VM_FILE:
				uninit_new(page, upage, init, type, aux, file_backed_initializer);	
				break ;
		}

		if (!spt_insert_page(spt, page))
			goto err;

		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page p;
	struct hash_elem *e;

	p.va = va;
	e = hash_find (&spt->h, &p.spt_elem);
	return e != NULL ? hash_entry (e, struct page, spt_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	printf("spt_insert_page : %p\n", page);
	return hash_insert(spt->h, &page->spt_elem) != NULL ? true : false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame	*frame = (struct frame *)calloc(sizeof(struct frame), 1);
	if (frame == NULL)
		perror("vm_get_frame", "calloc");
	frame->kva = palloc_get_page(PAL_USER);
	/* TODO: Fill this function. */
	if (frame->kva == NULL)
		PANIC("todo");

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = spt_find_page (spt, addr);
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	// spt 순회하면서 addr 일치하는 녀석 있는지 확인
	if (page == NULL) {
		perror("vm_try_handle_fault", "spt_find_page");
		return false;
	}

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	struct page *page = (struct page *)calloc(sizeof(struct page), 1);
	if (page == NULL)
		perror("va_claim_page", "calloc");
	/* TODO: Fill this function */
	page->va = va;
	uninit_new(page, va, NULL, VM_UNINIT, NULL, NULL);

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame	*frame = vm_get_frame ();
	struct thread	*cur = thread_current();
	uint64_t		uaddr = (uint64_t)page->va & ~PGMASK;
	bool			writable = (uint64_t)page->va & PTE_W;

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (pml4_get_page (cur->pml4, uaddr) == NULL) {
		perror("vm_do_claim_page", "pml4_get_page is NULL");
		palloc_free_page (frame->kva);
		return false;
	}
	pml4_set_page(cur->pml4, uaddr, frame->kva, writable);
	frame->kva = (uint64_t)frame->kva | ((uint64_t)page->va & 0xfff);
	// set_offset(frame->kva, get_offset(page->va));

	return swap_in (page, frame->kva);
}

static uint64_t spt_hash_func(const struct hash_elem *e, void *aux) {
	const struct page *page = hash_entry(e, struct page, spt_elem);
	return hash_bytes (&page->va, sizeof(page->va));
}

static bool spt_hash_less_func(const struct hash_elem *a,
						const struct hash_elem *b,
						void *aux) {
	const struct page *page_a = hash_entry(a, struct page, spt_elem);
	const struct page *page_b = hash_entry(b, struct page, spt_elem);
	return page_a->va < page_b->va ? true : false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init (spt->h, spt_hash_func, spt_hash_less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	dst->h->elem_cnt = src->h->elem_cnt;
	dst->h->bucket_cnt = src->h->bucket_cnt;
	dst->h->hash = src->h->hash;
	dst->h->less = src->h->less;
	dst->h->aux = src->h->aux;

	int cnt = 0;
	for (int i = 0; i < src->h->bucket_cnt; i++) {
		struct list_elem *iter = list_begin(&src->h->buckets[i]);
		for ( ; iter != list_end(&src->h->buckets[i]); list_next(iter)) {
			list_push_back(&dst->h->buckets[i], iter);
			cnt++;
		}
	}
	return cnt == src->h->elem_cnt ? true : false;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

}

static void perror(char * name, char * error) {
	printf("Error func: %s\n%s error\n", name, error);
}
