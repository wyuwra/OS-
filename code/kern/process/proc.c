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
#include <unistd.h>

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

//LAB4 function3 2412737
// proc_run - 让 proc 成为当前运行线程：切换 current、页表和寄存器上下文
void
proc_run(struct proc_struct *proc)
{
    // 目标就是当前线程时无需切换，避免把自己的 context 保存后再原样恢复。
    if (proc != current)
    {
        struct proc_struct *prev = current, *next = proc;
        bool intr_flag;
        // 更新 current、satp 和执行现场必须是一个整体：
        // 若中间被时钟中断打断，current 与实际运行的栈/页表可能不一致。
        local_intr_save(intr_flag);
        {
            current = next;
            // Sv39：satp = MODE(8) << 60 | 根页表物理页号；pgdir 保存的是物理地址。
            // 不能使用 libs/riscv.h 中的 lsatp()，它只按 Sv32 格式写 satp。
            write_csr(satp, ((uintptr_t)SATP_MODE_SV39 << 60) | (next->pgdir >> RISCV_PGSHIFT));
            // 切换根页表后刷新 TLB，避免继续使用旧地址空间的翻译结果。
            flush_tlb();
            // 保存 prev 的 ra/sp/s0~s11，恢复 next 的现场。
            // 返回到这里时，说明 prev 又被调度回来了，此时 current == prev。
            switch_to(&(prev->context), &(next->context));
        }
        // 恢复的是 prev 当初保存的中断状态（intr_flag 位于 prev 的栈上）。
        local_intr_restore(intr_flag);
    }
}

// cpu_idle - idleproc 的执行主体：内核初始化完成后在此空转，
// 一旦发现需要重新调度，就调用 schedule() 把 CPU 让给其他就绪线程。
void
cpu_idle(void)
{
    while (1)
    {
        // proc_init() 已将 idleproc->need_resched 置 1，首次进入即触发调度。
        // 之后若没有其他就绪线程，schedule() 会选回 idleproc，继续在此等待。
        if (current->need_resched)
        {
            schedule();
        }
    }
}


//LAB4 function2 2412832
//lab5 function2 YOUR CODE update alloc_proc：在此基础上补充 wait_state、cptr、yptr、optr 等字段的初始化
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

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
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

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
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

//lab5 function2 YOUR CODE update kernel_thread（以下为 lab4 版本，按 lab5 要求核对）
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

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir_va, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

//lab5 function3 2412832
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    assert(current != NULL && proc != NULL && proc->mm == NULL);

    struct mm_struct *parent_mm = current->mm;
    if (parent_mm == NULL)
    {
        // 父进程是内核线程，沿用 alloc_proc() 设置的启动页表。
        return 0;
    }

    // CLONE_VM 直接共享父进程的 mm。普通 fork 建立独立的用户地址空间。
    struct mm_struct *mm = parent_mm;
    if (!(clone_flags & CLONE_VM))
    {
        if ((mm = mm_create()) == NULL)
        {
            return -E_NO_MEM;
        }
        int ret = setup_pgdir(mm);
        if (ret != 0)
        {
            mm_destroy(mm);
            return ret;
        }

        // 复制期间锁住父 mm，保证 VMA 和对应映射在遍历期间一致。
        lock_mm(parent_mm);
        ret = dup_mmap(mm, parent_mm);
        unlock_mm(parent_mm);
        if (ret != 0)
        {
            // dup_mmap() 已回收部分复制的 VMA 和用户页。
            put_pgdir(mm);
            mm_destroy(mm);
            return ret;
        }
        // 新物理页可能存有用户指令，确保后续取指能看到复制后的内容。
        asm volatile("fence.i" ::: "memory");
    }

    // 地址空间就绪后再登记引用，并为 proc_run() 保存根页表物理地址。
    bool intr_flag;
    local_intr_save(intr_flag);
    mm_count_inc(mm);
    proc->mm = mm;
    proc->pgdir = PADDR(mm->pgdir);
    local_intr_restore(intr_flag);
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

