#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <limits.h>

struct filetable
{
  struct openfile *openfiles[OPEN_MAX];
};

struct filetable filetable_create(void);
void filetable_destory(struct filetable *ft);

#endif /* _FILETABLE_H_ */
