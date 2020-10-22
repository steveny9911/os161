#include <kern/unistd.h>
#include <types.h>
#include <lib.h>
#include <kern/fcntl.h>
#include <copyinout.h>
#include <kern/errno.h>
#include <syscall.h>
#include <filetable.h>
#include <openfile.h>
#include <kern/limits.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <uio.h>
#include <kern/seek.h>
#include <stat.h>
#include <vfs.h>
#include <lib.h>

int sys_open(const char *filename, int flags, mode_t mode, int *retval)
{
    KASSERT(filename != NULL);

    // validate flags (https://lwn.net/Articles/588444/)
    if (flags & ~(O_RDONLY | O_WRONLY | O_RDWR |
                  O_CREAT | O_EXCL | O_TRUNC |
                  O_APPEND))
    {
        return EINVAL;
    }

    // copy filename into kernel
    char *kname = kmalloc(NAME_MAX);
    if (kname == NULL) {
        return ENOMEM;
    }

    // size_t len = (size_t)strlen(filename);
    int result = copyinstr((const_userptr_t)filename, kname, NAME_MAX, NULL);
    if (result) {
        kfree(kname);
        return result;
    }
    kprintf("copyinstr pass\n");

    // openfile_open to get the actual file
    struct openfile *file;
    result = openfile_open(kname, flags, mode, &file); // return onto file; result is 0 or error code
    if (result) {
        kfree(kname);
        return result;
    }
    kprintf("openfile_open pass\n");

    // put openfile onto the process's filetable
    result = filetable_add(curproc->p_ft, file, retval);
    if (result) {
        kfree(kname);
        return result;
    }
    kprintf("filetable_add pass\n");

    kfree(kname);
    return 0;
}

int sys_read(int fd, void *buf, size_t buflen, int *retval)
{
    // get openfile with filetable_get --- using fd as filetable index
    struct openfile *file;
    int result = filetable_get(curproc->p_ft, fd, &file);
    if (result)
        return result;

    // get openfile's offset
    // acquire lock --- release lock after all read is done
    off_t offset;
    lock_acquire(file->file_offsetlock);
    offset = file->file_offset;

    // similar to load_elf
    // call uio_kinit for create user-io
    struct iovec iov;
    struct uio uu;
    uio_kinit(&iov, &uu, buf, buflen, offset, UIO_READ);

    // VOP_READ to actually perform the read
    struct vnode *v = file->file_vnode;
    result = VOP_READ(v, &uu);
    if (result)
    {
        lock_release(file->file_offsetlock);
        return result;
    }

    // done reading
    // how much did we read? --- update file_offset and return
    off_t new_offset = uu.uio_offset;
    file->file_offset = uu.uio_offset;
    lock_release(file->file_offsetlock);

    // retvel --- count of bytes read
    *retval = new_offset - offset;
    return 0;
}

int sys_write(int fd, void *buf, size_t nbytes, int *retval)
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

    // VOP_WRITE
    struct vnode *v = file->file_vnode;
    result = VOP_WRITE(v, &uu);
    if (result)
    {
        lock_release(file->file_offsetlock);
        return result;
    }

    // done writing
    // how much did we write? --- update file_offset and return
    off_t new_offset = uu.uio_offset;
    file->file_offset = uu.uio_offset;
    lock_release(file->file_offsetlock);

    // retvel --- count of bytes wrote
    *retval = new_offset - offset;
    return 0;
}

int sys_close(int fd)
{
    struct filetable *ft = curproc->p_ft;
    struct openfile *file;

    // get the openfile from the filetable
    int result = filetable_get(ft, fd, &file);
    if (result)
    {
        return result;
    }

    // remove the openfile from the filetable
    result = filetable_remove(ft, fd);
    if (result)
    {
        return result;
    }

    // openfile close file by decrementing the reference counter
    openfile_decref(file);

    return 0;
}

int sys_lseek(int fd, off_t pos, int whence, int *retval)
{
    // get openfile
    struct openfile *file;
    int result = filetable_get(curproc->p_ft, fd, &file);
    if (result)
    {
        return result;
    }

    // acquire openfile's offset lock
    off_t new_offset;
    lock_acquire(file->file_offsetlock);
    new_offset = file->file_offset;

    struct stat file_info;

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
        result = VOP_STAT(file->file_vnode, &file_info);
        if (result)
        {
            lock_release(file->file_offsetlock);
            return result;
        }
        new_offset = file_info.st_size + pos;
        break;
    // anything else, lseek fails.
    default:
        lock_release(file->file_offsetlock);
        return EINVAL;
    }

    // validate new offset
    if (new_offset < 0)
    {
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
    char *kname = kmalloc(PATH_MAX);
    size_t len = (size_t)strlen(pathname);
    int result = copyinstr((const_userptr_t)pathname, kname, len, NULL);
    if (result)
    {
        kfree(kname);
        return result;
    }

    result = vfs_chdir(kname);
    if (result)
    {
        kfree(kname);
        return result;
    }

    kfree(kname);
    return 0;
}

int sys_dup2(int oldfd, int newfd, int *retval)
{
    // validate file handles
    if (oldfd < 0 || newfd < 0) {
        return EBADF;
    }

    if (oldfd > OPEN_MAX || newfd > OPEN_MAX) {
        return EMFILE;
    }

    // Using dup2 to clone a file handle onto itself has no effect.
    if (oldfd == newfd) {
        *retval = newfd;
        return 0;
    }

    struct openfile *oldfile, *newfile;
    int result = filetable_get(curproc->p_ft, oldfd, &oldfile);
    if (result) {
        return result;
    }
    
    // If newfd names an already-open file, that file is closed.
    if (curproc->p_ft->openfiles[newfd] != NULL) {
        result = filetable_get(curproc->p_ft, newfd, &newfile);
        if (result) {
            return result;
        }

        result = filetable_remove(curproc->p_ft, newfd);
        if (result) {
            return result;
        }
        openfile_decref(newfile);
        *retval = newfd;
        return 0;
    }

    // clone oldfd onto newfd
    curproc->p_ft->openfiles[newfd] = oldfile;

    *retval = newfd;
    return 0;
}

int sys___getcwd(char *buf, size_t buflen)
{
    // this call behaves like read
    struct iovec iov;
    struct uio uu;
    uio_kinit(&iov, &uu, buf, buflen, 0, UIO_READ);

    int result = vfs_getcwd(&uu);
    if (result)
    {
        return result;
    }

    return (int)uu.uio_resid;
}