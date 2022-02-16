#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include <memory>
#include <set>

#include "thread.h"


namespace sylar {

    class TimerManager;

    class Timer: public std::enable_shared_from_this<Timer> {
    friend class TimerManager;

    public:
        typedef std::shared_ptr<Timer> ptr;

        // 取消该计时器
        bool cancel();
        // 刷新该计时器的下一次执行时间
        bool refresh();
        // 重置计时器（执行周期或者执行时间）
        bool reset(uint64_t ms, bool from_now);

    private:
        Timer(uint64_t ms, std::function<void()> cb,
              bool recurring, TimerManager* manager);
        explicit Timer(uint64_t next_time); // 该构造函数用来构造一个用来从set里取timer的timer


    private:
        bool m_recurring = false; // 是否循环使用定时器
        uint64_t m_ms = 0; // 执行周期
        uint64_t m_next = 0; // 下一次执行的具体时间点
        std::function<void()> m_cb;
        TimerManager* m_manager = nullptr;
    private:
        struct Comparator {
            bool operator()(const Timer::ptr& lhs,
                        const Timer::ptr& rhs) const;
        };
    };

    class TimerManager {
    friend class Timer;

    public:
        typedef RWMutex RWMutexType;

        TimerManager() = default;
        virtual ~TimerManager() = default;

        Timer::ptr add_timer(uint64_t ms, std::function<void()> cb,
                             bool recurring = false);
        Timer::ptr add_timer(Timer::ptr timer, RWMutexType::WriteLock& lock);
        Timer::ptr add_condition_timer(uint64_t ms, std::function<void()> cb,
                                       std::weak_ptr<void> weak_cond,
                                       bool recurring = false);

        uint64_t get_next_timeout();
        // 找出所有已经超时的cb
        void list_expired_cbs(std::vector<std::function<void()>>& cbs);
    protected:
        // 当新添加的定时器处于set的第一位（下一次触发时间最早）
        virtual void on_timer_insert_at_front() = 0;

        bool has_timer()
        {
            return !m_timers.empty();
        }

    private:
        bool detect_clock_rollover(uint64_t now_ms);
    private:
        RWMutexType m_mutex;
        std::set<Timer::ptr, Timer::Comparator> m_timers;
        uint64_t m_previous_time = 0;
    };


    static void Cond_cb(const std::weak_ptr<void>& weak_con, const std::function<void()>& cb);
}

#endif