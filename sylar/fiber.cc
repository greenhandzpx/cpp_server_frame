//
// Created by greenhandzpx on 1/25/22.
//

#include "fiber.h"
#include "config.h"
#include "macro.h"
#include "log.h"
#include "scheduler.h"

#include <atomic>
#include <utility>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static std::atomic<uint64_t> s_fiber_id{0};
static std::atomic<uint64_t> s_fiber_count{0};

// 当前线程中执行的协程
static thread_local Fiber *t_fiber = nullptr;
// 该线程的主协程
static thread_local std::shared_ptr<Fiber::ptr> t_threadFiber = nullptr;

// 每个协程默认拥有的栈空间大小
static ConfigVar<uint32_t>::ptr g_fiber_stack_size =
    Config::Lookup<uint32_t>("fiber.stack_size", 1024 * 1024, "fiber stack size");

// 以malloc的方式分配栈空间
class Malloc_Stack_Allocator {
public:
    static void *Alloc(size_t size)
    {
        return malloc(size);
    }
    static void Dealloc(void *vp/*size_t size*/)
    {
        free(vp);
    }
};

using Stack_Allocator = Malloc_Stack_Allocator;

// 主协程的构造函数(主协程用来创建子协程）
Fiber::Fiber()
{
    m_state = EXEC;
    SetThis(this);

    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "GetContext Error!")
    }

    ++s_fiber_count;

    SYLAR_LOG_INFO(g_logger) << "Main Fiber created ! id=" << m_id;

}

Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool use_caller)
    : m_id(++s_fiber_id), m_cb(std::move(cb))
{
    ++s_fiber_count;
    // 默认使用配置文件中的栈大小
    m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();

    m_stack = Stack_Allocator::Alloc(m_stacksize);

    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "GetContext Error!")
    }
    // 创建一个协程就需要创建一个上下文
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    // 每创建一个子协程，就把该子协程的上下文绑定到mainFunc上，从而每当切换
    // 到该子协程的上下文时，就会调用mainFunc
    // use_caller表示之后协程调度的时候会把调度器所在线程算进线程池里
    if (!use_caller) {
        makecontext(&m_ctx, &MainFunc, 0);
    } else {
        makecontext(&m_ctx, &Caller_MainFunc, 0);
    }
    m_state = INIT;

    SYLAR_LOG_INFO(g_logger) << "One sub fiber created ! id=" << m_id;
}

Fiber::~Fiber()
{
    --s_fiber_count;
    if (m_stack) {
        SYLAR_LOG_DEBUG(g_logger) << "One sub fiber will die, id=" << m_id
                                  << ", state=" << m_state;
        SYLAR_ASSERT(m_state == INIT ||
            m_state == TERM ||
            m_state == EXCPT)
        Stack_Allocator::Dealloc(m_stack);
    } else {
        SYLAR_ASSERT(!m_cb)
        SYLAR_ASSERT(m_state == EXEC)

        Fiber *cur = t_fiber;
        // 如果析构的是当前线程的协程
        if (cur == this) {
            SetThis(nullptr);
        }
    }
    SYLAR_LOG_INFO(g_logger) << "One fiber died: id=" << m_id;
}

// 重置当前的协程（必须是子协程）
void Fiber::reset(std::function<void()> cb)
{
    SYLAR_ASSERT(m_stack);
    SYLAR_ASSERT(m_state == INIT ||
        m_state == TERM ||
        m_state == EXCPT)
    m_cb = std::move(cb);
    // 拿出当前的上下文，并与mainFunc绑定
    if (getcontext(&m_ctx)) {
        SYLAR_ASSERT2(false, "GetContext Error!")
    }
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    makecontext(&m_ctx, &MainFunc, 0);
    m_state = INIT;

}
void Fiber::call()
{
    SetThis(this);
    SYLAR_ASSERT(m_state != EXEC)
    m_state = EXEC;
    if (swapcontext(&((*t_threadFiber)->m_ctx), &m_ctx)) {
        SYLAR_ASSERT2(false, "Call error!")
    }
}
void Fiber::back()
{
    SetThis(t_threadFiber->get());
    if (swapcontext(&m_ctx, &(*t_threadFiber)->m_ctx)) {
        SYLAR_ASSERT2(false, "Back to main fiber error.")
    }
}
// 将协程切换到当前线程执行
void Fiber::swapIn()
{
    SetThis(this);
    SYLAR_ASSERT(m_state != EXEC)
    m_state = EXEC;
//        if (swapcontext(&(*t_threadFiber)->m_ctx, &m_ctx)) {
//            SYLAR_ASSERT2(false, "SwapContext error!")
//        }
//        if (Scheduler::GetMainFiber() == t_threadFiber.get().get()) {
//            SYLAR_LOG_DEBUG(g_logger) << "Equal !";
//        }

    // 调用swapIn函数的协程应该是root_fiber(或者其他线程），所以将root_fiber切出去，将this指针对应的fiber切进来
    if (swapcontext(&(Scheduler::GetMainFiber()->m_ctx), &m_ctx)) {
        SYLAR_ASSERT2(false, "SwapContext error!")
    }
}
// 将协程切换到后台执行
void Fiber::swapOut()
{
    SetThis(Scheduler::GetMainFiber());

//        if (swapcontext(&m_ctx, &(*t_threadFiber)->m_ctx)) {
//            SYLAR_ASSERT2(false, "SwapContext error!")
//        }
    if (swapcontext(&m_ctx, &(Scheduler::GetMainFiber()->m_ctx))) {
        SYLAR_ASSERT2(false, "SwapContext error!")
    }

}

