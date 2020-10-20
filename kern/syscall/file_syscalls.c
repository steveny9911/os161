#include <type.h>
#include <syscall.h>

int
sys_open(const char *filename, int flags, mode_t mode)
{
  // validate flag

  // copy filename into kernel

  // openfile_open

  // put onto filetable
}

ssize_t
sys_read(int fd, void *buf, size_t buflen)
{
}

ssize_t
sys_write(int fd, const void *buf, size_t nbytes)
{
}

int sys_close(int fd)
{
}

off_t sys_lseek(int fd, off_t pos, int whence)
{
}

int sys_chdir(const char *pathname)
{
}

int sys_dup2(int oldfd, int newfd)
{
}

int sys___getcwd(char *buf, size_t buflen)
{
}