#include <type.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <openfile.h>
#include <vnode.h>
#include <synch.h>
#include <spinlock.h>
#include <vfs.h>

struct openfile *
openfile_init(struct vnode *vn, int status)
{
    struct openfile *file;

    file = kmalloc(sizeof(struct openfile));
    if (file == NULL) {
        return NULL;
    }

    file->file_vnode = vn;

    file->status = status;
    file->file_offset = 0;
    file->file_refcount = 1;

    file->file_offsetlock = lock_create("file");
    if (file->file_offsetlock == NULL) {
        return NULL;
    }
    spinlock_init(&file->file_countlock);

    return file;
}

void 
openfile_cleanup(struct openfile * file)
{
    KASSERT(file->file_refcount == 1);

    spinlock_cleanup(file->file_countlock);
    lock_destroy(file->file_offsetlock);
    
    file->file_refcount = 0;
    file->file_offset = NULL;
    file->status = NULL;

    vnode_cleanup(file->file_vnode);
}

void 
openfile_incref(struct openfile *file)
{
    KASSET(file != NULL);

    spinlock_acquire(&file->file_countlock);
    file->file_refcount++;
    spinlock_release(&file->file_countlock);
}

void 
openfile_decref(struct openfile *file)
{
    bool destroy;

    KASSERT(file != NULL);

    spinlock_acquire(&file->file_countlock);

    KASSERT(file->file_refcount > 0);
    if (file->file_refcount > 1) {
        file->file_refcount--;
        destroy = false;
    }
    else {
        destroy = true;
    }
    spinlock_release(&file->file_refcount);

    if (destroy) {
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
 */
int
openfile_open(char *path, int openflags, mode_t mode, struct openfile **ret) 
{
    struct vnode *vn;
    int result = vfs_open(path, openflags, mode, &vn); // actual return is "vn" --- double pointer
    if (result) {
        return result; // something bad happened
    }
    
    int status = openflags & O_ACCMODE; // from vfspath.c --- line 52 "how" --- O_RDONLY, O_WRONLY, O_RDWR
    
    struct openfile *file;
    file = openfile_init(vn, status);
    if (file == NULL) {
        vfs_close(vn);
        return 1; // should use some actual error code
    }

    *ret = file;
    return 0;
}

void 
openfile_close(struct openfile *file)
{
    openfile_decref(file);
}

int 
openfile_mkdir(char *path, mode_t mode)
{
    return vfs_mkdir(path, mode);
}

int 
openfile_chdir(char *path)
{
    return vfs_chdir(path);
}
