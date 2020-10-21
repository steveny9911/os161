#include <type.h>
#include <lib.h>
#include <errno.h>
#include <syscall.h>
#include <filetable.h>
#include <openfile.h>
#include <kern/limits.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <uio.h>
#include <seek.h>
#include <stat.h>

int sys_open(const char filename, int flags, mode_t mode, int *retval)
{
    KASSERT(filename != NULL);

    // validate flag (https://lwn.net/Articles/588444/)
    if (flags & ~(O_RDONLY | O_WRONLY | O_RDWR |
                  O_CREAT | O_EXCL | O_TRUNC |
                  O_APPEND))
    {
        return EINVAL;
    }

    // copy filename into kernel
    char *kname;
    size_t len = strlen(filename);
    size_t *actual;
    int result = copyinstr(filename, kname, len, actual);
    if (result)
    {
        return result;
    }

    // openfile_open
    struct openfile *file;
    result = openfile_open(kname, flags, mode, &file); // return onto file; result is 0 or error code
    if (result)
    {
        return result;
    }

    // put onto filetable
    result = filetable_add(curproc->p_ft, file, retval);
    if (result)
    {
        return result;
    }

    //?file handles 0 (STDIN_FILENO), 1 (STDOUT_FILENO), and 2 (STDERR_FILENO)
    return 0;
}

int sys_read(int fd, void *buf, size_t buflen, int *retval)
{
    // get openfile with filetable_get --- using fd as index
    struct openfile *file;
    int result = filetable_get(curproc->p_ft, fd, &file);
    if (result)
    {
        return result;
    }

    // get openfile offset
    // get lock --- release lock after all read is done
    off_t offset;
    lock_acquire(file->file_offsetlock);
    offset = file->file_offset;

    // similar to load_elf
    // call uio_kinit
    struct iovec iov;
    struct uio uu;
    uio_kinit(&iov, &uu, buf, buflen, offset, UIO_READ);

    // VOP_READ
    struct vnode *v = file->file_vnode;
    result = VOP_READ(v, &uu);
    if (result)
    {
        return result;
    }

    // done reading
    // how much did we read? --- update file_offset and return
    off_t new_offset = uu->uio_offset;
    file->file_offset = uu->uio_offset;
    lock_release(file->file_offsetlock);

    // retvel --- count of bytes read
    *retval = new_offset - offset;
    return 0;
}

int sys_write(int fd, const void *buf, size_t nbytes, int *retval)
{
    // same as sys_read
    // but UIO_WRITE and use nbytes
    // call VOP_WRITE

    // get openfile with filetable_get --- using fd as index
    struct openfile *file;
    int result = filetable_get(curproc->p_ft, fd, &file);
    if (result)
    {
        return result;
    }

    // get openfile offset
    // get lock --- release lock after all read is done
    off_t offset;
    lock_acquire(file->file_offsetlock);
    offset = file->file_offset;

    // similar to load_elf
    // call uio_kinit
    struct iovec iov;
    struct uio uu;
    uio_kinit(&iov, &uu, buf, nbytes, offset, UIO_WRITE);

    // VOP_READ
    struct vnode *v = file->file_vnode;
    result = VOP_WRITE(v, &uu);
    if (result)
    {
        return result;
    }

    // done reading
    // how much did we read? --- update file_offset and return
    off_t new_offset = uu->uio_offset;
    file->file_offset = uu->uio_offset;
    lock_release(file->file_offsetlock);

    //?In most cases, one should loop to make sure that all output has actually been written.

    // retvel --- count of bytes read
    *retval = new_offset - offset;
    return 0;
}

int sys_close(int fd)
{
    struct filetable *ft = curproc->p_ft;
    struct openfile *file;

    int result = filetable_get(ft, fd, &file);
    if (result)
    {
        return result;
    }

    result = filetable_remove(ft, fd);
    if (result)
    {
        return result;
    }

    // openfile close file decrement reference counter
    openfile_decref(file);

    return 0;
}

off_t sys_lseek(int fd, off_t pos, int whence, int *retval)
{
    struct openfile *file;
    off_t new_offset;

    // get openfile
    int result = filetable_get(curproc->p_ft, fd, file);
    if (result)
    {
        return result;
    }

    // acquire openfile's offset lock
    lock_acquire(file->file_offsetlock);
    new_offset = file->file_offset;

    // switch-case for "whence"
    switch (whence)
    {
    case SEEK_SET: //  - SEEK_SET --- just set it
        new_offset = pos;
        break;
    case SEEK_CUR: //  - SEEK_CUR --- just add onto offset
        new_offset = pos + new_offset;
        break;
    case SEEK_END: //  - SEEK_END --- where to find the end of file position? (look into vnode? vfs?) --- https://piazza.com/class/keabkwwe5wwpc?cid=735
        struct stat *file_info;
        result = VOP_STAT(file->file_vnode, file_info);
        if (result)
        {
            lock_release(file->file_offsetlock);
            return result;
        }
        new_offset = pos + file_info->st_size;
    default: //  - default --- release lock, return error
        lock_release(file->file_offsetlock);
        return EINVAL;
    }

    // offset should not be negative
    if (new_offset < 0)
    {
        lock_release(file->file_offsetlock);
        return EINVAL;
    }

    // set new offset
    file->file_offset = new_offset;
    *retval = new_offset;

    // release lock
    lock_release(file->file_offsetlock);

    // done --- set retval to be new offset
    return 0;
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