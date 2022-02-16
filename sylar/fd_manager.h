//
// Created by greenhandzpx on 2/13/22.
//

#ifndef __SYLAR_FD_MANAGER_H__
#define __SYLAR_FD_MANAGER_H__


#include <memory>
#include <vector>

#include "thread.h"
#include "singleton.h"

namespace sylar {

    // 定义每一个文件描述符的类型及属性
    class FdCtx: public std::enable_shared_from_this<FdCtx> {
    public:
        using ptr = std::shared_ptr<FdCtx>;
        explicit FdCtx(int fd);
        ~FdCtx();

        bool init();
        bool isInit() const { return m_isInit; }
        bool isSocket() const { return m_isSocket; }
        bool isClose() const { return m_isClosed; }
        bool close();

        void set_user_nonblock(bool f) { m_user_nonblock = f; }
        // 没有手动设置则为false
        bool get_user_nonblock() const { return m_user_nonblock; }

        void set_sys_nonblock(bool f) { m_sys_nonblock = f; }
        bool get_sys_nonblock() const { return m_sys_nonblock; }

        void set_timeout(int type, uint64_t timeout);
        uint64_t get_timeout(int type) const;



    private:
        bool m_isInit = false;
        bool m_isSocket = false;
        bool m_sys_nonblock = false;
        bool m_user_nonblock = false;
        bool m_isClosed = false;
        int m_fd;
        uint64_t m_recv_timeout = -1;
        uint64_t m_send_timeout = -1;
    };

    // 管理文件描述符
    class FdManger {
    public:
        using RWMutexType = RWMutex;

        FdCtx::ptr getFdCtx(int fd, bool auto_create = false);
        void delFdCtx(int fd);

    private:
        RWMutexType m_mutex;
        std::vector<FdCtx::ptr> m_datas;
    };

    using FdMgr = Singleton<FdManger>;
}

#endif //__SYLAR_FD_MANAGER_H__
