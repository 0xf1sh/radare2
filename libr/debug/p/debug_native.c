/* radare - LGPL - Copyright 2009-2014 - pancake */

#include <r_userconf.h>
#include <r_debug.h>
#include <r_asm.h>
#include <r_reg.h>
#include <r_lib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/param.h>
#include "native/drx.c" // x86 specific

#if DEBUGGER

#if __UNIX__
# include <errno.h>
# if !defined (__HAIKU__)
#  include <sys/ptrace.h>
# endif
# include <sys/wait.h>
# include <signal.h>
#endif

static int r_debug_native_continue(RDebug *dbg, int pid, int tid, int sig);
static int r_debug_native_reg_read(RDebug *dbg, int type, ut8 *buf, int size);
static int r_debug_native_reg_write(RDebug *dbg, int type, const ut8* buf, int size);

static int r_debug_handle_signals (RDebug *dbg) {
#if __linux__
	siginfo_t siginfo = {0};
	int ret = ptrace (PTRACE_GETSIGINFO, dbg->pid, 0, &siginfo);
	if (ret != -1 && siginfo.si_signo>0) {
		siginfo_t newsiginfo = {0};
		//ptrace (PTRACE_SETSIGINFO, dbg->pid, 0, &siginfo);
		dbg->reason = R_DBG_REASON_SIGNAL;
		dbg->signum = siginfo.si_signo;
		// siginfo.si_code -> USER, KERNEL or WHAT
#if 0
		eprintf ("[+] SIGNAL %d errno=%d code=%d ret=%d\n",
			siginfo.si_signo, siginfo.si_errno,
			siginfo.si_code, ret2);
#endif
		return R_TRUE;
	}
	return R_FALSE;
#else
	return -1;
#endif
}

#define MAXBT 128

#if __WINDOWS__
#include <windows.h>
#define R_DEBUG_REG_T CONTEXT
#include "native/w32.c"

#elif __BSD__
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#define R_DEBUG_REG_T struct reg
#if __KFBSD__
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

#elif __APPLE__

#define MACH_ERROR_STRING(ret) \
	(mach_error_string (ret) ? r_str_get (mach_error_string (ret)) : "(unknown)")

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <mach/exception_types.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/mach_interface.h>
#include <mach/mach_traps.h>
#include <mach/mach_types.h>
#include <mach/mach_vm.h>
#include <mach/mach_error.h>
#include <mach/task.h>
#include <mach/task_info.h>
#include <mach/thread_act.h>
#include <mach/thread_info.h>
#include <mach/vm_map.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <errno.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/fcntl.h>
#include <sys/proc.h>

#if __POWERPC__
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <mach/ppc/_types.h>
#include <mach/ppc/thread_status.h>
#define R_DEBUG_REG_T ppc_thread_state_t
#define R_DEBUG_STATE_T PPC_THREAD_STATE
#define R_DEBUG_STATE_SZ PPC_THREAD_STATE_COUNT
#elif __arm
#include <mach/arm/thread_status.h>
#ifndef ARM_THREAD_STATE
#define ARM_THREAD_STATE                1
#endif
#ifndef ARM_THREAD_STATE64
#define ARM_THREAD_STATE64              6
#endif
#define R_DEBUG_REG_T arm_thread_state_t
#define R_DEBUG_STATE_T ARM_THREAD_STATE
#define R_DEBUG_STATE_SZ ARM_THREAD_STATE_COUNT
#else

/* x86 32/64 */
#include <mach/i386/thread_status.h>
#include <sys/ucontext.h>
#include <mach/i386/_structs.h>

typedef union {
	ut64 x64[21];
	ut32 x32[16];
} R_DEBUG_REG_T;

// APPLE
#define R_DEBUG_STATE_T XXX


//(dbg->bits==64)?x86_THREAD_STATE:_STRUCT_X86_THREAD_STATE32
//#define R_DEBUG_REG_T _STRUCT_X86_THREAD_STATE64
#define R_DEBUG_STATE_SZ ((dbg->bits==R_SYS_BITS_64)?168:64)

#define REG_PC ((dbg->bits==R_SYS_BITS_64)?16:10)
#define REG_FL ((dbg->bits==R_SYS_BITS_64)?17:9)
#define REG_SP (7)
//(dbg->bits==64)?7:7

#if OLDIESHIT
#if __x86_64__
#define R_DEBUG_STATE_T x86_THREAD_STATE
#define R_DEBUG_REG_T _STRUCT_X86_THREAD_STATE64
#define R_DEBUG_STATE_SZ x86_THREAD_STATE_COUNT
#if 0
ut64[21]
        __ut64      rax;
        __ut64      rbx;
        __ut64      rcx;
        __ut64      rdx;
        __ut64      rdi;
        __ut64      rsi;
        __ut64      rbp;
        __ut64      rsp;
        __ut64      r8;
        __ut64      r9;
        __ut64      r10;
        __ut64      r11;
        __ut64      r12;
        __ut64      r13;
        __ut64      r14;
        __ut64      r15;
        __ut64      rip;
        __ut64      rflags;
        __ut64      cs;
        __ut64      fs;
        __ut64      gs;
21*8
#endif
#else
#define R_DEBUG_REG_T _STRUCT_X86_THREAD_STATE32
#define R_DEBUG_STATE_T i386_THREAD_STATE
#define R_DEBUG_STATE_SZ i386_THREAD_STATE_COUNT
#if 0
ut32[16]
16*4
    unsigned int        __eax;
    unsigned int        __ebx;
    unsigned int        __ecx;
    unsigned int        __edx;
    unsigned int        __edi;
    unsigned int        __esi;
    unsigned int        __ebp;
    unsigned int        __esp;
    unsigned int        __ss;
    unsigned int        __eflags;
    unsigned int        __eip;
    unsigned int        __cs;
    unsigned int        __ds;
    unsigned int        __es;
    unsigned int        __fs;
    unsigned int        __gs;
#endif
#endif
#endif
// oldie
#endif

#elif __sun
#define R_DEBUG_REG_T gregset_t
#undef DEBUGGER
#define DEBUGGER 0
#warning No debugger support for SunOS yet

#elif __linux__
#include <limits.h>

struct user_regs_struct_x86_64 {
  ut64 r15; ut64 r14; ut64 r13; ut64 r12; ut64 rbp; ut64 rbx; ut64 r11;
  ut64 r10; ut64 r9; ut64 r8; ut64 rax; ut64 rcx; ut64 rdx; ut64 rsi;
  ut64 rdi; ut64 orig_rax; ut64 rip; ut64 cs; ut64 eflags; ut64 rsp;
  ut64 ss; ut64 fs_base; ut64 gs_base; ut64 ds; ut64 es; ut64 fs; ut64 gs;
};

struct user_regs_struct_x86_32 {
  ut32 ebx; ut32 ecx; ut32 edx; ut32 esi; ut32 edi; ut32 ebp; ut32 eax;
  ut32 xds; ut32 xes; ut32 xfs; ut32 xgs; ut32 orig_eax; ut32 eip;
  ut32 xcs; ut32 eflags; ut32 esp; ut32 xss;
};

#ifdef __ANDROID__
// #if __arm__
# define R_DEBUG_REG_T struct pt_regs
#else
#include <sys/user.h>
# if __i386__ || __x86_64__
# define R_DEBUG_REG_T struct user_regs_struct
# elif __arm__
# define R_DEBUG_REG_T struct user_regs
# elif __mips__
#include <sys/ucontext.h>
typedef unsigned long mips64_regs_t [4096];
# define R_DEBUG_REG_T mips64_regs_t
#endif
# endif
#else // OS


#warning Unsupported debugging platform
#undef DEBUGGER
#define DEBUGGER 0
#endif // ARCH

#endif /* IF DEBUGGER */


/* begin of debugger code */
#if DEBUGGER

#if __APPLE__
// TODO: move into native/
task_t pid_to_task(int pid) {
	static task_t old_pid = -1;
	static task_t old_task = -1;
	task_t task = 0;
	int err;

	/* xlr8! */
	if (old_task!= -1) //old_pid != -1 && old_pid == pid)
		return old_task;

	err = task_for_pid (mach_task_self(), (pid_t)pid, &task);
	if ((err != KERN_SUCCESS) || !MACH_PORT_VALID (task)) {
		eprintf ("Failed to get task %d for pid %d.\n", (int)task, (int)pid);
		eprintf ("Reason: 0x%x: %s\n", err, (char *)MACH_ERROR_STRING (err));
		eprintf ("You probably need to add user to procmod group.\n"
			" Or chmod g+s radare && chown root:procmod radare\n");
		eprintf ("FMI: http://developer.apple.com/documentation/Darwin/Reference/ManPages/man8/taskgated.8.html\n");
		return -1;
	}
	old_pid = pid;
	old_task = task;
	return task;
}

// XXX intel specific -- generalize in r_reg..ease access
#define EFLAGS_TRAP_FLAG 0x100
static inline void debug_arch_x86_trap_set(RDebug *dbg, int foo) {
#if __i386__ || __x86_64__
        R_DEBUG_REG_T regs;
	r_debug_native_reg_read (dbg, R_REG_TYPE_GPR, (ut8*)&regs, sizeof (regs));
	if (dbg->bits == 64) {
		eprintf ("trap flag: %lld\n", (regs.x64[REG_PC]&0x100));
		if (foo) regs.x64[REG_FL] |= EFLAGS_TRAP_FLAG;
		else regs.x64[REG_FL] &= ~EFLAGS_TRAP_FLAG;
	} else {
		eprintf ("trap flag: %d\n", (regs.x32[REG_PC]&0x100));
		if (foo) regs.x32[REG_FL] |= EFLAGS_TRAP_FLAG;
		else regs.x32[REG_FL] &= ~EFLAGS_TRAP_FLAG;
	}
	r_debug_native_reg_write (dbg, R_REG_TYPE_GPR, (const ut8*)&regs, sizeof (regs));
#endif
}
#endif // __APPLE__

static int r_debug_native_step(RDebug *dbg) {
	int ret = R_FALSE;
	int pid = dbg->pid;
#if __WINDOWS__
	/* set TRAP flag */
/*
	CONTEXT regs __attribute__ ((aligned (16)));
	r_debug_native_reg_read (dbg, R_REG_TYPE_GPR, &regs, sizeof (regs));
	regs.EFlags |= 0x100;
	r_debug_native_reg_write (pid, dbg->tid, R_REG_TYPE_GPR, &regs, sizeof (regs));
*/
	r_debug_native_continue (dbg, pid, dbg->tid, dbg->signum);
#elif __APPLE__
	//debug_arch_x86_trap_set (dbg, 1);
	// TODO: not supported in all platforms. need dbg.swstep=
#if __arm__
	ret = ptrace (PT_STEP, pid, (caddr_t)1, 0); //SIGINT);
	if (ret != 0) {
		perror ("ptrace-step");
		eprintf ("mach-error: %d, %s\n", ret, MACH_ERROR_STRING (ret));
		ret = R_FALSE; /* do not wait for events */
	} else ret = R_TRUE;
#else
	#if 0 && __arm__
	if (!dbg->swstep)
		eprintf ("XXX hardware stepping is not supported in arm. set e dbg.swstep=true\n");
	else eprintf ("XXX: software step is not implemented??\n");
	return R_FALSE;
	#endif
	//eprintf ("stepping from pc = %08x\n", (ut32)get_offset("eip"));
	//ret = ptrace (PT_STEP, ps.tid, (caddr_t)get_offset("eip"), SIGSTOP);
	ret = ptrace (PT_STEP, pid, (caddr_t)1, 0); //SIGINT);
	if (ret != 0) {
		perror ("ptrace-step");
		eprintf ("mach-error: %d, %s\n", ret, MACH_ERROR_STRING (ret));
		ret = R_FALSE; /* do not wait for events */
	} else ret = R_TRUE;
#endif
#elif __BSD__
	ret = ptrace (PT_STEP, pid, (caddr_t)1, 0);
	if (ret != 0) {
		perror ("native-singlestep");
		ret = R_FALSE;
	} else ret = R_TRUE;
#else // linux
	ut64 addr = 0; /* should be eip */
	//ut32 data = 0;
	//printf("NATIVE STEP over PID=%d\n", pid);
	addr = r_debug_reg_get (dbg, "pc");
	ret = ptrace (PTRACE_SINGLESTEP, pid, (void*)(size_t)addr, 0); //addr, data);
	r_debug_handle_signals (dbg);
	if (ret == -1) {
		perror ("native-singlestep");
		ret = R_FALSE;
	} else ret = R_TRUE;
#endif
	return ret;
}

// return thread id
static int r_debug_native_attach(RDebug *dbg, int pid) {
	int ret = -1;
#if __WINDOWS__
	HANDLE hProcess = OpenProcess (PROCESS_ALL_ACCESS, FALSE, pid);
	if (hProcess != (HANDLE)NULL && DebugActiveProcess (pid))
		ret = w32_first_thread (pid);
	else ret = -1;
	ret = w32_first_thread (pid);
#elif __APPLE__ || __KFBSD__
	ret = ptrace (PT_ATTACH, pid, 0, 0);
	if (ret!=-1)
		perror ("ptrace (PT_ATTACH)");
	ret = pid;
#else
	ret = ptrace (PTRACE_ATTACH, pid, 0, 0);
	if (ret!=-1)
		ret = pid;
#endif
	return ret;
}

static int r_debug_native_detach(int pid) {
#if __WINDOWS__
	return w32_detach (pid)? 0 : -1;
#elif __APPLE__ || __BSD__
	return ptrace (PT_DETACH, pid, NULL, 0);
#else
	return ptrace (PTRACE_DETACH, pid, NULL, NULL);
#endif
}

static int r_debug_native_continue_syscall(RDebug *dbg, int pid, int num) {
#if __linux__
	return ptrace (PTRACE_SYSCALL, pid, 0, 0);
#elif __BSD__
	ut64 pc = r_debug_reg_get (dbg, "pc");
	return ptrace (PTRACE_SYSCALL, pid, (void*)(size_t)pc, 0);
#else
	eprintf ("TODO: continue syscall not implemented yet\n");
	return -1;
#endif
}

