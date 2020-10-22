#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <openfile.h>
#include <vnode.h>
#include <synch.h>
#include <spinlock.h>
#include <vfs.h>

/**
 * static private function used by openfile_open()
 */
struct openfile *
openfile_init(struct vnode *vn, int status)
{
    // malloc for openfile object
    struct openfile *file;

    file = kmalloc(sizeof(struct openfile));
    if (file == NULL)
    {
        return NULL;
    }

    // set object fields
    file->file_vnode = vn;

    file->status = status;
    file->file_offset = 0;
    file->file_refcount = 1;

    file->file_offsetlock = lock_create("file");
    if (file->file_offsetlock == NULL)
    {
        return NULL;
    }
    spinlock_init(&file->file_countlock);

    return file;
}

/**
 * Destroy an openfile object
 * @param file openfile object
 */
void openfile_cleanup(struct openfile *file)
{
    KASSERT(file->file_refcount == 1);

    spinlock_cleanup(&file->file_countlock);
    lock_destroy(file->file_offsetlock);

    file->file_refcount = 0;
    // file->file_offset = NULL;
    // file->status = NULL;

    vnode_cleanup(file->file_vnode);
}

/**
 * Increment reference counter of an openfile object
 * @param file openfile object
 */
void openfile_incref(struct openfile *file)
{
    KASSERT(file != NULL);

    spinlock_acquire(&file->file_countlock);
    file->file_refcount++;
    spinlock_release(&file->file_countlock);
}

/**
 * Decrement reference counter of an openfile object
 * Destory the openfile object upon none reference
 * @param file openfile object
 */
void openfile_decref(struct openfile *file)
{
    bool destroy;

    KASSERT(file != NULL);

    spinlock_acquire(&file->file_countlock);

    KASSERT(file->file_refcount > 0);
    if (file->file_refcount > 1)
    {
        file->file_refcount--;
        destroy = false;
    }
    else
    {
        destroy = true;
    }
    spinlock_release(&file->file_countlock);

    if (destroy)
    {
        openfile_cleanup(file);
    }
}

/**
 * From openfile table - open a file
 * ===========
 * - create vnode object and call vfs_open with that vnode
 * - create an openfile object, then
 * - put vnode onto that openfile object
 * - return the openfile (for sys_open)
 * @param path      file path / file name
 * @param openflags file permission flags logically OR-ed together
 * @param mode      mode to be opened at
 * @param ret       actual openfile object to be returned 
 * @return          0 success, else error code
 */
int openfile_open(char *path, int openflags, mode_t mode, struct openfile **ret)
{
    // get the vnode of the file we want to read
    // actual return is "vn" --- double pointer
    struct vnode *vn;
    int result = vfs_open(path, openflags, mode, &vn);
    if (result)
    {
        return result;
    }

    // check status code (access permission) is valid
    int status = openflags & O_ACCMODE; // from vfspath.c --- line 52 "how" --- O_RDONLY, O_WRONLY, O_RDWR

    // create the actual openfile object --- put onto ret
    struct openfile *file;
    file = openfile_init(vn, status);
    if (file == NULL)
    {
        vfs_close(vn);
        return 1; // should use some actual error code
    }

    *ret = file;
    return 0;
}
