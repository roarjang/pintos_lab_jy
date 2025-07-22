#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "intrinsic.h"
#include "lib/kernel/console.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "userprog/gdt.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

struct file_fd
{
    int fd;
    struct file *file;
    struct list_elem elem;
};

static int write_handler(int fd, const void *buffer, unsigned size);

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

// 시스템 초기화 시 시스템 콜 벡터 설정 등의 초기화 작업 수행
void syscall_init(void)
{
    write_msr(MSR_STAR, ((uint64_t) SEL_UCSEG - 0x10) << 48 |
                            ((uint64_t) SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

    /* The interrupt service rountine should not serve any interrupts
     * until the syscall_entry swaps the userland stack to the kernel
     * mode stack. Therefore, we masked the FLAG_FL. */
    write_msr(MSR_SYSCALL_MASK,
              FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
// 어셈블리 코드(syscall-entry.S)로부터 제어를 넘겨받음
void syscall_handler(struct intr_frame *f UNUSED)
{
    // 유저 스택에서 시스템콜 번호 꺼내기
    int syscall_number = f->R.rax;

    switch (syscall_number)
    {
        case SYS_HALT:
        {
            power_off();
        }
        case SYS_EXIT:
        {
            struct thread *curr = thread_current();
            printf("%s: exit(%d)\n", curr->name, f->R.rdi);
            thread_exit();
        }
        case SYS_CREATE:
        {
        }
        case SYS_WRITE:
        {
            // 인터럽트 프레임(struct intr_frame *f)을 통해 사용자 프로그램의
            // 레지스터 상태와 시스템 콜 번호 및 인자들을 읽어옴 f->R.rax :
            // 시스템 콜 반환값 (리턴값 저장) f->R.rdi : 첫 번째 인자 (fd)
            // f->R.rsi : 두 번째 인자 (buffer)
            // f->R.rdx : 세 번째 인자 (size)
            f->R.rax = write_handler(f->R.rdi, f->R.rsi, f->R.rdx);
            break;
        }
    }
}

/* 파일 또는 STDOUT으로 쓰기 */
static int write_handler(int fd, const void *buffer, unsigned size)
{
    // buffer를 fd에 쓰기
    if (is_user_vaddr(buffer) || is_user_vaddr(buffer + size))
    {
        if (fd == 1)  // fd가 1이면 표준 출력 (파일이 아니라 콘솔로 출력)
        {
            putbuf(buffer, size);
        }
        else if (fd > 2)  // open()으로 연 파일이 할당된 경우
        {
            struct file *f = process_get_file(fd);
            if (f == NULL) return -1;
            return file_write(f, buffer, size);
        }
    }

    return -1;
}

// 헌재 실행 중인 프로세스의 열린 파일 리스트에서 특정 파일 디스크립터(fd)에
// 해당하는 파일 포인터를 찾아 반환
struct file *process_get_file(int fd)
{
    struct thread *t = thread_current();
    struct list_elem *e;

    for (e = list_begin(&t->fd_list); e != list_end(&t->fd_list);
         e = list_next(e))
    {
        // 각 리스트 요소를 struct file_fd *f로 변환
        struct file_fd *f = list_entry(e, struct file_fd, elem);
        if (f->fd == fd) return f->file;
    }

    return NULL;  // 해당 fd를 가진 파일이 없으면 NULL
}