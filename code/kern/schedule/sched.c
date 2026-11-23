#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void wakeup_proc(struct proc_struct *proc)
{
    assert(proc->state != PROC_ZOMBIE);
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (proc->state != PROC_RUNNABLE)
        {
            proc->state = PROC_RUNNABLE;
            proc->wait_state = 0;
        }
        else
        {
            warn("wakeup runnable process.\n");
        }
    }
    local_intr_restore(intr_flag);
}

//LAB4 function3 2412737
// schedule - 从 proc_list 中选出下一个 PROC_RUNNABLE 线程并切换过去（FIFO/轮转）
void
schedule(void)
{
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;
    // 选择与切换期间关中断，避免链表遍历、current 与现场切换被打断。
    local_intr_save(intr_flag);
    {
        // 当前线程已响应本次调度请求。
        current->need_resched = 0;
        // idleproc 不在 proc_list 中：从链表头开始找；
        // 其他线程从自身的下一个节点开始找，实现轮转，避免总是选中同一线程。
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        do
        {
            // 跳过链表头哨兵节点，它不属于任何 proc_struct。
            if ((le = list_next(le)) != &proc_list)
            {
                next = le2proc(le, list_link);
                if (next->state == PROC_RUNNABLE)
                {
                    break;
                }
            }
        } while (le != last);
        // 绕完一圈仍没有就绪线程时，退回 idleproc 保底。
        if (next == NULL || next->state != PROC_RUNNABLE)
        {
            next = idleproc;
        }
        // runs 统计线程被调度选中的次数。
        next->runs++;
        if (next != current)
        {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}
