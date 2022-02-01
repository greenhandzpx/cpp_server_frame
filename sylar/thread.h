//
// Created by greenhandzpx on 1/15/22.
//

#ifndef SYLAR_THREAD_H
#define SYLAR_THREAD_H

#include <pthread.h>
#include <string>
#include <functional>
#include <memory>
#include <iostream>
#include <semaphore.h>
#include <cstdint>
#include <mutex>

namespace sylar {

    class Semaphore {
    public:
        explicit Semaphore(uint32_t count = 0);
        ~Semaphore();

        void wait();
        void notify();

    private:
        Semaphore(const Semaphore&) = delete;
        Semaphore(const Semaphore&&) = delete;
        Semaphore& operator=(const Semaphore&) = delete;

    private:
        sem_t m_semaphore{};
    };


    template<typename T>
    struct ScopedLockImpl {
    public:
        explicit ScopedLockImpl(T& mutex)
                : m_mutex(mutex)
        {
            m_mutex.lock();
            m_locked = true;
        }
        ~ScopedLockImpl()
        {
            unlock();
        }
        void lock()
        {
            if (!m_locked) {
                m_mutex.lock();
                m_locked = true;
            }
        }
        void unlock()
        {
            if (m_locked) {
                m_mutex.unlock();
                m_locked = false;
            }
        }
    private:
        T& m_mutex;
        bool m_locked;
    };


    // 局部读锁
    template<typename T>
    struct ReadScopedLockImpl {
    public:
        explicit ReadScopedLockImpl(T& mutex)
            : m_mutex(mutex)
        {
            m_mutex.rdlock();
            m_locked = true;
        }
        ~ReadScopedLockImpl()
        {
           unlock();
        }
        void lock()
        {
            if (!m_locked) {
                m_mutex.rdlock();
                m_locked = true;
            }
        }
        void unlock()
        {
            if (m_locked) {
                m_mutex.unlock();
                m_locked = false;
            }
        }
    private:
        T& m_mutex;
        bool m_locked;
    };

    // 局部写锁
    template<typename T>
    struct WriteScopedLockImpl {
    public:
        explicit WriteScopedLockImpl(T& mutex)
                : m_mutex(mutex)
        {
            m_mutex.wrlock();
            m_locked = true;
        }
        ~WriteScopedLockImpl()
        {
            unlock();
        }
        void lock()
        {
            if (!m_locked) {
                m_mutex.wrlock();
                m_locked = true;
            }
        }
        void unlock()
        {
            if (m_locked) {
                m_mutex.unlock();
                m_locked = false;
            }
        }
    private:
        T& m_mutex;
        bool m_locked;
    };

    // 读写锁
    class RWMutex {
    public:
        typedef ReadScopedLockImpl<RWMutex> ReadLock;
        typedef WriteScopedLockImpl<RWMutex> WriteLock;

        RWMutex() {
            pthread_rwlock_init(&m_lock, nullptr);
        }

        ~RWMutex() {
            pthread_rwlock_destroy(&m_lock);
        }

        void rdlock() {
            pthread_rwlock_rdlock(&m_lock);
        }

        void wrlock() {
            pthread_rwlock_wrlock(&m_lock);
        }

        void unlock() {
            pthread_rwlock_unlock(&m_lock);
        }
    private:
        /// 读写锁
        pthread_rwlock_t m_lock{};
    };

    // 普通锁
    class Mutex {
    public:
        typedef ScopedLockImpl<Mutex> Lock;
        Mutex()
        {
            pthread_mutex_init(&m_mutex, nullptr);
        }
        ~Mutex()
        {
            pthread_mutex_destroy(&m_mutex);
        }

        void lock()
        {
            pthread_mutex_lock(&m_mutex);
        }
        void unlock()
        {
            pthread_mutex_unlock(&m_mutex);
        }
    private:
        pthread_mutex_t m_mutex{};
    };

    // 自旋锁
    class Spin_Mutex {
    public:
        typedef ScopedLockImpl<Spin_Mutex> Lock;
        Spin_Mutex()
        {
            pthread_spin_init(&m_mutex, 0);
        }
        ~Spin_Mutex()
        {
            pthread_spin_destroy(&m_mutex);
        }

        void lock()
        {
            pthread_spin_lock(&m_mutex);
        }
        void unlock()
        {
            pthread_spin_unlock(&m_mutex);
        }
    private:
        pthread_spinlock_t m_mutex{};
    };


    class Thread {
    public:
        typedef  std::shared_ptr<Thread> ptr;
        Thread(std::function<void()> cb, const std::string& name);
        ~Thread();

        const std::string& getName() const { return m_name; }
        pid_t getId() const { return m_id; }

        void join();

    public:
        static Thread* GetThis();
        static const std::string& GetName();
        static void SetName(const std::string& name);
        static void SetId(pid_t id);

    private:
        Thread(const Thread&) = delete;
        Thread(const Thread&&) = delete;
        Thread& operator=(const Thread&) = delete;

        static void* run(void* arg);

    private:
        pid_t m_id{};
        pthread_t m_thread{};
        std::function<void()> m_cb;
        std::string m_name;
        Semaphore m_semaphore;
    };
}


#endif //SYLAR_THREAD_H