/* TODO: specify thread? */
/* TODO: must return true/false */
static int r_debug_native_continue(RDebug *dbg, int pid, int tid, int sig) {
	void *data = (void*)(size_t)((sig != -1)?sig: dbg->signum);
#if __WINDOWS__
	if (ContinueDebugEvent (pid, tid, DBG_CONTINUE) == 0) {
		print_lasterr ((char *)__FUNCTION__);
		eprintf ("debug_contp: error\n");
		return -1;
	}
	return 0;
#elif __APPLE__
#if __arm__
	int i, ret, status;
	thread_array_t inferior_threads = NULL;
	unsigned int inferior_thread_count = 0;

// XXX: detach is noncontrollable continue
        ptrace (PT_DETACH, pid, 0, 0);
#if 0
	ptrace (PT_THUPDATE, pid, (void*)(size_t)1, 0); // 0 = send no signal TODO !! implement somewhere else
	ptrace (PT_CONTINUE, pid, (void*)(size_t)1, 0); // 0 = send no signal TODO !! implement somewhere else
	task_resume (pid_to_task (pid));
	ret = waitpid (pid, &status, 0);
#endif
/*
        ptrace (PT_ATTACHEXC, pid, 0, 0);

        if (task_threads (pid_to_task (pid), &inferior_threads,
			&inferior_thread_count) != KERN_SUCCESS) {
                eprintf ("Failed to get list of task's threads.\n");
		return 0;
        }
        for (i = 0; i < inferior_thread_count; i++)
		thread_resume (inferior_threads[i]);
*/
	return 1;
#else
	//ut64 rip = r_debug_reg_get (dbg, "pc");
	ptrace (PT_CONTINUE, pid, (void*)(size_t)1, (int)(size_t)data);
        return 0;
#endif
#elif __BSD__
	ut64 pc = r_debug_reg_get (dbg, "pc");
	return ptrace (PTRACE_CONT, pid, (void*)(size_t)pc, (int)data);
#else
//eprintf ("SIG %d\n", dbg->signum);
	return ptrace (PTRACE_CONT, pid, NULL, data);
#endif
}

static int r_debug_native_wait(RDebug *dbg, int pid) {
#if __WINDOWS__
	return w32_dbg_wait (dbg, pid);
#else
	int stopsig, ret, status = -1;
	//printf ("prewait\n");
	if (pid==-1)
		return R_DBG_REASON_UNKNOWN;
	ret = waitpid (pid, &status, 0);
	//printf ("status=%d (return=%d)\n", status, ret);
	// TODO: switch status and handle reasons here
	r_debug_handle_signals (dbg);

	if (WIFSTOPPED (status)) {
		stopsig = WSTOPSIG (status);
		dbg->signum = WSTOPSIG (status);
		status = R_DBG_REASON_SIGNAL;
	} else
#if 0
	if (WIFEXITED (status)) {
		status = R_DBG_REASON_DEAD;
	} else
#endif
	if (status == 0 || ret == -1) {
		status = R_DBG_REASON_DEAD;
	} else {
		if (ret != pid)
			status = R_DBG_REASON_NEW_PID;
		else status = dbg->reason;
	}
	return status;
#endif
}

