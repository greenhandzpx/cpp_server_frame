#include "fd_manager.h"
#include "hook.h"

#include <sys/stat.h>
#include <sys/types.h>

namespace sylar {

    FdCtx::FdCtx(int fd) : m_fd(fd) {}

    FdCtx::~FdCtx() = default;

    bool FdCtx::init() {
        if (m_isInit) {
            return true;
        }
        m_recv_timeout = -1;
        m_send_timeout = -1;

        struct stat fd_stat{};
        // 查看m_fd这个文件描述符的状态
        if (-1 == fstat(m_fd, &fd_stat)) {
            // 查看失败
            m_isInit = false;
            m_isSocket = false;
        } else {
            m_isInit = true;
            // 判断是不是socket描述符
            m_isSocket = S_ISSOCK(fd_stat.st_mode);
        }
        if (m_isSocket) {
            // 获取m_fd的相关信息
            int flags = fcntl_f(m_fd, F_GETFL, 0);
            if (!(flags & O_NONBLOCK)) {
                // 如果不是非阻塞
                fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);
                m_sys_nonblock = true;
            } else {
                m_sys_nonblock = true;
            }
        }

        m_user_nonblock = false;
        m_isClosed = false;

        return m_isInit;
    }

    bool FdCtx::close() {

    }

    void FdCtx::set_timeout(int type, uint64_t timeout) {
        if (type == SO_RCVTIMEO) {
            m_recv_timeout = timeout;
        } else {
            m_send_timeout = timeout;
        }
    }

    uint64_t FdCtx::get_timeout(int type) const {
        if (type == SO_RCVTIMEO) {
            return m_recv_timeout;
        } else {
            return m_send_timeout;
        }
    }


    FdCtx::ptr FdManger::getFdCtx(int fd, bool auto_create)
    {
        RWMutexType::ReadLock lock(m_mutex);
        if (fd >= m_datas.size()) {
            if (!auto_create) {
                return nullptr;
            }
        } else {
            if (m_datas[fd] || !auto_create) {
                return m_datas[fd];
            }
        }
        lock.unlock();

        RWMutexType::WriteLock lock2(m_mutex);
        // 如果查找的fd对应的上下文不存在并且想要自动创建一个
        FdCtx::ptr ctx(new FdCtx(fd));
        if (fd >= m_datas.size()) {
            m_datas.resize(fd + 1);
        }
        m_datas[fd] = ctx;
        return ctx;
    }

    void FdManger::delFdCtx(int fd)
    {
        RWMutexType::WriteLock lock(m_mutex);
        if (fd >= m_datas.size()) {
            return;
        }
        m_datas[fd].reset();
    }




}