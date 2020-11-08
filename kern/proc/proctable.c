#include <proctable.h>
#include <kern/errno.h>
#include <limits.h>
#include <proc.h>
#include <synch.h>

void proctable_bootstrap(void)
{
    for (int i = 0; i < PROCS_MAX; i++) {
        pt[i] = NULL;
    }

    // assign pid 1 to kernel init process
    // --- kernel's parent pid should not exist, so assign 0 non-negative
    pt[1] = procinfo_create((pid_t) 0);
    if (pt[1] == NULL) {
        panic("Panic creating kernel procinfo in proctable");
    }

    p_count = 1;
    p_lock = lock_create("p_lock");
    if (p_lock == NULL) {
        panic("Panic creating proctable p_lock");
    }
}

struct proctable *proctable_init(void)
{
    struct proctable *pt = kmalloc(sizeof(struct proctable));
    if (pt == NULL) {
        return NULL;
    }

    for (int i = 0; i < PROCS_MAX; i++) {
        pt[i] = NULL;
    }

    return pt;
}

int proctable_assign(pid_t *pid)
{
    KASSERT(pt != NULL);

    // lock the table
    lock_acquire(p_lock);

    // increment p_count
    p_count++;
    // make a new variable to hold p_count ---> as new_pid for this process
    pid_t new_pid = p_count;
    // validate new_pid with PROCS_MAX
    if (new_pid > PROCS_MAX) {
        return ENPROC;
    }
    struct procinfo new_pinfo = procinfo_create(new_pid);
    if (new_pinfo == NULL) {
        return ENOMEM;
    }
    // put into the table (index: new_pid, value: new_pinfo)
    pt[new_pid] = new_pinfo;

    // release lock
    lock_release(p_lock);

    // return new_pid
    *pid = new_pid;
    return 0;
}

// function to remove a pid from the table
void proctable_unassign(pid_t this_pid) // need better name
{ 
    KASSERT(this_pid < 2 || this_pid > PROCS_MAX);

    // acquire lock
    lock_acquire(p_lock);

    // get procinfo from table to check validate
    struct procinfo *this_pinfo = pt[this_pid];
    // --- not null, exited == false, ppid???
    KASSERT(this_pinfo != NULL);

    this_pinfo->p_status = -1;  // set exit status to null (or -1)
    this_pinfo->p_exited = true // set exited to true
    this_pinfo->p_ppid = 0;       // set ppid to 0
    // destory procinfo
    procinfo_cleanup(this_pinfo);

    // set value of array to null
    pt[this_pid] = NULL;

    lock_release(p_lock);
}

// function to set exit status ---> since some waking up needs to happen

// ===== functions for procinfo =====
static struct procinfo *procinfo_create(pid_t ppid)
{
    struct procinfo *pinfo = kmalloc(sizeof(struct procinfo));
    if (pinfo == NULL) {
        kfree(pinfo);
        return NULL;
    }

    pinfo->p_ppid = ppid;
    pinfo->p_exited = false;
    pinfo->p_status = -1;
    pinfo->p_cv = cv_create("p_cv");
    if (pinfo->p_cv == NULL) {
        kfree(pinfo);
        return NULL;
    }

    return pinfo;
}

static void procinfo_cleanup(struct procinfo *pinfo) 
{
  KASSERT(pinfo != NULL);
  KASSERT(pinfo->p_exited == true);
  
  cv_destroy(pinfo->p_cv);
  kfree(pinfo);
}
