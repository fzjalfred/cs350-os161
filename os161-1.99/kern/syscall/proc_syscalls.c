#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <mips/trapframe.h>
#include <synch.h>

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
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
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

// #else 

// int sys_fork() {
//   return 0;
// }

// #endif /* OPT_A2 */
