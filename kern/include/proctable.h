#ifndef _PROCTABLE_H_
#define _PROCTABLE_H_

#include <types.h>
#include <limits.h>
#include <synch.h>

struct procinfo {
  pid_t p_ppid;           // process id of parent of this index
  bool p_exited;          // true exited
  int p_status;           // exit status
  struct cv *p_cv;        // wait for exit
};

// ===== functions for proc_table =====
void proctable_bootstrap(void);  // initialize global proctable (called in main.c along with other bootstrap)

// copied from filetable --- will need to change
// struct proctable *proctable_init(void);
// void proctable_cleanup(void); --- will never delete the process table since it should always exist as long as OS is running
int proctable_assign(pid_t *);
void proctable_unassign(pid_t);

// function to set exit status later
void proctable_exit(int);

// ====== functions for procinfo ======
struct procinfo* procinfo_create(pid_t);
void procinfo_cleanup(struct procinfo *);

#endif /* _PROCTABLE_H_ */