#include "scheduler.h"
#include "log.h"
#include "macro.h"

namespace sylar {

    static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

    static thread_local Scheduler* t_scheduler = nullptr;
    // 当前线程的主协程
    static thread_local Fiber* t_fiber = nullptr;

    Scheduler::Scheduler(size_t thread_count, bool use_caller, const std::string &name)
        : m_name(name)
    {
        SYLAR_ASSERT(thread_count > 0)

        if (use_caller) {
            // 若将协程调度器所在的线程算进去，则在给定线程数上减一
            sylar::Fiber::GetThis();
            --thread_count;

            // 确保当前还未存在任何协程调度器
            SYLAR_ASSERT(GetThis() == nullptr)
            t_scheduler = this;

            m_root_fiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
            Thread::SetName(name);

            t_fiber = m_root_fiber.get();
            m_root_thread_id = GetThreadId();
            m_thread_ids.emplace_back(m_root_thread_id);

        } else {
            // 如果不算协程调度器所在的线程，则直接赋值为-1
            m_root_thread_id = -1;
        }
        m_thread_count = thread_count;
    }

    Scheduler::~Scheduler()
    {
        SYLAR_ASSERT(m_stopping)
        if (t_scheduler == this) {
            t_scheduler = nullptr;
        }
    }

    Scheduler* Scheduler::GetThis()
    {
        return t_scheduler;
    }

    Fiber* Scheduler::GetMainFiber()
    {
        return t_fiber;
    }

    void Scheduler::start()
    {
        MutexType::Lock lock(m_mutex);
        if (!m_stopping) {
            // 如果已经开始了
            return;
        }
        SYLAR_LOG_DEBUG(g_logger) << "Scheduler starts";
        m_stopping = false;
        // 初始化时线程池必为空
        SYLAR_ASSERT(m_threads.empty())

        m_threads.resize(m_thread_count);
        for (size_t i = 0; i < m_thread_count; ++i) {
            // 每个线程都去执行run方法
            m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this),
                                          m_name + "_" + std::to_string(i)));
            m_thread_ids.emplace_back(m_threads[i]->getId());
        }

