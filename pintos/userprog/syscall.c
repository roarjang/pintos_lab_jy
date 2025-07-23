#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "filesys/filesys.h"
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
            break;
        }
        case SYS_EXIT:
        {
            struct thread *curr = thread_current();
            printf("%s: exit(%d)\n", curr->name, f->R.rdi);
            thread_exit();
            break;
        }
        case SYS_FORK:
        {
            printf("system call fork called!\n");
            break;
        }
        case SYS_EXEC:
        {
            printf("system call exec called!\n");
            break;
        }
        case SYS_WAIT:
        {
            printf("system call wait called!\n");
            break;
        }
        case SYS_CREATE:
        {
            // printf("system call create called!\n");
            const char *open_filename = (const char *) f->R.rdi;
            int32_t filesize = f->R.rsi;
            f->R.rax = filesys_create(open_filename, filesize);
            break;
        }
        case SYS_REMOVE:
        {
            printf("system call remove called!\n");
            break;
        }
        case SYS_OPEN:
        {
            printf("system call open called!\n");
            if (is_user_vaddr(f->R.rdi))
            {
                const char *open_file_name = (const char *) f->R.rdi;
                struct file *open_file = filesys_open(open_file_name);
                if (open_file != NULL)
                {
                    f->R.rax = process_add_file(open_file);
                }
                else
                {
                    f->R.rax = -1;
                }
                break;
            }
        }
        case SYS_FILESIZE:
        {
            printf("system call filesize called!\n");
            break;
        }
        case SYS_READ:
        {
            printf("system call read called!\n");
            break;
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
        case SYS_SEEK:
        {
            printf("system call seek called!\n");
            break;
        }
        case SYS_TELL:
        {
            printf("system call tell called!\n");
            break;
        }
        case SYS_CLOSE:
        {
            printf("system call close called!\n");
            break;
        }
    }
}

/* 파일 또는 STDOUT으로 쓰기 */
static int write_handler(int fd, const void *buffer, unsigned size)
{
    /* buffer를 fd에 쓰기 */
    if (is_user_vaddr(buffer) || is_user_vaddr(buffer + size))
    {
        if (fd == 1)
        {
            /* CASE: stdin */
            putbuf(buffer, size);
        }
        else if (fd > 2)
        {
            /* CASE: not std-in,out, err */
            /* 표준 입력(stdin:0), 표준 출력(stdout:1), 표준 에러(stderr:2)가
             * 아니면 파일을 연 것이므로, 이후 파일 디스크립터 번호를 할당한다.
             */
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

// 현재 실행 중인 프로세스의 열린 파일 리스트에 파일 추가
int process_add_file(struct file *file)
{
    if (file == NULL) return -1;

    struct thread *curr_thread = thread_current();

    // 새로운 file_fd 구조체를 동적 할당
    struct file_fd *f = malloc(sizeof(struct file_fd));
    if (f == NULL) return -1;

    // file_fd의 값 설정
    f->file = file;
    f->fd = curr_thread->next_fd++;

    // 리스트에 추가
    list_push_front(&curr_thread->fd_list, &f->elem);

    return f->fd;
}