// TODO: why strdup here?
static const char *r_debug_native_reg_profile(RDebug *dbg) {
#if __WINDOWS__
if (dbg->bits & R_SYS_BITS_32) {
	return strdup (
	"=pc	eip\n"
	"=sp	esp\n"
	"=bp	ebp\n"
	"=a0	eax\n"
	"=a1	ebx\n"
	"=a2	ecx\n"
	"=a3	edi\n"
	"drx	dr0	.32	4	0\n"
	"drx	dr1	.32	8	0\n"
	"drx	dr2	.32	12	0\n"
	"drx	dr3	.32	16	0\n"
	"drx	dr6	.32	20	0\n"
	"drx	dr7	.32	24	0\n"
	/* floating save area 4+4+4+4+4+4+4+80+4 = 112 */
	"seg	gs	.32	132	0\n"
	"seg	fs	.32	136	0\n"
	"seg	es	.32	140	0\n"
	"seg	ds	.32	144	0\n"
	"gpr	edi	.32	156	0\n"
	"gpr	esi	.32	160	0\n"
	"gpr	ebx	.32	164	0\n"
	"gpr	edx	.32	168	0\n"
	"gpr	ecx	.32	172	0\n"
	"gpr	eax	.32	176	0\n"
	"gpr	ebp	.32	180	0\n"
	"gpr	esp	.32	196	0\n"
	"gpr	eip	.32	184	0\n"
	"seg	cs	.32	184	0\n"
	"seg	ds	.32	152	0\n"
	"seg	gs	.32	140	0\n"
	"seg	fs	.32	144	0\n"
	"gpr	eflags	.32	192	0	c1p.a.zstido.n.rv\n" // XXX must be flg
	"gpr	cf	.1	.1536	0	carry\n"
	"gpr	pf	.1	.1538	0	parity\n"
	"gpr	af	.1	.1540	0	adjust\n"
	"gpr	zf	.1	.1542	0	zero\n"
	"gpr	sf	.1	.1543	0	sign\n"
	"gpr	tf	.1	.1544	0	trap\n"
	"gpr	if	.1	.1545	0	interrupt\n"
	"gpr	df	.1	.1546	0	direction\n"
	"gpr	of	.1	.1547	0	overflow\n"
	"seg	ss	.32	200	0\n"
	/* +512 bytes for maximum supoprted extension extended registers */
	);
} else {
	// XXX. this is wrong
	return strdup (
	"=pc	rip\n"
	"=sp	rsp\n"
	"=bp	rbp\n"
	"=a0	rax\n"
	"=a1	rbx\n"
	"=a2	rcx\n"
	"=a3	rdi\n"
	"drx	dr0	.32	4	0\n"
	"drx	dr1	.32	8	0\n"
	"drx	dr2	.32	12	0\n"
	"drx	dr3	.32	16	0\n"
	"drx	dr6	.32	20	0\n"
	"drx	dr7	.32	24	0\n"
	/* floating save area 4+4+4+4+4+4+4+80+4 = 112 */
	"seg	gs	.32	132	0\n"
	"seg	fs	.32	136	0\n"
	"seg	es	.32	140	0\n"
	"seg	ds	.32	144	0\n"
	"gpr	rdi	.32	156	0\n"
	"gpr	rsi	.32	160	0\n"
	"gpr	rbx	.32	164	0\n"
	"gpr	rdx	.32	168	0\n"
	"gpr	rcx	.32	172	0\n"
	"gpr	rax	.32	176	0\n"
	"gpr	rbp	.32	180	0\n"
	"gpr	rsp	.32	196	0\n"
	"gpr	rip	.32	184	0\n"
	"seg	cs	.32	184	0\n"
	"seg	ds	.32	152	0\n"
	"seg	gs	.32	140	0\n"
	"seg	fs	.32	144	0\n"
	"gpr	flags	.16	192	0	c1p.a.zstido.n.rv\n" // XXX must be flg
	"gpr	eflags	.32	192	0	c1p.a.zstido.n.rv\n" // XXX must be flg
	"gpr	rflags	.64	192	0	c1p.a.zstido.n.rv\n" // XXX must be flg
	"gpr	cf	.1	.1536	0	carry\n"
	"gpr	pf	.1	.1538	0	parity\n"
	"gpr	af	.1	.1540	0	adjust\n"
	"gpr	zf	.1	.1542	0	zero\n"
	"gpr	sf	.1	.1543	0	sign\n"
	"gpr	tf	.1	.1544	0	trap\n"
	"gpr	if	.1	.1545	0	interrupt\n"
	"gpr	df	.1	.1546	0	direction\n"
	"gpr	of	.1	.1547	0	overflow\n"
	"seg	ss	.32	200	0\n"
	/* +512 bytes for maximum supoprted extension extended registers */
	);
}
#elif __linux__ && __MIPS__
	return strdup (
	"=pc	r0\n"
	"=sp	29\n" // status register
	"=sr	v0\n" // status register
	"=a0	r4\n"
	"=a1	r5\n"
	"=a2	r6\n"
	"=a3	r7\n"
	"gpr	r0	.32	0	0\n"
	"gpr	r1	.32	4	0\n"
	"gpr	r2	.32	8	0\n"
	"gpr	r3	.32	16	0\n"
	"gpr	r4	.32	24	0\n"
	"gpr	r5	.32	32	0\n"
	"gpr	r6	.32	48	0\n"
	"gpr	r7	.32	56	0\n"
	"gpr	r8	.32	64	0\n"
	"gpr	r9	.32	72	0\n"
	"gpr	r10	.32	80	0\n"
	"gpr	r11	.32	88	0\n"
	"gpr	r12	.32	96	0\n"
	"gpr	r13	.32	104	0\n"
	"gpr	r14	.32	112	0\n"
	"gpr	r15	.32	120	0\n"
	"gpr	r16	.32	128	0\n"
	);
#elif __POWERPC__ && __APPLE__
	return strdup (
	"=pc	srr0\n"
	"=sp	srr1\n"
	"=sr	srr1\n" // status register ??
	"=a0	r0\n"
	"=a1	r1\n"
	"=a2	r2\n"
	"=a3	r3\n"
#if 0
	"=a4	r4\n"
	"=a5	r5\n"
	"=a6	r6\n"
	"=a7	r7\n"
#endif
	"gpr	srr0	.32	0	0\n"
	"gpr	srr1	.32	4	0\n"
	"gpr	r0	.32	8	0\n"
	"gpr	r1	.32	12	0\n"
	"gpr	r2	.32	16	0\n"
	"gpr	r3	.32	20	0\n"
	"gpr	r4	.32	24	0\n"
	"gpr	r5	.32	28	0\n"
	"gpr	r6	.32	32	0\n"
	"gpr	r7	.32	36	0\n"
	"gpr	r8	.32	40	0\n"
	"gpr	r9	.32	44	0\n"
	"gpr	r10	.32	48	0\n"
	"gpr	r11	.32	52	0\n"
	"gpr	r12	.32	56	0\n"
	"gpr	r13	.32	60	0\n"
	"gpr	r14	.32	64	0\n"
	"gpr	r15	.32	68	0\n"
	"gpr	r16	.32	72	0\n"
	"gpr	r17	.32	76	0\n"
	"gpr	r18	.32	80	0\n"
	"gpr	r19	.32	84	0\n"
	"gpr	r20	.32	88	0\n"
	"gpr	r21	.32	92	0\n"
	"gpr	r22	.32	96	0\n"

	"gpr	r23	.32	100	0\n"
	"gpr	r24	.32	104	0\n"
	"gpr	r25	.32	108	0\n"
	"gpr	r26	.32	112	0\n"
	"gpr	r27	.32	116	0\n"
	"gpr	r28	.32	120	0\n"
	"gpr	r29	.32	124	0\n"
	"gpr	r30	.32	128	0\n"
	"gpr	r31	.32	132	0\n"
	"gpr	cr	.32	136	0\n"
	"gpr	xer	.32	140	0\n"
	"gpr	lr	.32	144	0\n"
	"gpr	ctr	.32	148	0\n"
	"gpr	mq	.32	152	0\n"
	"gpr	vrsave	.32	156	0\n"
	);
#elif __i386__ && (__OpenBSD__ || __NetBSD__)
	return strdup (
	"=pc	eip\n"
	"=sp	esp\n"
	"=bp	ebp\n"
	"=a0	eax\n"
	"=a1	ebx\n"
	"=a2	ecx\n"
	"=a3	edi\n"
	"gpr	eax	.32	0	0\n"
	"gpr	ax	.16	0	0\n"
	"gpr	ah	.8	0	0\n"
	"gpr	al	.8	1	0\n"
	"gpr	ecx	.32	4	0\n"
	"gpr	cx	.16	4	0\n"
	"gpr	ch	.8	4	0\n"
	"gpr	cl	.8	5	0\n"
	"gpr	edx	.32	8	0\n"
	"gpr	dx	.16	8	0\n"
	"gpr	dh	.8	8	0\n"
	"gpr	dl	.8	9	0\n"
	"gpr	ebx	.32	12	0\n"
	"gpr	bx	.16	12	0\n"
	"gpr	bh	.8	12	0\n"
	"gpr	bl	.8	13	0\n"
	"gpr	esp	.32	16	0\n"
	"gpr	sp	.16	16	0\n"
	"gpr	ebp	.32	20	0\n"
	"gpr	bp	.16	20	0\n"
	"gpr	esi	.32	24	0\n"
	"gpr	si	.16	24	0\n"
	"gpr	edi	.32	28	0\n"
	"gpr	di	.16	28	0\n"
	"gpr	eip	.32	32	0\n"
	"gpr	ip	.16	32	0\n"
	"gpr	eflags	.32	36	0	c1p.a.zstido.n.rv\n"
	"gpr	cf	.1	.288	0	carry\n"
	"gpr	pf	.1	.290	0	parity\n"
	"gpr	af	.1	.292	0	adjust\n"
	"gpr	zf	.1	.294	0	zero\n"
	"gpr	sf	.1	.295	0	sign\n"
	"gpr	tf	.1	.296	0	trap\n"
	"gpr	if	.1	.297	0	interrupt\n"
	"gpr	df	.1	.298	0	direction\n"
	"gpr	of	.1	.299	0	overflow\n"
	"seg	cs	.32	40	0\n"
	"seg	ss	.32	44	0\n"
	"seg	ds	.32	48	0\n"
	"seg	es	.32	52	0\n"
	"seg	fs	.32	56	0\n"
	"seg	gs	.32	60	0\n"
// TODO: implement flags like in linux --those flags are wrong
	);
#elif __i386__ && __KFBSD__
	return strdup (
	"=pc	eip\n"
	"=sp	esp\n"
	"=bp	ebp\n"
	"=a0	eax\n"
	"=a1	ebx\n"
	"=a2	ecx\n"
	"=a3	edi\n"
	"seg	fs	.32	0	0\n"
	"seg	es	.32	4	0\n"
	"seg	ds	.32	8	0\n"
	"gpr	edi	.32	12	0\n"
	"gpr	di	.16	12	0\n"
	"gpr	esi	.32	16	0\n"
	"gpr	si	.16	16	0\n"
	"gpr	ebp	.32	20	0\n"
	"gpr	bp	.16	20	0\n"
	"gpr	isp	.32	24	0\n"
	"gpr	ebx	.32	28	0\n"
	"gpr	bx	.16	28	0\n"
	"gpr	bh	.8	28	0\n"
	"gpr	bl	.8	29	0\n"
	"gpr	edx	.32	32	0\n"
	"gpr	dx	.16	32	0\n"
	"gpr	dh	.8	32	0\n"
	"gpr	dl	.8	33	0\n"
	"gpr	ecx	.32	36	0\n"
	"gpr	cx	.16	36	0\n"
	"gpr	ch	.8	36	0\n"
	"gpr	cl	.8	37	0\n"
	"gpr	eax	.32	40	0\n"
	"gpr	ax	.16	40	0\n"
	"gpr	ah	.8	40	0\n"
	"gpr	al	.8	41	0\n"
	"gpr	trapno	.32	44	0\n"
	"gpr	err	.32	48	0\n"
	"gpr	eip	.32	52	0\n"
	"gpr	ip	.16	52	0\n"
	"seg	cs	.32	56	0\n"
	"gpr	eflags	.32	60	0	c1p.a.zstido.n.rv\n"

	"gpr	cf	.1	.480	0	carry\n"
	"gpr	pf	.1	.482	0	parity\n"
	"gpr	af	.1	.484	0	adjust\n"
	"gpr	zf	.1	.486	0	zero\n"
	"gpr	sf	.1	.487	0	sign\n"
	"gpr	tf	.1	.488	0	trap\n"
	"gpr	if	.1	.489	0	interrupt\n"
	"gpr	df	.1	.490	0	direction\n"
	"gpr	of	.1	.491	0	overflow\n"

	"gpr	esp	.32	64	0\n"
	"gpr	sp	.16	64	0\n"
	"seg	ss	.32	68	0\n"
	"seg	gs	.32	72	0\n"
// TODO: implement flags like in linux --those flags are wrong
	);
#elif (__mips__ && __linux__)
#if 0
	reg      name    usage
	---+-----------+-------------
	0        zero   always zero
	1         at    reserved for assembler
	2-3     v0-v1   expression evaluation, result of function
	4-7     a0-a3   arguments for functions
	8-15    t0-t7   temporary (not preserved across calls)
	16-23   s0-s7   saved temporary (preserved across calls)
	24-25   t8-t9   temporary (not preserved across calls)
	26-27   k0-k1   reserved for OS kernel
	28      gp      points to global area
	29      sp      stack pointer
	30      fp      frame pointer
	31      ra      return address
#if 0
16 /* 0 - 31 are integer registers, 32 - 63 are fp registers.  */
PC = 272
17 #define FPR_BASE        32
18 #define PC              64
19 #define CAUSE           65
20 #define BADVADDR        66
21 #define MMHI            67
22 #define MMLO            68
23 #define FPC_CSR         69
24 #define FPC_EIR         70
#endif

#endif
	return strdup (
	"=pc	pc\n"
	"=sp	sp\n"
	"=bp	fp\n"
	"=a0	a0\n"
	"=a1	a1\n"
	"=a2	a2\n"
	"=a3	a3\n"
	"gpr	zero	.64	0	0\n"
	"gpr	at	.32	8	0\n"
	"gpr	at	.64	8	0\n"
	"gpr	v0	.64	16	0\n"
	"gpr	v1	.64	24	0\n"
	/* args */
	"gpr	a0	.64	32	0\n"
	"gpr	a1	.64	40	0\n"
	"gpr	a2	.64	48	0\n"
	"gpr	a3	.64	56	0\n"
	/* tmp */
	"gpr	t0	.64	64	0\n"
	"gpr	t1	.64	72	0\n"
	"gpr	t2	.64	80	0\n"
	"gpr	t3	.64	88	0\n"
	"gpr	t4	.64	96	0\n"
	"gpr	t5	.64	104	0\n"
	"gpr	t6	.64	112	0\n"
	"gpr	t7	.64	120	0\n"
	/* saved */
	"gpr	s0	.64	128	0\n"
	"gpr	s1	.64	136	0\n"
	"gpr	s2	.64	144	0\n"
	"gpr	s3	.64	152	0\n"
	"gpr	s4	.64	160	0\n"
	"gpr	s5	.64	168	0\n"
	"gpr	s6	.64	176	0\n"
	"gpr	s7	.64	184	0\n"
	"gpr	s8	.64	192	0\n"
	"gpr	s9	.64	200	0\n"
	/* special */
	"gpr	k0	.64	208	0\n"
	"gpr	k1	.64	216	0\n"
	"gpr	gp	.64	224	0\n"
	"gpr	sp	.64	232	0\n"
	"gpr	fp	.64	240	0\n"
	"gpr	ra	.64	248	0\n"
	/* extra */
	"gpr	pc	.64	272	0\n"
	);
#elif (__i386__ || __x86_64__) && __linux__
if (dbg->bits & R_SYS_BITS_32) {
#if __x86_64__
	// 64bit host debugging 32bit binary
	return strdup (
	"=pc	eip\n"
	"=sp	esp\n"
	"=bp	ebp\n"
	"=a0	eax\n"
 	"=a1	ebx\n"
 	"=a2	ecx\n"
 	"=a3	edi\n"
 	"=zf	zf\n"
 	"=sf	sf\n"
 	"=of	of\n"
 	"=cf	cf\n"
	"gpr	eip	.32	128	0\n"
	"gpr	ip	.16	128	0\n"
	"gpr	oeax	.32	120	0\n"
	"gpr	eax	.32	80	0\n"
	"gpr	ax	.16	80	0\n"
	"gpr	ah	.8	80	0\n"
	"gpr	al	.8	81	0\n"
	"gpr	ebx	.32	40	0\n"
	"gpr	bx	.16	40	0\n"
	"gpr	bh	.8	40	0\n"
	"gpr	bl	.8	41	0\n"
	"gpr	ecx	.32	88	0\n"
	"gpr	cx	.16	88	0\n"
	"gpr	ch	.8	88	0\n"
	"gpr	cl	.8	89	0\n"
	"gpr	edx	.32	96	0\n"
	"gpr	dx	.16	96	0\n"
	"gpr	dh	.8	96	0\n"
	"gpr	dl	.8	97	0\n"
	"gpr	esp	.32	152	0\n"
	"gpr	sp	.16	152	0\n"
	"gpr	ebp	.32	32	0\n"
	"gpr	bp	.16	32	0\n"
	"gpr	esi	.32	104	0\n"
	"gpr	si	.16	104	0\n"
	"gpr	edi	.32	112	0\n"
	"gpr	di	.16	112	0\n"
	"seg	xfs	.32	200	0\n"
	"seg	xgs	.32	208	0\n"
	"seg	xcs	.32	136	0\n"
	"seg	cs	.16	136	0\n"
	"seg	xss	.32	160	0\n"
	"gpr	flags	.16	144	0\n"
	"gpr	eflags	.32	144	0	c1p.a.zstido.n.rv\n"
	"gpr	rflags	.64	144	0	c1p.a.zstido.n.rv\n"
	"gpr	cf	.1	.1152	0	carry\n"
	"gpr	pf	.1	.1154	0	parity\n"
	"gpr	af	.1	.1156	0	adjust\n"
	"gpr	zf	.1	.1158	0	zero\n"
	"gpr	sf	.1	.1159	0	sign\n"
	"gpr	tf	.1	.1160	0	trap\n"
	"gpr	if	.1	.1161	0	interrupt\n"
	"gpr	df	.1	.1162	0	direction\n"
	"gpr	of	.1	.1163	0	overflow\n"
#if 0
 	"drx	dr0	.64	0	0\n"
 	"drx	dr1	.64	8	0\n"
 	"drx	dr2	.64	16	0\n"
 	"drx	dr3	.64	24	0\n"
	// dr4 32
	// dr5 40
 	"drx	dr6	.64	48	0\n"
 	"drx	dr7	.64	56	0\n"
#endif
	"drx	dr0	.32	0	0\n"
	"drx	dr1	.32	4	0\n"
	"drx	dr2	.32	8	0\n"
	"drx	dr3	.32	12	0\n"
	//"drx	dr4	.32	16	0\n"
	//"drx	dr5	.32	20	0\n"
	"drx	dr6	.32	24	0\n"
	"drx	dr7	.32	28	0\n"
	);
#else
	// 32bit host debugging 32bit target
	return strdup (
	"=pc	eip\n"
	"=sp	esp\n"
	"=bp	ebp\n"
	"=a0	eax\n"
	"=a1	ebx\n"
	"=a2	ecx\n"
	"=a3	edi\n"
	"gpr	eip	.32	48	0\n"
	"gpr	ip	.16	48	0\n"
	"gpr	oeax	.32	44	0\n"
	"gpr	eax	.32	24	0\n"
	"gpr	ax	.16	24	0\n"
	"gpr	ah	.8	24	0\n"
	"gpr	al	.8	25	0\n"
	"gpr	ebx	.32	0	0\n"
	"gpr	bx	.16	0	0\n"
	"gpr	bh	.8	0	0\n"
	"gpr	bl	.8	1	0\n"
	"gpr	ecx	.32	4	0\n"
	"gpr	cx	.16	4	0\n"
	"gpr	ch	.8	4	0\n"
	"gpr	cl	.8	5	0\n"
	"gpr	edx	.32	8	0\n"
	"gpr	dx	.16	8	0\n"
	"gpr	dh	.8	8	0\n"
	"gpr	dl	.8	9	0\n"
	"gpr	esp	.32	60	0\n"
	"gpr	sp	.16	60	0\n"
	"gpr	ebp	.32	20	0\n"
	"gpr	bp	.16	20	0\n"
	"gpr	esi	.32	12	0\n"
	"gpr	si	.16	12	0\n"
	"gpr	edi	.32	16	0\n"
	"gpr	di	.16	16	0\n"
	"seg	xfs	.32	36	0\n"
	"seg	xgs	.32	40	0\n"
	"seg	xcs	.32	52	0\n"
	"seg	cs	.16	52	0\n"
	"seg	xss	.32	52	0\n"
	"gpr	eflags	.32	56	0	c1p.a.zstido.n.rv\n"
	"gpr	flags	.16	56	0\n"
	"gpr	cf	.1	.448	0	carry\n"
	"gpr	pf	.1	.450	0	parity\n"
	"gpr	af	.1	.452	0	adjust\n"
	"gpr	zf	.1	.454	0	zero\n"
	"gpr	sf	.1	.455	0	sign\n"
	"gpr	tf	.1	.456	0	trap\n"
	"gpr	if	.1	.457	0	interrupt\n"
	"gpr	df	.1	.458	0	direction\n"
	"gpr	of	.1	.459	0	overflow\n"
	"drx	dr0	.32	0	0\n"
	"drx	dr1	.32	4	0\n"
	"drx	dr2	.32	8	0\n"
	"drx	dr3	.32	12	0\n"
	//"drx	dr4	.32	16	0\n"
	//"drx	dr5	.32	20	0\n"
	"drx	dr6	.32	24	0\n"
	"drx	dr7	.32	28	0\n"
	);
#endif
} else {
	// 64bit host debugging 64bit target
	return strdup (
	"=pc	rip\n"
	"=sp	rsp\n"
	"=bp	rbp\n"
	"=a0	rax\n"
	"=a1	rbx\n"
	"=a2	rcx\n"
	"=a3	rdx\n"
	"# no profile defined for x86-64\n"
	"gpr	r15	.64	0	0\n"
	"gpr	r14	.64	8	0\n"
	"gpr	r13	.64	16	0\n"
	"gpr	r12	.64	24	0\n"
	"gpr	rbp	.64	32	0\n"
	"gpr	rbx	.64	40	0\n"
	"gpr	ebx	.32	40	0\n"
	"gpr	bx	.16	40	0\n"
	"gpr	bh	.8	40	0\n"
	"gpr	bl	.8	41	0\n"
	"gpr	r11	.64	48	0\n"
	"gpr	r10	.64	56	0\n"
	"gpr	r9	.64	64	0\n"
	"gpr	r8	.64	72	0\n"
	"gpr	rax	.64	80	0\n"
	"gpr	eax	.32	80	0\n"
	"gpr	ax	.16	80	0\n"
	"gpr	ah	.8	80	0\n"
	"gpr	al	.8	81	0\n"
	"gpr	rcx	.64	88	0\n"
	"gpr	ecx	.32	88	0\n"
	"gpr	cx	.16	88	0\n"
	"gpr	ch	.8	88	0\n"
	"gpr	cl	.8	89	0\n"
	"gpr	rdx	.64	96	0\n"
	"gpr	edx	.32	96	0\n"
	"gpr	dx	.16	96	0\n"
	"gpr	dh	.8	96	0\n"
	"gpr	dl	.8	97	0\n"
	"gpr	rsi	.64	104	0\n"
	"gpr	rdi	.64	112	0\n"
	"gpr	oeax	.64	120	0\n"
	"gpr	rip	.64	128	0\n"
	"seg	cs	.64	136	0\n"
	"gpr	rflags	.64	144	0	c1p.a.zstido.n.rv\n"
	"gpr	eflags	.32	144	0	c1p.a.zstido.n.rv\n"
	"gpr	cf	.1	.1152	0	carry\n"
	"gpr	pf	.1	.1154	0	parity\n"
	"gpr	af	.1	.1156	0	adjust\n"
	"gpr	zf	.1	.1158	0	zero\n"
	"gpr	sf	.1	.1159	0	sign\n"
	"gpr	tf	.1	.1160	0	trap\n"
	"gpr	if	.1	.1161	0	interrupt\n"
	"gpr	df	.1	.1162	0	direction\n"
	"gpr	of	.1	.1163	0	overflow\n"

	"gpr	rsp	.64	152	0\n"
	"seg	ss	.64	160	0\n"
	"seg	fs_base	.64	168	0\n"
	"seg	gs_base	.64	176	0\n"
	"seg	ds	.64	184	0\n"
	"seg	es	.64	192	0\n"
	"seg	fs	.64	200	0\n"
	"seg	gs	.64	208	0\n"
 	"drx	dr0	.64	0	0\n"
 	"drx	dr1	.64	8	0\n"
 	"drx	dr2	.64	16	0\n"
 	"drx	dr3	.64	24	0\n"
	// dr4 32
	// dr5 40
 	"drx	dr6	.64	48	0\n"
 	"drx	dr7	.64	56	0\n"
	);
}
#elif (defined(__arm64__) || __arm__) && __APPLE__
// arm64 aarch64
if (dbg->bits & R_SYS_BITS_64) {
#if 0
        __ut64    __x[29];  /* General purpose registers x0-x28 */
        __ut64    __fp;             /* Frame pointer x29 */
        __ut64    __lr;             /* Link register x30 */
        __ut64    __sp;             /* Stack pointer x31 */
        __ut64    __pc;             /* Program counter */
        __uint32_t    __cpsr;   /* Current program status register */
#endif
	return strdup (
	"=pc	pc\n"
	"=sp	sp\n" // XXX
	"=bp	x30\n" // XXX
	"=a0	x0\n"
	"=a1	x1\n"
	"=a2	x2\n"
	"=a3	x3\n"
 	"=zf	zf\n"
 	"=sf	nf\n"
 	"=of	vf\n"
 	"=cf	cf\n"
	"gpr	x0	.64	0	0\n" // r14
	"gpr	x1	.64	8	0\n" // r14
	"gpr	x2	.64	16	0\n" // r14
	"gpr	x3	.64	24	0\n" // r14
	"gpr	x4	.64	32	0\n" // r14
	"gpr	x5	.64	40	0\n" // r14
	"gpr	x6	.64	48	0\n" // r14
	"gpr	x7	.64	56	0\n" // r14
	"gpr	x8	.64	64	0\n" // r14
	"gpr	x9	.64	72	0\n" // r14
	"gpr	x10	.64	80	0\n" // r14
	"gpr	x11	.64	88	0\n" // r14
	"gpr	x12	.64	96	0\n" // r14
	"gpr	x13	.64	104	0\n" // r14
	"gpr	x14	.64	112	0\n" // r14
	"gpr	x15	.64	120	0\n" // r14
	"gpr	x16	.64	128	0\n" // r14
	"gpr	x17	.64	136	0\n" // r14
	"gpr	x18	.64	144	0\n" // r14
	"gpr	x19	.64	152	0\n" // r14
	"gpr	x20	.64	160	0\n" // r14
	"gpr	x21	.64	168	0\n" // r14
	"gpr	x22	.64	176	0\n" // r14
	"gpr	x23	.64	184	0\n" // r14
	"gpr	x24	.64	192	0\n" // r14
	"gpr	x25	.64	200	0\n" // r14
	"gpr	x26	.64	208	0\n" // r14
	"gpr	x27	.64	216	0\n" // r14
	"gpr	x28	.64	224	0\n" // r14
	"gpr	x29	.64	232	0\n" // r14
	// words (32bit lower part of x
	"gpr	w0	.32	0	0\n" // w0
	"gpr	w1	.32	8	0\n" // w0
	"gpr	w2	.32	16	0\n" // w0
	"gpr	w3	.32	24	0\n" // w0
	"gpr	w4	.32	32	0\n" // w0
	"gpr	w5	.32	40	0\n" // w0
	"gpr	w6	.32	48	0\n" // w0
	"gpr	w7	.32	56	0\n" // w0
	"gpr	w8	.32	64	0\n" // w0
	"gpr	w9	.32	72	0\n" // w0
	"gpr	w10	.32	80	0\n" // w0
	"gpr	w11	.32	88	0\n" // w0
	"gpr	w12	.32	96	0\n" // w0
	"gpr	w13	.32	104	0\n" // w0
	"gpr	w14	.32	112	0\n" // w0
	"gpr	w15	.32	120	0\n" // w0
	"gpr	w16	.32	128	0\n" // w0
	"gpr	w17	.32	136	0\n" // w0
	"gpr	w18	.32	144	0\n" // w0
	"gpr	w19	.32	152	0\n" // w0
	"gpr	w20	.32	160	0\n" // w0
	"gpr	w21	.32	168	0\n" // w0
	"gpr	w22	.32	176	0\n" // w0
	"gpr	w23	.32	184	0\n" // w0
	"gpr	w24	.32	192	0\n" // w0
	"gpr	w25	.32	200	0\n" // w0
	"gpr	w26	.32	208	0\n" // w0
	"gpr	w27	.32	216	0\n" // w0
	"gpr	w28	.32	224	0\n" // w0
	"gpr	w29	.32	232	0\n" // w0
	// TODO complete w list ...
	// special registers
	"gpr	fp	.64	240	0\n" // r15
	"gpr	lr	.64	248	0\n" // r15
	"gpr	sp	.64	256	0\n" // r15
	"gpr	pc	.64	264	0\n" // r15
	"gpr	cpsr	.32	272	0\n" // r16
	// TODO flags
	"gpr	nf	.1	.2176	0	sign\n" // XXX wrong offset
	);
} else {
#if 0
ut32 r[13]
ut32 sp -- r13
ut32 lr -- r14
ut32 pc -- r15
ut32 cpsr -- program status
--> ut32[17]
// TODO: add
MMX: NEON
	ut128 v[32] // or 16 in arm32
	ut32 fpsr;
	ut32 fpcr;
VFP: FPU
	ut32 r[64]
	ut32 fpscr
#endif
	return strdup (
	"=pc	r15\n"
	"=sp	r14\n" // XXX
	"=a0	r0\n"
	"=a1	r1\n"
	"=a2	r2\n"
	"=a3	r3\n"
 	"=zf	zf\n"
 	"=sf	nf\n"
 	"=of	vf\n"
 	"=cf	cf\n"
	"gpr	lr	.32	56	0\n" // r14
	"gpr	pc	.32	60	0\n" // r15
	"gpr	cpsr	.32	64	0\n" // r16
	"gpr	nf	.1	.512	0	sign\n" // msb bit of last op
	"gpr	zf	.1	.513	0	zero\n" // set if last op is 0
/*
A carry occurs:
    if the result of an addition is greater than or equal to 232
    if the result of a subtraction is positive or zero
    as the result of an inline barrel shifter operation in a move or logical instruction.
*/
	"gpr	cf	.1	.514	0	carry\n" // set if last op carries
/*
Overflow occurs if the result of an add, subtract, or compare is greater than or equal to 231, or less than -231.
*/
	"gpr	vf	.1	.515	0	overflow\n" // set if overflows
	"gpr	r0	.32	0	0\n"
	"gpr	r1	.32	4	0\n"
	"gpr	r2	.32	8	0\n"
	"gpr	r3	.32	12	0\n"
	"gpr	r4	.32	16	0\n"
	"gpr	r5	.32	20	0\n"
	"gpr	r6	.32	24	0\n"
	"gpr	r7	.32	28	0\n"
	"gpr	r8	.32	32	0\n"
	"gpr	r9	.32	36	0\n"
	"gpr	r10	.32	40	0\n"
	"gpr	r11	.32	44	0\n"
	"gpr	r12	.32	48	0\n"
	"gpr	r13	.32	52	0\n"
	"gpr	r14	.32	56	0\n"
	"gpr	r15	.32	60	0\n"
	);
}
#elif __APPLE__
if (dbg->bits & R_SYS_BITS_32) {
	return strdup (
	"=pc	eip\n"
	"=sp	esp\n"
	"=bp	ebp\n"
	"=a0	eax\n"
	"=a1	ebx\n"
	"=a2	ecx\n"
	"=a3	edi\n"
	"=zf	zf\n"
	"=of	of\n"
	"=sf	sf\n"
	"=cf	cf\n"
	"gpr	eax	.32	0	0\n"
	"gpr	ebx	.32	4	0\n"
	"gpr	ecx	.32	8	0\n"
	"gpr	edx	.32	12	0\n"
	"gpr	edi	.32	16	0\n"
	"gpr	esi	.32	20	0\n"
	"gpr	ebp	.32	24	0\n"
	"gpr	esp	.32	28	0\n"
	"seg	ss	.32	32	0\n"
	"gpr	eflags	.32	36	0	c1p.a.zstido.n.rv\n"
	"gpr	cf	.1	.288	0	carry\n"
	"gpr	pf	.1	.290	0	parity\n"
	"gpr	af	.1	.292	0	adjust\n"
	"gpr	zf	.1	.294	0	zero\n"
	"gpr	sf	.1	.295	0	sign\n"
	"gpr	tf	.1	.296	0	trap\n"
	"gpr	if	.1	.297	0	interrupt\n"
	"gpr	df	.1	.298	0	direction\n"
	"gpr	of	.1	.299	0	overflow\n"
	"gpr	eip	.32	40	0\n"
	"drx	dr0	.32	0	0\n"
	"drx	dr1	.32	4	0\n"
	"drx	dr2	.32	8	0\n"
	"drx	dr3	.32	12	0\n"
	"drx	dr6	.32	24	0\n"
	"drx	dr7	.32	28	0\n"
	"seg	cs	.32	44	0\n"
	"seg	ds	.32	48	0\n"
	"seg	es	.32	52	0\n"
	"seg	fs	.32	56	0\n"
	"seg	gs	.32	60	0\n"
	);
} else if (dbg->bits == R_SYS_BITS_64) {
	return strdup (
	"=pc	rip\n"
	"=sp	rsp\n"
	"=bp	rbp\n"
	"=a0	rax\n"
	"=a1	rbx\n"
	"=a2	rcx\n"
	"=a3	rdx\n"
	"=zf	zf\n"
	"=of	of\n"
	"=sf	sf\n"
	"=cf	cf\n"
	"gpr	rax	.64	8	0\n"
	"gpr	rbx	.64	16	0\n"
	"gpr	rcx	.64	24	0\n"
	"gpr	rdx	.64	32	0\n"
	"gpr	rdi	.64	40	0\n"
	"gpr	rsi	.64	48	0\n"
	"gpr	rbp	.64	56	0\n"
	"gpr	rsp	.64	64	0\n"
	"gpr	r8	.64	72	0\n"
	"gpr	r9	.64	80	0\n"
	"gpr	r10	.64	88	0\n"
	"gpr	r11	.64	96	0\n"
	"gpr	r12	.64	104	0\n"
	"gpr	r13	.64	112	0\n"
	"gpr	r14	.64	120	0\n"
	"gpr	r15	.64	128	0\n"
	"gpr	rip	.64	136	0\n"
	"gpr	eflags	.32	144	0	c1p.a.zstido.n.rv\n"
	"gpr	rflags	.64	144	0	c1p.a.zstido.n.rv\n"
	"gpr	cf	.1	.1152	0	carry\n"
	"gpr	pf	.1	.1154	0	parity\n"
	"gpr	af	.1	.1156	0	adjust\n"
	"gpr	zf	.1	.1158	0	zero\n"
	"gpr	sf	.1	.1159	0	sign\n"
	"gpr	tf	.1	.1160	0	trap\n"
	"gpr	if	.1	.1161	0	interrupt\n"
	"gpr	df	.1	.1162	0	direction\n"
	"gpr	of	.1	.1163	0	overflow\n"
	"seg	cs	.64	144	0\n"
	"seg	fs	.64	152	0\n"
	"seg	gs	.64	160	0\n"

	"drx	dr0	.64	0	0\n"
	"drx	dr1	.64	8	0\n"
	"drx	dr2	.64	16	0\n"
	"drx	dr3	.64	24	0\n"
	"drx	dr6	.64	32	0\n"
	"drx	dr7	.64	40	0\n"
	);
} else {
	eprintf ("invalid bit size\n");
	return NULL;
}
#elif __x86_64__  && (__OpenBSD__ || __NetBSD__)
	return strdup (
	"=pc	rip\n"
	"=sp	rsp\n"
	"=bp	rbp\n"
	"=a0	rax\n"
	"=a1	rbx\n"
	"=a2	rcx\n"
	"=a3	rdx\n"
	"# no profile defined for x86-64\n"
	"gpr	rdi	.64	0	0\n"
	"gpr	rsi	.64	8	0\n"
	"gpr	rdx	.64	16	0\n"
	"gpr	rcx	.64	24	0\n"
	"gpr	r8	.64	32	0\n"
	"gpr	r9	.64	40	0\n"
	"gpr	r10	.64	48	0\n"
	"gpr	r11	.64	56	0\n"
	"gpr	r12	.64	64	0\n"
	"gpr	r13	.64	72	0\n"
	"gpr	r14	.64	80	0\n"
	"gpr	r15	.64	88	0\n"
	"gpr	rbp	.64	96	0\n"
	"gpr	rbx	.64	104	0\n"
	"gpr	rax	.64	112	0\n"
	"gpr	rsp	.64	120	0\n"
	"gpr	rip	.64	128	0\n"
	"gpr	rflags	.64	136	0	c1p.a.zstido.n.rv\n"
	"seg	cs	.64	144	0\n"
	"seg	ss	.64	152	0\n"
	"seg	ds	.64	160	0\n"
	"seg	es	.64	168	0\n"
	"seg	fs	.64	176	0\n"
	"seg	gs	.64	184	0\n"
	"drx	dr0	.32	0	0\n"
	"drx	dr1	.32	4	0\n"
	"drx	dr2	.32	8	0\n"
	"drx	dr3	.32	12	0\n"
	"drx	dr6	.32	24	0\n"
	"drx	dr7	.32	28	0\n"
	);
#elif __x86_64__  && __KFBSD__
	return strdup (
	"=pc	rip\n"
	"=sp	rsp\n"
	"=bp	rbp\n"
	"=a0	rax\n"
	"=a1	rbx\n"
	"=a2	rcx\n"
	"=a3	rdx\n"
	"# no profile defined for x86-64\n"
	"gpr	r15	.64	0	0\n"
	"gpr	r14	.64	8	0\n"
	"gpr	r13	.64	16	0\n"
	"gpr	r12	.64	24	0\n"
	"gpr	r11	.64	32	0\n"
	"gpr	r10	.64	40	0\n"
	"gpr	r9	.64	48	0\n"
	"gpr	r8	.64	56	0\n"
	"gpr	rdi	.64	64	0\n"
	"gpr	rsi	.64	72	0\n"
	"gpr	rbp	.64	80	0\n"
	"gpr	rbx	.64	88	0\n"
	"gpr	rdx	.64	96	0\n"
	"gpr	rcx	.64	104	0\n"
	"gpr	rax	.64	112	0\n"
	"gpr	trapno	.64	120	0\n"
	"gpr	err	.64	128	0\n"
	"gpr	rip	.64	136	0\n"
	"seg	cs	.64	144	0\n"
	"gpr	rflags	.64	152	0	c1p.a.zstido.n.rv\n"
	"gpr	rsp	.64	160	0\n"
	"seg	ss	.64	168	0\n"
	);
#elif __arm__ && __linux__
	return strdup (
	"=pc	r15\n"
	"=sp	r14\n" // XXX
	"=a0	r0\n"
	"=a1	r1\n"
	"=a2	r2\n"
	"=a3	r3\n"
 	"=zf	zf\n"
 	"=sf	nf\n"
 	"=of	vf\n"
 	"=cf	cf\n"
	"gpr	lr	.32	56	0\n" // r14
	"gpr	pc	.32	60	0\n" // r15
	"gpr	cpsr	.32	64	0\n" // r16
	"gpr	nf	.1	.512	0	sign\n" // msb bit of last op
	"gpr	zf	.1	.513	0	zero\n" // set if last op is 0
	"gpr	cf	.1	.514	0	carry\n" // set if last op carries
	"gpr	vf	.1	.515	0	overflow\n" // set if overflows

	"gpr	r0	.32	0	0\n"
	"gpr	r1	.32	4	0\n"
	"gpr	r2	.32	8	0\n"
	"gpr	r3	.32	12	0\n"
	"gpr	r4	.32	16	0\n"
	"gpr	r5	.32	20	0\n"
	"gpr	r6	.32	24	0\n"
	"gpr	r7	.32	28	0\n"
	"gpr	r8	.32	32	0\n"
	"gpr	r9	.32	36	0\n"
	"gpr	r10	.32	40	0\n"
	"gpr	r11	.32	44	0\n"
	"gpr	r12	.32	48	0\n"
	"gpr	r13	.32	52	0\n"
	"gpr	r14	.32	56	0\n"
	"gpr	r15	.32	60	0\n"
	"gpr	r16	.32	64	0\n"
	"gpr	r17	.32	68	0\n"
	);
#else
#warning NO DEBUGGER REGISTERS PROFILE DEFINED
	return NULL;
#endif
}

