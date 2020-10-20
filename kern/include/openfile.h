#ifndef _OPENFILE_H_
#define _OPENFILE_H_

struct openfile
{
  struct vnode *file_vnode;
  int status; // O_RDONLY, O_WRONLY, O_RDWR

  struct lock *offset_lock;
  off_t offset;

  struct spinlock file_countlock;
  int file_refcount;
};

int openfile_open(char *path, int openflag, mode_t mode, struct openfile **ret); // create vnode, call vfs_open
void openfile_incref(struct openfile *file);
void openfile_decref(struct openfile *file);

#endif
