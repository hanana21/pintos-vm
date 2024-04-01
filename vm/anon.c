/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

#include "lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

static struct swap_table swap_table;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
#define ONE_MB (1 << 20) // 1MB
#define CHUNK_SIZE (20*ONE_MB)
#define PAGE_COUNT (CHUNK_SIZE / PAGE_SIZE)

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	disk_size(swap_disk);
	swap_table.slots = bitmap_create(PAGE_COUNT);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk. */
/* 스왑 디스크 데이터 내용을 읽어서 익명 페이지를(디스크에서 메모리로)  swap in합니다. 
	스왑 아웃 될 때 페이지 구조체는 스왑 디스크에 저장되어 있어야 합니다.
	스왑 테이블을 업데이트해야 합니다(스왑 테이블 관리 참조). */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
/*	메모리에서 디스크로 내용을 복사하여 익명 페이지를 스왑 디스크로 교체합니다. 
	먼저 스왑 테이블을 사용하여 디스크에서 사용 가능한 스왑 슬롯을 찾은 다음 
	데이터 페이지를 슬롯에 복사합니다.
	데이터의 위치는 페이지 구조체에 저장되어야 합니다.
	디스크에 사용 가능한 슬롯이 더 이상 없으면 커널 패닉이 발생할 수 있습니다. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	disk_sector_t slot_num = salloc_get_slot();
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	if (anon_page->aux)
		free(anon_page->aux);
}