#if __APPLE__
// XXX
static RDebugPid *darwin_get_pid(int pid) {
	int psnamelen, foo, nargs, mib[3];
	size_t size, argmax = 2048;
	char *curr_arg, *start_args, *iter_args, *end_args;
	char *procargs = NULL;
	char psname[4096];
#if 0
	/* Get the maximum process arguments size. */
	mib[0] = CTL_KERN;
	mib[1] = KERN_ARGMAX;
	size = sizeof(argmax);
	if (sysctl (mib, 2, &argmax, &size, NULL, 0) == -1) {
		eprintf ("sysctl() error on getting argmax\n");
		return NULL;
	}
#endif
	/* Allocate space for the arguments. */
	procargs = (char *)malloc (argmax);
	if (procargs == NULL) {
		eprintf ("getcmdargs(): insufficient memory for procargs %d\n", (int)(size_t)argmax);
		return NULL;
	}

	/*
	 * Make a sysctl() call to get the raw argument space of the process.
	 */
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROCARGS2;
	mib[2] = pid;

	size = argmax;
	procargs[0] = 0;
	if (sysctl (mib, 3, procargs, &size, NULL, 0) == -1) {
		if (EINVAL == errno) { // invalid == access denied for some reason
			//eprintf("EINVAL returned fetching argument space\n");
			free (procargs);
			return NULL;
		}
		eprintf ("sysctl(): unspecified sysctl error - %i\n", errno);
		free (procargs);
		return NULL;
	}

	// copy the number of argument to nargs
	memcpy (&nargs, procargs, sizeof(nargs));
	iter_args =  procargs + sizeof(nargs);
	end_args = &procargs[size-30]; // end of the argument space
	if (iter_args >= end_args) {
		eprintf ("getcmdargs(): argument length mismatch");
		free (procargs);
		return NULL;
	}

	//TODO: save the environment variables to envlist as well
	// Skip over the exec_path and '\0' characters.
	// XXX: fix parsing
#if 0
	while (iter_args < end_args && *iter_args != '\0') { iter_args++; }
	while (iter_args < end_args && *iter_args == '\0') { iter_args++; }
#endif
	if (iter_args == end_args) {
		free (procargs);
		return NULL;
	}
	/* Iterate through the '\0'-terminated strings and add each string
	 * to the Python List arglist as a Python string.
	 * Stop when nargs strings have been extracted.  That should be all
	 * the arguments.  The rest of the strings will be environment
	 * strings for the command.
	 */
	curr_arg = iter_args;
	start_args = iter_args; //reset start position to beginning of cmdline
	foo = 1;
	*psname = 0;
	psnamelen = 0;
	while (iter_args < end_args && nargs > 0) {
		if (*iter_args++ == '\0') {
			int alen = strlen (curr_arg);
			if (foo) {
				memcpy (psname, curr_arg, alen+1);
				foo = 0;
			} else {
				psname[psnamelen] = ' ';
				memcpy (psname+psnamelen+1, curr_arg, alen+1);
			}
			psnamelen += alen;
			//printf("arg[%i]: %s\n", iter_args, curr_arg);
			/* Fetch next argument */
			curr_arg = iter_args;
			nargs--;
		}
	}

#if 1
	/*
	 * curr_arg position should be further than the start of the argspace
	 * and number of arguments should be 0 after iterating above. Otherwise
	 * we had an empty argument space or a missing terminating \0 etc.
	 */
	if (curr_arg == start_args || nargs > 0) {
		psname[0] = 0;
//		eprintf ("getcmdargs(): argument parsing failed");
		free (procargs);
		return NULL;
	}
#endif
	return r_debug_pid_new (psname, pid, 's', 0); // XXX 's' ??, 0?? must set correct values
}
#endif

