#include <filetable.h>
#include <limits.h>
#include <openfile.h>

struct filetable *
filetable_init(void)
{
    struct openfile *openfiles;
    openfiles = kmalloc(OPEN_NAX * sizeof(struct openfile));

    return openfiles;
}

void 
filetable_cleanup(struct filetable *openfiles)
{
    for (int i = 0; i < OPEN_MAX; i++) {
        openfile_cleanup(openfiles[i]);
    }
}