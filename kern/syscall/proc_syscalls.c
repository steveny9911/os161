#include <syscall.h>
#include <mips/trapframe.h>
#include <addrspace.h>
#include <current.h>
#include <types.h>
#include <kern/errno.h>
#include <proc.h>
#include <proctable.h>
#include <kern/wait.h>
#include <copyinout.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <addrspace.h>

int sys_getpid(pid_t *retval)
{
    *retval = curproc->p_pid;
    return 0;
}

void sys__exit(int exitcode)
{
    int exitstatus = _MKWAIT_EXIT(exitcode);
    proctable_exit(exitstatus);
}

/**
 * Called by parent to create a child process
 * 
 */
int sys_fork(struct trapframe *parent_tf, pid_t *retval)
{
    int result;

    // create a new child process
    // kprintf("create runprogram\n");
    struct proc *child = proc_create_runprogram("child");
    if (child == NULL)
    {
        // kprintf("failed create runprogram\n");
        return ENOMEM;
    }

    // assign pid to child and set process table
    // kprintf("assign pid\n");
    // kprintf("%d", curproc->p_pid);
    result = proctable_assign(&child->p_pid);
    if (result)
    {
        // kprintf("failed assign pid\n");
        proc_destroy(child);
        // kfree(child_tf);
        return result;
    }

    // set retval to child pid for parent
    *retval = child->p_pid;

    // copy address space
    // as_copy returns a pointer so we do no need to initialize
    // kprintf("copy address space\n");
    result = as_copy(proc_getas(), &child->p_addrspace);
    if (result)
    {
        // kprintf("failed address space\n");
        proc_destroy(child);
        return result;
    }

    // copy file table
    // filetable_copy returns a pointer so we do no need to initialize
    // kprintf("copy filetable\n");
    result = filetable_copy(curproc->p_filetable, &child->p_filetable);
    if (result)
    {
        // kprintf("failed filetable\n");
        proc_destroy(child);
        return result;
    }

    // kprintf("malloc trapframe\n");
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL)
    {
        // kprintf("failed malloc trapframe\n");
        proc_destroy(child);
        kfree(child_tf);
        return ENOMEM;
    }

    // copy trapframe
    // still need to change trapframe inside "enter_forked_process"
    // the actual change for registers (v0, epc) will occur inside "enter_forked_process" mainly for child
    // parent will remain the same
    //
    // more in "enter_forked_process"
    //
    // trapframe_copy(parent_tf, &child_tf);
    *child_tf = *parent_tf;

    // kprintf("begin enter_forked_process\n");
    result = thread_fork("child", child, &enter_forked_process, (void *)child_tf, (unsigned long)NULL);
    if (result)
    {
        // kprintf("failed enter_forked_process\n");
        proc_destroy(child);
        kfree(child_tf);
        return result;
    }

    // no error
    return 0;
}

int sys_waitpid(pid_t waitpid, int *status, int options, pid_t *retval)
{
    int result;
    (void)options;

    result = proctable_wait(waitpid, status);
    if (result)
    {
        return result;
    }

    *retval = waitpid;
    return 0;
}

int sys_execv(const char *program, char **args)
{
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;

    char *progname = (char *)kmalloc(PATH_MAX);
    result = copyinstr((const_userptr_t)program, progname, PATH_MAX, NULL);
    if (result)
    {
        kfree(progname);
        return result;
    }

    int argc = 0;
    for (argc = 0; args[argc] != NULL; argc++) { }

    char **argbuf = (char **)kmalloc((argc + 1) * sizeof(char *));
    if (argbuf == NULL) {
        kfree(progname);
        for (int j = 0; j < argc; j++) kfree(argbuf[j]);
        kfree(argbuf);
        return ENOMEM;
    }

    for (int i = 0; i < argc; i++)
    {
        size_t arglen = strlen(args[i]) + 1;
        size_t copylen = arglen * sizeof(char);
        argbuf[i] = (char *)kmalloc(copylen);
        result = copyin((const_userptr_t)args[i], argbuf[i], copylen);
        if (result)
        {
            kfree(progname);
            for (int j = 0; j < argc; j++) kfree(argbuf[j]);
            kfree(argbuf);
            return result;
        }
    }

    argbuf[argc] = NULL;

    result = vfs_open(progname, O_RDONLY, 0, &v);
    if (result)
    {
        for (int j = 0; j < argc; j++) kfree(argbuf[j]);
        kfree(argbuf);
        vfs_close(v);
        kfree(progname);
        return result;
    }

    kfree(progname);

    as = as_create();
    if (as == NULL)
    {
        for (int j = 0; j < argc; j++) kfree(argbuf[j]);
        kfree(argbuf);
        vfs_close(v);
        return ENOMEM;
    }

    struct addrspace *oldas = proc_setas(as);

    result = load_elf(v, &entrypoint);
    if (result)
    {
        for (int j = 0; j < argc; j++) kfree(argbuf[j]);
        kfree(argbuf);
        vfs_close(v);
        proc_setas(oldas);
        return result;
    }

    vfs_close(v);

    result = as_define_stack(as, &stackptr);
    if (result)
    {
        for (int j = 0; j < argc; j++) kfree(argbuf[j]);
        kfree(argbuf);
        proc_setas(oldas);
        return result;
    }

    vaddr_t *uargs_addr = (vaddr_t *)kmalloc((argc + 1) * sizeof(vaddr_t));
    for (int i = 0; i < argc; i++) // this should be flipped
    {
        size_t arglen = strlen(argbuf[i]) + 1;
        kprintf("arglen: %x\n", arglen);
        size_t copylen = ROUNDUP(arglen, 4) * sizeof(char);
        kprintf("copylen: %x\n", copylen);
        kprintf("BEFORE stackptr: %p\n", (void *)stackptr);
        stackptr -= (copylen);
        kprintf("AFTER stackptr: %p\n", (void *)stackptr);
        result = copyout((void *)argbuf[i], (userptr_t)stackptr, copylen);
        if (result)
        {
            for (int j = 0; j < argc; j++) kfree(argbuf[j]);
            kfree(argbuf);
            kfree(uargs_addr);
            return result;
        }
        uargs_addr[i] = stackptr;
        kprintf("uargs_addr[%d]: %p\n", i, (void *)uargs_addr[i]);
    }

    uargs_addr[argc] = (vaddr_t)NULL;

    for (int i = argc; i >= 0; i--)
    {
        size_t copylen = sizeof(vaddr_t); // vaddr_t is u32 --- which is 4 bytes
        stackptr -= copylen;
        result = copyout((void *)&uargs_addr[i], (userptr_t)stackptr, copylen);
        if (result)
        {
            for (int j = 0; j < argc; j++) kfree(argbuf[j]);
            kfree(argbuf);
            kfree(uargs_addr);
            return result;
        }
    }

    as_destroy(oldas);
    as_activate();

    for (int j = 0; j < argc; j++) kfree(argbuf[j]);
    kfree(argbuf);
    kfree(uargs_addr);

    enter_new_process(argc, (userptr_t)stackptr, NULL, stackptr, entrypoint);

    return EINVAL;
}