#undef MAXPID
#define MAXPID 69999

static RList *r_debug_native_tids(int pid) {
	printf ("TODO: Threads: \n");
	// T
	return NULL;
}

static RList *r_debug_native_pids(int pid) {
	RList *list = r_list_new ();
#if __WINDOWS__
	return w32_pids (pid, list);
#elif __APPLE__
	if (pid) {
		RDebugPid *p = darwin_get_pid (pid);
		if (p) r_list_append (list, p);
	} else {
		int i;
		for(i=1; i<MAXPID; i++) {
			RDebugPid *p = darwin_get_pid (i);
			if (p) r_list_append (list, p);
		}
	}
#else
	int i, fd;
	char *ptr, cmdline[1024];
// TODO: new syntax: R_LIST (r_debug_pid_free)
	list->free = (RListFree)&r_debug_pid_free;
	/* TODO */
	if (pid) {
		r_list_append (list, r_debug_pid_new ("(current)", pid, 's', 0));
		/* list parents */
		DIR *dh;
		struct dirent *de;
		dh = opendir ("/proc");
		if (dh == NULL) {
			r_list_purge (list);
			free (list);
			return NULL;
		}
		//for (i=2; i<39999; i++) {
		while ((de = readdir (dh))) {
			i = atoi (de->d_name); if (!i) continue;
			snprintf (cmdline, sizeof (cmdline), "/proc/%d/status", i);
			fd = open (cmdline, O_RDONLY);
			if (fd == -1)
				continue;
			if (read (fd, cmdline, sizeof (cmdline))==-1) {
				close (fd);
				continue;
			}
			cmdline[sizeof (cmdline)-1] = '\0';
			ptr = strstr (cmdline, "PPid: ");
			if (ptr) {
				int ret, ppid = atoi (ptr+6);
				close (fd);
				if (ppid != pid)
					continue;
				snprintf (cmdline, sizeof (cmdline), "/proc/%d/cmdline", ppid);
				fd = open (cmdline, O_RDONLY);
				if (fd == -1)
					continue;
				ret = read (fd, cmdline, sizeof (cmdline));
				if (ret>0) {
					cmdline[ret-1] = '\0';
					r_list_append (list, r_debug_pid_new (
						cmdline, i, 's', 0));
				}
			}
			close (fd);
		}
		closedir (dh);
	} else
	for (i = 2; i < MAXPID; i++) {
		if (!r_sandbox_kill (i, 0)) {
			int ret;
			// TODO: Use slurp!
			snprintf (cmdline, sizeof (cmdline), "/proc/%d/cmdline", i);
			fd = open (cmdline, O_RDONLY);
			if (fd == -1)
				continue;
			cmdline[0] = '\0';
			ret = read (fd, cmdline, sizeof (cmdline));
			if (ret>0) {
				cmdline[ret-1] = '\0';
				r_list_append (list, r_debug_pid_new (
					cmdline, i, 's', 0));
			}
			close (fd);
		}
	}
#endif
	return list;
}