void Fiber::setState(State state)
{
    //SYLAR_LOG_DEBUG(g_logger) << "Change state to " << state << ", id=" << m_id;
    m_state = state;
}
// 设置当前线程执行的协程
void Fiber::SetThis(Fiber *f)
{
    t_fiber = f;
}
// 返回当前线程执行的协程
Fiber::ptr Fiber::GetThis()
{
    if (t_fiber) {
        return t_fiber->shared_from_this();
    }
    // 如果此时线程内没有执行的协程，则创建一个主协程
    // 成员函数内可以调用私有构造函数, 且该构造函数里调用了SetThis
    Fiber::ptr main_fiber(new Fiber);
    SYLAR_ASSERT(t_fiber == main_fiber.get())
    t_threadFiber = std::make_shared<Fiber::ptr>(main_fiber);
    return t_fiber->shared_from_this();
}

// 协程切换到后台，并设置为ready状态
void Fiber::Yield_to_Ready()
{
    Fiber::ptr cur = GetThis();
    cur->m_state = READY;
    cur->swapOut();
}

// 协程切换到后台，并设置为hold状态
void Fiber::Yield_to_Hold()
{
    Fiber::ptr cur = GetThis();
    SYLAR_LOG_DEBUG(g_logger) << "Change state to HOLD, id=" << cur->getId();
    cur->m_state = HOLD;
    cur->swapOut();
}

// 总协程数
uint64_t Fiber::Total_Fibers()
{
    return s_fiber_count;
}

// 普通协程的回调函数（除了mainFiber外）
void Fiber::MainFunc()
{
    // 由于协程一般是堆对象，所以应当手动控制其析构时间
    // 当前协程被cur引用了，所以一直不会析构
    Fiber::ptr cur = GetThis();
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        SYLAR_LOG_DEBUG(g_logger) << "Change state to TERM, id=" << cur->getId();
        cur->m_state = TERM;
    } catch (std::exception &ex) {
        cur->m_state = EXCPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber except: " << ex.what();
    } catch (...) {
        cur->m_state = EXCPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber except.";
    }

    // 取出裸指针，把智能指针释放掉，然后再将当前协程挂起，以便于当前协程的析构
    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->swapOut();

    SYLAR_ASSERT2(false, "Should not reach here. id=" + std::to_string(raw_ptr->getId()))
}

// 在协程调度中，处于调度器所在线程的root_fiber的回调函数（只有当把该线程算进去才会有）
void Fiber::Caller_MainFunc()
{
    // 由于协程一般是堆对象，所以应当手动控制其析构时间
    // 当前协程被cur引用了，所以一直不会析构
    Fiber::ptr cur = GetThis();
    try {
        cur->m_cb();
        cur->m_cb = nullptr;
        cur->m_state = TERM;
    } catch (std::exception &ex) {
        cur->m_state = EXCPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber except: " << ex.what();
    } catch (...) {
        cur->m_state = EXCPT;
        SYLAR_LOG_ERROR(g_logger) << "Fiber except.";
    }

    // 取出裸指针，把智能指针释放掉，然后再将当前协程挂起，以便于当前协程的析构
    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->back();

    SYLAR_ASSERT2(false, "Should not reach here. caller_id=" + std::to_string(raw_ptr->getId()))
}

uint64_t Fiber::GetFiberId()
{
    if (t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}

}