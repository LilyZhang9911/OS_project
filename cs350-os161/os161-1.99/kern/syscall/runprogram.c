/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include "opt-A2.h"

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
#if OPT_A2
#include "copyinout.h"
int runprogram (char *progname, unsigned long nargs, char **args)
#else
int
runprogram(char *progname)
#endif /* OPT_A2 */
{
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(curproc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	curproc_setas(as); // prev addrspace is NULL
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
#if OPT_A2
	int total_len = 0;
	int args_count = (int) nargs; 
        for (int i = 0; i < args_count; ++i) {
                total_len += strlen(args[i]) + 1;
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
                arg_len = strlen(args[i]) + 1;
                curStackptr -= arg_len; // update stack pointer
                status = copyoutstr(args[i], curStackptr, arg_len, &copylen);
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
	kfree (args_loc);	
	
	enter_new_process(args_count, curStackptr,
			  (vaddr_t)curStackptr, entrypoint);

#else
	/* Warp to user mode. */
	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  stackptr, entrypoint);
#endif /* OPT_A2 */	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}