static RList *r_debug_native_threads(RDebug *dbg, int pid) {
	RList *list = r_list_new ();
	if (list == NULL) {
		eprintf ("No list?\n");
		return NULL;
	}
#if __WINDOWS__
	return w32_thread_list (pid, list);
#elif __APPLE__
#if __arm__
	#define OSX_PC state.__pc
#elif __arm64__
	#define OSX_PC state.__pc
#elif __POWERPC__
	#define OSX_PC state.srr0
#elif __x86_64__
	#define OSX_PC state.__rip
#undef OSX_PC
#define OSX_PC state.x64[REG_PC]
#else
	#define OSX_PC state.__eip
#undef OSX_PC
#define OSX_PC state.x32[REG_PC]
#endif
	int i, tid; //, err;
	//unsigned int gp_count;
	static thread_array_t inferior_threads = NULL;
	static unsigned int inferior_thread_count = 0;
	R_DEBUG_REG_T state;

	if (task_threads (pid_to_task (pid), &inferior_threads,
		&inferior_thread_count) != KERN_SUCCESS) {
		eprintf ("Failed to get list of task's threads.\n");
		return list;
    }
    for (i = 0; i < inferior_thread_count; i++) {
		tid = inferior_threads[i];
/*
		XXX overflow here
        	gp_count = R_DEBUG_STATE_SZ; //sizeof (R_DEBUG_REG_T);
                if ((err = thread_get_state (tid, R_DEBUG_STATE_T,
				(thread_state_t) &state, &gp_count)) != KERN_SUCCESS) {
                        // eprintf ("debug_list_threads: %s\n", MACH_ERROR_STRING(err));
			OSX_PC = 0;
                }
*/
		r_list_append (list, r_debug_pid_new ("???", tid, 's', OSX_PC));
    }
#elif __linux__
	int i, fd, thid = 0;
	char *ptr, cmdline[1024];

	if (!pid) {
		r_list_free (list);
		return NULL;
	}
	r_list_append (list, r_debug_pid_new ("(current)", pid, 's', 0));
	/* list parents */

	/* LOL! linux hides threads from /proc, but they are accessible!! HAHAHA */
	//while ((de = readdir (dh))) {
	snprintf (cmdline, sizeof (cmdline), "/proc/%d/task", pid);
	if (r_file_exists (cmdline)) {
		struct dirent *de;
		DIR *dh = opendir (cmdline);
		while ((de = readdir (dh))) {
			int tid = atoi (de->d_name);
			// TODO: get status, pc, etc..
			r_list_append (list, r_debug_pid_new (cmdline, tid, 's', 0));
		}
		closedir (dh);
	} else {
		/* LOL! linux hides threads from /proc, but they are accessible!! HAHAHA */
		//while ((de = readdir (dh))) {
		for (i=pid; i<MAXPID; i++) { // XXX
			snprintf (cmdline, sizeof (cmdline), "/proc/%d/status", i);
			fd = open (cmdline, O_RDONLY);
			if (fd == -1)
				continue;
			read (fd, cmdline, 1024);
			cmdline[sizeof(cmdline)-1] = '\0';
			ptr = strstr (cmdline, "Tgid:");
			if (ptr) {
				int tgid = atoi (ptr+5);
				if (tgid != pid) {
					close (fd);
					continue;
				}
				(void)read (fd, cmdline, sizeof (cmdline)-1);
				snprintf (cmdline, sizeof (cmdline), "thread_%d", thid++);
				cmdline[sizeof (cmdline)-1] = '\0';
				r_list_append (list, r_debug_pid_new (cmdline, i, 's', 0));
			}
			close (fd);
		}
	}
#else
	eprintf ("TODO\n");
#endif
	return list;
}
// TODO: what about float and hardware regs here ???
// TODO: add flag for type
static int r_debug_native_reg_read(RDebug *dbg, int type, ut8 *buf, int size) {
	int pid = dbg->pid;
#if __WINDOWS__
	CONTEXT ctx __attribute__ ((aligned (16)));
	ctx.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
	if (!GetThreadContext (tid2handler (dbg->pid, dbg->tid), &ctx)) {
		eprintf ("GetThreadContext: %x\n", (int)GetLastError ());
		return R_FALSE;
	}
	if (sizeof (CONTEXT) < size)
		size = sizeof (CONTEXT);
#if 0
// TODO: fix missing regs deltas in profile (DRX+..)
#include <r_util.h>
eprintf ("++ EAX = 0x%08x  %d\n", ctx.Eax, r_offsetof (CONTEXT, Eax));
eprintf ("++ EBX = 0x%08x  %d\n", ctx.Ebx, r_offsetof (CONTEXT, Ebx));
eprintf ("++ ECX = 0x%08x  %d\n", ctx.Ecx, r_offsetof (CONTEXT, Ecx));
eprintf ("++ EDX = 0x%08x  %d\n", ctx.Edx, r_offsetof (CONTEXT, Edx));
eprintf ("++ EIP = 0x%08x  %d\n", ctx.Eip, r_offsetof (CONTEXT, Eip));
eprintf ("++ EDI = 0x%08x  %d\n", ctx.Edi, r_offsetof (CONTEXT, Edi));
eprintf ("++ ESI = 0x%08x  %d\n", ctx.Esi, r_offsetof (CONTEXT, Esi));
eprintf ("++ ESP = 0x%08x  %d\n", ctx.Esp, r_offsetof (CONTEXT, Esp));
eprintf ("++ EBP = 0x%08x  %d\n", ctx.Ebp, r_offsetof (CONTEXT, Ebp));
eprintf ("++ CS = 0x%08x  %d\n", ctx.SegCs, r_offsetof (CONTEXT, SegCs));
eprintf ("++ DS = 0x%08x  %d\n", ctx.SegDs, r_offsetof (CONTEXT, SegDs));
eprintf ("++ GS = 0x%08x  %d\n", ctx.SegGs, r_offsetof (CONTEXT, SegGs));
eprintf ("++ FS = 0x%08x  %d\n", ctx.SegFs, r_offsetof (CONTEXT, SegFs));
eprintf ("++ SS = 0x%08x  %d\n", ctx.SegSs, r_offsetof (CONTEXT, SegSs));
eprintf ("++ EFL = 0x%08x  %d\n", ctx.EFlags, r_offsetof (CONTEXT, EFlags));
#endif
	memcpy (buf, &ctx, size);
	return size;
// XXX this must be defined somewhere else
#elif __APPLE__
	int ret;
	thread_array_t inferior_threads = NULL;
	unsigned int inferior_thread_count = 0;
	R_DEBUG_REG_T *regs = (R_DEBUG_REG_T*)buf;
        unsigned int gp_count = R_DEBUG_STATE_SZ; //sizeof (R_DEBUG_REG_T);

	if (size<sizeof (R_DEBUG_REG_T)) {
		eprintf ("Small buffer passed to r_debug_read\n");
		return R_FALSE;
	}
        ret = task_threads (pid_to_task (pid), &inferior_threads, &inferior_thread_count);
        if (ret != KERN_SUCCESS) {
                return R_FALSE;
        }

	int tid = dbg->tid;
	if (tid == dbg->pid)
		tid = 0;
        if (inferior_thread_count>0) {
                /* TODO: allow to choose the thread */
		gp_count = R_DEBUG_STATE_SZ;

		if (tid <0 || tid>=inferior_thread_count) {
			eprintf ("Tid out of range %d\n", inferior_thread_count);
			return R_FALSE;
		}
// XXX: kinda spaguetti coz multi-arch
#if __i386__ || __x86_64__
		switch (type) {
		case R_REG_TYPE_SEG:
		case R_REG_TYPE_FLG:
		case R_REG_TYPE_GPR:
			if (dbg->bits== R_SYS_BITS_64) {
				ret = thread_get_state (inferior_threads[tid],
					x86_THREAD_STATE, (thread_state_t) regs,
					&gp_count);
			} else {
				ret = thread_get_state (inferior_threads[tid],
					i386_THREAD_STATE, (thread_state_t) regs,
					&gp_count);
			}
			break;
		case R_REG_TYPE_DRX:
			if (dbg->bits== R_SYS_BITS_64) {
				ret = thread_get_state (inferior_threads[tid],
					x86_DEBUG_STATE64, (x86_debug_state64_t*)
					regs, &gp_count);
			} else {
				ret = thread_get_state (inferior_threads[tid],
					x86_DEBUG_STATE32, (x86_debug_state32_t*)
					regs, &gp_count);
			}
			break;
		}
#elif __arm__ || __arm64__
		if (dbg->bits==R_SYS_BITS_64) {
			ret = thread_get_state (inferior_threads[tid],
				ARM_THREAD_STATE64, (thread_state_t) regs, &gp_count);
		} else {
			ret = thread_get_state (inferior_threads[tid],
				ARM_THREAD_STATE, (thread_state_t) regs, &gp_count);
				//R_DEBUG_STATE_T, (thread_state_t) regs, &gp_count);
		}
#else
		eprintf ("Unknown architecture\n");
#endif
		if (ret != KERN_SUCCESS) {
                        eprintf ("debug_getregs: Failed to get thread %d %d.error (%x). (%s)\n",
				(int)pid, pid_to_task (pid), (int)ret, MACH_ERROR_STRING (ret));
                        perror ("thread_get_state");
                        return R_FALSE;
                }
        } else eprintf ("There are no threads!\n");
        return sizeof (R_DEBUG_REG_T);
#elif __linux__ || __sun || __NetBSD__ || __KFBSD__ || __OpenBSD__
	int ret;
	switch (type) {
	case R_REG_TYPE_DRX:
#if __i386__ || __x86_64__
#if __KFBSD__
	{
		// TODO
		struct dbreg dbr;
		ret = ptrace (PT_GETDBREGS, pid, (caddr_t)&dbr, sizeof (dbr));
		if (ret != 0)
			return R_FALSE;
		// XXX: maybe the register map is not correct, must review
	}
#elif __linux__
#ifndef __ANDROID__
	{
		int i;
		for (i=0; i<8; i++) { // DR0-DR7
			if (i==4 || i == 5) continue;
			long ret = ptrace (PTRACE_PEEKUSER, pid, r_offsetof (
				struct user, u_debugreg[i]), 0);
			memcpy (buf+(i*sizeof(ret)), &ret, sizeof(ret));
		}
		return sizeof (R_DEBUG_REG_T);
	}
#else
#warning Android X86 does not support DRX
#endif
#endif
#endif
		return R_TRUE;
		break;
	case R_REG_TYPE_SEG:
	case R_REG_TYPE_FLG:
	case R_REG_TYPE_GPR:
		{
		R_DEBUG_REG_T regs;
		memset (&regs, 0, sizeof (regs));
		memset (buf, 0, size);
#if __NetBSD__ || __OpenBSD__
		ret = ptrace (PTRACE_GETREGS, pid, &regs, sizeof (regs));
#elif __KFBSD__
		ret = ptrace(PT_GETREGS, pid, (caddr_t)&regs, 0);
#elif __linux__ && __powerpc__
		ret = ptrace (PTRACE_GETREGS, pid, &regs, NULL);
#else
		/* linux/arm/x86/x64 */
		if (dbg->bits & R_SYS_BITS_32) {
// XXX. this is wrong
#if 0
			struct user_regs_struct_x86_64 r64;
			ret = ptrace (PTRACE_GETREGS, pid, NULL, &r64);
eprintf (" EIP : 0x%x\n", r32.eip);
eprintf (" ESP : 0x%x\n", r32.esp);
#endif

#if 0
int i=0;
unsigned char *p = &r64;;
for(i=0;i< sizeof (r64); i++) {
printf ("%02x ", p[i]);
}
printf ("\n");
#endif
			ret = ptrace (PTRACE_GETREGS, pid, NULL, &regs);
		} else {
			ret = ptrace (PTRACE_GETREGS, pid, NULL, &regs);
		}
#endif
		if (ret != 0)
			return R_FALSE;
		if (sizeof (regs) < size)
			size = sizeof (regs);
		memcpy (buf, &regs, size);
		return sizeof (regs);
		}
		break;
	}
	return R_TRUE;
#else
#warning dbg-native not supported for this platform
	return R_FALSE;
#endif
}

static int r_debug_native_reg_write(RDebug *dbg, int type, const ut8* buf, int size) {
	// XXX use switch or so
	if (type == R_REG_TYPE_DRX) {
#if __i386__ || __x86_64__
#if __KFBSD__
		return (0 == ptrace (PT_SETDBREGS, dbg->pid,
			(caddr_t)buf, sizeof (struct dbreg)));
#elif __linux__
// XXX: this android check is only for arm
#ifndef __ANDROID__
		{
		int i;
		long *val = (long*)buf;
		for (i=0; i<8; i++) { // DR0-DR7
			if (i==4 || i == 5) continue;
			long ret = ptrace (PTRACE_POKEUSER, dbg->pid, r_offsetof (
				struct user, u_debugreg[i]), val[i]); //*(val+i));
			if (ret != 0) {
				eprintf ("ptrace error for dr %d\n", i);
				perror("ptrace");
				//return R_FALSE;
			}
		}
		}
		return sizeof (R_DEBUG_REG_T);
#else
		return R_FALSE;
#endif
#elif __APPLE__
		int ret;
		thread_array_t inferior_threads = NULL;
		unsigned int inferior_thread_count = 0;
		R_DEBUG_REG_T *regs = (R_DEBUG_REG_T*)buf;
		unsigned int gp_count = R_DEBUG_STATE_SZ;

		ret = task_threads (pid_to_task (dbg->pid),
			&inferior_threads, &inferior_thread_count);
		if (ret != KERN_SUCCESS) {
			eprintf ("debug_getregs\n");
			return R_FALSE;
		}

		/* TODO: thread cannot be selected */
		if (inferior_thread_count>0) {
			gp_count = ((dbg->bits == R_SYS_BITS_64))? 44:16;
			// XXX: kinda spaguetti coz multi-arch
			int tid = inferior_threads[0];
#if __i386__ || __x86_64__
			switch (type) {
			case R_REG_TYPE_DRX:
				if (dbg->bits== R_SYS_BITS_64) {
					ret = thread_set_state (inferior_threads[tid],
						x86_DEBUG_STATE64, (x86_debug_state64_t*)
						regs, &gp_count);
				} else {
					ret = thread_set_state (inferior_threads[tid],
						x86_DEBUG_STATE32, (x86_debug_state32_t*)
						regs, &gp_count);
				}
				break;
			default:
				if (dbg->bits == R_SYS_BITS_64) {
					ret = thread_set_state (tid, x86_THREAD_STATE,
						(thread_state_t) regs, gp_count);
				} else {
					ret = thread_set_state (tid, i386_THREAD_STATE,
						(thread_state_t) regs, gp_count);
				}
			}
#else
			ret = thread_set_state (inferior_threads[tid],
					R_DEBUG_STATE_T, (thread_state_t) regs, &gp_count);
#endif
//if (thread_set_state (inferior_threads[0], R_DEBUG_STATE_T, (thread_state_t) regs, gp_count) != KERN_SUCCESS) {
		if (ret != KERN_SUCCESS) {
			eprintf ("debug_setregs: Failed to set thread %d %d.error (%x). (%s)\n",
					(int)dbg->pid, pid_to_task (dbg->pid), (int)ret,
					MACH_ERROR_STRING (ret));
			perror ("thread_set_state");
			return R_FALSE;
		}
		} else eprintf ("There are no threads!\n");
		return sizeof (R_DEBUG_REG_T);
#else
		eprintf ("TODO: add support for write DRX registers\n");
		return R_FALSE;
#endif
#else // i386/x86-64
		return R_FALSE;
#endif
	} else
	if (type == R_REG_TYPE_GPR) {
		int pid = dbg->pid;
#if __WINDOWS__
		int tid = dbg->tid;
		CONTEXT ctx __attribute__((aligned (16)));
		memcpy (&ctx, buf, sizeof (CONTEXT));
		ctx.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS;
	//	eprintf ("EFLAGS =%x\n", ctx.EFlags);
		return SetThreadContext (tid2handler (pid, tid), &ctx)? R_TRUE: R_FALSE;
#elif __linux__
		int ret = ptrace (PTRACE_SETREGS, pid, 0, (void*)buf);
		if (sizeof (R_DEBUG_REG_T) < size)
			size = sizeof (R_DEBUG_REG_T);
		return (ret != 0) ? R_FALSE: R_TRUE;
#elif __sun || __NetBSD__ || __KFBSD__ || __OpenBSD__
		int ret = ptrace (PTRACE_SETREGS, pid, (void*)(size_t)buf, sizeof (R_DEBUG_REG_T));
		if (sizeof (R_DEBUG_REG_T) < size)
			size = sizeof (R_DEBUG_REG_T);
		return (ret != 0) ? R_FALSE: R_TRUE;
#elif __APPLE__
		int ret;
		thread_array_t inferior_threads = NULL;
		unsigned int inferior_thread_count = 0;
		R_DEBUG_REG_T *regs = (R_DEBUG_REG_T*)buf;
		unsigned int gp_count = R_DEBUG_STATE_SZ;

		ret = task_threads (pid_to_task (pid),
			&inferior_threads, &inferior_thread_count);
		if (ret != KERN_SUCCESS) {
			eprintf ("debug_getregs\n");
			return R_FALSE;
		}

		/* TODO: thread cannot be selected */
		if (inferior_thread_count>0) {
			gp_count = ((dbg->bits == R_SYS_BITS_64))? 44:16;
			// XXX: kinda spaguetti coz multi-arch
			int tid = inferior_threads[0];
#if __i386__ || __x86_64__
			switch (type) {
			case R_REG_TYPE_DRX:
				if (dbg->bits== R_SYS_BITS_64) {
					ret = thread_get_state (inferior_threads[tid],
						x86_DEBUG_STATE64, (x86_debug_state64_t*)
					regs, &gp_count);
				} else {
					ret = thread_get_state (inferior_threads[tid],
						x86_DEBUG_STATE32, (x86_debug_state32_t*)
					regs, &gp_count);
				}
				break;
			default:
				if (dbg->bits == R_SYS_BITS_64) {
					ret = thread_set_state (tid, x86_THREAD_STATE,
						(thread_state_t) regs, gp_count);
				} else {
					ret = thread_set_state (tid, i386_THREAD_STATE,
						(thread_state_t) regs, gp_count);
				}
			}
#else
			ret = thread_set_state (inferior_threads[tid],
					R_DEBUG_STATE_T, (thread_state_t) regs, &gp_count);
#endif
//if (thread_set_state (inferior_threads[0], R_DEBUG_STATE_T, (thread_state_t) regs, gp_count) != KERN_SUCCESS) {
		if (ret != KERN_SUCCESS) {
			eprintf ("debug_setregs: Failed to set thread %d %d.error (%x). (%s)\n",
					(int)pid, pid_to_task (pid), (int)ret, MACH_ERROR_STRING (ret));
			perror ("thread_set_state");
			return R_FALSE;
		}
		} else eprintf ("There are no threads!\n");
		return sizeof (R_DEBUG_REG_T);
#else
#warning r_debug_native_reg_write not implemented
#endif
	} else eprintf ("TODO: reg_write_non-gpr (%d)\n", type);
	return R_FALSE;
}

