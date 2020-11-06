#include <syscall.h>
#include <trapframe.h>
#include <addrspace.h>
#include <current.h>
#include <filetable.h>
#include <trapframe.h>

int sys_fork(struct trapframe *parent_tf, pid_t *retval)
{
    struct proc *parent = curproc;
    struct proc *child = proc_create("child");
    child->p_addrspace = as_create();
    child->p_filetable = filetable_init();
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));

    int result = as_copy(parent->p_addrspace, &child->p_addrspace);
    if (result) {
        return result;
    }
    result = as_define_stack(child->p_addrspace, NULL);
    if (result) {
        return result;
    }
    result = filetable_copy(parentc->p_filetable, &child->p_filetable);
    trapframe_copy(parent_tf, &child_tf);
}