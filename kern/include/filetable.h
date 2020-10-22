#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <limits.h>
#include <openfile.h>

struct filetable
{
  struct openfile *openfiles[OPEN_MAX];
};

struct filetable *filetable_init(void);
void filetable_cleanup(struct filetable *);
int filetable_add(struct filetable *, struct openfile *, int *);
int filetable_get(struct filetable *, int, struct openfile **);
int filetable_remove(struct filetable *, int);

#endif /* _FILETABLE_H_ */
