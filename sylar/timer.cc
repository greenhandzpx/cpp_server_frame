#include "timer.h"
#include "util.h"

#include <utility>

namespace sylar {

    bool Timer::Comparator::operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const
    {
        if (!lhs && !rhs) {
            return false;
        }
        if (!lhs) {
            return true;
        }
        if (!rhs) {
            return false;
        }
        if (lhs->m_next < rhs->m_next) {
            return true;
        }
        if (lhs->m_next > rhs->m_next) {
            return true;
        }
        // 否则比较地址
        return lhs.get() < rhs.get();
    }
    Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager *manager)
        : m_ms(ms),
          m_cb(std::move(cb)),
          m_manager(manager)
    {
        m_next = Get_current_ms() + m_ms;
    }

    Timer::Timer(uint64_t next_time)
        : m_next(next_time)
    {}

    bool Timer::cancel()
    {
        TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
        if (m_cb) {
            m_cb = nullptr;
            auto it = m_manager->m_timers.find(shared_from_this());
            if (it != m_manager->m_timers.end()) {
                m_manager->m_timers.erase(it);
            }
            return true;
        }
        return false;
    }

    bool Timer::refresh()
    {
        TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
        if (!m_cb) {
            return false;
        }
        auto it = m_manager->m_timers.find(shared_from_this());
        if (it == m_manager->m_timers.end()) {
            return false;
        }
        // 先删，再添加，因为set的迭代器是const的，直接修改会影响数据结构
        m_manager->m_timers.erase(it);
        m_next = Get_current_ms() + m_ms;
        m_manager->add_timer(shared_from_this());
        //m_manager->m_timers.insert(shared_from_this());
        return true;
    }

    bool Timer::reset(uint64_t ms, bool from_now)
    {
        if (ms == m_ms && !from_now) {
            // 如果执行周期一样并且不需要从当前开始改变
            return true;
        }
        TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);

        auto it = m_manager->m_timers.find(shared_from_this());
        if (it == m_manager->m_timers.end()) {
            return false;
        }
        m_manager->m_timers.erase(shared_from_this());
        if (from_now) {
            m_next = Get_current_ms() + ms;
        } else {
            // 否则先求出当初添加time时的时间点，再加上新的执行周期
            m_next = m_next - m_ms + ms;
        }
        m_ms = ms;
        m_manager->add_timer(shared_from_this());
        //m_manager->m_timers.insert(shared_from_this());
        return true;
    }

    Timer::ptr TimerManager::add_timer(Timer::ptr timer)
    {
        RWMutexType::WriteLock lock(m_mutex);
        auto it = m_timers.insert(timer).first;
        bool at_front = (it == m_timers.begin());
        lock.unlock();

        if (at_front) {
            on_timer_insert_at_front();
        }
        return timer; // 把新添加的timer返回出去，以防需要做进一步操作
    }

    Timer::ptr TimerManager::add_timer(uint64_t ms, std::function<void()> cb, bool recurring)
    {
        Timer::ptr timer(new Timer(ms, std::move(cb), recurring, this));
        return add_timer(timer);
    }

    // 该函数将cb封装起来
    static void Cond_cb(const std::weak_ptr<void>& weak_con, const std::function<void()>& cb)
    {
        auto ptr = weak_con.lock();
        if (ptr) {
            // 如果该条件还存在，才调用cb
            cb();
        }
    }

    Timer::ptr TimerManager::add_condition_timer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond,
                                                 bool recurring)
    {
         return add_timer(ms, std::bind(&Cond_cb, weak_cond, cb), recurring);
    }

    uint64_t TimerManager::get_next_timeout()
    {
        RWMutexType::ReadLock lock(m_mutex);
        if (m_timers.empty()) {
            return ~0ull; // 返回最大时间
        }

        const Timer::ptr& next = *m_timers.begin();
        uint64_t now_ms = Get_current_ms();
        if (now_ms >= next->m_next) {
            // 当前时间已经超过了执行时间
            return 0;
        } else {
            return next->m_next - now_ms;
        }
    }

    void TimerManager::list_expired_cbs(std::vector<std::function<void()>> &cbs)
    {
        uint64_t now_ms = Get_current_ms();
        std::vector<Timer::ptr> expired_timers;
        {
            RWMutexType::ReadLock lock(m_mutex);
            if (m_timers.empty()) {
                return;
            }
        }
        RWMutexType::WriteLock lock(m_mutex);
        Timer::ptr now_timer(new Timer(now_ms));
        // 找出第一个执行时间大于当前时刻的timer
        auto it = m_timers.upper_bound(now_timer);
        if (it == m_timers.begin()) {
            // 说明所有的计时器的执行时间都没到
            return;
        }

        expired_timers.insert(expired_timers.begin(), m_timers.begin(), --it);
        m_timers.erase(m_timers.begin(), it);
        // 扩容
        cbs.reserve(expired_timers.size());

        for (auto& timer: expired_timers) {
            cbs.push_back(timer->m_cb);
            if (timer->m_recurring) {
                // 如果是循环计时器,则重新放回去
                timer->m_next = now_ms + timer->m_ms;
                add_timer(timer);
            } else {
                timer->m_cb = nullptr;
            }
        }
    }

    bool TimerManager::detect_clock_rollover(uint64_t now_ms)
    {
        bool rollover = false;
        if (now_ms < m_previous_time &&
                now_ms < m_previous_time - 60 * 60 *1000)
        {
            rollover = true;
        }
        m_previous_time = now_ms;
        return rollover;
    }

}