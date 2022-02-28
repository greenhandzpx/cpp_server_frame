#include "hook.h"
#include "fiber.h"
#include "iomanager.h"
#include "log.h"
#include "fd_manager.h"
#include "config.h"

#include <dlfcn.h>
#include <cstdarg>

namespace sylar {

static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 定义一个配置项
static ConfigVar<int>::ptr g_tcp_connect_timeout =
    Config::Lookup("tcp connect timeout", 5000, "tcp connect timeout");

static thread_local bool t_hook_enable = false;

#define HOOK_FUN(XX) \
    XX(sleep)        \
    XX(usleep)       \
    XX(nanosleep)    \
    XX(socket)       \
    XX(accept)       \
    XX(connect)      \
    XX(read)         \
    XX(readv)        \
    XX(recv)         \
    XX(recvfrom)     \
    XX(recvmsg)      \
    XX(write)        \
    XX(writev)       \
    XX(send)         \
    XX(sendto)       \
    XX(sendmsg)      \
    XX(close)        \
    XX(fcntl)        \
    XX(ioctl)        \
    XX(getsockopt)   \
    XX(setsockopt)

void hook_init()
{
    static bool is_inited = false;
    if (is_inited) {
        return;
    }
        // 从动态链接库里通过sleep函数的符号找出sleep函数
        // 这里能拿得到sleep_f的原因是头文件里有extern声明
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX)
#undef XX
}

static uint64_t s_connect_timeout = -1;
// 以下操作的目地是：
// 让hook_init()函数在main函数前执行，因为静态变量在main函数前初始化
struct _Hook_initer {
    _Hook_initer()
    {
        hook_init();
        s_connect_timeout = g_tcp_connect_timeout->getValue();
        g_tcp_connect_timeout->addListener([](const int &old_value, const int &new_value) {
            SYLAR_LOG_INFO(sylar::g_logger) << "Tcp connect timeout changed from " <<
                                            old_value << " to " << new_value;
        });
    }
};
static _Hook_initer s_hook_initer;

bool is_hook_enable()
{
    return t_hook_enable;
}

void set_hook_enable(bool flag)
{
    t_hook_enable = flag;
}

} // namespace-sylar



// 定时器的信息
struct timer_info {
    int cancelled = 0;
};

template<typename Original_fun, typename ... Args>
static ssize_t do_io(int fd, Original_fun fun, const char *hook_fun_name,
                     uint32_t event, int timeout_so, Args... args)
{
    if (!sylar::is_hook_enable()) {
        // 直接调用传进来的函数
        fun(fd, std::forward<Args>(args)...);
    }

    // 通过fd_manager获取该fd对应的上下文信息
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->getFdCtx(fd);
    if (!ctx) {
        // 如果对应的信息不存在
        return fun(fd, std::forward<Args>(args)...);
    }

    if (ctx->isClosed()) {
        // 该fd已经关闭了
        errno = EBADF;
        return -1;
    }

    if (!ctx->isSocket() || ctx->get_user_nonblock()) {
        // 如果不是socket或者用户手动设置了非阻塞
        return fun(fd, std::forward<Args>(args)...);
    }

    std::shared_ptr<timer_info> tinfo(new timer_info);
    // 获取该fd对应的超时时间
    int64_t fd_timeout = ctx->get_timeout(timeout_so);

    while (true) {
        // 先调用一下试试看
        ssize_t n = fun(fd, std::forward<Args>(args)...);
        while (n == -1 && errno == EINTR) {
            // 如果调用过程中被系统阻断了，则重新调用
            n = fun(fd, std::forward<Args>(args)...);
        }
        if (n == -1 && errno == EAGAIN) {
            // 如果是EAGAIN代表该调用是个异步调用
            // 则以该fd对应的超时时间开一个定时器(定时器发挥作用即代表时间到了，该事件直接取消）
            // 并且将该事件丢到iomanager里，等待epoll_wait接收到信息
            sylar::IOManager *iom = sylar::IOManager::GetThis();
            sylar::Timer::ptr timer;
            std::weak_ptr<timer_info> winfo(tinfo);

            if (fd_timeout != -1) {
                timer = iom->add_condition_timer(fd_timeout, [iom, winfo, fd, event]() {
                    // 将weak_ptr转为shared_ptr
                    auto t = winfo.lock();
                    // 如果该条件已经不存在了或者条件中已设置为取消
                    if (!t || t->cancelled) {
                        return;
                    }
                    // 如果还没有则手动设置为取消
                    t->cancelled = ETIMEDOUT;
                    iom->cancel_event(fd, (sylar::IOManager::Event) (1));
                }, winfo);
            }

            // 这里没有传入cb,则会将当前协程作为回调任务
            int rt = iom->add_event(fd, (sylar::IOManager::Event) (1));
            if (rt) {
                SYLAR_LOG_ERROR(sylar::g_logger) << hook_fun_name << " addEvent("
                                                 << fd << ", " << 1 << ")Failed!";
                if (timer) {
                    timer->cancel();
                }
                return -1;
            }
            // 添加好定时器和事件之后就可以让出协程了
            sylar::Fiber::Yield_to_Hold();
            // 下一次该任务被切回来时就会来到这里
            if (timer) {
                // 定时器还存在意味着还没超时
                timer->cancel();
            }
            if (tinfo->cancelled) {
                // 如果取消掉了，说明已经超时
                errno = tinfo->cancelled;
                return -1;
            }
            // 不然说明定时器还没超时，意味着是epoll_wait等到了事件, 则重新执行这个过程
            continue;
        }
        return n;
    }

}

