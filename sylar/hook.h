#ifndef __SYLAR_HOOK_H__
#define __SYLAR_HOOK_H__

#include <unistd.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>


namespace sylar {
    bool is_hook_enable();
    void set_hook_enable(bool flag);
}

extern "C" {

    // sleep
    // 起两个函数指针别名
    using sleep_fun = unsigned int(*)(unsigned int);

    extern sleep_fun sleep_f;

    using usleep_fun = int(*)(useconds_t);

    extern usleep_fun usleep_f;

    using nanosleep_fun = int(*)(const struct timespec*, const struct timespec*);

    extern nanosleep_fun nanosleep_f;

    // read
    using read_fun = ssize_t (*)(int fd, void *buf, size_t count);

    extern read_fun read_f;

    using readv_fun = ssize_t (*)(int fd, const struct iovec *iov, int iovcnt);

    extern readv_fun readv_f;

    using recv_fun = ssize_t(*)(int sockfd, void *buf, size_t len, int flags);

    extern recv_fun recv_f;

    using recvfrom_fun = ssize_t(*)(int sockfd, void *buf, size_t len, int flags,
                                          struct sockaddr * src_addr,
                                          socklen_t * addrlen);

    extern recvfrom_fun recvfrom_f;

    using recvmsg_fun = ssize_t(*)(int sockfd, struct msghdr *msg, int flags);

    extern recvmsg_fun recvmsg_f;

    // write
    using write_fun = ssize_t(*)(int fd, const void *buf, size_t count);

    extern write_fun write_f;

    using writev_fun = ssize_t(*)(int fd, const struct iovec *iov, int iovcnt);

    extern writev_fun writev_f;

    using send_fun = ssize_t(*)(int sockfd, const void *buf, size_t len, int flags);

    extern send_fun send_f;

    using sendto_fun = ssize_t(*)(int sockfd, const void *buf, size_t len, int flags,
                                      const struct sockaddr *dest_addr, socklen_t addrlen);

    extern sendto_fun sendto_f;

    using sendmsg_fun = ssize_t(*)(int sockfd, const struct msghdr *msg, int flags);

    extern sendmsg_fun sendmsg_f;

    // close
    using close_fun = int(*)(int fd);

    extern close_fun close_f;

    // fcntl
    using fcntl_fun = int(*)(int fd, int cmd, ... /* arg */ );

    extern fcntl_fun fcntl_f;

    // ioctl
    using ioctl_fun = int(*)(int fd, unsigned long request, ...);

    extern ioctl_fun ioctl_f;


}


#endif