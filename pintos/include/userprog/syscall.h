#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);
struct file *process_get_file(int fd);
int process_add_file(struct file *file);

#endif /* userprog/syscall.h */
