/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"

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
	list_init(&frame_table);
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
	//printf("########vm_alloc_page_with_initializer 진입\n");
	struct supplemental_page_table *spt = &thread_current ()->spt;
	//printf("########vm_alloc_page_with_initializer ADDR : %p\n", upage);
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		//printf("########vm_alloc_page_with_initializer SPT FIND FAGE == NULL\n");
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *p = (struct page*)malloc(sizeof(struct page));
		bool (*page_initializer)(struct page *, enum vm_type, void *);

		switch (VM_TYPE(type)){
		case VM_ANON: 
			page_initializer = anon_initializer;
			break;
		
		case VM_FILE:
			page_initializer = file_backed_initializer;
			break;
		}
		uninit_new(p,upage,init,type,aux,page_initializer);
		p -> writable = writable;
		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt,p);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	//printf("spt find page 진입 \n");
	page = (struct page *)malloc(sizeof(struct page));
	struct hash_elem *e;
	page -> va = pg_round_down(va);
	//page -> va = va;
	//printf("spt find page spt_ hash : %p\n", spt->spt_hash);
	//printf("spt find page spt_ page hash elem: %p\n", page->h_elem);
	e = hash_find(&spt->spt_hash,&page->h_elem);
	//free(page);

	//printf("spt find page hash find \n");
	return e != NULL ? hash_entry(e,struct page,h_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	if (hash_insert(&spt->spt_hash,&page->h_elem) == NULL)
		succ = true;
	return succ;
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
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	void *kva = palloc_get_page(PAL_USER); // 새로운 물리 주소 받아옴 

	if(kva == NULL){
		PANIC("todo");
	}

	frame = (struct frame *)malloc(sizeof(struct frame));
	frame -> kva = kva;
	frame -> page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	vm_alloc_page(VM_ANON|VM_MARKER_0,pg_round_down(addr),1);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	//printf("vm try handle fault addr : %p\n", addr);
	if (is_kernel_vaddr(addr) && addr == NULL)
		return false;
	
	if(not_present){
		void *rsp = f->rsp;
		if(!user)
			rsp = thread_current()->rsp;

		if(USER_STACK- (1<<20) <= rsp && rsp <= addr && addr <= USER_STACK)
			vm_stack_growth(addr);
		else if(USER_STACK- (1<<20) <= rsp-8 && rsp-8 == addr && addr <= USER_STACK)
			vm_stack_growth(addr);

		page = spt_find_page(spt,addr);
		if (page == NULL)
			return false;
		if (write == 1 && page -> writable == 0) 
			return false; 	
		return vm_do_claim_page (page);		
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
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */

	page = spt_find_page(&thread_current()->spt,va);
	if (page == NULL)
		return false;
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *curr = thread_current();
	pml4_set_page(curr->pml4,page->va,frame->kva,page->writable);
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {

	hash_init (&spt->spt_hash,page_hash,page_less,NULL);

}
/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,struct supplemental_page_table *src UNUSED) {
	struct hash_iterator i;
	hash_first(&i,&src->spt_hash);
	while(hash_next(&i)){
		struct page *src_page = hash_entry(hash_cur(&i),struct page,h_elem);
		enum vm_type type = src_page -> operations -> type;
		void *upage = src_page -> va;
		bool writable = src_page -> writable;

		if(type == VM_UNINIT){
			vm_initializer *init = src_page->uninit.init;
			void *aux = src_page->uninit.aux;
			vm_alloc_page_with_initializer(VM_ANON,upage,writable,init,aux);
			continue;
		}
		if(!vm_alloc_page(type,upage,writable))
			return false;
		if(!vm_claim_page(upage))
			return false;
		struct page *dst_page = spt_find_page(dst,upage);
		memcpy(dst_page->frame->kva,src_page->frame->kva,PGSIZE);
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->spt_hash,hash_page_destroy);
}
unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED){
	struct page *p = hash_entry(p_,struct page,h_elem);

	return hash_bytes(&p->va,sizeof(p->va));
}
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux){
	struct page *page_a = hash_entry(a,struct page,h_elem);
	struct page *page_b = hash_entry(b,struct page,h_elem);

	return page_a->va < page_b->va;
}
void hash_page_destroy(struct hash_elem *e,void *aux){
	struct page *page = hash_entry(e,struct page,h_elem);
	//destroy(page);
	free(page);
}