//        if (m_root_fiber) {
//            m_root_fiber->call();
//        }

    }

    void Scheduler::stop()
    {
        m_auto_stop = true;
        if (m_root_fiber && m_thread_count == 0
                         && (m_root_fiber->getState() == Fiber::TERM
                         || m_root_fiber->getState() == Fiber::INIT))
        {
            SYLAR_LOG_INFO(g_logger) << this->m_name << " stopped";
            m_stopping = true;

            if (stopping()) {
                return;
            }
        }

        bool exit_on_this_fiber = false;
        if (m_root_thread_id != -1) {
            // 如果有将协程调度器所在线程算进去
            // 那么必须在协程调度器所在线程内进行停止操作
            SYLAR_ASSERT(GetThis() == this)
        } else {
            SYLAR_ASSERT(GetThis() != this)
        }

        m_stopping = true;
        // 每个线程都去tickle一次，通知他们停止
        for (size_t i = 0; i < m_thread_count; ++i) {
            tickle();
        }

        if (m_root_fiber) {
            tickle();
        }

        if (m_root_fiber) {
            // 在这个地方才调用协程调度器所在线程的子协程的回调
            if (!stopping()) {
                SYLAR_LOG_DEBUG(g_logger) << "Now call the root fiber";
                m_root_fiber->call();
            }
        }

        std::vector<Thread::ptr> thrs;
        {
            MutexType::Lock lock(m_mutex);
            thrs.swap(m_threads);
        }
        // 等待所有线程跑完
        for (auto& i: thrs) {
            i->join();
        }

        if (stopping()) {
            return;
        }

    }
    void Scheduler::setThis()
    {
        t_scheduler = this;
    }

    void Scheduler::run()
    {
        // 每个新线程都会运行该函数
        // 设置当前线程的scheduler
        setThis();
        if (sylar::GetThreadId() != m_root_thread_id) {
            // 排除掉协程调度器所在线程的任务协程
            t_fiber = Fiber::GetThis().get();
        }

        // 空闲协程，用来占住cpu
        Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
        Fiber::ptr cb_fiber;

        Fiber_and_Thread ft;
        while (true) {
            //SYLAR_LOG_INFO(g_logger) << "Loop !";
            ft.reset();
            bool tickle_me = false;

            MutexType::Lock lock(m_mutex);
            auto it = m_fibers.begin();
            while (it != m_fibers.end()) {
                if (it->thread != -1 &&
                    it->thread != GetThreadId()) {
                    // 如果该协程任务指定了线程且不是本线程
                    // 则发出信号给其他线程，然后跳过该协程
                    ++it;
                    tickle_me = true;
                    continue;
                }

                SYLAR_ASSERT(it->fiber || it->cb)
                if (it->fiber && it->fiber->getState() == Fiber::EXEC) {
                    // 该协程任务正在执行中则跳过
                    ++it;
                    continue;
                }

                ft = *it;
                m_fibers.erase(it);

                break;
            }
            // 此处注意解锁，不然后面idle协程调用stopping()函数判断的时候
            // 拿不到锁，导致死锁
            lock.unlock();


            if (tickle_me) {
                tickle();
            }
            if (ft.fiber && ft.fiber->getState() != Fiber::TERM) {
                // 如果是协程并且该协程还没有结束,
                // 则执行该协程，标记该线程为活跃状态
                ++m_active_thread_count;
                ft.fiber->swapIn();
                --m_active_thread_count;

                if (ft.fiber->getState() == Fiber::READY) {
                    // 如果该协程调用了“Yield_to_Ready"，则说明还有任务要做
                    // 则重新把该协程丢进协程队列
                    schedule(ft.fiber);
                } else if (ft.fiber->getState() != Fiber::TERM
                        || ft.fiber->getState() != Fiber::EXCPT)
                {
                    // 如果该协程还没有结束，则设为挂起状态
                    ft.fiber->setState(Fiber::HOLD);
                }
                ft.reset();

            } else if (ft.cb) {
                // 如果是回调
                // 则用cb_fiber来接收这个回调
                if (cb_fiber) {
                    // 如果cb_fiber已经初始化过
                    cb_fiber->reset(ft.cb);
                } else {
                    cb_fiber.reset(new Fiber(ft.cb));
                }
                ft.reset();
                ++m_active_thread_count;
                cb_fiber->swapIn();
                --m_active_thread_count;

                if (cb_fiber->getState() == Fiber::READY) {
                    schedule(cb_fiber);
                } else if (cb_fiber->getState() == Fiber::TERM
                        || cb_fiber->getState() == Fiber::EXCPT)
                {
                    // 该协程已经结束
                    cb_fiber->reset(nullptr);
                } else {
                    cb_fiber->setState(Fiber::HOLD);
                }

            } else {
                SYLAR_LOG_DEBUG(g_logger) << "idle fiber state: " << idle_fiber->getState();
                // 如果啥都不是，则用空闲协程占着cpu
                if (idle_fiber->getState() == Fiber::TERM) {
                    SYLAR_LOG_DEBUG(g_logger) << "idle fibber died";
                    break;
                }

                ++m_idle_thread_count;
                idle_fiber->swapIn();
                --m_idle_thread_count;
                if (idle_fiber->getState() != Fiber::TERM
                 && idle_fiber->getState() != Fiber::EXCPT)
                {
                    idle_fiber->setState(Fiber::HOLD);
                }
            }


        }

    }

    void Scheduler::idle()
    {

        SYLAR_LOG_DEBUG(g_logger) << "idle fiber is running.";
        while(!stopping()) {

            SYLAR_LOG_DEBUG(g_logger) << "idle fiber is running in loop.";
            sylar::Fiber::Yield_to_Hold();
        }
        SYLAR_LOG_DEBUG(g_logger) << "idle fiber is running after loop.";
        //sylar::Fiber::Yield_to_Hold();
    }
    void Scheduler::tickle()
    {

    }
    bool Scheduler::stopping()
    {
        MutexType::Lock lock(m_mutex);
        return m_auto_stop && m_stopping
                           && m_fibers.empty()
                           && m_active_thread_count == 0;
    }


}