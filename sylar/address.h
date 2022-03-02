//
// Created by 86135 on 2022/2/28.
//

#ifndef SYLAR_ADDRESS_H
#define SYLAR_ADDRESS_H

#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <iostream>

namespace sylar {

class Address {
public:
    typedef std::shared_ptr<Address> ptr;
    virtual ~Address() {}

    int getFamily() const;

    virtual const sockaddr* getAddr() const = 0;
    virtual socklen_t getAddrLen() const = 0;

    virtual std::ostream& insert(std::ostream& buf) const = 0;
    std::string toString();

    bool operator<(const Address& rhs) const;
    bool operator==(const Address& rhs) const;
    bool operator!=(const Address& rhs) const;
};

class IPAddress: public Address {
public:
    typedef std::shared_ptr<IPAddress> ptr;

    // 广播地址
    virtual IPAddress::ptr broadcastAddress(uint32_t prefix_len) = 0;
    // 网络地址
    virtual IPAddress::ptr networkAddress(uint32_t prefix_len) = 0;
    // 子网掩码
    virtual IPAddress::ptr subnetMask(uint32_t prefix_len) = 0;

    virtual uint32_t getPort() const = 0;
    virtual void setPort(uint32_t port) = 0;
};

class IPv4Address: public IPAddress {
public:
    typedef std::shared_ptr<IPv4Address> ptr;
    IPv4Address(uint32_t address = 0, uint32_t port = 0);

    const sockaddr* getAddr() const override;
    socklen_t getAddrLen() const override;
    std::ostream& insert(std::ostream& buf) const override;
};
}



#endif //SYLAR_ADDRESS_H
