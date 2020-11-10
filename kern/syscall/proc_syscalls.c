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
#include <vfs.h>

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
    (void) options;

    int result = proctable_wait(waitpid, status);
    // *status = pt[waitpid]->p_status;
    *retval = waitpid;
    return result;
}

// int sys_execv(const char *program, char **argv, int *retval)
// {
//     struct addrspace *as;
//     struct vnode *v;
//     vaddr_t entrypoint, stackptr;
//     int result;
//     char **kargv[ARG_MAX];  // kargv as buffer for copied arguments
//     size_t ALIGN = 4;
//     size_t argc, cnt;

//     // copy in arguments from old address space

//     // copy argument pointers to buffer
//     // pointers are aligned by 4
//     for (argc = 0; argv[argc] != NULL; argc++) {
//         result = copyinstr((const_userptr_t)argv[argc], kargv[argc * ALIGN], ALIGN, NULL);
//         if (result) {
//             return result;
//         }
//     }

//     // copy argument strings to buffer
//     for (int i = 0; i < argc; i++) {
//         char *ptr = kargv[i * ALIGN];
//         size_t base = (argc + 1) * ALIGN;
//         size_t len = 0;
//         cnt = 0;

//         // copy strings from address argument pointers point to
//         // length of string is stored in len
//         result = copyinstr((const_userptr_t)&ptr, kargv[base + cnt * ALIGN], ARG_MAX, &len);
//         if (result) {
//             return result;
//         }

//         // add paddings if string is not aligned by 4
//         if (len % ALIGN != 0) {
//             for (int pos = len + 1; pos <= (len / ALIGN + 1) * ALIGN; pos++) {
//                 kargv[base + cnt * ALIGN + pos] = "\0";
//             }
//         }

//         // use cnt to record string position
//         cnt += (len / ALIGN);
//     }

//     // copied from runprogram()
//     result = vfs_open(program, O_RDONLY, 0, &v);
//     if (result) {
//         return result;
//     }

//     // create new address space
//     KASSERT(proc_getas() == NULL);
//     as = as_create();
//     if (as == NULL) {
//         vfs_close(v);
//         return ENOMEM;
//     }
//     proc_setas(as);
//     as_active();

//     // load executable
//     result = load_elf(v, &entrypoint);
//     if (result) {
//         vfs_close(v);
//         return result;
//     }

//     vfs_close(v);

//     // define new stack region
//     result = as_define_stack(as, &stackptr);
//     if (result) {
//         return result;
//     }

//     // copy arguments to new address space

//     // minus stack pointer with length of buffer
//     stackptr -= (argc + cnt) * ALIGN;
//     // update argument pointers to user stack
//     for (int i = 0; i < argc; i++) {
//         kargv[i * ALIGN] += stackptr;
//     }

//     // copy out arguments to user stack
//     for (int i = 0; i < (argc + cnt) * ALIGN; i++) {
//         result = copyoutsrt(kargv[i], (userptr_t)&stackptr, ARG_MAX, NULL);
//         if (result) { 
//             return result;
//         }
//     }

//     // clean up old address space
//     as_destroy();

//     // wrap to user mode
//     enter_new_process(argc, NULL, NULL, stackptr, entrypoint);

//     panic("enter_new_process returned\n");
//     return EINVAL;
// }