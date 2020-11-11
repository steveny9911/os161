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
    if (child == NULL) {
        // kprintf("failed create runprogram\n");
        return ENOMEM;
    }

    // assign pid to child and set process table
    // kprintf("assign pid\n");
    // kprintf("%d", curproc->p_pid);
    result = proctable_assign(&child->p_pid);
    if (result) {
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
    if (result) {
        // kprintf("failed address space\n");
        proc_destroy(child);
        return result;
    }

    // copy file table
    // filetable_copy returns a pointer so we do no need to initialize
    // kprintf("copy filetable\n");
    result = filetable_copy(curproc->p_filetable, &child->p_filetable);
    if (result) {
        // kprintf("failed filetable\n");
        proc_destroy(child);
        return result;
    }

    // kprintf("malloc trapframe\n");
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if (child_tf == NULL) {
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
    if (result) {
        // kprintf("failed enter_forked_process\n");
        proc_destroy(child);
        kfree(child_tf);
        return result;
    }

    // no error
    return 0;
}

int sys_waitpid(pid_t waitpid, int *status, int options, pid_t *retval) {
    int result;
    (void) options;

    result = proctable_wait(waitpid, status);
    if (result) {
        return result;
    }

    // *status = pt[waitpid]->p_status;
    *retval = waitpid;
    return 0;
}

int sys_execv(const char *program, char **args)
{
    /*
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;
    char **argbuf[ARG_MAX];  // argbuf as buffer for copied arguments
    int ALIGN = 4;
    int argc = 0;
    int cnt = 0;
    
    // copy in arguments from old address space

    // copyin program name into kernel
    char *progname = kmalloc(PATH_MAX);
    result = copyinstr((const_userptr_t)program, progname, PATH_MAX, NULL);
    if (result) {
        kfree(progname);
        return result;
    }

    
    kprintf("number of argc: %d", argc);
    for (int i = 0; args[i] != NULL; i++) {
        argc++;
    }
    kprintf("number of argc: %d", argc);

    // copy argument pointers to buffer
    // pointers are aligned by 4
    for (argc = 0; args[argc] != NULL; argc++) {
        result = copyinstr((const_userptr_t)args[argc * ALIGN], *argbuf[argc * ALIGN], ALIGN, NULL);
        if (result) {
            kfree(progname);
            return result;
        }
    }

    // copy argument strings to buffer
    for (int i = 0; i < argc; i++) {
        char *ptr = *argbuf[i * ALIGN];
        size_t base = (argc + 1) * ALIGN;
        size_t len = 0;
        cnt = 0;

        // copy strings from address argument pointers point to
        // length of string is stored in len
        result = copyinstr((const_userptr_t)&ptr, *argbuf[base + cnt * ALIGN], ARG_MAX, &len);
        if (result) {
            kfree(progname);
            return result;
        }

        // add paddings if string is not aligned by 4
        if (len % ALIGN != 0) {
            for (size_t pos = len + 1; pos <= (len / ALIGN + 1) * ALIGN; pos++) {
                *argbuf[base + cnt * ALIGN + pos] = '\0';
            }
        }

        // use cnt to record string position
        cnt += (len / ALIGN);
    }

    // copied from runprogram()
    result = vfs_open(progname, O_RDONLY, 0, &v);
    if (result) {
        vfs_close(v);
        kfree(progname);
        return result;
    }

    kfree(progname);

    // create new address space
    KASSERT(proc_getas() == NULL);
    as = as_create();
    if (as == NULL) {
        vfs_close(v);
        return ENOMEM;
    }
    
    proc_setas(as);
    as_activate();

    // load executable
    result = load_elf(v, &entrypoint);
    if (result) {
        vfs_close(v);
        as_deactivate();
        as_destroy(as);
        return result;
    }

    vfs_close(v);

    // define new stack region
    result = as_define_stack(as, &stackptr);
    if (result) {
        as_deactivate();
        as_destroy(as);
        return result;
    }

    // copy arguments to new address space

    // minus stack pointer with length of buffer
    stackptr -= (argc + cnt) * ALIGN;
    // update argument pointers to user stack
    for (int i = 0; i < argc; i++) {
        argbuf[i * ALIGN] += stackptr;
    }

    // copy out arguments to user stack
    for (int i = 0; i < (argc + cnt) * ALIGN; i++) {
        result = copyoutstr(*argbuf[i], (userptr_t)&stackptr, ARG_MAX, NULL);
        if (result) { 
            as_deactivate();
            as_destroy(as);
            return result;
        }
    }

    // clean up old address space
    as_deactivate();
    as_destroy(as);

    // wrap to user mode
    enter_new_process(argc, NULL, NULL, stackptr, entrypoint);

    panic("enter_new_process returned\n");
    return EINVAL;
    */

    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;
    int ALIGN = 4;
    int argc = 0;
    int arglen, argpos;

    // malloc argument buffer on heap
    char **argbuf = kmalloc(ARG_MAX * sizeof(char *));
    if (argbuf == NULL) {
        // kprintf("Malloc failed\n");
        kfree(argbuf);
        return ENOMEM;
    }

    // copyin program name into kernel
    char *progname = kmalloc(PATH_MAX);
    result = copyinstr((const_userptr_t)program, progname, PATH_MAX, NULL);
    if (result) {
        kfree(progname);
        return result;
    }
    kprintf("progname: %s\n", progname);

    for (int i = 0; args[i] != NULL; i++) {
        argc++;
    }
    kprintf("number of argc: %d\n", argc);
    
    /**
     *  STEP 1: Copy arguments from user space into kernel buffer
     * 
     * 
     */
     
    // copy argument pointers to buffer
    // pointers are aligned by 4
    for (argc = 0; args[argc] != NULL; argc++) {
        argbuf[argc] = kmalloc(ALIGN);
        // kprintf("args[argc]: %p\n", &args[argc]);
        result = copyin((const_userptr_t)&args[argc], (char *)argbuf[argc], ALIGN);
        if (result) {
            kfree(progname);
            return result;
        }
        // kprintf("argbuf pointer: %p\n", argbuf[argc]);
    }
    // kprintf("number of argc: %d\n", argc);

    // copy argument strings to buffer
    argpos = argc * ALIGN;
    for (int i = 0; i < argc; i++) {
        arglen = (strlen(args[i])) * sizeof(char);
        argbuf[argpos] = kmalloc(arglen);
        
        kprintf("argpos: %d\n", argpos);
        result = copyin((const_userptr_t)args[i], (char *)argbuf[argpos], arglen);
        if (result) {
            kfree(progname);
            return result;
        }

        kprintf("arg str: %s\n", argbuf[argpos]);
        argpos += ROUNDUP(arglen, 4);
    }
    // kprintf("length of argbuf: %d\n", argpos - ROUNDUP(arglen, 4) / ALIGN);

    /**
     * STEP 2: Open the executable, create a new address space and load the elf into it
     * ====== Copied from runprogram and modified ======
     * 
     * 
     */
    /* Open the file. */
    result = vfs_open(progname, O_RDONLY, 0, &v);
    if (result) {
        return result;
    }

    /* We should be a new process. */
    KASSERT(proc_getas() != NULL);

    /* Create a new address space. */
    as = as_create();
    if (as == NULL) {
        vfs_close(v);
        return ENOMEM;
    }

    /* Switch to it and activate it. */
    // struct addrspace * oldas = proc_setas(as); // MODIFIED: save old address space so that we will delete it later
    as_activate();

    /* Load the executable. */
    result = load_elf(v, &entrypoint);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        vfs_close(v);
        return result;
    }

    /* Done with the file now. */
    vfs_close(v);

    /* Define the user stack in the address space */
    result = as_define_stack(as, &stackptr);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        return result;
    }

    /**
     *  ====== END of Copied from runprogram and modified ======
     *  STEP 3: Copy the arguments from kernel buffer into user stack
     * 
     */


    /**
     * STEP 4: Return user mode using enter_new_process
     * 
     * 
     */
    /* Warp to user mode. */
    enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
              NULL /*userspace addr of environment*/,
              stackptr, entrypoint);

    /* enter_new_process does not return. */
    panic("enter_new_process returned\n");
    return EINVAL;
}