//lab5 function3 YOUR CODE update do_fork（以下为 lab4 版本，需要加入 set_links、父子关系、copy_mm 对用户进程的处理等）
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{   // 完成申请PCB和内核栈->准备地址空间与现场->分配PID->登记并唤醒线程，失败时释放已申请的资源。
    // 初始现场由调用者提供，子进程的地址空间由 copy_mm() 准备。
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
        // copy_mm() 已登记一次引用。撤销引用后，只有最后一个持有者回收 mm。
        struct mm_struct *mm = proc->mm;
        bool release_mm = mm != NULL && mm_count_dec(mm) == 0;
        local_intr_restore(intr_flag);
        if (release_mm)
        {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
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




//lab5 function2 2412832
static int load_icode(unsigned char *binary, size_t size);

// exec 需要实际读取这些字节。用户区间除了属于可读 VMA，还要有可读的页映射。
static bool
exec_input_readable(struct mm_struct *mm, const void *buffer, size_t length)
{
    uintptr_t start = (uintptr_t)buffer;
    if (length > (uintptr_t)-1 - start || !user_mem_check(mm, start, length, 0))
    {
        return 0;
    }

    if (mm != NULL)
    {
        const pte_t required = PTE_V | PTE_U | PTE_R;
        uintptr_t end = start + length;
        for (uintptr_t va = ROUNDDOWN(start, PGSIZE); va < end; va += PGSIZE)
        {
            pte_t *ptep = get_pte(mm->pgdir, va, 0);
            if (ptep == NULL || (*ptep & required) != required)
            {
                return 0;
            }
        }
    }
    return 1;
}

int
do_execve(const char *name, size_t len, unsigned char *binary, size_t size)
{
    struct mm_struct *old_mm = current->mm;
    bool replacing_user_image = old_mm != NULL;
    char proc_name[PROC_NAME_LEN + 1] = {0};
    size_t name_len = len < PROC_NAME_LEN ? len : PROC_NAME_LEN;
    struct Page *image_pages = NULL;
    size_t image_npages = 0;

    lock_mm(old_mm);
    if (!exec_input_readable(old_mm, binary, size) ||
        (name_len != 0 && !exec_input_readable(old_mm, name, name_len)))
    {
        unlock_mm(old_mm);
        return -E_INVAL;
    }
    if (name_len != 0)
    {
        memcpy(proc_name, name, name_len);
    }

    // 用户传入的 ELF 位于旧页表中，先复制到切换页表后仍可访问的内核内存。
    // 首次从内核线程启动用户程序时，嵌入内核的 ELF 可以直接使用。
    if (replacing_user_image)
    {
        image_npages = ROUNDUP(size, PGSIZE) / PGSIZE;
        image_pages = alloc_pages(image_npages);
        if (image_pages == NULL)
        {
            unlock_mm(old_mm);
            return -E_NO_MEM;
        }
        unsigned char *image = page2kva(image_pages);
        memcpy(image, binary, size);
        binary = image;
    }
    unlock_mm(old_mm);

    if (replacing_user_image)
    {
        // 先切回内核根页表，再解除当前进程对旧地址空间的引用。
        bool intr_flag;
        local_intr_save(intr_flag);
        lsatp(boot_pgdir_pa);
        flush_tlb();
        current->pgdir = boot_pgdir_pa;
        current->mm = NULL;
        int remaining = mm_count_dec(old_mm);
        local_intr_restore(intr_flag);

        if (remaining == 0)
        {
            exit_mmap(old_mm);
            put_pgdir(old_mm);
            mm_destroy(old_mm);
        }
    }

    int ret = load_icode(binary, size);
    if (image_pages != NULL)
    {
        free_pages(image_pages, image_npages);
    }
    if (ret != 0)
    {
        // 旧用户映像已解除，进程必须退出；首次内核装载失败可交给调用者处理。
        return replacing_user_image ? do_exit(ret) : ret;
    }
    set_proc_name(current, proc_name);
    return 0;
}

//lab5 function2 2412832
static int
load_elf_segment(struct mm_struct *mm, const unsigned char *binary,
                 size_t size, const struct proghdr *ph)
{
    if (ph->p_filesz > ph->p_memsz || ph->p_offset > size ||
        ph->p_filesz > size - ph->p_offset)
    {
        return -E_INVAL_ELF;
    }
    if (ph->p_memsz == 0)
    {
        return 0;
    }

    // 在计算段尾之前检查长度，并给用户栈保留独立的地址区间。
    const uintptr_t stack_bottom = USTACKTOP - USTACKSIZE;
    uintptr_t start = ph->p_va;
    if (start < USERBASE || start >= stack_bottom || ph->p_memsz > stack_bottom - start)
    {
        return -E_INVAL_ELF;
    }
    uintptr_t page_start = ROUNDDOWN(start, PGSIZE);
    uintptr_t page_end = ROUNDUP(start + ph->p_memsz, PGSIZE);

    // mm_map() 以页为单位登记 VMA，装载前检查该页区间是否与已有段重叠。
    list_entry_t *head = &mm->mmap_list;
    for (list_entry_t *le = list_next(head); le != head; le = list_next(le))
    {
        struct vma_struct *vma = le2vma(le, list_link);
        if (page_end <= vma->vm_start)
        {
            break;
        }
        if (page_start < vma->vm_end)
        {
            return -E_INVAL_ELF;
        }
    }

    uint32_t vm_flags = 0;
    uint32_t perm = PTE_U;
    if (ph->p_flags & (ELF_PF_R | ELF_PF_W))
    {
        // RISC-V 的可写叶子页表项同时需要读权限。
        vm_flags |= VM_READ;
        perm |= PTE_R;
    }
    if (ph->p_flags & ELF_PF_W)
    {
        vm_flags |= VM_WRITE;
        perm |= PTE_W;
    }
    if (ph->p_flags & ELF_PF_X)
    {
        vm_flags |= VM_EXEC;
        perm |= PTE_X;
    }
    if (vm_flags == 0)
    {
        return -E_INVAL_ELF;
    }

    int ret = mm_map(mm, start, ph->p_memsz, vm_flags, NULL);
    if (ret != 0)
    {
        return ret;
    }

    uintptr_t file_end = start + ph->p_filesz;
    for (uintptr_t va = page_start; va < page_end; va += PGSIZE)
    {
        struct Page *page = pgdir_alloc_page(mm->pgdir, va, perm);
        if (page == NULL)
        {
            return -E_NO_MEM;
        }
        // 整页清零，再经内核直映地址复制文件覆盖的字节，剩下的 BSS 保持为零。
        unsigned char *kva = page2kva(page);
        memset(kva, 0, PGSIZE);
        uintptr_t copy_start = va < start ? start : va;
        uintptr_t copy_end = va + PGSIZE < file_end ? va + PGSIZE : file_end;
        if (copy_start < copy_end)
        {
            memcpy(kva + (copy_start - va), binary + ph->p_offset + (copy_start - start),
                   copy_end - copy_start);
        }
    }
    return 0;
}

static int
load_icode(unsigned char *binary, size_t size)
{
    enum { ELFCLASS64 = 2, ELFDATA2LSB = 1, EV_CURRENT = 1, ET_EXEC = 2, EM_RISCV = 243 };
    if (current->mm != NULL || current->tf == NULL)
    {
        return -E_INVAL;
    }
    if (binary == NULL || size < sizeof(struct elfhdr))
    {
        return -E_INVAL_ELF;
    }

    // ELF 字节的起始地址不保证按结构体对齐，复制到局部变量后再读头部。
    struct elfhdr elf;
    memcpy(&elf, binary, sizeof(elf));
    if (elf.e_magic != ELF_MAGIC || elf.e_elf[0] != ELFCLASS64 ||
        elf.e_elf[1] != ELFDATA2LSB || elf.e_elf[2] != EV_CURRENT ||
        elf.e_type != ET_EXEC || elf.e_machine != EM_RISCV || elf.e_version != EV_CURRENT ||
        elf.e_ehsize != sizeof(elf) || elf.e_phentsize != sizeof(struct proghdr) ||
        elf.e_phoff < sizeof(elf) || elf.e_phoff > size || elf.e_phnum == 0 ||
        elf.e_phnum > (size - elf.e_phoff) / sizeof(struct proghdr) || (elf.e_entry & 1))
    {
        return -E_INVAL_ELF;
    }

    struct mm_struct *mm = mm_create();
    if (mm == NULL)
    {
        return -E_NO_MEM;
    }
    int ret = setup_pgdir(mm);
    if (ret != 0)
    {
        goto bad_mm;
    }

    bool entry_is_executable = 0;
    for (size_t i = 0; i < elf.e_phnum; i++)
    {
        struct proghdr ph;
        memcpy(&ph, binary + elf.e_phoff + i * sizeof(ph), sizeof(ph));
        if (ph.p_type != ELF_PT_LOAD)
        {
            continue;
        }
        ret = load_elf_segment(mm, binary, size, &ph);
        if (ret != 0)
        {
            goto bad_pgdir;
        }
        if ((ph.p_flags & ELF_PF_X) && elf.e_entry >= ph.p_va &&
            elf.e_entry - ph.p_va < ph.p_memsz)
        {
            entry_is_executable = 1;
        }
    }
    if (!entry_is_executable)
    {
        ret = -E_INVAL_ELF;
        goto bad_pgdir;
    }

    // 当前实验不按需调页，预先映射可用的用户栈空间，留出底部保护页。
    uintptr_t stack_bottom = USTACKTOP - USTACKSIZE;
    ret = mm_map(mm, stack_bottom, USTACKSIZE, VM_READ | VM_WRITE | VM_STACK, NULL);
    if (ret != 0)
    {
        goto bad_pgdir;
    }
    for (uintptr_t va = stack_bottom + PGSIZE; va < USTACKTOP; va += PGSIZE)
    {
        struct Page *page = pgdir_alloc_page(mm->pgdir, va, PTE_U | PTE_R | PTE_W);
        if (page == NULL)
        {
            ret = -E_NO_MEM;
            goto bad_pgdir;
        }
        memset(page2kva(page), 0, PGSIZE);
    }

    // 装载成功后一次性交付新地址空间和用户态现场。
    bool intr_flag;
    local_intr_save(intr_flag);
    struct trapframe *tf = current->tf;
    uintptr_t status = read_csr(sstatus);
    memset(tf, 0, sizeof(*tf));
    tf->gpr.sp = USTACKTOP;
    tf->epc = elf.e_entry;
    tf->status = (status | SSTATUS_SPIE) & ~(SSTATUS_SPP | SSTATUS_SIE);

    mm_count_inc(mm);
    current->mm = mm;
    current->pgdir = PADDR(mm->pgdir);
    lsatp(current->pgdir);
    flush_tlb();
    asm volatile("fence.i" ::: "memory");
    local_intr_restore(intr_flag);
    return 0;

bad_pgdir:
    exit_mmap(mm);
    put_pgdir(mm);
bad_mm:
    mm_destroy(mm);
    return ret;
}

//lab5 function2 YOUR CODE: C 线的模块二函数待补充


//lab5 function3 YOUR CODE


//lab5 function4 YOUR CODE


#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)


//lab5 function2 YOUR CODE：以下 init_main 为 lab4 版本，需改为创建 user_main 并 do_wait
// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg)
{
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));
    cprintf("To U: \"%s\".\n", (const char *)arg);
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");
    return 0;
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




