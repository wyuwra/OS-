#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}


// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}


// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
int do_exit(int error_code)
{
    panic("process exit!!.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg)
{
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));
    cprintf("To U: \"%s\".\n", (const char *)arg);
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");
    return 0;
}

//LAB4 function2 2412832
static struct proc_struct *
alloc_proc(void)
{   // 申请并初始化一个PCB，为创建线程做准备
    // 这里只申请进程控制块（PCB）；独立内核栈由后续创建流程分配。
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc == NULL)
    {
        // 分配失败时交给调用者处理，不能继续访问 proc 的字段。
        return NULL;
    }

    // 清除分配器返回内存中的旧数据：runs、need_resched、flags 为 0，
    // kstack 为 0，parent、mm、tf 为空，context 和 name 也全部清零。
    memset(proc, 0, sizeof(struct proc_struct));
    // PCB 尚未准备好运行；正式 PID 在后续 do_fork() 登记线程时分配。
    proc->state = PROC_UNINIT;
    proc->pid = -1;
    // 当前实验的内核线程共用启动页表；pgdir 保存物理地址，
    // 后续切换地址空间时可据此构造 satp，不能填入页表的虚拟地址。
    proc->pgdir = boot_pgdir_pa;
    // 清零不能得到合法的空链表节点，需分别初始化为自环。
    // 此时尚未加入全局进程链表和 PID 哈希表，也不更新进程总数。
    list_init(&proc->list_link);
    list_init(&proc->hash_link);
    return proc;
}


static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{   // 处理线程使用的地址空间
    // clone_flags 为后续共享或复制用户地址空间预留，本实验暂不区分。
    (void)clone_flags;

    // 当前阶段只创建内核线程；若出现用户 mm，应先补充地址空间处理，
    // 不能直接返回成功，让一个未处理的用户进程继续进入创建流程。
    assert(current != NULL && current->mm == NULL);
    assert(proc != NULL && proc->mm == NULL);

    // alloc_proc() 已将新 PCB 的 mm 置空，并设置了内核页表 pgdir。
    // mm 为空表示没有独立用户地址空间，内核页表映射仍然有效。
    // 因此这里不分配页表、不复制页面，也不改变现有 pgdir，直接成功返回。
    return 0;
}

static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{   // 把初始寄存器现场复制到新线程的内核栈中，并设置运行栈和首次切换入口，为线程第一次执行做好准备
    // 调用者应已分配 PCB 和独立内核栈，tf 是待复制的初始寄存器现场。
    assert(proc != NULL && proc->kstack != 0 && tf != NULL);
    uintptr_t stack_top = proc->kstack + KSTACKSIZE;

    // 栈向低地址增长：在栈顶下方留出一个完整 trapframe。
    // 必须复制到新线程自己的栈内，不能保存调用者临时 tf 的指针。
    proc->tf = (struct trapframe *)stack_top - 1;
    *proc->tf = *tf;

    // 子线程的 fork 返回值为 0；父线程的返回值由 do_fork() 提供。
    proc->tf->gpr.a0 = 0;
    // esp 为 0 时使用新线程的内核栈顶；非零时采用调用者指定的栈地址。
    proc->tf->gpr.sp = (esp != 0) ? esp : stack_top;

    // 首次 switch_to() 恢复 context 后，先进入 forkret()，
    // 再经 forkrets -> __trapret 恢复 tf，最后由 sret 跳到 tf->epc。
    // context.sp 指向恢复现场，tf 中的 sp 才是 sret 后运行时使用的栈。
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)proc->tf;
}

