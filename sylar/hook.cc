#include "hook.h"
#include "fiber.h"
#include "iomanager.h"
#include "log.h"
#include "fd_manager.h"

#include <dlfcn.h>

namespace sylar {

    static Logger::ptr g_logger = SYLAR_LOG_NAME("system");

    static thread_local bool t_hook_enable = false;

#define HOOK_FUN(XX) \
    XX(sleep)                     \
    XX(usleep)                    \
    XX(nanosleep)

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

    // 以下操作的目地是：
    // 让hook_init()函数在main函数前执行，因为静态变量在main函数前初始化
    struct _Hook_initer {
        _Hook_initer()
        {
            hook_init();
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

}

// 定时器的信息
struct timer_info {
    int cancelled = 0;
};

template<typename Original_fun, typename ... Args>
static ssize_t do_io(int fd, Original_fun fun, const char* hook_fun_name,
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

    if (ctx->isClose()) {
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
            sylar::IOManager* iom = sylar::IOManager::GetThis();
            sylar::Timer::ptr timer;
            std::weak_ptr<timer_info> winfo(tinfo);

            if (fd_timeout != -1) {
                timer = iom->add_condition_timer(fd_timeout, [iom, winfo, fd, event](){
                    // 将weak_ptr转为shared_ptr
                    auto t = winfo.lock();
                    // 如果该条件已经不存在了或者条件中已设置为取消
                    if (!t || t->cancelled) {
                        return;
                    }
                    // 如果还没有则手动设置为取消
                    t->cancelled = ETIMEDOUT;
                    iom->cancel_event(fd, (sylar::IOManager::Event)(event));
                }, winfo);
            }

            // 这里没有传入cb,则会将当前协程作为回调任务
            int rt = iom->add_event(fd, (sylar::IOManager::Event)(event));
            if (rt) {
                SYLAR_LOG_ERROR(sylar::g_logger) << hook_fun_name << " addEvent("
                    << fd << ", " << event << ")Failed!";
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
        iom->add_timer(seconds * 1000, [fiber, iom, seconds](){
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
        iom->add_timer(useconds / 1000, [fiber, iom](){
            iom->schedule(fiber);
        });
        fiber->Yield_to_Hold();
        return 0;
    }

//    int nanosleep(const struct timespec* req, const struct timespec* rem)
//    {
//        if (!sylar::is_hook_enable()) {
//            return nanosleep_f(req, rem);
//        }
//
//        sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
//        auto iom = sylar::IOManager::GetThis();
//        iom->add_timer(req->tv_sec * 1000 + req->tv_nsec / 1e6,
//                       [fiber, iom](){
//            iom->schedule(fiber);
//        });
//        fiber->Yield_to_Hold();
//        return 0;
//    }



}




