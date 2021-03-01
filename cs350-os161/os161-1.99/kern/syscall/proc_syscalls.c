#include "opt-A2.h"
#include "opt-A3.h"
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

#if OPT_A2
#include "vfs.h"
#include "../include/kern/fcntl.h" // O_RDONLY
#include "../include/kern/limits.h" // PATH_MAX
#include "copyinout.h"
//#include 
#endif /* OPT_A2 */

// copied directly from sys__exit
void sys__kill (int exitcode) {
  //kprintf ("kill called!"); 
  struct addrspace *as;
  struct proc *p = curproc;
  lock_acquire(curproc->lk_process_info);

  curproc->exit_status = __WSIGNALED; // modification: changed exit_status
  curproc->exit_code = exitcode;

  int num = array_num(curproc->children);
  struct proc *curChild;
  for (int i = 0; i < num; ++i) {
	curChild = array_get(curproc->children, i); 
	lock_acquire(curChild->lk_process_info);
	curChild->parent = NULL;
	if(curChild->exit_status != -1) {
		lock_release(curChild->lk_process_info); 
		proc_destroy(curChild); 
	}
	else {
		lock_release(curChild->lk_process_info); 
	}
  }
	
  lock_release(curproc->lk_process_info); 
  cv_signal (curproc->cv_parent_waitpid, curproc->lk_process_info); // wake up parent
 KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  as = curproc_setas(NULL);
 
  as_destroy(as);
  
 proc_remthread(curthread);

  if (p->parent == NULL) {
	  proc_destroy(p);
  }
  thread_exit();
}




void sys__exit(int exitcode) {
  struct addrspace *as;
  struct proc *p = curproc;
  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);
#if OPT_A2
  lock_acquire(curproc->lk_process_info);

  // set exit status and code
  curproc->exit_status = __WEXITED;
  curproc->exit_code = exitcode;

  // delete dead children and notify living children of parent's death
  int num = array_num(curproc->children);
  struct proc *curChild;
  for (int i = 0; i < num; ++i) {
	curChild = array_get(curproc->children, i); 
	lock_acquire(curChild->lk_process_info);
	curChild->parent = NULL;
        // delete zombie processes	
	if(curChild->exit_status != -1) {
		lock_release(curChild->lk_process_info); 
		//kprintf ("deleting dead children with PID %d\n", curChild->PID); 
		proc_destroy(curChild); 
	}
	else {
		lock_release(curChild->lk_process_info); 
	}
  }
	
  lock_release(curproc->lk_process_info); 
  cv_signal (curproc->cv_parent_waitpid, curproc->lk_process_info); // wake up parent
#else
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;
#endif /* OPT_A2 */
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
  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  
 proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
#if OPT_A2 // do not destroy the process if it has a parent
  if (p->parent == NULL) {
	//kprintf ("destroying proc with no parent, PID %d\n", p->PID); 
	  proc_destroy(p);
  }
#else
  proc_destroy(p);
#endif 
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
#if OPT_A2
  *retval = curproc->PID; 
#else
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = 1;
#endif /* OPT_A2 */ 
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
	 if (options != 0) {
    		return(EINVAL);
   	 }
	 int exitstatus;
  	 int result;


#if OPT_A2
	lock_acquire(curproc->lk_process_info);
	// check if called process is child process
	int children_count = array_num(curproc->children);
	bool found = false;
        struct proc *curChild = NULL; 	
	for (int i = 0; i < children_count; ++i) {
		curChild = curproc->children->v[i];
		if(curChild->PID == pid) {
	 		found = true;
			break; 	
		}
	}
        if (!found) {
		lock_release(curproc->lk_process_info); 
		return ECHILD; 
	}
	lock_release(curproc->lk_process_info);
	lock_acquire(curChild->lk_process_info); 	
	while (curChild->exit_status == -1) { 
		cv_wait (curChild->cv_parent_waitpid, curChild->lk_process_info);  
	}
