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
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;

    // copyin program name into kernel
    char *progname = kmalloc(PATH_MAX);
    result = copyinstr((const_userptr_t)program, progname, PATH_MAX, NULL);
    if (result) {
        kfree(progname);
        return result;
    }

    // malloc argument buffer on heap
    char **argbuf = kmalloc(ARG_MAX * sizeof(char *));
    if (argbuf == NULL) {
        kfree(progname);
        kfree(argbuf);
        return ENOMEM;
    }
    
    int argc = 0;
    // copyin argument pointers to buffer
    // pointers are aligned by 4
    for (argc = 0; args[argc] != NULL; argc++) {
        // argbuf[argc] = kmalloc(4);
        // argbuf[argc] = args[argc];
        // kprintf("argbuf[%d]: %p\n", argc, argbuf[argc]);
    }

    // // copy argument strings to buffer
    // int argpos = argc; 
    // for (int i = 0; i < argc; i++) {
    //     size_t arglen = strlen(args[i]) + 1; // last one is null
    //     size_t copylen = arglen * sizeof(char);
    //     kprintf("copylen: %d\n", copylen);
    //     // argbuf[argpos] = (char *)kmalloc(copylen);
    //     argbuf[argpos] = (char *)kmalloc(copylen);

    //     result = copyin((const_userptr_t)args[i], argbuf[argpos], copylen);
    //     if (result) {
    //         kfree(argbuf);
    //         return result;
    //     }
    //     // argpos += arglen;
    //     argpos++;
    // }


    /**
     * STEP 2: Open the executable, create a new address space and load the elf into it
     * ====== Copied from runprogram and modified ======
     * 
     * 
     */
     
    // open program file
    result = vfs_open(progname, O_RDONLY, 0, &v);
    if (result) {
        vfs_close(v);
        kfree(progname);
        return result;
    }
    // kprintf("open program ok\n");

    kfree(progname);

    // create new address space
    // KASSERT(proc_getas() == NULL);
    as = as_create();
    if (as == NULL) {
        vfs_close(v);
        return ENOMEM;
    }
    // kprintf("create as ok\n");

    struct addrspace * oldas = proc_setas(as); // MODIFIED: save old address space so that we will delete it later
    as_activate();
    // kprintf("activate as ok\n");

    // load executable
    result = load_elf(v, &entrypoint);
    if (result) {
        vfs_close(v);
        proc_setas(oldas);
        return result;
    }
    // kprintf("load executable ok\n");

    /* Done with the file now. */
    vfs_close(v);

    /* Define the user stack in the address space */
    result = as_define_stack(as, &stackptr);
    if (result) {
        proc_setas(oldas);
        return result;
    }
    // kprintf("define user stakc ok\n");

    /**
     *  ====== END of Copied from runprogram and modified ======
     *  STEP 3: Copy the arguments from kernel buffer into user stack
     * 
     */

    // copy arguments to new address space
    // minus stack pointer with length of buffer

    kprintf("stackptr: %04x\n", stackptr);
    stackptr -= argc;
    kprintf("new stackptr: %04x\n", stackptr);

    // update argument pointers to user stack
    // addresses didn't change...
    // for (int i = 0; i < argc; i++) {
    //     if (i == 0) {
    //         *argbuf[0] = stackptr;
    //     } else {
    //         *argbuf[i] += strlen(argbuf[i]);
    //         kprintf("argbuf[%d]: %p\n", i, argbuf[i]);
    //     }
    // }

    // copyout arguments to user stack
    // int argpos = stackptr;
    for (int i = 0; i < argc; i++) {
        size_t copylen = (strlen(args[i]) + 1) * sizeof(char);

        kprintf("stackptr: %p\n", (void *)stackptr);
        kprintf("copylen: %d\n", copylen);

        result = copyout((void *)args[i], (userptr_t)stackptr, copylen);
        if (result) {
            kprintf("failed to copyout argbuf\n");
            proc_setas(oldas);
            return result;
        }
        stackptr += copylen;
    }

    kprintf("new arg str: %p\n", (void *)stackptr);
    /*
    vaddr_t *user_arg = (vaddr_t *) kmalloc(argc * sizeof(vaddr_t));
    for (int i = 0; i < argc; i++) {
        size_t arglen = strlen(args[i]) + 1; // last one is null
        stackptr -= sizeof(char) * ROUNDUP(arglen, 4);

        result = copyout((void *)argbuf[i], (userptr_t)stackptr, arglen);
        if (result) {
            return result;
        }

        user_arg[i] = stackptr;
    }
    */



    // clean up old address space
    as_destroy(oldas);

    /**
     * STEP 4: Return user mode using enter_new_process
     * 
     * 
     */

    // warp to user mode
    enter_new_process(argc, (userptr_t) argbuf, NULL, stackptr, entrypoint);

    panic("enter_new_process returned\n");
    return EINVAL;
}

void free_buffer(char **argbuf, int argc) {
    for (int i = 0; i < argc - 1; i++) {
        kfree(argbuf[i]);
    }
    kfree(argbuf);
}
