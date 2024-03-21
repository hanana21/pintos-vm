#include <syscall.h>

int main (int, char *[]);
void _start (int argc, char *argv[]);

// 사용자 프로그램의 시작점 : 시작하면 main을 호출하고 main끝나면 exit 호출하여 종료
void
_start (int argc, char *argv[]) {
	exit (main (argc, argv));
}
