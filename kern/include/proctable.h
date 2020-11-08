#ifndef _PROCTABLE_H_
#define _PROCTABLE_H_

#include <limits.h>
#include <synch.h>

// just like filetable but global
// global process table / array (or pid table)
//
// index is pid
// value is procinfo
//
static struct procinfo *pt[PROCS_MAX];

static pid_t p_count;              // keep track of number of assigned pid
static struct lock *p_lock;      // lock for exit status

static struct procinfo {
  pid_t p_ppid;            // process id of parent of this index
  bool p_exited;           // true exited
  int p_status;           // exit status
  struct cv *p_cv;   // wait for exit
};

// ===== functions for proc_table =====
void protable_bootstrap(void);  // initialize global proctable (called in main.c along with other bootstrap)

// copied from filetable --- will need to change
struct proctable *proctable_init(void);
// void proctable_cleanup(void); --- will never delete the process table since it should always exist as long as OS is running
int proctable_assign(pid_t *);
int proctable_unassign(pid_t);

// function to set exit status later

// ====== functions for procinfo ======
static struct procinfo* procinfo_create(pid_t);
static void procinfo_cleanup(struct procinfo *);

#endif /* _PROCTABLE_H_ */