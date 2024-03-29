#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
pid_t fork(const char *thread_name, struct intr_frame *f);

// File Descriptor
static struct file *find_file_by_fd(int fd);
int add_file_to_fdt(struct file *file);
void remove_file_from_fdt(int fd);
void check_writable_addr(const uint64_t *uaddr);
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&file_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// printf ("system call!\n");
	// thread_exit ();
	#ifdef VM
		thread_current()->rsp = f->rsp;
	#endif

	switch (f->R.rax){
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi,f);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi,f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi,f->R.rsi,f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi,(char*)f->R.rsi,f->R.rdx);
		break;	
	case SYS_SEEK:
		seek(f->R.rdi,f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	
	default:
		exit(-1);
		break;
	}
}
//유효성 체크를 위한 
void check_address(const uint64_t *useradd){
	#ifdef VM
	struct page *page = spt_find_page (&thread_current() -> spt, useradd);
	if (page == NULL) exit(-1);
	#endif
	
	struct thread *curr = thread_current();

	if(useradd == NULL || !(is_user_vaddr(useradd)||pml4_get_page(curr->pml4,useradd) == NULL)){
		exit(-1);
	}
}
// void check_writable_addr(const uint64_t *uaddr){
// 	struct page *page = spt_find_page (&thread_current() -> spt, uaddr);
// 	if (page == NULL || !page->writable) exit(-1);
// }
void halt (void) {
	// therad/init.c 
	power_off();
}

void exit (int status) {
	struct thread *curr = thread_current();
	curr -> exit_status = status;
	printf ("%s: exit(%d)\n", thread_name(),status);
	thread_exit();
}

pid_t fork(const char *thread_name, struct intr_frame *f){
	//check_address(thread_name);
	return process_fork(thread_name,f);
}

int exec (const char *file) {
	check_address(file);

	char *file_copy;
    file_copy = palloc_get_page (PAL_ZERO);
	
	if(file_copy == NULL)
		exit(-1);
	strlcpy (file_copy, file, strlen(file)+1);
	if(process_exec(file_copy) == -1){
		exit(-1);
	}
		
}

int wait (pid_t pid) {
	return process_wait(pid);
}

bool create (const char *file, unsigned initial_size) {
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove (const char *file) {
	check_address(file);
	return filesys_remove(file);
}

int open (const char *file) {
	check_address(file);
	struct file *fileobj = filesys_open(file);

	if(fileobj == NULL)
		return -1;

	// file을 fd table에 추가 성공하면 fd리턴 아니면 -1
	int fd = add_file_to_fdt(fileobj);

	// 실패 했다면(자리 없다면) close
	if(fd == -1)
		file_close(fileobj);

	return fd;
}
int filesize (int fd) {
	struct file *fileobj = find_file_by_fd(fd);
	if(fileobj == NULL)
		return -1;
	return file_length(fileobj);
}

int read (int fd, void *buffer, unsigned length) {
	check_address(buffer);
	int ret;
	if(fd == 0){
		int i;
		unsigned char *buf = buffer;
		for(i=0;i<length;i++){
			char c = input_getc();
			*buf++ = c;
			if(c == '\0')
				break;
		}
		ret = i;
	}else if(fd == 1){
		return -1;
	}else{
		struct thread *curr = thread_current();
		struct file *fileobj = find_file_by_fd(fd);

		if(fileobj == NULL)
			return -1;

		lock_acquire(&file_lock);
		ret = file_read(fileobj,buffer,length);
		lock_release(&file_lock);
	}
	return ret;
}

int write (int fd, const void *buffer, unsigned length) {
	check_address(buffer);
	int ret;

	if(fd == 1){ // 표준 출력 : 버퍼의 내용 콘솔창으로 출력
		putbuf(buffer,length);
		ret = length;
	}else if(fd == 0){// 표준 입력
		return -1;
	}else {
		struct thread *curr = thread_current();
		struct file *fileobj = find_file_by_fd(fd);

		if(fileobj == NULL)
			return -1;

		lock_acquire(&file_lock);
		ret = file_write(fileobj,buffer,length);
		lock_release(&file_lock);
		
	}
	return ret;
}

void seek (int fd, unsigned position) {
	struct file *fileobj = find_file_by_fd(fd);
	if(fileobj <= 2)
		return;
	file_seek(fileobj,position);
}

unsigned tell (int fd) {
	struct file *fileobj = find_file_by_fd(fd);
	if(fileobj<=2)
		return;
	return file_tell(fileobj);
}

void close (int fd) {
	if(fd ==0 || fd == 1)
		return;
	else{
		struct thread *curr = thread_current();
		struct file *fileobj = find_file_by_fd(fd);

		if(fileobj == NULL)
			return;
		remove_file_from_fdt(fd);
		file_close(fileobj);
	}
}

//file descriptor 서브 함수들 

/* fdt안에 파일 넣기*/
int add_file_to_fdt(struct file *file){
	struct thread *curr = thread_current();
	struct file **fdt = curr->fdt;
	
	// file 자리 찾기 : FDCOUNT_LIMIT 범위 안에서 fdt NULL인곳 찾기
	//                  최근 파일 이후부터 확인 
	while(curr->fd_index < FDCOUNT_LIMIT && fdt[curr->fd_index])
		curr-> fd_index++;
	
	if(curr->fd_index >= FDCOUNT_LIMIT)
		return -1;
	
	//넣어주고
	fdt[curr->fd_index] = file;

	//fd_index 반환
	return curr->fd_index;
}

/* fdt에서 file 지우기 */
void remove_file_from_fdt(int fd){
	struct thread *curr = thread_current();

	//파일 디스크럽터는 정수이고, 최대값 넘지 않아야
	if(fd < 0 || fd > FDCOUNT_LIMIT)
		return NULL;
	curr -> fdt[fd] = NULL;
}

/* 만약 유효성 검사 통과하면 fd로 fdt안의 파일 꺼내기*/
static struct file *find_file_by_fd(int fd){
	struct thread *curr = thread_current();
	
	//파일 디스크럽터는 정수이고, 최대값 넘지 않아야
	if(fd < 0 || fd > FDCOUNT_LIMIT)
		return NULL;
	
	return curr-> fdt[fd];

}