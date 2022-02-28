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
    : Scheduler(threads, use_caller, name)
{
    m_epfd = epoll_create(5000);
    SYLAR_ASSERT(m_epfd > 0)

    int rt = pipe(m_tickle_fds);
    SYLAR_ASSERT(rt == 0)

    epoll_event event{}; // 某个事件的配置
    memset(&event, 0, sizeof(epoll_event));
    event.events = EPOLLIN | EPOLLET; // 将事件设为可读和边缘触发
    event.data.fd = m_tickle_fds[0]; // 把事件句柄与管道的读端连起来

    rt = fcntl(m_tickle_fds[0], F_SETFL, O_NONBLOCK); // 将读端设成非阻塞
    SYLAR_ASSERT(rt != -1)

    // 将m_tickle_fds[0]句柄对应的事件，按照event中的设定，加入到m_epfd中，
    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickle_fds[0], &event);
    SYLAR_ASSERT(rt == 0)

    context_resize(32);

    start();
}

IOManager::~IOManager() noexcept
{
    stop();
    close(m_epfd);
    close(m_tickle_fds[0]);
    close(m_tickle_fds[1]);
    for (auto i : m_fd_contexts) {
        // 删除nullptr没有关系
        delete i;

    }
}

void IOManager::context_resize(size_t size)
{
    m_fd_contexts.resize(size);
    for (int i = 0; i < m_fd_contexts.size(); ++i) {
        if (!m_fd_contexts[i]) {
            m_fd_contexts[i] = new FdContext;
            m_fd_contexts[i]->fd = i;
        }
    }
}

int IOManager::add_event(int fd, Event event, std::function<void()> cb)
{
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
    SYLAR_LOG_DEBUG(g_logger) << "fd_ctx->m_events: " << fd_ctx->m_events;
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
        SYLAR_ASSERT(event_ctx.fiber->getState() == Fiber::EXEC)
    }
    return 0;
}

bool IOManager::del_event(int fd, Event event)
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

IOManager *IOManager::GetThis()
{
    // 基类指针转派生类
    return dynamic_cast<IOManager *>(Scheduler::GetThis());
}

void IOManager::tickle()
{
    if (hasIdleThread()) {
        // 有空闲线程才处理任务
        // 发送消息唤醒
        int rt = write(m_tickle_fds[1], "T", 1);
        SYLAR_ASSERT(rt == 1)
    }
}

bool IOManager::stopping(uint64_t &timeout)
{
    timeout = get_next_timeout();
    //SYLAR_LOG_DEBUG(g_logger) << "stopping timeout: " << timeout
    //    << "pending_events: " << m_pending_event_count;
    return timeout == ~0ull
        && m_pending_event_count == 0
        && Scheduler::stopping();
}

bool IOManager::stopping()
{
    uint64_t timeout = 0;
    return stopping(timeout);
}

