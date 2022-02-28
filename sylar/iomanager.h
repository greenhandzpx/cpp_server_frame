//
// Created by greenhandzpx on 2/1/22.
//

#ifndef SYLAR_IOMANAGER_H
#define SYLAR_IOMANAGER_H

#include "scheduler.h"
#include "timer.h"

namespace sylar {
class IOManager : public Scheduler, public TimerManager {
public:
    typedef std::shared_ptr<IOManager::ptr> ptr;
    typedef RWMutex RWMutexType;

    enum Event {
        NONE = 0x0,
        READ = 0x1, // EPOLLIN
        WRITE = 0x4  // EPOLLOUT
    };

private:
    struct FdContext {
        typedef Mutex MutexType;
        struct EventContext {
            Scheduler *scheduler = nullptr; // 执行该事件的调度器
//                std::variant<Fiber::ptr, std::function<void()>>
//                    fiber_or_cb;
            Fiber::ptr fiber;
            std::function<void()> cb;
        };

        EventContext &getContext(Event event);

        static void resetContext(EventContext &event_ctx);

        void triggerEvent(Event event);

        EventContext read; // 读事件
        EventContext write; // 写事件
        int fd; // 事件关联的句柄
        Event m_events = NONE; // 已经注册的事件
        MutexType m_mutex;
    };

public:
    explicit IOManager(size_t threads = 1, bool use_caller = true,
                       const std::string &name = "");
    ~IOManager() noexcept override;

    // 0 success, -1 error
    int add_event(int fd, Event event, std::function<void()>
    cb = nullptr);
    bool del_event(int fd, Event event);
    bool cancel_event(int fd, Event event);
    bool cancel_all(int fd);

public:
    static IOManager *GetThis();

protected:
    void context_resize(size_t size);

    bool stopping(uint64_t &timeout);

    void tickle() override;
    bool stopping() override;
    void idle() override;

    void on_timer_insert_at_front() override;

    //bool has_timer();

private:
    int m_epfd = 0; // epoll实例的fd
    int m_tickle_fds[2];

    std::atomic<size_t> m_pending_event_count{0};
    RWMutexType m_mutex;
    std::vector<FdContext *> m_fd_contexts;

};
}
#endif //SYLAR_IOMANAGER_H