extern "C" {
// 该宏是定义一个跟sleep同函数签名的函数指针(全局变量）
#define XX(name) name ## _fun name ## _f = nullptr;
HOOK_FUN(XX)
#undef XX

unsigned int sleep(unsigned int seconds)
{
    if (!sylar::is_hook_enable()) {
        // 如果没有hook住的话，就调用系统本身的sleep
        return sleep_f(seconds);
    }

    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    auto iom = sylar::IOManager::GetThis();
    // 把当前协程丢到一个计时器的回调里，然后切出去，等一段时间后再触发回调切回来
    // 这样就能防止sleep的时候程序啥都不做
//        SYLAR_LOG_DEBUG(sylar::g_logger) << "Add a timer: " << seconds << "s"
//            << ", fiber id=" << fiber->getId();
    iom->add_timer(seconds * 1000, [fiber, iom, seconds]() {
//            SYLAR_LOG_DEBUG(sylar::g_logger) << "timeout: " << seconds << "s"
//                << ", fiber id=" << fiber->getId();
        iom->schedule(fiber);
    });
    fiber->Yield_to_Hold();
    return 0;
}

int usleep(useconds_t useconds)
{
    if (!sylar::is_hook_enable()) {
        return usleep_f(useconds);
    }

    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    auto iom = sylar::IOManager::GetThis();
    iom->add_timer(useconds / 1000, [fiber, iom]() {
        iom->schedule(fiber);
    });
    fiber->Yield_to_Hold();
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (!sylar::is_hook_enable()) {
        return nanosleep_f(req, rem);
    }

    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    auto iom = sylar::IOManager::GetThis();
    iom->add_timer(req->tv_sec * 1000 + req->tv_nsec / 1e6,
                   [fiber, iom]() {
                       iom->schedule(fiber);
                   });
    fiber->Yield_to_Hold();
    return 0;
}

int socket(int domain, int type, int protocol)
{
    if (!sylar::is_hook_enable()) {
        return socket_f(domain, type, protocol);
    }
    int fd = socket_f(domain, type, protocol);
    if (fd == -1) {
        return fd;
    }
    // 创建一个fd的相关信息
    sylar::FdMgr::GetInstance()->getFdCtx(fd, true);
    return fd;
}

int connect_with_timeout(int fd, const struct sockaddr *addr,
                         socklen_t addrlen, uint64_t timeout_ms)
{
    if (!sylar::is_hook_enable()) {
        return connect_f(fd, addr, addrlen);
    }
    auto ctx = sylar::FdMgr::GetInstance()->getFdCtx(fd);
    if (!ctx || ctx->isClosed()) {
        errno = EBADF;
        return -1;
    }
    if (!ctx->isSocket()) {
        // 如果不是socket
        return connect_f(fd, addr, addrlen);
    }
    if (ctx->get_user_nonblock()) {
        // 如果设置了用户态非阻塞
        return connect_f(fd, addr, addrlen);
    }

    int n = connect_f(fd, addr, addrlen);
    if (n == 0) {
        return 0;
    } else if (n != -1 || errno != EINPROGRESS) {
        // 说明没有被中断，正常返回了一个socketfd
        return n;
    }

    sylar::IOManager *iom = sylar::IOManager::GetThis();
    sylar::Timer::ptr timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    if (timeout_ms != (uint64_t) -1) {
        timer = iom->add_condition_timer(timeout_ms, [iom, fd, winfo]() {
            auto t = winfo.lock();
            // 查看该条件是否仍为真
            if (!t || t->cancelled) {
                // 说明该事件已经无了，直接返回
                return;
            }
            // 该事件已经超时了，取消掉
            t->cancelled = ETIMEDOUT;
            iom->cancel_event(fd, sylar::IOManager::WRITE);
        }, winfo);
    }

    // connect是一个可写事件
    int rt = iom->add_event(fd, sylar::IOManager::WRITE);
    if (rt == 0) {
        // 添加完io事件后就可以挂起了
        sylar::Fiber::Yield_to_Hold();
        // 下次唤醒该协程时会回到这里
        if (timer) {
            // 说明还没超时
            timer->cancel();
        }
        if (tinfo->cancelled) {
            // 说明已经超时
            errno = tinfo->cancelled;
            return -1;
        }
    } else {
        if (timer) {
            timer->cancel();
        }
        SYLAR_LOG_ERROR(sylar::g_logger) << "connect addEvent(" << fd << ", WRITE) error!";
    }

    int error = 0;
    socklen_t len = sizeof(int);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
        return -1;
    }
    if (!error) {
        return 0;
    }
    errno = error;
    return -1;
}
int connect(int sockfd, const struct sockaddr *addr,
             socklen_t addrlen)
{
    return connect_with_timeout(sockfd, addr, addrlen, sylar::s_connect_timeout);
}