void IOManager::idle()
{
    // 定义堆数组的原因是协程不适合定义太大的栈数组（协程拥有的栈空间有限）
    auto events = new epoll_event[64]();
    // 由于智能指针不能处理数组，需要我们在析构的时候手动删除
    // 定义该智能指针的目的是方便自动析构
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr) {
        delete[] ptr;
    });

    while (true) {
        uint64_t next_timeout;
        if (stopping(next_timeout)) {
            // stopping函数里修改了next_timeout的值
            // 说明此时set里没有定时器了
            SYLAR_LOG_INFO(g_logger) << "name=" << Scheduler::getName()
                                     << " idle stopping, exit.";
            break;
        }

        int rt;
        do {
            // 从定时器中取出的最近一次需要执行的时间，并且跟最大超时时间比较
            //next_timeout = get_next_timeout();
            static const int MAX_TIMEOUT = 5000; // 5000ms
            if (next_timeout == ~0ull) {
                next_timeout = MAX_TIMEOUT;
            } else {
                next_timeout = MAX_TIMEOUT > next_timeout ? next_timeout : MAX_TIMEOUT;
            }
            SYLAR_LOG_DEBUG(g_logger) << "Next_timeout: " << next_timeout;


            // 核心函数！
            rt = epoll_wait(m_epfd, events, 64, (int) next_timeout);

            if (rt < 0 && errno == EINTR) {
                // 如果没有事件并且是EINTR,说明是被中断了，则接着循环
                continue;
            } else {
                // 不然跳出循环开始处理所有发出响应的事件
                break;
            }
        } while (true);

        //SYLAR_LOG_DEBUG(g_logger) << "Get out of epoll_wait!";

        // 找出所有已经超时的定时器，触发一遍
        std::vector<std::function<void()>> cbs;
        list_expired_cbs(cbs);
        if (!cbs.empty()) {
            // 批量将任务加入调度器
            SYLAR_LOG_DEBUG(g_logger) << "Schedule timer cbs.";
            schedule(cbs.begin(), cbs.end());
            cbs.clear();
        }

        // 遍历所有待处理的事件句柄
        SYLAR_LOG_DEBUG(g_logger) << "Epoll wait: rt=" << rt;
        for (int i = 0; i < rt; ++i) {
            epoll_event &event = events[i];

            //SYLAR_LOG_DEBUG(g_logger) << "Deal with event fd=" << event.data.fd;

            if (event.data.fd == m_tickle_fds[0]) {
                // 说明该事件是被tickle唤醒的
                uint8_t dummy;
                // 可能有多次，当作一次处理，所以得读干净
                // 并且由于是边沿触发，下次epoll_wait就不会再来一次了
                while (read(m_tickle_fds[0], &dummy, 1) == 1);
                continue;
            }

            auto fd_ctx = (FdContext *) event.data.ptr;
            SYLAR_LOG_DEBUG(g_logger) << "new fd_ctx->m_events: " << fd_ctx->m_events;
            FdContext::MutexType::Lock lock(fd_ctx->m_mutex);
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                // 如果事件里有错误或者中断
                SYLAR_LOG_DEBUG(g_logger) << "ERR or HUP";
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->m_events;
            }
            int real_events = NONE;
            if (event.events & EPOLLIN) {
                // 有读事件
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) {
                // 有写事件
                real_events |= WRITE;
            }

            if ((fd_ctx->m_events & real_events) == NONE) {
                // 说明事件已经被别人处理完了
                continue;
            }

            // 修改该fd对应的事件设置
            // 剩余事件
            int left_events = (fd_ctx->m_events & (~real_events));
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) {
                SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                                          << op << ", " << fd_ctx->fd << ", " << event.events
                                          << "): " << rt << " (" << errno << ") ("
                                          << strerror(errno) << ") ";
                continue;
            }

            if (real_events & READ) {
                SYLAR_LOG_DEBUG(g_logger) << "Start to trigger read event..";
                fd_ctx->triggerEvent(READ);
                --m_pending_event_count;
            }
            if (real_events & WRITE) {
                SYLAR_LOG_DEBUG(g_logger) << "Start to trigger write event..";
                fd_ctx->triggerEvent(WRITE);
                --m_pending_event_count;
            }
        }

        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();

        //SYLAR_LOG_DEBUG(g_logger) << "Next i'm gonna swap out!";
        // 将当前协程切出去
        raw_ptr->swapOut();
    }

}

void IOManager::on_timer_insert_at_front()
{
    // 当新添加的计时器刚好是set里的首个元素时
    tickle();
}

IOManager::FdContext::EventContext &
IOManager::FdContext::getContext(IOManager::Event event)
{
    switch (event) {
    case READ:return read;
    case WRITE:return write;
    default:SYLAR_ASSERT2(false, "GetContext error!")
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
    SYLAR_LOG_DEBUG(g_logger) << "m_events: " << m_events << " event: " << event;
    SYLAR_ASSERT(m_events & event)
    // 清除该事件
    m_events = (Event) (m_events & (~event));
    EventContext &event_ctx = getContext(event);
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