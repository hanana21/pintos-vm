/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/pte.h"
#include "threads/mmu.h"
#include "threads/init.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "userprog/process.h"

static struct list frame_list;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
	list_init(&frame_list);
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

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	if (spt_find_page (spt, upage) == NULL) {
		struct page *page = (struct page *)calloc(sizeof(struct page), 1);
		if (page == NULL)
			goto err;
	
		switch (VM_TYPE(type)) {
			case VM_ANON:
				uninit_new(page, upage, init, type, aux, anon_initializer);	
				break ;
			case VM_FILE:
				uninit_new(page, upage, init, type, aux, file_backed_initializer);	
				break ;
		}

		page->writable = writable;

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

	p.va = pg_round_down (va);
	e = hash_find (&spt->h, &p.hash_elem);
	return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	return hash_insert(&spt->h, &page->hash_elem) == NULL ? true : false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	/* TODO: The policy for eviction is up to you. */
	// if (!list_empty(&frame_list))
	return list_entry (list_front (&frame_list), struct frame, f_elem);
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();

	/* TODO: swap out the victim and return the evicted frame. */
	if (!victim)
		return NULL;

	if (!swap_out(victim->page))
		return NULL;

	list_remove (&victim->f_elem);
	victim->page->frame = NULL;
	victim->page = NULL;

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame	*frame = NULL;
	void			*kva = NULL;

	kva = palloc_get_page(PAL_USER);
	if (kva == NULL) {
		frame = vm_evict_frame();
	}
	else {
		frame = (struct frame *)calloc(sizeof(struct frame), 1);
		if (frame == NULL)
			PANIC("vm_get_frame : malloc fault");
		frame->kva = kva;
	}

	list_push_back (&frame_list, &frame->f_elem);

	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	addr = pg_round_down(addr);
	while ((spt_find_page(&thread_current()->spt, addr)) == NULL) {
		vm_alloc_page(VM_ANON | VM_MARKER_0, addr, 1);
		vm_claim_page(addr);
		addr += PGSIZE;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	// if (pg_round_down (addr) == 0x4747f000)
	// 	printf("handle user %d/ write %d /present %d\n", user, write, not_present);

	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	if (not_present) {
		// printf("addr : %p, cmp1 : %p, cmp2 : %p\n", (uint64_t)addr, USER_STACK - STACK_MAX_SIZE, f->rsp - 8);
		page = spt_find_page(spt, pg_round_down (addr));
		if (page == NULL) {
			if ((uint64_t)addr > USER_STACK - STACK_MAX_SIZE && \
				(uint64_t)addr & VM_MARKER_0 && \
				(uint64_t)addr ==  f->rsp - 8) {
				// ((uint64_t)addr <=  f->rsp - 8 || (uint64_t)addr < f->rsp - 32)) {
				// printf("!!!!\n");
				vm_stack_growth(addr);
				return true;
			}
			else 
				return false;
		}

		// if (page == NULL) {
		// 	return false;
		// }
		return vm_claim_page (addr);
	}
	return false;
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
	struct page *page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL)
		return false;
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame	*frame = vm_get_frame ();
	struct thread	*cur = thread_current();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page(cur->pml4, page->va, frame->kva, page->writable))
		return false;

	return swap_in (page, frame->kva);
}

static uint64_t spt_hash_func(const struct hash_elem *e, void *aux) {
	const struct page *page = hash_entry(e, struct page, hash_elem);
	return hash_bytes (&page->va, sizeof(page->va));
}

static bool spt_hash_less_func(const struct hash_elem *a,
						const struct hash_elem *b,
						void *aux) {
	const struct page *page_a = hash_entry(a, struct page, hash_elem);
	const struct page *page_b = hash_entry(b, struct page, hash_elem);
	return page_a->va < page_b->va;
}


/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init (&spt->h, spt_hash_func, spt_hash_less_func, NULL);
}

static bool
duplicate_aux(void *src_aux, void **aux) {
    if (src_aux == NULL)
        return true;

    struct file_info *parent_aux = (struct file_info *)src_aux;
    struct file_info *child_aux = calloc(sizeof(struct file_info), 1);
    if(child_aux == NULL)
        return false;

    child_aux->file = parent_aux->file;
    child_aux->ofs = parent_aux->ofs;
    child_aux->page_read_bytes = parent_aux->page_read_bytes;
    child_aux->page_zero_bytes = parent_aux->page_zero_bytes;
    *aux = (void *)child_aux;

    return true;
}