int accept(int sockfd, struct sockaddr *addr,
            socklen_t *addrlen)
{
    int fd = do_io(sockfd, accept_f, "accept", sylar::IOManager::READ,
                   SO_RCVTIMEO, addr, addrlen);
    // 由于accept会返回一个新连接的句柄，需要初始化一下该句柄的相关信息
    if (fd >= 0) {
        sylar::FdMgr::GetInstance()->getFdCtx(fd, true);
    }
    return fd;
}

ssize_t read(int fd, void *buf, size_t count)
{
    return do_io(fd, read_f, "read", sylar::IOManager::READ,
                 SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    return do_io(fd, readv_f, "readv", sylar::IOManager::READ,
                 SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    return do_io(sockfd, recv_f, "recv", sylar::IOManager::READ,
                 SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                  struct sockaddr *src_addr,
                  socklen_t *addrlen)
{
    return do_io(sockfd, recvfrom_f, "recvfrom", sylar::IOManager::READ,
                 SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return do_io(sockfd, recvmsg_f, "recvmsg", sylar::IOManager::READ,
                 SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return do_io(fd, write_f, "write", sylar::IOManager::WRITE,
                 SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    return do_io(fd, writev_f, "writev", sylar::IOManager::WRITE,
                 SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    return do_io(sockfd, send_f, "send", sylar::IOManager::WRITE,
                 SO_SNDTIMEO, buf, len, flags);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return do_io(sockfd, sendto_f, "sendto", sylar::IOManager::WRITE,
                 SO_SNDTIMEO, buf, len, flags, dest_addr, addrlen);
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    return do_io(sockfd, sendmsg_f, "sendmsg", sylar::IOManager::WRITE,
                 SO_SNDTIMEO, msg, flags);
}

int close(int fd)
{
    if (!sylar::is_hook_enable()) {
        return close_f(fd);
    }

    auto ctx = sylar::FdMgr::GetInstance()->getFdCtx(fd);
    if (ctx) {
        // 取消掉该fd对应的所有事件
        auto iom = sylar::IOManager::GetThis();
        iom->cancel_all(fd);
        sylar::FdMgr::GetInstance()->delFdCtx(fd);
    }
    return close_f(fd);
}

int fcntl(int fd, int cmd, ...)
{
    // 创建一个可变参数列表
    std::va_list vl;
    // 指定可变参数列表的第一个元素（不包括该元素）
    va_start(vl, cmd);
    switch (cmd) {
    case F_SETFL: {
        int arg = va_arg(vl, int);
        va_end(vl);
        sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->getFdCtx(fd);
        if (!ctx || ctx->isClosed() || !ctx->isSocket()) {
            // 不存在或者已经关闭或者不是socket
            return fcntl_f(fd, cmd, arg);
        }
        // 设置为用户态的非阻塞
        ctx->set_user_nonblock(arg & O_NONBLOCK);
        // 如果系统态为非阻塞，则强制加上非阻塞，否则强制删掉非阻塞
        if (ctx->get_sys_nonblock()) {
            arg |= O_NONBLOCK;
        } else {
            arg &= (~O_NONBLOCK);
        }
        return fcntl_f(fd, cmd, arg);
    }
        break;
    case F_GETFL: {
        va_end(vl);
        int arg = fcntl_f(fd, cmd);
        auto ctx = sylar::FdMgr::GetInstance()->getFdCtx(fd);
        if (!ctx || ctx->isClosed() || !ctx->isSocket()) {
            return arg;
        }
        if (ctx->get_user_nonblock()) {
            return arg | O_NONBLOCK;
        } else {
            return arg & (~O_NONBLOCK);
        }
    }
        break;
        // 以下按照函数签名进行分类
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
    case F_SETFD:
    case F_SETOWN:
    case F_SETSIG:
    case F_SETLEASE:
    case F_NOTIFY:
    case F_SETPIPE_SZ: {
        // 取出可变参数的第一个元素
        int arg = va_arg(vl, int);
        va_end(vl);
        return fcntl_f(fd, cmd, arg);
    }
        break;
    case F_GETFD:
    case F_GETOWN:
    case F_GETSIG:
    case F_GETLEASE:
    case F_GETPIPE_SZ: {
        va_end(vl);
        return fcntl_f(fd, cmd);
    }
        break;
    case F_SETLK:
    case F_SETLKW:
    case F_GETLK: {
        auto arg = va_arg(vl, struct flock*);
        va_end(vl);
        return fcntl_f(fd, cmd, arg);
    }
        break;
    case F_GETOWN_EX:
    case F_SETOWN_EX: {
        auto arg = va_arg(vl, struct f_owner_ex*);
        va_end(vl);
        return fcntl_f(fd, cmd, arg);
    }
        break;
    default:va_end(vl);
        return fcntl_f(fd, cmd);
    }
}

int ioctl(int fd, unsigned long request, ...)
{
    std::va_list vl;
    va_start(vl, request);
    void *arg = va_arg(vl, void*);
    va_end(vl);

    if (request == FIONBIO) {
        // 如果请求是设置非阻塞相关
        // 查看附加参数中有没有关于用户态阻塞的信息
        bool user_nonblock = !!*(int *) arg;
        auto ctx = sylar::FdMgr::GetInstance()->getFdCtx(fd);
        if (!ctx || ctx->isClosed() || !ctx->isSocket()) {
            return ioctl_f(fd, request, arg);
        }
        ctx->set_user_nonblock(user_nonblock);
    }
    return ioctl_f(fd, request, arg);
}

int getsockopt(int sockfd, int level, int optname,
                void *optval, socklen_t *optlen)
{
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname,
                const void *optval, socklen_t optlen)
{
    if (!sylar::is_hook_enable()) {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }
    if (level == SOL_SOCKET) {
        // 表示是socket层级的
        if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            // 表示设置超时时间，说明是异步io
            auto ctx = sylar::FdMgr::GetInstance()->getFdCtx(sockfd);
            if (ctx) {
                auto tv = (const timeval *) optval;
                ctx->set_timeout(optname, tv->tv_sec * 1000 + tv->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

}




