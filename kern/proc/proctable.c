#include <proctable.h>
#include <kern/errno.h>
#include <types.h>
#include <limits.h>
#include <proc.h>
#include <synch.h>
#include <current.h>

static struct procinfo *pt[PROCS_MAX];

static pid_t p_count;              // keep track of number of assigned pid
static struct lock *p_lock;        // lock for exit status

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

    curproc->p_pid = 1;

    p_count = 1;
    p_lock = lock_create("p_lock");
    if (p_lock == NULL) {
        panic("Panic creating proctable p_lock");
    }
}

int proctable_assign(pid_t *pid)
{
    KASSERT(pt != NULL);
    KASSERT(curproc->p_pid != 0);

    // lock the table
    lock_acquire(p_lock);

    // increment p_count
    p_count++;
    // make a new variable to hold p_count ---> as new_pid for this process
    pid_t new_pid = p_count;
    // validate new_pid with PROCS_MAX
    if (new_pid > PROCS_MAX) {
        lock_release(p_lock);
        return ENPROC;
    }

    struct procinfo *new_pinfo = procinfo_create(curproc->p_pid);
    if (new_pinfo == NULL) {
        lock_release(p_lock);
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
    this_pinfo->p_exited = true; // set exited to true
    this_pinfo->p_ppid = 0;       // set ppid to 0
    // destory procinfo
    procinfo_cleanup(this_pinfo);

    // set value of array to null
    pt[this_pid] = NULL;

    lock_release(p_lock);
}

// function to set exit status ---> since some waking up needs to happen
void proctable_exit(int exitstatus) {
    // lock proctable
    lock_acquire(p_lock);

    // get our parent if any
    struct proc *proc = curproc;
    struct procinfo *pinfo = pt[curproc->p_pid];

    pinfo->p_exited = true;
    pinfo->p_status = exitstatus;

    // assuming we are parent --- find all child and destory
    for (int i = 2; i < PROCS_MAX; i++) {
        if (pt[i] != NULL && pt[i]->p_ppid == curproc->p_pid) {
            // set these child to zombie parent
            pt[i]->p_ppid = 0;
            // if child has exited --- destory
            if (pt[i]->p_exited == true) {
                procinfo_cleanup(pt[i]);
                pt[i] = NULL;
            }
            // else nothing (we keep the child)
        }
    }

    // assuming we are (also) a child, get our parent
    // if our parent is alive --- wakeup our parent
    if (pinfo->p_ppid != 0) {
        cv_signal(pinfo->p_cv, p_lock);
    } else {
        // else our parent has exited --- we destory ourself
        procinfo_cleanup(pt[curproc->p_pid]);
        pt[curproc->p_pid] = NULL;
    }

    curproc->p_pid = 0;

    // release lock
    lock_release(p_lock);
    
    // remove current thread from current process 
    // (that is --- curthread's process field will be set to null, so thread loses track of current process
    // curthread is still somewhere... we lost track of curthread...
    proc_remthread(curthread);
    
    // so, we put curthread onto kernel process
    // then call thread_exit() which will move curthread into zombie state to be cleanup 
    proc_addthread(kproc, curthread);

    proc_destroy(proc);

    thread_exit();
}

// ===== functions for procinfo =====
struct procinfo *procinfo_create(pid_t ppid)
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

void procinfo_cleanup(struct procinfo *pinfo) 
{
  KASSERT(pinfo != NULL);
  KASSERT(pinfo->p_exited == true);
  
  cv_destroy(pinfo->p_cv);
  kfree(pinfo);
}
