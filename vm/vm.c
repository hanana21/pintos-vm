/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "lib/kernel/hash.h"
#include "threads/mmu.h"

#include "userprog/process.h"
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
void print_spt(void);
void print_spt_2(struct supplemental_page_table *spt);
// static void set_offset(void *va, int64_t offset);
// static int64_t find_offset(void *va);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	// printf("########################  %s  ########################\n", "vm_alloc_page_with_initializer");
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	enum vm_type	check_type =  (enum vm_type)VM_TYPE(type);
	// printf("type:  %d\nupage : %p\n", type, upage);
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		struct page *page = (struct page *)malloc(sizeof(struct page));
		if (page == NULL) {
			perror("vm_alloc_page_with_initializer", "calloc");
			goto err;
		}

		if (writable)
			upage = (void *)((uint64_t)upage | PTE_W);

		switch (check_type) {
			case VM_ANON:
				uninit_new(page, upage, init, type, aux, anon_initializer);
				break ;
			case VM_FILE:
				uninit_new(page, upage, init, type, aux, file_backed_initializer);	
				break ;
		}

		if (spt_insert_page(spt, page) != NULL) {
			free(page);
			goto err;
		}
		// print_spt();
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	// printf("########################  %s  ########################\n", "spt_find_page");
	struct page p;
	struct hash_elem *e;

	p.va = pg_round_down(va);
	e = hash_find (&spt->h, &p.spt_elem);
	return e != NULL ? hash_entry (e, struct page, spt_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	// printf("########################  %s  ########################\n", "spt_insert_page");
	// printf("spt_insert_page : %p\n", page);
	return hash_insert(&spt->h, &page->spt_elem);
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
	// printf("########################  %s  ########################\n", "vm_get_frame");
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
vm_stack_growth (void *addr) {
	vm_alloc_page (VM_ANON | VM_MARKER_0, pg_round_down (addr), 1);
	vm_claim_page (addr);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	// printf("########################  %s  ########################\n", "vm_try_handle_fault");
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page;

	if (addr == NULL || !(is_user_vaddr(addr)))
		return false;

	if (not_present) {
		page = spt_find_page(spt, pg_round_down (addr));
		if (page == NULL) {
			if (USER_STACK - (uint64_t)addr < (1 << 20) && \
				(uint64_t)addr & VM_MARKER_0 && \
				(uint64_t)addr <=  f->rsp - 8) {
				printf("f->rsp : %d/ addr : %d %d\n", f->rsp, addr,0x4747f002);
				vm_stack_growth(addr);
				print_spt();
				return true;
			}
			else {
				// perror("vm_try_handle_fault", "spt_find_page");
				return false;
			}
		}
		// if (write == 1 && (uint64_t)page->va & PTE_W == 0) {
		// 	perror("vm_try_handle_fault", "writable is not matched");
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
	// printf("########################  %s  ########################\n", "vm_dealloc_page");
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	// printf("########################  %s  ########################\n", "vm_claim_page");
	struct page *page = spt_find_page(&thread_current()->spt, pg_round_down (va));
	if (page == NULL) {
		return false;
	}
	/* TODO: Fill this function */
	// print_spt();
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	// printf("########################  %s  ########################\n", "vm_do_claim_page");
	struct frame	*frame = vm_get_frame ();
	struct thread	*cur = thread_current();
	uint64_t		uaddr = (uint64_t)page->va & ~PGMASK;
	bool			writable = (uint64_t)page->va & PTE_W;

	/* Set links */
	frame->page = page;
	page->frame = frame;
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!(pml4_get_page (cur->pml4, uaddr) == NULL
		&& pml4_set_page(cur->pml4, uaddr, frame->kva, writable))) {
		perror("vm_do_claim_page", "pml4_get_page is NULL");
		palloc_free_page (frame->kva);
		return false;
	}

	frame->kva = (uint64_t)frame->kva | ((uint64_t)page->va & 0xfff);
	// set_offset(frame->kva, get_offset(page->va));
	
	return swap_in (page, frame->kva);
}

static uint64_t spt_hash_func(const struct hash_elem *e, void *aux) {
	// printf("########################  %s  ########################\n", "spt_hash_func");
	const struct page *page = hash_entry(e, struct page, spt_elem);
	uint64_t tmp = pg_round_down(page->va);
	return hash_bytes (&tmp, sizeof(page->va));
}

static bool spt_hash_less_func(const struct hash_elem *a,
						const struct hash_elem *b,
						void *aux) {
	// printf("########################  %s  ########################\n", "spt_hash_less_func");
	const struct page *page_a = hash_entry(a, struct page, spt_elem);
	const struct page *page_b = hash_entry(b, struct page, spt_elem);
	return pg_round_down (page_a->va) < pg_round_down (page_b->va) ? true : false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	// printf("########################  %s  ########################\n", "supplemental_page_table_init");
	hash_init (&spt->h, spt_hash_func, spt_hash_less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	// printf("########################  %s  ########################\n", "supplemental_page_table_copy");
	struct hash_iterator i;

	void *va, *kva, *aux;
	enum vm_type type;
	vm_initializer *init;
	bool writable;
	bool (*initializer)(struct page *, enum vm_type, void *);

	struct file_info *file_i;

	hash_first (&i, &src->h);
	while (hash_next (&i)) {
		const struct page *src_page = hash_entry (hash_cur (&i), struct page, spt_elem);
		struct page *dst_page = (struct page *)malloc(sizeof(struct page));

		if (dst_page == NULL || src_page == NULL) {
			perror("page_table_copy", "calloc");
			return false;
		}
		
		va = src_page->va;
		if (is_kernel_vaddr(va))
			return true;

		writable = (uint64_t)va & PTE_W;

		switch (VM_TYPE(src_page->operations->type)) {
			case VM_UNINIT:
				type = src_page->uninit.type;
				init = src_page->uninit.init;
				if (src_page->uninit.aux) {
					file_i = (struct file_info *)malloc(sizeof(struct file_info));
					memcpy(file_i, src_page->uninit.aux, sizeof(struct file_info));
				}
				aux = file_i;
				initializer = src_page->uninit.page_initializer;
				uninit_new(dst_page, va, init, type, aux, initializer);
				if (spt_insert_page(dst, dst_page) != NULL) {
					free(dst_page);
					return false;
				}
				break;

			case VM_ANON:
				type = src_page->anon.type;
				init = src_page->anon.init;
				if (src_page->anon.aux) {
					file_i = (struct file_info *)malloc(sizeof(struct file_info));
					memcpy(file_i, src_page->anon.aux, sizeof(struct file_info));
				}
				aux = file_i;
				initializer = src_page->anon.page_initializer;
				uninit_new(dst_page, va, init, type, aux, initializer);
				if (spt_insert_page(dst, dst_page) != NULL) {
					perror("supplemental_page_table_copy", "anon spt_insert_page");
					free(dst_page);
					return false;
				}
				if (vm_do_claim_page(dst_page) == NULL) {
					perror("supplemental_page_table_copy", "anon vm_do_claim_page");
					return false;
				}
				memcpy(pg_round_down(dst_page->frame->kva), pg_round_down(src_page->frame->kva), PGSIZE);
				// printf("\n=====================CHILD=====================\n");
				// print_spt();
				// printf("=====================PARENT=====================\n");
				// print_spt_2(src);
				break;

			case VM_FILE:
				type = src_page->file.type;
				init = src_page->file.init;
				if (src_page->file.aux) {
					file_i = (struct file_info *)malloc(sizeof(struct file_info));
					memcpy(file_i, src_page->file.aux, sizeof(struct file_info));
				}
				aux = file_i;
				initializer = src_page->file.page_initializer;
				uninit_new(dst_page, va, init, type, aux, initializer);
				if (spt_insert_page(dst, dst_page) != NULL) {
					free(dst_page);
					return false;
				}
				if (!vm_do_claim_page(dst_page))
					return false;
				memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
				break;
		}
	}
	// print_spt();
	// print_spt_2(src);
	return true;
}



static void
hash_destroy_action(struct hash_elem *e, void *aux) {
	// printf("########################  %s  ########################\n", "hash_destroy_action");
	// if (page != NULL && page->frame) {
	// 	// printf("!!!!!\n");
	// 	palloc_free_page(page->frame->kva);
	// 	free(page->frame);
	// }
	// print_spt();
	struct page *page = hash_entry(e, struct page, spt_elem);
	// if (page)
	vm_dealloc_page(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	// /* TODO: Destroy all the supplemental_page_table hold by thread and
	//  * TODO: writeback all the modified contents to the storage. */
	// printf("########################  %s  ########################\n", "supplemental_page_table_kill");
	if (!hash_empty(&spt->h))
		hash_clear(&spt->h, hash_destroy_action);
}

static void perror(char * name, char * error) {
	printf("Error func: %s\n%s error\n", name, error);
}

void print_spt(void) {
	struct hash *h = &thread_current()->spt.h;
	struct hash_iterator i;

	printf("============= {%s} SUP. PAGE TABLE (%d entries) =============\n", thread_current()->name, hash_size(h));
	printf("   USER VA    | KERN VA (PA) |     TYPE     | STK | WRT | DRT(K/U) \n");

	void *va, *kva;
	enum vm_type type;
	char *type_str, *stack_str, *writable_str;
	size_t read_bytes = 0;
	stack_str = " - ";

	hash_first (&i, h);
	struct page *page;
	while (hash_next (&i)) {
		page = hash_entry (hash_cur (&i), struct page, spt_elem);

		va = page->va;
		if (page->frame) {
			kva = page->frame->kva;
			writable_str = (uint64_t)page->va & PTE_W ? "YES" : "NO";
		} 
		// else {
		// 	kva = NULL;
		// 	dirty_k_str = " - ";
		// 	dirty_u_str = " - ";
		// }

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
					struct file_info *file_i =  (struct file_info *)page->anon.aux;
					if (file_i)
						read_bytes = file_i->read_bytes;
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
			printf(" %12p | %12p | %12s | %3s | %3s | %8d \n",
			   va, kva, type_str, stack_str, writable_str, read_bytes);
		else
			printf(" %12p | %12p | %12s | %3s | %3s | \n",
			   va, kva, type_str, stack_str, writable_str);
	}
}

void print_spt_2(struct supplemental_page_table *spt) {
	struct hash *h = &spt->h;
	struct hash_iterator i;

	printf("============= {%s} SUP. PAGE TABLE (%d entries) =============\n", thread_current()->name, hash_size(h));
	printf("   USER VA    | KERN VA (PA) |     TYPE     | STK | WRT | DRT(K/U) \n");

	void *va, *kva;
	enum vm_type type;
	char *type_str, *stack_str, *writable_str, *dirty_k_str, *dirty_u_str;
	size_t		read_bytes = 0;
	stack_str = " - ";

	hash_first (&i, h);
	struct page *page;
	// uint64_t *pte;
	while (hash_next (&i)) {
		page = hash_entry (hash_cur (&i), struct page, spt_elem);

		va = page->va;
		if (page->frame) {
			kva = page->frame->kva;
			// pte = pml4e_walk(thread_current()->pml4, page->va, 0);
			writable_str = (uint64_t)page->va & PTE_W ? "YES" : "NO";
			// dirty_str = pml4_is_dirty(thread_current()->pml4, page->va) ? "YES" : "NO";
			// dirty_k_str = is_dirty(page->frame->kpte) ? "YES" : "NO";
			// dirty_u_str = is_dirty(page->frame->upte) ? "YES" : "NO";
		} else {
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
		} 
		else {
			stack_str = "NO";
			switch (VM_TYPE(type)) {
				case VM_ANON:
					type_str = "ANON";
					struct file_info *file_i =  (struct file_info *)page->anon.aux;
					if (file_i)
						read_bytes = file_i->read_bytes;
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
			printf(" %12p | %12p | %12s | %3s | %3s | %8d \n",
			   va, kva, type_str, stack_str, writable_str, read_bytes);
		else
			printf(" %12p | %12p | %12s | %3s | %3s \n",
			   va, kva, type_str, stack_str, writable_str);
		// if (read_bytes)
		// 	printf(" %12p | %12p | %12s | %3s | %3s |  %3s/%3s | %8d \n",
		// 	   va, kva, type_str, stack_str, writable_str, dirty_k_str, dirty_u_str, read_bytes);
		// else
		// 	printf(" %12p | %12p | %12s | %3s | %3s |  %3s/%3s \n",
		// 	   va, kva, type_str, stack_str, writable_str, dirty_k_str, dirty_u_str);
	}
}
