#ifndef _OPENFILE_H_
#define _OPENFILE_H_

#include <type.h>
#include <vnode.h>

struct openfile
{
  struct vnode *file_vnode;
  int status; // O_RDONLY, O_WRONLY, O_RDWR

  struct lock *file_offsetlock;
  off_t file_offset;

  struct spinlock file_countlock;
  int file_refcount;
};

struct openfile *openfile_init(struct vnode *, int);
void openfile_cleanup(struct openfile *);
void openfile_incref(struct openfile *);
void openfile_decref(struct openfile *);

int openfile_open(char *path, int openflag, mode_t mode, struct openfile **ret); // create vnode, call vfs_open

#endif
