#include <filetable.h>
#include <limits.h>
#include <openfile.h>
#include <kern/errno.h>

struct filetable *
filetable_init(void)
{
    struct filetable *ft;
    ft = kmalloc(sizeof(struct filetable));
    if (ft == NULL)
        return NULL;

    for (int i = 0; i < OPEN_MAX; i++)
    {
        ft->openfiles[i] = NULL;
    }

    return ft;
}

void filetable_cleanup(struct filetable *ft)
{
    for (int i = 0; i < OPEN_MAX; i++)
    {
        openfile_cleanup(ft->openfiles[i]);
        ft->openfiles[i] = NULL;
    }
}

/**
 * Add an "openfile" onto the file table array
 * Actual return - file descriptor index
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

int filetable_remove(struct filetable *ft, int index)
{
    if (index < 0 || index > OPEN_MAX)
        return EBADF;

    ft->openfiles[index] = NULL;
    return 0;
}