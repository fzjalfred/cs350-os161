#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <mips/trapframe.h>
#include <synch.h>
#include <vfs.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

extern struct array* top_pids;
/*
-1 for uinitialized exit code
*/
extern struct array* top_exit_info;


void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  lock_acquire(curproc->p_mutex);
  array_set(top_exit_info, curproc->p_pid, (void*)&exitcode);
  lock_release(curproc->p_mutex);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  *retval = curproc->p_pid;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  #if OPT_A2
  // check pid
  if (top_pids[pid]->p_parent_pid != curproc->pid) {
    return ECHILD;
  }
  if (pid >= PID_COUNTER||(top_pid[pid] == NULL && top_exit_info[pid] == -1)) {
    return ESRCH;
  }

  lock_acquire(p_mutex);
  while (top_pids[pid] != NULL) {
    cv_wait(p_cv, p_mutex);
  }
  exitstatus = _MKWAIT_EXIT(top_exit_info[pid]);
  lock_release(p_mutex);

  #else 
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  #endif /* OPT_A2 */

  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

// #if OPT_A2
int sys_fork(struct trapframe* tf, int32_t *err) {

  struct proc* child;

  //create a new process and attach PID
  child = proc_create_runprogram(curproc->p_name);
  if (child == NULL) {
    *err = ENOMEM;
    return -1;
  }
  if (child->p_pid < 0) {
    *err = ENPROC; // too many processes on system
    return -1;
  }

  //attach address space
  struct addrspace *pointer_p_addrspace = as_create();
  int copy_fail = as_copy(curproc->p_addrspace, &pointer_p_addrspace);
  if (copy_fail) {
    *err = copy_fail;
    return -1;
  }
  child->p_addrspace = pointer_p_addrspace;
  child->p_parent_pid = curproc->p_pid;

  // put trapframe
  struct trapframe *tf_heap = kmalloc(sizeof(struct trapframe));
  *tf_heap = *tf;

  // attach thread
  int thread_fail = thread_fork("start_thread", child, enter_forked_process, tf_heap, 0);
  if (thread_fail) {
    *err = thread_fail;
    return -1;
  }

  // // build parent-child relation
  // int add_fail1 = array_add(curproc->p_children, (void*)&child->p_pid, NULL);
  // if (add_fail1) {
  //   *err = add_fail1;
  //   return -1;
  // }
  return child->p_pid;
}

int sys_execv(uint32_t* a0, uint32_t* a1, int32_t *err) {

// name
  char* tmp;
  size_t pathlen;
  copyinstr((userptr_t)*a0, tmp, 128, &pathlen);
  //kprintf("Name: %s\n", tmp);
  char* progname = kmalloc(sizeof(char)*pathlen);
  for (unsigned i = 0; i<pathlen;i++) {
    progname[i] = tmp[i];
  }
  *a0 = (uint32_t)progname;

// argument
  uint32_t cur_arg;
  copyin((userptr_t)*a1, (void*)&cur_arg, 8);
  size_t argc = 0;
  char* args = kmalloc(128);

  uint32_t total_len = 0;
  while (!copyin((userptr_t)(*a1+sizeof(void*)*argc), (void*)&cur_arg, 4) && (void*)cur_arg != NULL) {
    argc++;
    int i = 0;
    char tmp_char;
    while (!copyin((userptr_t)(cur_arg+i), &tmp_char, 1)) {
      args[total_len+i] = tmp_char;
      i++;
      if (tmp_char == '\0') break;
    }
    total_len+=i;
  }
  
  *a1 = (uint32_t)args;
  // kprintf("---ALL---\n");
  // for (int i = 0; i<64; i++) {
  //    if (args[i] == '\0') {
  //     kprintf("\\0");
  //     } else 
  //     kprintf("%c", args[i]);
  // }
  //  kprintf("---ALL---\n");


  struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
  if (result) {
		*err = result;
    return -1;
	}


	/* Create a new address space. */
	as = as_create();
  if (as ==NULL) {
		vfs_close(v);
		*err = ENOMEM;
    return -1;
	}

	/* Switch to it and activate it. */
  // struct addrspace* old = 
  curproc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		*err = result;
    return -1;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
  result = as_build_stack(as, &stackptr, args, argc, a0, a1);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		*err = result;
    return -1;
	}
  
  kfree(progname);
  kfree(args);
  //as_destroy(old);
	/* Warp to user mode. */
	enter_new_process(argc /*argc*/, (userptr_t)stackptr-128/*userspace addr of argv*/,
			  ROUNDUP(stackptr-128, 8), entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	*err = EINVAL;
  return -1;
}
// #else 

// int sys_fork() {
//   return 0;
// }

// #endif /* OPT_A2 */