//	kprintf ("exit code: %d exit status: %d \n", curChild->exit_code, curChild->exit_status); 
	exitstatus = _MKWAIT_EXIT(curChild->exit_code);
  //     kprintf ("combined status %d\n", exitstatus); 	
	lock_release(curChild->lk_process_info);
   	
	// fully delete child here after waitpid has been called
	// not needed because parent will delete all its dead children when parent exits
	// proc_destroy(curChild); 	
#else
  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */


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

#if OPT_A2 
#include "mips/trapframe.h" // to add definition of trapframe in

// tf is the trapframe passed in from the system call dispatcher
int sys_fork (struct trapframe *tf, pid_t *retval){
	struct proc *child = proc_create_runprogram(curproc->p_name); 
	// proc_create_runprogram will also add PID, and initialize children
	if (child == NULL) {
		return ENOMEM; 
	}
	// add parent for child
	child->parent = curproc; 
	if(child->children==NULL) {
		lock_release(child->lk_process_info); 
	       return ENOMEM; // not enough memory to create dynamic array to store children 
	}
	int status; // stores all return values
	
	// add child to parent	
	lock_acquire(curproc->lk_process_info); 
	array_add(curproc->children, child, (unsigned *)&status);
	lock_release(curproc->lk_process_info); 	

	if(status < 0) {
		return ENOMEM; 
	}

	// copy address space
	struct addrspace *child_as; 
	status = as_copy(curproc->p_addrspace, &child_as); 
	if (status != 0) {
		proc_destroy(child); 
		as_destroy(child_as); 
		return ENOMEM; 
	}
	// safely attach to child->p_addrspace
	spinlock_acquire(&child->p_lock); 
	child->p_addrspace = child_as; // since child's addr was initialized to NULL
	spinlock_release(&child->p_lock);

	// place parent's stack frame in the heap
	struct trapframe *tf_backup = kmalloc(sizeof(struct trapframe)); 	
	if (tf_backup == NULL) {
		proc_destroy(child); 
		as_destroy(child_as); 
		return ENOMEM; 
	}	
	*tf_backup = *(struct trapframe *)tf; // copy values

	// create child's thread
	void (*f_ptr)(void * data1, unsigned long data2) = (void *)&enter_forked_process; 
	status = thread_fork (child->p_name, child, f_ptr, tf_backup, 0);
	if (status) { // if an error occurs -> status != 0
		proc_destroy(child);
		as_destroy(child_as); 
		kfree (tf_backup); 
		return status; // return the error returned by thread_fork
	}	
	*retval = child->PID;
	return 0;
}

const size_t MAX_ARGS_SIZE = 128; 

// may return an error message
static int load_stack(struct addrspace *as, char ** kargs, int args_count, vaddr_t stackptr, userptr_t *initial_ptr) {
	int total_len = 0; 
	for (int i = 0; i < args_count; ++i) {
		total_len += strlen(kargs[i]) + 1;
	} // determine length of all args

	total_len += (args_count+1) * (sizeof (char*)); // determine length of all args
	int padding = ROUNDUP (total_len, 8) - total_len; 
        userptr_t curStackptr = (userptr_t)stackptr - padding;
	// array to store the address of the args
	vaddr_t * args_loc = kmalloc ((args_count + 1) * sizeof(char *)); // add NULL at the end
        if (args_loc == NULL) {
		return ENOMEM; 
	}
        size_t copylen; 
	int arg_len, status; 
	// copy chars into the stack
	for (int i = 0; i < args_count; ++i) {
		arg_len = strlen(kargs[i]) + 1; 
	        
		curStackptr -= arg_len; // update stack pointer
		status = copyoutstr(kargs[i], curStackptr, arg_len, &copylen);	
		if (status) {
			kfree (args_loc); 
			return status; 
		}
		KASSERT((int)copylen == (int)arg_len); 	
		// save loc in args_loc
		args_loc[i] = (vaddr_t) curStackptr; 
	}
	args_loc[args_count] = (vaddr_t) NULL; 
	for (int i = args_count; i >= 0; i--) {
		curStackptr -= 4; 
		status = copyout((void *)&args_loc[i], curStackptr, 4);
		if (status) {
			kfree (args_loc); 
			return status; 
		}
	}
	*initial_ptr = curStackptr;
	//kfree (args_loc); // newly removed from A2b
	(void)as; 
	return 0; 
}

