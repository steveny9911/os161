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

/**
 * Open the file, device, or other kernel object named by the pathname
 * @param filename pathname of the file 
 * @param flags    how to open the file
 * @param mode     permissions to use the file
 * @param retval   actual return - file descriptor index
 * @return 0 success, else error code
 */
int sys_open(const char *filename, int flags, mode_t mode, int *retval)
{
    // validate flags
    if (flags & ~(O_RDONLY | O_WRONLY | O_RDWR |
                  O_CREAT | O_EXCL | O_TRUNC |
                  O_APPEND)) {
        return EINVAL;
    }

    // copy filename into kernel
    char *kname = kmalloc(NAME_MAX);
    if (kname == NULL) {
        return ENOMEM;
    }

    // copy filename into kernel
    int result = copyinstr((const_userptr_t)filename, kname, NAME_MAX, NULL);
    if (result) {
        kfree(kname);
        return result;
    }

    // openfile_open to get the actual file
    struct openfile *file;
    result = openfile_open(kname, flags, mode, &file); // return onto file; result is 0 or error code
    if (result) {
        kfree(kname);
        return result;
    }

    // put openfile onto process's filetable
    result = filetable_add(curproc->p_filetable, file, retval);
    if (result) {
        kfree(kname);
        return result;
    }

    kfree(kname);
    return 0;
}

/**
 * Read bytes from the file
 * @param fd     file to read from
 * @param buf    space to store the read bytes
 * @param buflen length to read
 * @param retval actual return - bytes read
 * @return 0 success, else error code
 */
int sys_read(int fd, void *buf, size_t buflen, int *retval)
{
    // get openfile with filetable_get
    struct openfile *file;
    int result = filetable_get(curproc->p_filetable, fd, &file);
    if (result) {
        return result;
    }

    // get openfile's offset
    // acquire lock
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
    if (result) {
        lock_release(file->file_offsetlock);
        return result;
    }

    // done reading
    // update file_offset and return
    off_t new_offset = uu.uio_offset;
    file->file_offset = uu.uio_offset;
    lock_release(file->file_offsetlock);

    // return count of bytes read
    *retval = new_offset - offset;
    return 0;
}

/**
 * Write bytes to the file.
 * @param fd file to write to
 * @param buf space to store the written bytes 
 * @param nbytes length to write
 * @param retval return value
 */
int sys_write(int fd, void *buf, size_t nbytes, int *retval)
{
    // same as sys_read
    // but UIO_WRITE and use nbytes
    // call VOP_WRITE

    // get openfile with filetable_get --- using fd as index
    struct openfile *file;
    int result = filetable_get(curproc->p_filetable, fd, &file);
    if (result) {
        return result;
    }

    // get openfile offset
    // release lock after all read is done
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
    if (result) {
        lock_release(file->file_offsetlock);
        return result;
    }

    // done writing
    // update file_offset and return
    off_t new_offset = uu.uio_offset;
    file->file_offset = uu.uio_offset;
    lock_release(file->file_offsetlock);

    // return count of bytes wrote
    *retval = new_offset - offset;
    return 0;
}

/**
 * Close file
 * @param fd file descriptor index
 * @return 0 success, else error code
 */
int sys_close(int fd)
{
    struct filetable *ft = curproc->p_filetable;
    struct openfile *file;

    // get the openfile from the filetable
    int result = filetable_get(ft, fd, &file);
    if (result) {
        return result;
    }

    // remove the openfile from the filetable
    result = filetable_remove(ft, fd);
    if (result) {
        return result;
    }

    // openfile close file by decrementing the reference counter
    openfile_decref(file);

    return 0;
}

/**
 * Change current position in file
 * @param fd file descriptor index
 * @param pos position
 * @param whence seek operation
 * @param retval actual return - new file offset
 * @return 0 success, else error code
 */
int sys_lseek(int fd, off_t pos, int whence, int64_t *retval)
{
    // get openfile
    struct openfile *file;
    int result = filetable_get(curproc->p_filetable, fd, &file);
    if (result) {
        return result;
    }

    // acquire openfile's offset lock
    off_t new_offset;
    lock_acquire(file->file_offsetlock);
    new_offset = file->file_offset;

    struct stat file_info;

    switch (whence)
    {
    case SEEK_SET: // SEEK_SET, the new position is pos
        new_offset = pos;
        break;
    case SEEK_CUR: // SEEK_CUR, the new position is the current position plus pos
        new_offset += pos;
        break;
    case SEEK_END: // SEEK_END, the new position is the position of end-of-file plus pos
        result = VOP_STAT(file->file_vnode, &file_info);
        if (result) {
            lock_release(file->file_offsetlock);
            return result;
        }
        new_offset = file_info.st_size + pos;
        break;
    default: // anything else, lseek fails
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

    // done --- set retval to be new offset
    *retval = new_offset;

    // release lock
    lock_release(file->file_offsetlock);

    return 0;
}

/**
 * Change current directory
 * @param pathname directory pathname
 * @return 0 success, else error code
 */
int sys_chdir(const char *pathname)
{
    // validate pathname
    if (pathname == NULL) {
        return EFAULT;
    }

    // copy pathname into kernel
    char *kname = kmalloc(PATH_MAX);
    int result = copyinstr((const_userptr_t)pathname, kname, PATH_MAX, NULL);
    if (result) {
        kfree(kname);
        return result;
    }

    result = vfs_chdir(kname);
    if (result) {
        kfree(kname);
        return result;
    }

    kfree(kname);
    return 0;
}

/**
 * Clone file handle / descriptor from old file descriptor to new file descriptor
 * @param oldfd  from this file descriptor index
 * @param newfd  to this file descriptor index
 * @param retval actual return - new file descriptor index
 * @return 0 success, else error code
 */
int sys_dup2(int oldfd, int newfd, int *retval)
{
    // validate file handles
    if (oldfd < 0 || newfd < 0 || oldfd > OPEN_MAX || newfd > OPEN_MAX) {
        return EBADF;
    }

    // Using dup2 to clone a file handle onto itself has no effect.
    if (oldfd == newfd) {
        *retval = newfd;
        return 0;
    }

    struct openfile *oldfile, *newfile;
    int result = filetable_get(curproc->p_filetable, oldfd, &oldfile);
    if (result) {
        return result;
    }
    
    // If newfd names an already-open file, that file is closed
    if (curproc->p_filetable->openfiles[newfd] != NULL) {
        result = filetable_get(curproc->p_filetable, newfd, &newfile);
        if (result) {
            return result;
        }
        result = filetable_remove(curproc->p_filetable, newfd);
        if (result) {
            return result;
        }
        openfile_cleanup(newfile);
    }

    // clone oldfd onto newfd
    curproc->p_filetable->openfiles[newfd] = oldfile;
    openfile_incref(oldfile);

    *retval = newfd;
    return 0;
}

/**
 * Get name of current working directory (backend)
 * @param buf    buffer to read to
 * @param buflen buffer size
 * @param retval actual return - the length of data returned
 * @return 0 success, else error code
 */
int sys___getcwd(char *buf, size_t buflen, int *retval)
{
    // validate buflen
    if (buflen == 0) {
        return ENOENT;
    }

    // this call behaves like read
    struct iovec iov;
    struct uio uu;
    uio_kinit(&iov, &uu, buf, buflen, 0, UIO_READ);

    int result = vfs_getcwd(&uu);
    if (result)
    {
        return result;
    }

    // returns the length of the data returned
    *retval = buflen - uu.uio_resid;
    return 0;
}