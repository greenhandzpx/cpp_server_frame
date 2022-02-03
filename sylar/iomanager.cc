#include "iomanager.h"
#include "log.h"
#include "macro.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

namespace sylar {

    static Logger::ptr g_logger = SYLAR_LOG_NAME("system");



    IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
            : Scheduler(threads, use_caller, name) {
        m_epfd = epoll_create(5000);
        SYLAR_ASSERT(m_epfd > 0)

        int rt = pipe(m_tickle_fds);
        SYLAR_ASSERT(rt > 0)

        epoll_event event{}; // 某个事件的配置
        memset(&event, 0, sizeof(epoll_event));
        event.events = EPOLLIN | EPOLLET; // 将事件设为可读和边缘触发
        event.data.fd = m_tickle_fds[0]; // 把事件句柄与管道的写入端连起来

        rt = fcntl(m_tickle_fds[0], F_SETFL, O_NONBLOCK); // 将写入端设成非阻塞
        SYLAR_ASSERT(rt > 0)

        // 将m_tickle_fds[0]句柄对应的事件，按照event中的设定，加入到m_epfd中，
        rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickle_fds[0], &event);
        SYLAR_ASSERT(rt > 0)

        context_resize(32);

        start();
    }

    IOManager::~IOManager() noexcept {
        stop();
        close(m_epfd);
        close(m_tickle_fds[0]);
        close(m_tickle_fds[1]);
        for (auto i: m_fd_contexts) {
            // 删除nullptr没有关系
            delete i;

        }
    }

    void IOManager::context_resize(size_t size) {
        m_fd_contexts.resize(size);
        for (int i = 0; i < m_fd_contexts.size(); ++i) {
            if (!m_fd_contexts[i]) {
                m_fd_contexts[i] = new FdContext;
                m_fd_contexts[i]->fd = i;
            }
        }
    }

    int IOManager::add_event(int fd, Event event, std::function<void()> cb) {
        FdContext *fd_ctx = nullptr;
        RWMutexType::ReadLock lock(m_mutex);
        if (m_fd_contexts.size() > fd) {
            fd_ctx = m_fd_contexts[fd];
            lock.unlock();
        } else {
            lock.unlock();
            RWMutexType::WriteLock lock2(m_mutex);
            while (m_fd_contexts.size() <= fd) {
                context_resize(m_fd_contexts.size() + m_fd_contexts.size() / 2);
            }
            fd_ctx = m_fd_contexts[fd];
        }

        FdContext::MutexType::Lock lock2(fd_ctx->m_mutex);
        if (fd_ctx->m_events & event) {
            // 如果新加的事件已经存在，则添加失败
            SYLAR_LOG_ERROR(g_logger) << "Add_event assert fd=" << fd
                                      << " fd_ctx.event=" << fd_ctx->m_events;
            SYLAR_ASSERT(!(fd_ctx->m_events & event))
            return -1;
        }

        // 如果该句柄已有事件，则是修改(mod)，否则是添加(add)
        int op = fd_ctx->m_events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
        // 定义一个新的事件设置（将新事件或上原有事件集合）
        epoll_event new_event_setting{};
        new_event_setting.events = EPOLLET | fd_ctx->m_events | event;
        new_event_setting.data.ptr = fd_ctx;

        // 将新加的事件添加到m_epfd中
        int rt = epoll_ctl(m_epfd, op, fd, &new_event_setting);
        if (rt) {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                      << op << ", " << fd << ", " << new_event_setting.events
                                      << "): " << rt << " (" << errno << ") (" << strerror(errno)
                                      << ") ";
            return -1;
        }

        ++m_pending_event_count;

        fd_ctx->m_events = (Event) (fd_ctx->m_events | event);
        FdContext::EventContext &event_ctx = fd_ctx->getContext(event);
        // 确保新添加的事件不存在调度器或者协程回调
        SYLAR_ASSERT(!event_ctx.scheduler
                     && !event_ctx.fiber
                     && !event_ctx.cb)

        // 将调度器设为当前线程的调度器
        event_ctx.scheduler = Scheduler::GetThis();
        if (cb) {
            // 有传入cb就直接给cb
            event_ctx.cb.swap(cb);
        } else {
            // 没有则将当前线程的协程赋给该事件
            event_ctx.fiber = Fiber::GetThis();
            // 确保当前协程正在执行
            SYLAR_ASSERT(event_ctx.fiber->getState() == Fiber::EXEC);
        }
        return 0;
    }

    bool IOManager::del_event(int fd, Event event) {
        RWMutexType::ReadLock lock(m_mutex);
        if (m_fd_contexts.size() <= fd) {
            SYLAR_LOG_ERROR(g_logger) << "Event fd=" << fd << " doesn't exist.";
            return false;
        }
        FdContext *fd_ctx = m_fd_contexts[fd];

        // 解开外面的锁，锁住里面的东西
        lock.unlock();
        FdContext::MutexType lock2(fd_ctx->m_mutex);
        if (!(fd_ctx->m_events & event)) {
            // 如果该fd对应的事件不存在
            SYLAR_LOG_ERROR(g_logger) << "Del_event assert fd=" << fd
                                      << " fd_ctx.event=" << fd_ctx->m_events;
            // SYLAR_ASSERT(fd_ctx->m_events & event)
            return false;
        }
        auto new_event = (Event) (fd_ctx->m_events & (~event));
        int op = new_event ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event new_event_setting{};
        new_event_setting.events = new_event | EPOLLET;
        new_event_setting.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &new_event_setting);
        if (rt) {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                      << op << ", " << fd << ", " << new_event_setting.events
                                      << "): " << rt << " (" << errno << ") (" << strerror(errno)
                                      << ") ";
            return false;
        }

        --m_pending_event_count;

        fd_ctx->m_events = new_event;
        FdContext::EventContext &event_ctx = fd_ctx->getContext(event);
        fd_ctx->resetContext(event_ctx);
        return true;
    }

    bool IOManager::cancel_event(int fd, Event event)
    {
        RWMutexType::ReadLock lock(m_mutex);
        if (m_fd_contexts.size() <= fd) {
            SYLAR_LOG_ERROR(g_logger) << "Event fd=" << fd << " doesn't exist.";
            return false;
        }
        FdContext *fd_ctx = m_fd_contexts[fd];

        // 解开外面的锁，锁住里面的东西
        lock.unlock();
        FdContext::MutexType lock2(fd_ctx->m_mutex);
        if (!(fd_ctx->m_events & event)) {
            // 如果该fd对应的事件不存在
            SYLAR_LOG_ERROR(g_logger) << "Del_event assert fd=" << fd
                                      << " fd_ctx.event=" << fd_ctx->m_events;
            // SYLAR_ASSERT(fd_ctx->m_events & event)
            return false;
        }
        auto new_event = (Event) (fd_ctx->m_events & (~event));
        int op = new_event ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event new_event_setting{};
        new_event_setting.events = new_event | EPOLLET;
        new_event_setting.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &new_event_setting);
        if (rt) {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                      << op << ", " << fd << ", " << new_event_setting.events
                                      << "): " << rt << " (" << errno << ") (" << strerror(errno)
                                      << ") ";
            return false;
        }


        //FdContext::EventContext &event_ctx = fd_ctx->getContext(event);
        fd_ctx->triggerEvent(event);
        --m_pending_event_count;
        return true;

    }

    bool IOManager::cancel_all(int fd)
    {
        RWMutexType::ReadLock lock(m_mutex);
        if (m_fd_contexts.size() <= fd) {
            SYLAR_LOG_ERROR(g_logger) << "Event fd=" << fd << " doesn't exist.";
            return false;
        }
        FdContext *fd_ctx = m_fd_contexts[fd];

        // 解开外面的锁，锁住里面的东西
        lock.unlock();
        FdContext::MutexType lock2(fd_ctx->m_mutex);
        if (!fd_ctx->m_events) {
            // 如果该fd对应的事件不存在
            SYLAR_LOG_ERROR(g_logger) << "Del_event assert fd=" << fd
                                      << " fd_ctx.event=" << fd_ctx->m_events;
            // SYLAR_ASSERT(fd_ctx->m_events & event)
            return false;
        }
        int op = EPOLL_CTL_DEL;
        int rt = epoll_ctl(m_epfd, op, fd, nullptr);
        if (rt) {
            SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                      << op << ", " << fd << ", " << "nullptr"
                                      << "): " << rt << " (" << errno << ") (" << strerror(errno)
                                      << ") ";
            return false;
        }

        if (fd_ctx->m_events & READ) {
            //FdContext::EventContext &event_ctx = fd_ctx->getContext(READ);
            fd_ctx->triggerEvent(READ);
            --m_pending_event_count;
        }

        if (fd_ctx->m_events & WRITE) {
            //FdContext::EventContext &event_ctx = fd_ctx->getContext(WRITE);
            fd_ctx->triggerEvent(WRITE);
            --m_pending_event_count;
        }

        // 虽然没有手动设置为0,但triggerEvent之后应该取消了
        SYLAR_ASSERT(fd_ctx->m_events == 0)
        return true;
    }

    IOManager *IOManager::GetThis() {
        // 基类指针转派生类
        return dynamic_cast<IOManager*>(Scheduler::GetThis());
    }

    IOManager::FdContext::EventContext&
    IOManager::FdContext::getContext(IOManager::Event event)
    {
        switch (event) {
            case READ:
                return read;
            case WRITE:
                return write;
            default:
                SYLAR_ASSERT2(false, "GetContext error!")
        }
    }

    void
    IOManager::FdContext::resetContext(IOManager::FdContext::EventContext &event_ctx)
    {
        event_ctx.scheduler = nullptr;
        event_ctx.fiber.reset();
        event_ctx.cb = nullptr;
    }

    void IOManager::FdContext::triggerEvent(IOManager::Event event)
    {
        // 先确认该事件存在
        SYLAR_ASSERT(m_events & event)
        // 清除该事件
        m_events = (Event)(m_events & (~event));
        EventContext& event_ctx = getContext(event);
        if (event_ctx.cb) {
            // 由于'Fiber_and_Thread'构造函数有指针的指针版本
            // 会调用swap将传入的实参交换为nullptr
            event_ctx.scheduler->schedule(&event_ctx.cb);
        } else {
            event_ctx.scheduler->schedule(&event_ctx.fiber);
        }
        event_ctx.scheduler = nullptr;
    }
}