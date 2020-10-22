#include <unistd.h>
#include <type.h>
#include <lib.h>
#include <errno.h>
#include <syscall.h>
#include <filetable.h>
#include <openfile.h>
#include <kern/limits.h>
#include <proc.h>
#include <current.h>
#include <synch.c>
#include <uio.h>
#include <seek.h>
#include <kern/stat.h>
#include <emu.c>

int sys_open(const char filename, int flags, mode_t mode, int *retval)
{
    KASSERT(filename != NULL);

    // validate flag (https://lwn.net/Articles/588444/)
    if (flags & ~(O_RDONLY | O_WRONLY | O_RDWR |
                  O_CREAT | O_EXCL | O_TRUNC |
                  O_APPEND)) {
        return EINVAL;
    }

    // copy filename into kernel
    char *kname;
    size_t len = strlen(filename);
    size_t *actual;
    int result = copyinstr(filename, kname, len, actual);
    if (result) {
        return result;
    }

    // openfile_open
    struct openfile *file;
    result = openfile_open(kname, flags, mode, &file); // return onto file; result is 0 or error code
    if (result) {
        return result;
    }

    // put onto filetable
    result = filetable_add(curproc->p_ft, file, retval);
    if (result) {
        return result;
    }

    return 0;
}

int sys_read(int fd, void *buf, size_t buflen, int *retval)
{
    // get openfile with filetable_get --- using fd as index
    struct openfile *file;
    int result = filetable_get(curproc->p_ft, fd, &file);
    if (result) {
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
    if (result) {
        lock_release(file->file_offsetlock);
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
    if (result) {
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
    if (result) {
        lock_release(file->file_offsetlock);
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

int sys_close(int fd)
{
    struct filetable *ft = curproc->p_ft;
    struct openfile *file;

    int result = filetable_get(ft, fd, &file);
    if (result) {
        return result;
    }

    result = filetable_remove(ft, fd);
    if (result) {
        return result;
    }

    // openfile close file decrement reference counter
    openfile_decref(file);

    return 0;
}

off_t sys_lseek(int fd, off_t pos, int whence, int *retval)
{
    // get openfile
    struct openfile *file;
    int result = filetable_get(curproc->p_ft, fd, &file);
    if (result) {
        return result;
    }

    // acquire openfile's offset lock
    offset_t new_offset;
    lock_acquire(file->file_offset_lock);
    new_offset = file->file_offset;

    // switch-case for "whence"
    switch (whence)
    {
    // SEEK_SET, the new position is pos.
    case SEEK_SET:
        new_offset = pos;
        break;
    // SEEK_CUR, the new position is the current position plus pos.
    case SEEK_CUR:
        new_offset += pos;
        break;
    // SEEK_END, the new position is the position of end-of-file plus pos.
    case SEEK_END:
        struct stat stats;
        result = emufs_stat(file->file_vnode, stats);
        if (result) {
            lock_release(file->file_offsetlock);
            return result;
        }
        new_offset = stats->st_size + pos;
        break;
    // anything else, lseek fails.
    default:
        lock_release(file->file_offsetlock);
        return EINVAL;
    }
    
    // validate new offset
    if (new_offset < 0) {
        lock_release(file->file_offsetlock);
        return EINVAL;
    }

    // set new offset
    file->file_offset = new_offset;

    // release lock
    lock_release(file->file_offsetlock);

    // done --- set retval to be new offset
    *retval = new_offset;
    return new_offset;
}

int sys_chdir(const char *pathname)
{
    KASSERT(pathname != NULL);

    // copy pathname into kernel
    char *kname;
    size_t len = strlen(pathname);
    size_t *actual;
    int result = copyinstr(pathname, kname, len, actual);
    if (result) {
        return result;
    }

    result = vfs_chdir(kname);
    if (result) {
        return result;
    }
    return 0;
}

int sys_dup2(int oldfd, int newfd)
{
    // validate file handles
    if (oldfd < 0 || newfd < 0) {
        return EBADF;
    }
    if (oldfd > __PID_MAX || newfd > __PID_MAX) {
        return EMFILE;
    }

    struct openfile *oldfile, *newfile;
    int result = filetable_get(curproc->p_ft, oldfd, &oldfile);
    if (result) {
        return result;
    }
    // If newfd names an already-open file, that file is closed.
    result = filetable_add(curproc->p_ft, newfd, &newfile);
    if (!result) {
        result = filetable_remove(curproc->p_ft, newfd);
        if (result) {
            return result;
        }
    }

    // let two handles refer to the same "open" of the file
    
}

int sys___getcwd(char *buf, size_t buflen)
{
    // this call behaves like read
    struct iovec iov;
    struct uio uu;
    uio_kinit(&iov, &uu, buf, buflen, 0, UIO_READ);

    int result = vfs_getcwd(&uu);
    if (result) {
        return result;
    }

    return uu->uio_resid;
}