#ifndef _OPENFILE_H_
#define _OPENFILE_H_

#include <type.h>
#include <vnode.h>

struct openfile
{
  struct vnode *file_vnode;
  int status;

  struct lock *file_offsetlock;
  off_t file_offset;

  struct spinlock file_countlock;
  int file_refcount;
};

struct openfile *openfile_init(struct vnode *, int);
void openfile_cleanup(struct openfile *);
void openfile_incref(struct openfile *);
void openfile_decref(struct openfile *);
int openfile_open(char *, int, mode_t, struct openfile **);

#endif /* _OPENFILE_H_ */