int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{   // 完成申请PCB和内核栈->准备地址空间与现场->分配PID->登记并唤醒线程，失败时释放已申请的资源。
    // 当前阶段只从已有内核线程创建子线程，初始现场由调用者提供。
    assert(current != NULL && tf != NULL);
    if (nr_process >= MAX_PROCESS)
    {
        return -E_NO_FREE_PROC;
    }

    int ret = -E_NO_MEM;
    struct proc_struct *proc = alloc_proc();
    if (proc == NULL)
    {
        return ret;
    }
    proc->parent = current;

    // 先准备 PCB、独立内核栈、地址空间和现场，暂不加入全局链表。
    // 这样调度器只能看到资源已经准备完整的线程。
    if ((ret = setup_kstack(proc)) != 0)
    {
        goto bad_fork_cleanup_proc;
    }
    if ((ret = copy_mm(clone_flags, proc)) != 0)
    {
        goto bad_fork_cleanup_kstack;
    }
    copy_thread(proc, stack, tf);

    // 单核下保护 PID 分配、全局登记和唤醒，退出时恢复原中断状态。
    bool intr_flag;
    local_intr_save(intr_flag);
    // 资源准备期间可能被中断，因此在登记前再次检查数量上限。
    if (nr_process >= MAX_PROCESS)
    {
        local_intr_restore(intr_flag);
        ret = -E_NO_FREE_PROC;
        goto bad_fork_cleanup_kstack;
    }
    proc->pid = get_pid();
    hash_proc(proc);
    list_add(&proc_list, &proc->list_link);
    nr_process++;
    // 登记完成后才转为 runnable；这里不直接执行线程切换。
    wakeup_proc(proc);
    int pid = proc->pid;
    local_intr_restore(intr_flag);
    return pid;

bad_fork_cleanup_kstack:
    // 按申请的逆序释放资源；尚未登记，所以无需撤销全局链表节点。
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    return ret;
}

int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    // 线程必须有可执行的函数入口；无效请求不进入资源申请流程。
    if (fn == NULL)
    {
        return -E_INVAL;
    }

    // 构造临时的首次运行现场；do_fork() 会把它复制到新线程自己的栈。
    struct trapframe tf;
    memset(&tf, 0, sizeof(tf));
    // kernel_thread_entry 先执行 move a0, s1，再执行 jalr s0，
    // 因此 s0 保存函数入口，s1 保存参数，恢复现场后即可调用 fn(arg)。
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.epc = (uintptr_t)kernel_thread_entry;

    // SPP=1：sret 返回 S 态；SPIE=1：sret 后开启 S 态中断。
    // SIE=0：恢复现场期间保持关中断，直到 sret 完成状态切换。
    // 其余状态位沿用当前 sstatus；这里只设置新线程现场，不改当前 CSR。
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;

    // 内核线程共享内核地址空间，附加 CLONE_VM 并保留调用者其他标志。
    // stack=0 让 copy_thread() 使用新线程的独立内核栈顶。
    // 将创建成功的 PID 或失败的负错误码原样返回给调用者。
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

void
proc_init(void)
{
    // 先建立全局管理结构，后续 do_fork() 分配 PID 和登记线程时会使用。
    list_init(&proc_list);
    for (int i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_init(&hash_list[i]);
    }

    idleproc = alloc_proc();
    if (idleproc == NULL)
    {
        panic("proc_init: cannot allocate idleproc");
    }
    // idleproc 是当前启动现场对应的线程，直接复用启动栈，
    // 无需 setup_kstack() 或伪造首次运行现场，也不加入普通进程链表。
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;

    // set_proc_name() 按固定长度复制，传入完整长度的数组以保证源范围有效。
    const char idle_name[PROC_NAME_LEN + 1] = "idle";
    const char init_name[PROC_NAME_LEN + 1] = "init";
    set_proc_name(idleproc, idle_name);
    nr_process = 1;  // idleproc 计入总数，但不参与普通 PID 哈希查找。
    current = idleproc;

    // current 必须先设置好，do_fork() 才能为 initproc 建立正确的父子关系。
    // initproc 使用独立内核栈，从 init_main("Hello world!!") 开始执行。
    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0)
    {
        panic("proc_init: cannot create initproc, error %d", pid);
    }
    initproc = find_proc(pid);
    assert(initproc != NULL && initproc->pid == 1);
    set_proc_name(initproc, init_name);
    // 此时 current 仍为 idleproc，initproc 已就绪，等待后续 schedule() 选中。
}

//LAB4 function3 YOUR CODE