#if __APPLE__
static const char * unparse_inheritance (vm_inherit_t i) {
        switch (i) {
        case VM_INHERIT_SHARE: return "share";
        case VM_INHERIT_COPY: return "copy";
        case VM_INHERIT_NONE: return "none";
        default: return "???";
        }
}

// TODO: move to p/native/darwin.c
// TODO: this loop MUST be cleaned up
static RList *darwin_dbg_maps (RDebug *dbg) {
	RDebugMap *mr;
	char buf[128];
	int i, print;
	kern_return_t kret;
	vm_region_basic_info_data_64_t info, prev_info;
	mach_vm_address_t prev_address;
	mach_vm_size_t size, prev_size;
	mach_port_t object_name;
	mach_msg_type_number_t count;
	int nsubregions = 0;
	int num_printed = 0;
	size_t address = 0;
	task_t task = pid_to_task (dbg->pid);
	RList *list = r_list_new ();
	// XXX: wrong for 64bits
/*
	count = VM_REGION_BASIC_INFO_COUNT_64;
	kret = mach_vm_region (pid_to_task (dbg->pid), &address, &size, VM_REGION_BASIC_INFO_64,
			(vm_region_info_t) &info, &count, &object_name);
	if (kret != KERN_SUCCESS) {
		printf("No memory regions.\n");
		return;
	}
	memcpy (&prev_info, &info, sizeof (vm_region_basic_info_data_64_t));
*/
	size = 4096;
	memset (&prev_info, 0, sizeof (prev_info));
	prev_address = address;
	prev_size = size;
	nsubregions = 1;

	for (i=0; ; i++) {
		int done = 0;

		address = prev_address + prev_size;

		if (prev_size==0)
			break;
		/* Check to see if address space has wrapped around. */
		if (address == 0)
			done = 1;

		if (!done) {
			count = VM_REGION_BASIC_INFO_COUNT_64;
			kret = mach_vm_region (task, (mach_vm_address_t *)&address,
					&size, VM_REGION_BASIC_INFO_64,
					(vm_region_info_t) &info, &count, &object_name);
			if (kret != KERN_SUCCESS) {
				size = 0;
				print = done = 1;
			}
		}

		if (address != prev_address + prev_size)
			print = 1;

		if ((info.protection != prev_info.protection)
				|| (info.max_protection != prev_info.max_protection)
				|| (info.inheritance != prev_info.inheritance)
				|| (info.shared != prev_info.reserved)
				|| (info.reserved != prev_info.reserved))
			print = 1;

		#define xwr2rwx(x) ((x&1)<<2) | (x&2) | ((x&4)>>2)
		if (print) {
			snprintf (buf, sizeof (buf), "%s %02x %s/%s/%s",
					r_str_rwx_i (xwr2rwx (prev_info.max_protection)), i,
					unparse_inheritance (prev_info.inheritance),
					prev_info.shared ? "shar" : "priv",
					prev_info.reserved ? "reserved" : "not-reserved");
			// TODO: MAPS can have min and max protection rules
			// :: prev_info.max_protection
			mr = r_debug_map_new (buf, prev_address, prev_address+prev_size,
				xwr2rwx (prev_info.protection), 0);
			if (mr == NULL) {
				eprintf ("Cannot create r_debug_map_new\n");
				break;
			}
			r_list_append (list, mr);
		}
#if 0
		if (1==0 && rest) { /* XXX never pritn this info here */
			addr = 0LL;
			addr = (ut64) (ut32) prev_address;
			if (num_printed == 0)
				fprintf(stderr, "Region ");
			else    fprintf(stderr, "   ... ");
			fprintf(stderr, " 0x%08llx - 0x%08llx %s (%s) %s, %s, %s",
					addr, addr + prev_size,
					unparse_protection (prev_info.protection),
					unparse_protection (prev_info.max_protection),
					unparse_inheritance (prev_info.inheritance),
					prev_info.shared ? "shared" : " private",
					prev_info.reserved ? "reserved" : "not-reserved");

			if (nsubregions > 1)
				fprintf(stderr, " (%d sub-regions)", nsubregions);

			fprintf(stderr, "\n");

			prev_address = address;
			prev_size = size;
			memcpy (&prev_info, &info, sizeof (vm_region_basic_info_data_64_t));
			nsubregions = 1;

			num_printed++;
		} else {
#endif
#if 0
			prev_size += size;
			nsubregions++;
#else
			prev_address = address;
			prev_size = size;
			memcpy (&prev_info, &info, sizeof (vm_region_basic_info_data_64_t));
			nsubregions = 1;

			num_printed++;
#endif
			//              }
	}
	return list;
}
#endif

#if __KFBSD__
static RList *r_debug_native_sysctl_map (RDebug *dbg) {
	int mib[4];
	size_t len;
	char *buf, *bp, *eb;
	struct kinfo_vmentry *kve;
	RList *list = NULL;
	RDebugMap *map;

	len = 0;
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_VMMAP;
	mib[3] = dbg->pid;

	if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0)
		return NULL;
	len = len * 4 / 3;
	buf = malloc(len);
	if (buf == NULL)
		return (NULL);
	if (sysctl(mib, 4, buf, &len, NULL, 0) != 0) {
		free (buf);
		return NULL;
	}
	bp = buf;
	eb = buf + len;
	list = r_list_new ();
	while (bp < eb) {
		kve = (struct kinfo_vmentry *)(uintptr_t)bp;
		map = r_debug_map_new (kve->kve_path, kve->kve_start,
				kve->kve_end, kve->kve_protection, 0);
		if (map == NULL)
			break;
		r_list_append (list, map);
		bp += kve->kve_structsize;
	}
	free (buf);
	return list;
}
#endif

static RDebugMap* r_debug_native_map_alloc(RDebug *dbg, ut64 addr, int size) {
#if __APPLE__
	RDebugMap *map = NULL;
	kern_return_t ret;
	unsigned char *base = (unsigned char *)addr;
	boolean_t anywhere = !VM_FLAGS_ANYWHERE;

	if (addr == -1)
		anywhere = VM_FLAGS_ANYWHERE;

	ret = vm_allocate (pid_to_task (dbg->tid),
			(vm_address_t*)&base,
			(vm_size_t)size,
			anywhere);
	if (ret != KERN_SUCCESS) {
		printf("vm_allocate failed\n");
		return NULL;
	}
	r_debug_map_sync (dbg); // update process memory maps
	map = r_debug_map_get (dbg, (ut64)base);
	return map;
#else
#warning malloc not implemented for this platform
	return NULL;
#endif
}

static int r_debug_native_map_dealloc(RDebug *dbg, ut64 addr, int size) {
#if __APPLE__
	int ret;
	ret = vm_deallocate (pid_to_task (dbg->tid),
			(vm_address_t)addr,
			(vm_size_t)size);
	if (ret != KERN_SUCCESS) {
		printf("vm_deallocate failed\n");
		return R_FALSE;
	}
	return R_TRUE;
#else
#warning mdealloc not implemented for this platform
	return R_FALSE;
#endif
}

static RList *r_debug_native_map_get(RDebug *dbg) {
	RList *list = NULL;
#if __KFBSD__
	int ign;
	char unkstr[128];
#endif
#if __APPLE__
	list = darwin_dbg_maps (dbg);
#elif __WINDOWS__
	list = w32_dbg_maps (); // TODO: moar?
#else
#if __sun
	char path[1024];
	/* TODO: On solaris parse /proc/%d/map */
	snprintf (path, sizeof (path)-1, "pmap %d > /dev/stderr", ps.tid);
	system (path);
#else
	RDebugMap *map;
	int i, perm, unk = 0;
	char *pos_c;
	char path[1024], line[1024];
	char region[100], region2[100], perms[5];
	FILE *fd;
	if (dbg->pid == -1) {
		eprintf ("r_debug_native_map_get: No selected pid (-1)\n");
		return NULL;
	}
#if __KFBSD__
	list = r_debug_native_sysctl_map (dbg);
	if (list != NULL)
		return list;
	snprintf (path, sizeof (path), "/proc/%d/map", dbg->pid);
#else
	snprintf (path, sizeof (path), "/proc/%d/maps", dbg->pid);
#endif
	fd = fopen (path, "r");
	if (!fd) {
		perror ("debug_init_maps: /proc");
		return NULL;
	}

	list = r_list_new ();

	while (!feof (fd)) {
		line[0]='\0';
		fgets (line, sizeof (line)-1, fd);
		if (line[0]=='\0')
			break;
		path[0]='\0';
		line[strlen (line)-1]='\0';
#if __KFBSD__
		// 0x8070000 0x8072000 2 0 0xc1fde948 rw- 1 0 0x2180 COW NC vnode /usr/bin/gcc
		sscanf (line, "%s %s %d %d 0x%s %3s %d %d",
			&region[2], &region2[2], &ign, &ign,
			unkstr, perms, &ign, &ign);
		pos_c = strchr (line, '/');
		if (pos_c) strncpy (path, pos_c, sizeof (path)-1);
		else path[0]='\0';
#else
		char null[64]; // XXX: this can overflow
		sscanf (line, "%s %s %s %s %s %s",
			&region[2], perms, null, null, null, path);

		pos_c = strchr (&region[2], '-');
		if (!pos_c)
			continue;

		pos_c[-1] = (char)'0'; // xxx. this is wrong
		pos_c[ 0] = (char)'x';
		strncpy (region2, pos_c-1, sizeof (region2)-1);
#endif // __KFBSD__
		region[0] = region2[0] = '0';
		region[1] = region2[1] = 'x';

		if (!*path)
			snprintf (path, sizeof (path), "unk%d", unk++);

		perm = 0;
		for (i = 0; perms[i] && i < 4; i++)
			switch (perms[i]) {
			case 'r': perm |= R_IO_READ; break;
			case 'w': perm |= R_IO_WRITE; break;
			case 'x': perm |= R_IO_EXEC; break;
			}

		map = r_debug_map_new (path,
			r_num_get (NULL, region),
			r_num_get (NULL, region2),
			perm, 0);
		if (map == NULL)
			break;
#if 0
		mr->ini = get_offset(region);
		mr->end = get_offset(region2);
		mr->size = mr->end - mr->ini;
		mr->bin = strdup(path);
		mr->perms = 0;
		if(!strcmp(path, "[stack]") || !strcmp(path, "[vdso]"))
			mr->flags = FLAG_NOPERM;
		else
			mr->flags = 0;

		for(i = 0; perms[i] && i < 4; i++) {
			switch(perms[i]) {
				case 'r':
					mr->perms |= REGION_READ;
					break;
				case 'w':
					mr->perms |= REGION_WRITE;
					break;
				case 'x':
					mr->perms |= REGION_EXEC;
			}
		}
#endif
		r_list_append (list, map);
	}
	fclose (fd);
#endif // __sun
#endif // __WINDOWS
	return list;
}

// TODO: deprecate???
#if 0
static int r_debug_native_bp_write(int pid, ut64 addr, int size, int hw, int rwx) {
	if (hw) {
		/* implement DRx register handling here */
		return R_TRUE;
	}
	return R_FALSE;
}

/* TODO: rethink */
static int r_debug_native_bp_read(int pid, ut64 addr, int hw, int rwx) {
	return R_TRUE;
}
#endif

/* TODO: Can I use this as in a coroutine? */
static RList *r_debug_native_frames_x86_32(RDebug *dbg, ut64 at) {
	RRegItem *ri;
	RReg *reg = dbg->reg;
	ut32 i, _esp, esp, ebp2;
	RList *list = r_list_new ();
	RIOBind *bio = &dbg->iob;
	ut8 buf[4];

	list->free = free;
	ri = (at==UT64_MAX)? r_reg_get (reg, "ebp", R_REG_TYPE_GPR): NULL;
	_esp = (ut32) ((ri)? r_reg_get_value (reg, ri): at);
		// TODO: implement [stack] map uptrace method too
	esp = _esp;
	for (i=0; i<MAXBT; i++) {
		bio->read_at (bio->io, esp, (void *)&ebp2, 4);
		if (ebp2 == UT32_MAX)
			break;
		*buf = '\0';
		bio->read_at (bio->io, (ebp2-5)-(ebp2-5)%4, (void *)&buf, 4);

		// TODO: arch_is_call() here and this fun will be portable
		if (buf[(ebp2-5)%4]==0xe8) {
			RDebugFrame *frame = R_NEW (RDebugFrame);
			frame->addr = ebp2;
			frame->size = esp-_esp;
			r_list_append (list, frame);
		}
		esp += 4;
	}
	return list;
}

// XXX: Do this work correctly?
static RList *r_debug_native_frames_x86_64(RDebug *dbg, ut64 at) {
	int i;
	ut8 buf[8];
	RDebugFrame *frame;
	ut64 ptr, ebp2;
	ut64 _rip, _rsp, _rbp;
	RList *list;
	RReg *reg = dbg->reg;
	RIOBind *bio = &dbg->iob;

	_rip = r_reg_get_value (reg, r_reg_get (reg, "rip", R_REG_TYPE_GPR));
	if (at == UT64_MAX) {
		_rsp = r_reg_get_value (reg, r_reg_get (reg, "rsp", R_REG_TYPE_GPR));
		_rbp = r_reg_get_value (reg, r_reg_get (reg, "rbp", R_REG_TYPE_GPR));
	} else {
		_rsp = _rbp = at;
	}

	list = r_list_new ();
	list->free = free;
	bio->read_at (bio->io, _rip, (ut8*)&buf, 8);
	/* %rbp=old rbp, %rbp+4 points to ret */
	/* Plugin before function prelude: push %rbp ; mov %rsp, %rbp */
	if (!memcmp (buf, "\x55\x89\xe5", 3) || !memcmp (buf, "\x89\xe5\x57", 3)) {
		if (bio->read_at (bio->io, _rsp, (ut8*)&ptr, 8) != 8) {
			eprintf ("read error at 0x%08"PFMT64x"\n", _rsp);
			r_list_purge (list);
			free (list);
			return R_FALSE;
		}
		RDebugFrame *frame = R_NEW (RDebugFrame);
		frame->addr = ptr;
		frame->size = 0; // TODO ?
		r_list_append (list, frame);
		_rbp = ptr;
	}

	for (i=1; i<MAXBT; i++) {
		// TODO: make those two reads in a shot
		bio->read_at (bio->io, _rbp, (ut8*)&ebp2, 8);
		if (ebp2 == UT64_MAX)
			break;
		bio->read_at (bio->io, _rbp+8, (ut8*)&ptr, 8);
		if (!ptr || !_rbp)
			break;
		frame = R_NEW (RDebugFrame);
		frame->addr = ptr;
		frame->size = 0; // TODO ?
		r_list_append (list, frame);
		_rbp = ebp2;
	}
	return list;
}

