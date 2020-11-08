#include <filetable.h>
#include <limits.h>
#include <openfile.h>
#include <kern/errno.h>
#include <types.h>
#include <lib.h>
#include <kern/fcntl.h>
#include <vfs.h>

/**
 * Create a new filetable array (should be called by each process)
 * @return filetable object
 */
struct filetable *
filetable_init(void)
{
    // malloc space for filetable object
    struct filetable *ft;
    ft = kmalloc(sizeof(struct filetable));
    if (ft == NULL)
        return NULL;

    // set all entries to NULL as creation of filetable object
    for (int i = 0; i < OPEN_MAX; i++)
    {
        ft->openfiles[i] = NULL;
    }

    return ft;
}

/**
 * Remove (set to NULL) all entries
 * @param ft current process's filetable
 */
void filetable_cleanup(struct filetable *ft)
{
    for (int i = 0; i < OPEN_MAX; i++)
    {
        openfile_cleanup(ft->openfiles[i]);
        ft->openfiles[i] = NULL;
    }
}

/**
 * Add an "openfile" onto the file table array (call by sys_open)
 * @param ft    current process's filetable
 * @param file  openfile object to be added
 * @param index filetable descriptor index that has this openfile object
 * @return      0 success, else error code
 */
int filetable_add(struct filetable *ft, struct openfile *file, int *index)
{
    KASSERT(ft != NULL);
    KASSERT(file != NULL);

    // loop the filetable array until there is an empty NULL entry
    for (int i = 0; i < OPEN_MAX; i++)
    {
        if (ft->openfiles[i] == NULL)
        {
            ft->openfiles[i] = file; // put the openfile at that space
            *index = i;              // return file descriptor index
            return 0;
        }
    }

    return EMFILE; // seems like the filetable is full
}

/**
 * Get the openfile object at filetable descriptor index
 * @param ft    current process's filetable
 * @param index filetable descriptor index
 * @param ret   actual return for openfile object
 * @return      0 success, else error code
 */
int filetable_get(struct filetable *ft, int index, struct openfile **ret)
{
    KASSERT(ft != NULL);

    // check if file descriptor index is in range or not
    if (index < 0 || index > OPEN_MAX)
    {
        return EBADF;
    }

    // validate fd
    if (ft->openfiles[index] == NULL)
    {
        return EBADF;
    }

    // get the open file at file descriptor index
    struct openfile *file;
    file = ft->openfiles[index];
    if (file == NULL)
    {
        return ENOENT;
    }

    *ret = file;
    return 0;
}

/**
 * Remove (set to NULL) an openfile entry on filetable descriptor index
 * @param ft    current process's filetable
 * @param index filetable descriptor index
 * @return      0 success, else error code
 */
int filetable_remove(struct filetable *ft, int index)
{
    // kprintf("filetable_remove index: %d\n", index);
    // make sure index is in range
    if (index < 0 || index > OPEN_MAX)
        return EBADF;

    // set to NULL
    ft->openfiles[index] = NULL;
    return 0;
}

int filetable_copy(struct filetable *old, struct filetable **ret)
{
    struct filetable *new;
    new = filetable_init();
    if (new == NULL) {
        return ENOMEM;
    }
    
    for (int i = 0; i < OPEN_MAX; i++) {
        new->openfiles[i] = old->openfiles[i];
        openfile_incref(old->openfiles[i]);
    }

    *ret = new;
    return 0;
}