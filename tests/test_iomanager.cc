#include "sylar/sylar.h"
#include "sylar/iomanager.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

sylar::Logger::ptr g_logger = SYLAR_LOGGER_ROOT();

int sock;

void test_fiber()
{
    SYLAR_LOG_INFO(g_logger) << "Test fiber ...";

    sock = socket(AF_INET, SOCK_STREAM, 0);
    // 将sock设为非阻塞
    fcntl(sock, F_SETFL, O_NONBLOCK);

    sockaddr_in addr{};
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    inet_pton(AF_INET, "192.168.0.106", &addr.sin_addr.s_addr);

    // 这里connect是非阻塞的
    if (!connect(sock, (const sockaddr*)&addr, sizeof(addr))) {

    } else if (errno == EINPROGRESS) {
        // 非阻塞异步情况下没有立即连接成功
        SYLAR_LOG_INFO(g_logger) << "Add event errno=" << errno << " " << strerror(errno);

        sylar::IOManager::GetThis()->add_event(sock, sylar::IOManager::WRITE, [](){
            SYLAR_LOG_INFO(g_logger) << "Connected !";
        });
    } else {
        SYLAR_LOG_INFO(g_logger) << "Add event(else) errno=" << errno << " " << strerror(errno);
    }
}

int main()
{
    sylar::IOManager iom(1, true, "Dummy");
    iom.schedule(&test_fiber);

}