static RList *r_debug_native_frames(RDebug *dbg, ut64 at) {
	if (dbg->bits == R_SYS_BITS_64)
		return r_debug_native_frames_x86_64 (dbg, at);
	return r_debug_native_frames_x86_32 (dbg, at);
}

// TODO: implement own-defined signals
static int r_debug_native_kill(RDebug *dbg, int pid, int tid, int sig) {
#if __WINDOWS__
	// TODO: implement thread support signaling here
	eprintf ("TODO: r_debug_native_kill\n");
#if 0
	HANDLE hProcess; // XXX
	static uint WM_CLOSE = 0x10;
	static bool CloseWindow(IntPtr hWnd) {
		hWnd = FindWindowByCaption (0, "explorer");
		SendMessage(hWnd, WM_CLOSE, NULL, NULL);
		CloseWindow(hWnd);
		return true;
	}
	TerminateProcess (hProcess, 1);
#endif
	return R_FALSE;
#else
	int ret = R_FALSE;
#if 0
	if (thread) {
// XXX this is linux>2.5 specific..ugly
		if (dbg->tid>0 && (ret = tgkill (dbg->pid, dbg->tid, sig))) {
			if (ret != -1)
				ret = R_TRUE;
		}
	} else {
#endif
		if (pid==0) pid = dbg->pid;
		if ((r_sandbox_kill (pid, sig) != -1))
			ret = R_TRUE;
		if (errno == 1) // EPERM
			ret = -R_TRUE;
#if 0
//	}
#endif
	return ret;
#endif
}

struct r_debug_desc_plugin_t r_debug_desc_plugin_native;
static int r_debug_native_init(RDebug *dbg) {
	dbg->h->desc = r_debug_desc_plugin_native;
#if __WINDOWS__
	return w32_dbg_init ();
#else
	return R_TRUE;
#endif
}

#if __i386__ || __x86_64__
// XXX: wtf cmon this  must use drx.c #if __linux__ too..
static int drx_add(RDebug *dbg, ut64 addr, int rwx) {
	// TODO
	return R_FALSE;
}

static int drx_del(RDebug *dbg, ut64 addr, int rwx) {
	// TODO
	return R_FALSE;
}
#endif

static int r_debug_native_drx(RDebug *dbg, int n, ut64 addr, int sz, int rwx, int g) {
#if __i386__ || __x86_64__
	drxt regs[8] = {0};

	// sync drx regs
#define R dbg->reg
	regs[0] = r_reg_getv (R, "dr0");
	regs[1] = r_reg_getv (R, "dr1");
	regs[2] = r_reg_getv (R, "dr2");
	regs[3] = r_reg_getv (R, "dr3");
/*
	RESERVED
	regs[4] = r_reg_getv (R, "dr4");
	regs[5] = r_reg_getv (R, "dr5");
*/
	regs[6] = r_reg_getv (R, "dr6");
	regs[7] = r_reg_getv (R, "dr7");

	if (sz == 0) {
		drx_list ((drxt*)&regs);
		return R_FALSE;
	}
	if (sz<0) { // remove
		drx_set (regs, n, addr, -1, 0, 0);
	} else {
		drx_set (regs, n, addr, sz, rwx, g);
	}
	r_reg_setv (R, "dr0", regs[0]);
	r_reg_setv (R, "dr1", regs[1]);
	r_reg_setv (R, "dr2", regs[2]);
	r_reg_setv (R, "dr3", regs[3]);
	r_reg_setv (R, "dr6", regs[6]);
	r_reg_setv (R, "dr7", regs[7]);
	return R_TRUE;
#else
	eprintf ("drx: Unsupported platform\n");
#endif
	return R_FALSE;
}

static int r_debug_native_bp(void *user, int add, ut64 addr, int hw, int rwx) {
#if __i386__ || __x86_64__
	RDebug *dbg = user;
	if (hw) {
		if (add) return drx_add (dbg, addr, rwx);
		return drx_del (dbg, addr, rwx);
	}
#endif
	return R_FALSE;
}

#if __KFBSD__
#include <sys/un.h>
#include <arpa/inet.h>
static void addr_to_string(struct sockaddr_storage *ss, char *buffer, int buflen) {
	char buffer2[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	struct sockaddr_un *sun;

	if (buflen>0)
	switch (ss->ss_family) {
	case AF_LOCAL:
		sun = (struct sockaddr_un *)ss;
		strncpy (buffer, (sun && *sun->sun_path)?
			sun->sun_path: "-", buflen-1);
		break;
	case AF_INET:
		sin = (struct sockaddr_in *)ss;
		snprintf (buffer, buflen, "%s:%d", inet_ntoa (sin->sin_addr),
		    ntohs (sin->sin_port));
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)ss;
		if (inet_ntop (AF_INET6, &sin6->sin6_addr, buffer2,
		    sizeof (buffer2)) != NULL)
			snprintf (buffer, buflen, "%s.%d", buffer2,
			    ntohs (sin6->sin6_port));
		else strcpy (buffer, "-");
		break;
	default:
		*buffer = 0;
		break;
	}
}
#endif

static RList *r_debug_desc_native_list (int pid) {
	RList *ret = NULL;
// TODO: windows
#if __KFBSD__
	int perm, type, mib[4];
	size_t len;
	char *buf, *bp, *eb, *str, path[1024];
	RDebugDesc *desc;
	struct kinfo_file *kve;

	len = 0;
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_FILEDESC;
	mib[3] = pid;

	if (sysctl (mib, 4, NULL, &len, NULL, 0) != 0)
		return NULL;
	len = len * 4 / 3;
	buf = malloc(len);
	if (buf == NULL)
		return (NULL);
	if (sysctl (mib, 4, buf, &len, NULL, 0) != 0) {
		free (buf);
		return NULL;
	}
	bp = buf;
	eb = buf + len;
	ret = r_list_new ();
	if (ret) {
		ret->free = (RListFree) r_debug_desc_free;
		while (bp < eb) {
			kve = (struct kinfo_file *)(uintptr_t)bp;
			bp += kve->kf_structsize;
			if (kve->kf_fd < 0) // Skip root and cwd. We need it ??
				continue;
			str = kve->kf_path;
			switch (kve->kf_type) {
				case KF_TYPE_VNODE: type = 'v'; break;
				case KF_TYPE_SOCKET:
					type = 's';
					if (kve->kf_sock_domain == AF_LOCAL) {
						struct sockaddr_un *sun =
							(struct sockaddr_un *)&kve->kf_sa_local;
						if (sun->sun_path[0] != 0)
							addr_to_string (&kve->kf_sa_local, path, sizeof (path));
						else
							addr_to_string (&kve->kf_sa_peer, path, sizeof (path));
					} else {
						addr_to_string (&kve->kf_sa_local, path, sizeof (path));
						strcat (path, " ");
						addr_to_string (&kve->kf_sa_peer, path + strlen (path),
								sizeof (path));
					}
					str = path;
					break;
				case KF_TYPE_PIPE: type = 'p'; break;
				case KF_TYPE_FIFO: type = 'f'; break;
				case KF_TYPE_KQUEUE: type = 'k'; break;
				case KF_TYPE_CRYPTO: type = 'c'; break;
				case KF_TYPE_MQUEUE: type = 'm'; break;
				case KF_TYPE_SHM: type = 'h'; break;
				case KF_TYPE_PTS: type = 't'; break;
				case KF_TYPE_SEM: type = 'e'; break;
				case KF_TYPE_NONE:
				case KF_TYPE_UNKNOWN:
				default: type = '-'; break;
			}
			perm = (kve->kf_flags & KF_FLAG_READ)?R_IO_READ:0;
			perm |= (kve->kf_flags & KF_FLAG_WRITE)?R_IO_WRITE:0;
			desc = r_debug_desc_new (kve->kf_fd, str, perm, type,
					kve->kf_offset);
			if (desc == NULL)
				break;
			r_list_append (ret, desc);
		}
	}
	free (buf);
#elif __linux__
	char path[512], file[512], buf[512];
	struct dirent *de;
	RDebugDesc *desc;
	int type, perm;
	int len, len2;
	struct stat st;
	DIR *dd;

	snprintf (path, sizeof (path), "/proc/%i/fd/", pid);
	if (!(dd = opendir (path))) {
		eprintf ("Cannot open /proc\n");
		return NULL;
	}

	if ((ret = r_list_new ())) {
		ret->free = (RListFree) r_debug_desc_free;
		while ((de = (struct dirent *)readdir(dd))) {
			if (de->d_name[0]=='.')
				continue;

			len = strlen (path);
			len2 = strlen (de->d_name);
			if (len+len2+1 >= sizeof (file)) {
				r_list_free (ret);
				closedir (dd);
				eprintf ("Filename is too long");
				return NULL;
			}
			memcpy (file, path, len);
			memcpy (file+len, de->d_name, len2+1);

			memset (buf, 0, sizeof (buf));
			readlink (file, buf, sizeof (buf) - 1);
			type = perm = 0;
			if (stat (file, &st) != -1) {
				type  = st.st_mode & S_IFIFO  ? 'P':
					st.st_mode & S_IFSOCK ? 'S':
					st.st_mode & S_IFCHR  ? 'C':'-';
			}
			if (lstat(path, &st) != -1) {
				if (st.st_mode & S_IRUSR)
					perm |= R_IO_READ;
				if (st.st_mode & S_IWUSR)
					perm |= R_IO_WRITE;
			}
			//TODO: Offset
			desc = r_debug_desc_new (atoi (de->d_name), buf, perm, type, 0);
			if (desc == NULL)
				break;
			r_list_append (ret, desc);
		}
		closedir(dd);
	}
#endif
	return ret;
}

#if __APPLE__
vm_prot_t unix_prot_to_darwin(int prot) {
        return ((prot&1<<4)?VM_PROT_READ:0 |
                (prot&1<<2)?VM_PROT_WRITE:0 |
                (prot&1<<1)?VM_PROT_EXECUTE:0);
}
#endif
static int r_debug_native_map_protect (RDebug *dbg, ut64 addr, int size, int perms) {
#if __WINDOWS__
        DWORD old;
	HANDLE hProcess = tid2handler (dbg->pid, dbg->tid);
	// TODO: align pointers
        return VirtualProtectEx (WIN32_PI (hProcess), (LPVOID)(UINT)addr, size, perms, &old);
#elif __APPLE__
	int ret;
	// TODO: align pointers
	ret = vm_protect (pid_to_task (dbg->tid),
			(vm_address_t)addr,
			(vm_size_t)size,
			(boolean_t)0, /* maximum protection */
			VM_PROT_COPY|perms); //unix_prot_to_darwin (perms));
	if (ret != KERN_SUCCESS) {
		printf("vm_protect failed\n");
		return R_FALSE;
	}
	return R_TRUE;
#elif __linux__
#warning mprotect not implemented for this Linux.. contribs are welcome. use r_egg here?
	return R_FALSE;
#else
#warning mprotect not implemented for this platform
	return R_FALSE;
#endif
}

static int r_debug_desc_native_open (const char *path) {
	return 0;
}

struct r_debug_desc_plugin_t r_debug_desc_plugin_native = {
	.open = r_debug_desc_native_open,
	.list = r_debug_desc_native_list,
};

struct r_debug_plugin_t r_debug_plugin_native = {
	.name = "native",
	.license = "LGPL3",
#if __i386__
	.bits = R_SYS_BITS_32,
	.arch = R_ASM_ARCH_X86,
	.canstep = 1,
#elif __x86_64__
	.bits = R_SYS_BITS_32 | R_SYS_BITS_64,
	.arch = R_ASM_ARCH_X86,
	.canstep = 1,
#elif __arm__
	.bits = R_SYS_BITS_32,
	.arch = R_ASM_ARCH_ARM,
	.canstep = 0, // XXX it's 1 on some platforms...
#elif __mips__
	.bits = R_SYS_BITS_64,
	.arch = R_ASM_ARCH_MIPS,
	.canstep = 0,
#elif __powerpc__
	.bits = R_SYS_BITS_32,
	.arch = R_ASM_ARCH_PPC,
	.canstep = 1,
#else
	.bits = 0,
	.arch = 0,
	.canstep = 0,
#warning Unsupported architecture
#endif
	.init = &r_debug_native_init,
	.step = &r_debug_native_step,
	.cont = &r_debug_native_continue,
	.contsc = &r_debug_native_continue_syscall,
	.attach = &r_debug_native_attach,
	.detach = &r_debug_native_detach,
	.pids = &r_debug_native_pids,
	.tids = &r_debug_native_tids,
	.threads = &r_debug_native_threads,
	.wait = &r_debug_native_wait,
	.kill = &r_debug_native_kill,
	.frames = &r_debug_native_frames, // rename to backtrace ?
	.reg_profile = (void *)r_debug_native_reg_profile,
	.reg_read = r_debug_native_reg_read,
	.reg_write = (void *)&r_debug_native_reg_write,
	.map_alloc = r_debug_native_map_alloc,
	.map_dealloc = r_debug_native_map_dealloc,
	.map_get = r_debug_native_map_get,
	.map_protect = r_debug_native_map_protect,
	.breakpoint = r_debug_native_bp,
	.drx = r_debug_native_drx,
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_DBG,
	.data = &r_debug_plugin_native
};
#endif // CORELIB

//#endif
#else // DEBUGGER
struct r_debug_plugin_t r_debug_plugin_native = {
	.name = "native",
};

#endif // DEBUGGER
