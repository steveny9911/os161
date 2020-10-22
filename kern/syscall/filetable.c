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

    /*
    // By default, fd 0, 1 and 2 are used for stdin, stdout, stderr 
    // create new files
    struct vnode *vn0 = NULL, 
                 *vn1 = NULL,
                 *vn2 = NULL;
    struct openfile *file0 = openfile_init(vn0, O_RDONLY);
    struct openfile *file1 = openfile_init(vn1, O_WRONLY);
    struct openfile *file2 = openfile_init(vn2, O_WRONLY);

    // set file paths
    char *con_in = kstrdup("con:");
    char *con_out = kstrdup("con:");
    char *con_err = kstrdup("con:");

    int result;
    // open the files 
    //?return error
    result = vfs_open(con_in, O_RDONLY, 0, &file0->file_vnode);
    if (result) {
        vfs_close(file0->file_vnode);
    }
    result = vfs_open(con_out, O_WRONLY, 0, &file1->file_vnode);
    if (result) {
        vfs_close(file1->file_vnode);
    }
    result = vfs_open(con_err, O_WRONLY, 0, &file2->file_vnode);
    if (result) {
        vfs_close(file2->file_vnode);
    }

    // make fd 0, 1 and 2 point to the files
    ft->openfiles[0] = file0;
    ft->openfiles[1] = file1;
    ft->openfiles[2] = file2;
    */

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

    for (int i = 0; i < OPEN_MAX; i++)
    {
        if (ft->openfiles[i] == NULL)
        {
            ft->openfiles[i] = file;
            *index = i;
            return 0;
        }
    }

    return EMFILE;
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
        return EBADF;

    // get the open file at file descriptor index
    struct openfile *file;
    file = ft->openfiles[index];
    if (file == NULL)
        return ENOENT;

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
    // make sure index is in range
    if (index < 0 || index > OPEN_MAX)
        return EBADF;

    // set to NULL
    ft->openfiles[index] = NULL;
    return 0;
}