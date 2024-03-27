/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <hash.h>
#include "threads/mmu.h"
#include "threads/synch.h"

struct lock hash_lock;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	lock_init(&hash_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */

/*
- upage에 대응하도록 페이지 구조체를 생성한 뒤, spt에 할당
- 함수의 입력으로 들어올 수 있는 것은 VM_ANON과 VM_FILE뿐
- VM_UNINIT은 임의로 생성할 수 있는 것이 아닌 시스템 상 임시로만 존재
*/
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)
	struct page* page;
	struct supplemental_page_table *spt = &thread_current()->spt;
	bool (*initializer)(struct page *, enum vm_type, void *);
	bool success;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
		page = malloc(sizeof(struct page));
		initializer = (VM_TYPE(type) == VM_ANON) ? anon_initializer : file_backed_initializer;
		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;
		success = spt_insert_page(spt, page);
		if(init)
		{
			init(page, aux);
		}
		
		return success;

	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = NULL;
	struct page p;
	struct hash_elem *h_el;
	p.va = va;
	h_el = hash_find(&spt->hash, &p.spt_hash_elem);
	if (h_el != NULL)
	{
		page = hash_entry(h_el, struct page, spt_hash_elem);
	}
	return page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	int succ = false;
	struct hash_elem* result;
	if(spt_find_page(spt, page->va))
	{
		return false;
	}	

	result = hash_insert(&spt->hash, &page->spt_hash_elem);

	if(result ==NULL)
	{
		succ = true;
	}
	return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;

	/* TODO: The policy for eviction is up to you. */
	
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	void* newpage = palloc_get_page(PAL_USER);
	if(!newpage)
	{
		PANIC("to do");
	}
	frame = malloc(sizeof(struct frame));
	frame->page = NULL;
	frame->kva = newpage;

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	page = spt_find_page(&thread_current()->spt, pg_round_down(addr));
	if(page ==NULL)
	{
		return false;
	}
	if(is_kernel_vaddr(addr) || is_kernel_vaddr(page->va))
	{
		return false;
	}
	
	return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = malloc(sizeof(struct page));
	page->va = va;
	spt_insert_page(&thread_current()->spt, page);
	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
bool vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();
	bool success;
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	success= pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);
	if (!success)
		return false;
	return swap_in(page, frame->kva);
}


/* returns a hash value for page p*/
unsigned spt_hash(const struct hash_elem *h_el, void *aux UNUSED)
{
	const struct page *p = hash_entry(h_el, struct page, spt_hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

/*returns true if page current precedes page compare*/
bool spt_less(const struct hash_elem *curr, const struct hash_elem *cmp, void *aux UNUSED)
{
	const struct page *current = hash_entry(curr, struct page, spt_hash_elem);
	const struct page *compare = hash_entry(curr, struct page, spt_hash_elem);
	return current->va < compare->va;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	hash_init(&spt->hash, spt_hash, spt_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