static bool
page_copy_action(struct supplemental_page_table *spt, struct page *src) {
    enum vm_type type = page_get_type(src);
    bool writable = src->writable;
    vm_initializer *init = NULL;
    void *src_aux = NULL;
    void *upage = src->va;
    struct page *dst;

    switch (VM_TYPE(src->operations->type)) {
        case VM_UNINIT:
            src_aux = src->uninit.aux;
            init = src->uninit.init;
            break;
        case VM_ANON:
            src_aux = src->anon.aux;
            init = src->anon.init;
            break;
        case VM_FILE:
            src_aux = src->file.aux;
            init = src->file.init;
            break;
    }

    void *aux = NULL;
    if (!duplicate_aux(src_aux, &aux))
        return false;

    if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
        return false;

    dst = spt_find_page(spt, upage);
    if (dst == NULL)
        return false;

    if (VM_TYPE(src->operations->type) != VM_UNINIT) {
        if (vm_do_claim_page(dst))
            memcpy(dst->frame->kva, src->frame->kva, PGSIZE);
    }

    return true;
}

bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	// printf("########################  %s  ########################\n", "supplemental_page_table_copy");
	struct hash *src_hash = &src->h;
    struct hash_iterator src_iter;

    hash_first(&src_iter, src_hash);
    while (hash_next (&src_iter)) {
        struct page *src_page = hash_entry (hash_cur (&src_iter), struct page, hash_elem);
        if(!page_copy_action(dst, src_page)) {
            return false;
        }
    }
    return true;
}


static void
hash_destroy_action(struct hash_elem *e, void *aux) {
	struct page *page = hash_entry(e, struct page, hash_elem);
	vm_dealloc_page(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	hash_clear(&spt->h, hash_destroy_action);
}

void print_spt(void) {
	struct hash *h = &thread_current()->spt.h;
	struct hash_iterator i;

	printf("============= {%s} SUP. PAGE TABLE (%d entries) =============\n", thread_current()->name, hash_size(h));
	printf("   USER VA    | KERN VA (PA) |     TYPE     | STK | WRT | DRT(K/U) \n");

	void *va, *kva;
	enum vm_type type;
	char *type_str, *stack_str, *writable_str,*dirty_k_str, *dirty_u_str;
	size_t read_bytes = 0;
	stack_str = " - ";

	hash_first (&i, h);
	struct page *page;
	while (hash_next (&i)) {
		page = hash_entry (hash_cur (&i), struct page, hash_elem);

		va = page->va;
		if (page->frame) {
			kva = page->frame->kva;
			writable_str = (uint64_t)page->va & PTE_W ? "YES" : "NO";
			dirty_u_str = pml4_is_dirty(thread_current()->pml4, page->va) ? "YES" : "NO";
			dirty_k_str = pml4_is_dirty(base_pml4, page->frame->kva) ? "YES" : "NO";
		} 
		else {
			kva = NULL;
			dirty_k_str = " - ";
			dirty_u_str = " - ";
		}

		type = page->operations->type;
		if (VM_TYPE(type) == VM_UNINIT) {
			type = page->uninit.type;
			switch (VM_TYPE(type)) {
				case VM_ANON:
					type_str = "UNINIT-ANON";
					break;
				case VM_FILE:
					type_str = "UNINIT-FILE";
					break;
				case VM_PAGE_CACHE:
					type_str = "UNINIT-P.C.";
					break;
				default:
					type_str = "UNKNOWN (#)";
					type_str[9] = VM_TYPE(type) + 48; // 0~7 사이 숫자의 아스키 코드
			}
			// stack_str = type & IS) ? "YES" : "NO";
			struct file_page_args *fpargs = (struct file_page_args *) page->uninit.aux;
			writable_str = (uint64_t)page->va & PTE_W ? "(Y)" : "(N)";
		} else {
			stack_str = "NO";
			switch (VM_TYPE(type)) {
				case VM_ANON:
					type_str = "ANON";
					struct file_info *file_i =  (struct file_info *)page->uninit.aux;
					if (file_i)
						read_bytes = file_i->page_read_bytes;
					// stack_str = page->anon.is_stack ? "YES" : "NO";
					break;
				case VM_FILE:
					type_str = "FILE";
					break;
				case VM_PAGE_CACHE:
					type_str = "PAGE CACHE";
					break;
				default:
					type_str = "UNKNOWN (#)";
					type_str[9] = VM_TYPE(type) + 48; // 0~7 사이 숫자의 아스키 코드
			}
			

		}
		if (read_bytes)
			printf(" %12p | %12p | %12s | %3s | %3s | %3s/%3s | %8d \n",
			   va, kva, type_str, stack_str, writable_str,dirty_k_str,dirty_u_str, read_bytes);
		else
			printf(" %12p | %12p | %12s | %3s | %3s | %3s/%3s | \n",
			   va, kva, type_str, stack_str,dirty_k_str,dirty_u_str, writable_str);
	}
}