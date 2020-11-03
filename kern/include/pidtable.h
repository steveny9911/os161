#ifndef _PIDTABLE_H_
#define _PIDTABLE_H_

#include <limits.h>

struct pidtable
{
    struct proc_info *proc_infos[PID_MAX];
};

struct pidtable *pidtable_init(void);
void pidtable_cleanup(struct pidtable *);
int pidtable_add(struct pidtable *, struct proc_info *, int *);
int pidtable_get(struct pidtable *, int, struct proc_info *);
int pidtable_remove(struct pidtable *, int);

#endif /* _PIDTABLE_H_ */