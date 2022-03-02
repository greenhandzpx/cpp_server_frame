#include "address.h"

#include <cstring>


namespace sylar {

int Address::getFamily() const
{
    return getAddr()->sa_family;
}
std::string Address::toString()
{
//    std::stringstream ss;

}
bool Address::operator<(const Address &rhs) const
{
    socklen_t count = std::min(getAddrLen(), rhs.getAddrLen());
    int result = std::memcmp(getAddr(), rhs.getAddr(), count);
    if (result < 0) {
        return true;
    } else if (result > 0) {
        return false;
    } else if (getAddrLen() < rhs.getAddrLen()) {
        return true;
    }
    return false;
}

bool Address::operator==(const Address &rhs) const
{
    return getAddrLen() == rhs.getAddrLen()
        && (std::memcmp(getAddr(), rhs.getAddr(), getAddrLen()))
            == 0;
}

bool Address::operator!=(const Address &rhs) const
{
    return !(*this == rhs);
}


}