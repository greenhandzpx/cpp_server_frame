//
// Created by greenhandzpx on 1/28/22.
//

#ifndef SYLAR_SCHEDULER_H
#define SYLAR_SCHEDULER_H

#include <memory>
#include <utility>
#include <vector>
#include <list>
#include <string>
#include <atomic>
#include <variant>

#include "fiber.h"
#include "thread.h"

namespace sylar {

    class Scheduler {
    public:
        typedef std::shared_ptr<Scheduler> ptr;
        typedef Mutex MutexType;

        explicit Scheduler(size_t threads = 1, bool use_caller = true,
                  const std::string& name = "");
        virtual ~Scheduler();

        const std::string& getName() { return m_name; }

        void start();
        void stop();

        // 单个加入队列
        template<typename Fiber_or_Cb>
        void schedule(Fiber_or_Cb fc, int thread = -1)
        {
            bool need_tickle = false;
            {
                MutexType::Lock lock(m_mutex);
                need_tickle = schedule_no_lock(fc, thread);
            }
            if (need_tickle) {
                tickle();
            }
        }
        // 批量加入队列
        template<typename Input_Iterator>
        void schedule(Input_Iterator begin, Input_Iterator end)
        {
            bool need_tickle = false;
            {
                MutexType::Lock lock(m_mutex);
                while (begin != end) {
                    // 解引用得到元素，再取地址，从而使用第二个构造函数（swap版本）
                    need_tickle = schedule_no_lock(&*begin) || need_tickle;
                    ++begin;
                }
            }
            if (need_tickle) {
                tickle();
            }
        }

    protected:
        virtual void tickle();
        // 协程调度的主函数
        void run();
        virtual bool stopping();
        virtual void idle();

        void setThis();

    private:
        template<typename Fiber_or_Cb>
        bool schedule_no_lock(Fiber_or_Cb fc, int thread = -1)
        {
            // 若m_fibers为空，说明此时没有协程任务，则插入一个任务并返回true
            bool need_tickle = m_fibers.empty();
            Fiber_and_Thread ft(fc, thread);
            //if (ft.fiber || ft.cb)
            if (std::holds_alternative<Fiber::ptr>(ft.fiber_or_cb) ||
                    std::holds_alternative<std::function<void()>>(ft.fiber_or_cb)){
                m_fibers.push_back(ft);
            }
            return need_tickle;

        }

    public:
        // 获得当前的协程调度器
        static Scheduler* GetThis();
        static Fiber* GetMainFiber();

    private:
        struct Fiber_and_Thread {
            // 可以存一个协程，也可以只存一个函数指针
            std::variant<Fiber::ptr, std::function<void()>> fiber_or_cb;
//            Fiber::ptr fiber;
//            std::function<void()> cb;
            int thread;

            Fiber_and_Thread(Fiber::ptr f, int thr)
                : fiber_or_cb(std::move(f)), thread(thr)
            {}
            // 此处传智能指针的指针的目的是：当我们不需要传入实参的引用时，可以通过swap,
            // 从而使协程对象的引用计数不发生改变
            Fiber_and_Thread(Fiber::ptr* f, int thr)
                : thread(thr)
            {
                std::get<0>(fiber_or_cb).swap(*f);
                //fiber.swap(*f);
            }

            Fiber_and_Thread(std::function<void()> f, int thr)
                : fiber_or_cb(std::move(f)), thread(thr)
            {}

            Fiber_and_Thread(std::function<void()>* f, int thr)
                : thread(thr)
            {
                std::get<1>(fiber_or_cb).swap(*f);
                //cb.swap(*f);
            }
            // 定义默认构造函数的原因是：
            // stl容器初始化非空时，里面的元素都是以默认构造函数构造的
            Fiber_and_Thread(): thread(-1)
            {}

            void reset()
            {
                fiber_or_cb.emplace<0>(nullptr);
                fiber_or_cb.emplace<1>(nullptr);
//                fiber = nullptr;
//                cb = nullptr;
                thread = -1;
            }
        };

    private:
        MutexType m_mutex;
        // 线程池
        std::vector<Thread::ptr> m_threads;
        // 协程队列（可以是协程，也可以是函数指针）
        std::list<Fiber_and_Thread> m_fibers;
        std::string m_name;
        Fiber::ptr m_root_fiber; // 创建协程调度器的线程中执行run方法的协程

    protected:
        std::vector<int> m_thread_ids;
        size_t m_thread_count = 0; // 协程调度器支配的线程数
        std::atomic<size_t> m_active_thread_count{0};
        std::atomic<size_t> m_idle_thread_count{0};
        bool m_stopping = true;
        bool m_auto_stop = true;
        int m_root_thread_id = 0; // 协程调度器所在线程的id

    };
}
#endif //SYLAR_SCHEDULER_H
