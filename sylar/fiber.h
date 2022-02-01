//
// Created by greenhandzpx on 1/25/22.
//

#ifndef SYLAR_FIBER_H
#define SYLAR_FIBER_H

#include <memory>
#include <functional>
#include <ucontext.h>
#include "thread.h"
//#include "scheduler.h"


namespace sylar {

    class Fiber: public std::enable_shared_from_this<Fiber> {
    friend class Scheduler;
    public:
        typedef std::shared_ptr<Fiber> ptr;

        enum State {
           INIT,
           HOLD,
           EXEC,
           TERM,
           READY,
           EXCPT
        };
    private:
        Fiber();

    public:
        explicit Fiber(std::function<void()> cb, size_t stacksize = 0,
                       bool use_caller = false);
        ~Fiber();

        // 重置协程函数，并重置状态
        // 只有在INIT、TERM期间发生
        void reset(std::function<void()> cb);

        void call();

        void back();
        // 切换到当前协程执行
        void swapIn();
        // 切换到后台执行
        void swapOut();
        // 返回该协程的id
        uint64_t getId() const { return m_id; }
        // 返回该协程的状态
        State getState() const { return m_state; }
        void setState(State state)
        {
            m_state = state;
        }
    public:
        // 设置当前协程
        static void SetThis(Fiber* f);
        // 返回当前协程
        static Fiber::ptr GetThis();

        static void Yield_to_Ready();
        // 协程切换到后台，并设置为hold状态
        static void Yield_to_Hold();
        // 总协程数
        static uint64_t Total_Fibers();
        
        // 用来调用当前线程执行的协程的cb
        static void MainFunc();

        static void Caller_MainFunc();

        // 返回当前线程执行的协程的id
        static uint64_t GetFiberId();

    private:
        uint64_t m_id = 0;
        uint32_t m_stacksize = 0;
        State m_state = INIT;

        ucontext_t m_ctx{};
        void* m_stack = nullptr;

        std::function<void()> m_cb;

    public:

    };
}
#endif //SYLAR_FIBER_H
