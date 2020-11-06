#include <syscall.h>
#include <trapframe.h>
#include <addrspace.h>
#include <current.h>
#include <filetable.h>
#include <trapframe.h>
#include <kern/errno.h>
#include <proc.h>

/**
 * Called by parent to create a child process
 * 
 */
int sys_fork(struct trapframe *parent_tf, pid_t *retval)
{
    // current process is the parent
    struct proc *parent = curproc;

    // create a new child process
    struct proc *child = proc_create_runprogram("child");
    if (child == NULL)
        return ENOMEM;

    // copy address space
    // as_copy returns a pointer so we do no need to initialize
    int result = as_copy(proc_getas(), &child->p_addrspace);
    if (result)
        return result;

    // copy file table
    // filetable_copy returns a pointer so we do no need to initialize
    result = filetable_copy(parentc->p_filetable, &child->p_filetable);
    if (result)
        return result;

    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL)
    {
        proc_destroy(child);
        kfree(child_tf);
        return ENOMEM;
    }

    // copy trapframe
    // still need to change trapframe inside "enter_forked_process"
    // the actual change for registers (v0, a3, sp) will occur inside "enter_forked_process" mainly for child
    // parent will remain the same
    //
    // more in "enter_forked_process"
    //
    // trapframe_copy(parent_tf, &child_tf);
    *child_tf = *parent_tf;

    result = thread_fork("child", child, &enter_forked_process, (void *) child_tf, NULL);
    if (result) {
        proc_destroy(child);
        kfree(child_tf);
        return result;
    }

    /* as_define_stack related to execv
    result = as_define_stack(child->p_addrspace, NULL);
    if (result)
    {
        return result;
    }
    */

    // TODO: need to set process table

    // set retval to child pid for parent
    *retval = child->p_pid;

    // no error
    return 0;
}