int
sys_execv(userptr_t in_prog_name, userptr_t in_args)
{
	char ** args = (char **) in_args; 
	char * prog_name = (char *) in_prog_name; 
	// copy program name
	char * kprog_name = kmalloc(__PATH_MAX * sizeof(char)); 
	if (kprog_name == NULL) {
		return ENOMEM; 
	}
	size_t prog_name_size; 
	int status = copyinstr((const_userptr_t)prog_name, kprog_name, __PATH_MAX, &prog_name_size); 
	if (status) {
		kfree (kprog_name); 
		return status; 
	}	
	// copy args
	int args_counter = 0;
	while (args[args_counter] != NULL) {
		++args_counter; 
	}
	
	
	char ** kargs = kmalloc ((args_counter+1) * sizeof(char*));
	if (kargs == NULL) {
		kfree (kprog_name); 
		return ENOMEM; 
	}
        for (int i = 0; i < args_counter; i++) {
	      	kargs[i] = kmalloc (MAX_ARGS_SIZE * sizeof (char)); // allocate memory for current argument
		status = copyinstr ((const_userptr_t) args[i], kargs[i], MAX_ARGS_SIZE, &prog_name_size);   
		if (status) {
			kfree (kprog_name); 
			kfree (kargs); 
			return status;
		}	
	}
        kargs[args_counter] = NULL; // manually set null terminator
	
	// checks if the args have been correctly copied 
	/*for (int i = 0; i <= args_counter; ++i) {
		kprintf ("%s and %s\n", args[i], kargs[i]); 
	}*/
	

	// copied directly from runprogram
        struct addrspace *as;
        struct vnode *v;
        vaddr_t entrypoint, stackptr;
        int result;

        /* Open the file. */
        result = vfs_open(kprog_name,O_RDONLY, 0, &v); // correct program name here
        if (result) {
		kfree (kprog_name); 
		kfree (kargs); 
                return result;
        }

        /* Create a new address space. */
        as = as_create();
        if (as ==NULL) {
                vfs_close(v);
                return ENOMEM;
	}
	/* Switch to it and activate it. */
	struct addrspace *prev_as = curproc_setas(as);
	as_activate();
	as_destroy(prev_as); 

        /* Load the executable. */
        result = load_elf(v, &entrypoint);
        if (result) {
                /* p_addrspace will go away when curproc is destroyed */
                kfree (kprog_name); 
		kfree(kargs); 
		vfs_close(v);
                return result;
        }

        /* Done with the file now. */
        vfs_close(v);

        /* Define the user stack in the address space */
        result = as_define_stack(as, &stackptr);
       if (result) {
                /* p_addrspace will go away when curproc is destroyed */
	        kfree(kprog_name); 
		kfree(kargs); 
                return result;
        }
        userptr_t start_loc; 
       	load_stack(as, kargs, args_counter, stackptr, &start_loc); 
		
	kfree (kprog_name);
	for (int i = 0; i < args_counter; ++i) {
		kfree (kargs[i]); 
	}
	kfree (kargs);
	
	/* Warp to user mode. */
        enter_new_process(args_counter, start_loc,
                          (vaddr_t) start_loc, entrypoint);

        /* enter_new_process does not return. */
        panic("enter_new_process returned\n");
        return EINVAL;
	// end of copy from runprogram
}


#endif /* OPT_A2 */

