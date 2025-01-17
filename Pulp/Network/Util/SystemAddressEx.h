#pragma once

#include <SystemAddress.h>

X_NAMESPACE_BEGIN(net)


class SystemAddressEx : public SystemAddress
{
public:
    typedef core::FixedArray<SystemAddressEx, MAX_RESOLVE_ADDR> AddressArr;

public:
    SystemAddressEx();

    bool operator>(const SystemAddressEx& rhs) const;
    bool operator<(const SystemAddressEx& rhs) const;

    void setFromSocket(SocketHandle socket);
    void setFromAddInfo(platform::addrinfo* pAddInfo);
    void setFromAddStorage(const platform::sockaddr_storage& addStorage);

    X_INLINE const platform::sockaddr& getSocketAdd(void) const;
    X_INLINE int32_t getSocketAddSize(void) const;

    const char* toString(IPStr& strBuf, bool incPort = true) const;

    bool fromIP(const IPStr& ip, char portDelineator = PORT_DELIMITER, IpVersion::Enum ipVersion = IpVersion::Any);
    bool fromIP(const IPStr& ip, Port port, IpVersion::Enum ipVersion = IpVersion::Any);

    bool fromHost(const HostStr& host, char portDelineator = PORT_DELIMITER, IpVersion::Enum ipVersion = IpVersion::Any);
    bool fromHost(const HostStr& host, Port port, IpVersion::Enum ipVersion = IpVersion::Any);

    static bool resolve(const HostStr& hostStr, bool isHost, AddressArr& address, IpVersion::Enum ipVersion);

    static bool isValidIP(const IPStr& ip, IpVersion::Enum ipVersion);

private:
    bool fromString(const char* pBegin, const char* pEnd, bool isHost, char portDelineator, IpVersion::Enum ipVersion);
};

static_assert(sizeof(SystemAddressEx) == sizeof(SystemAddress), "Size should be same, to prevent slicing.");

X_NAMESPACE_END

#include "SystemAddressEx.inl"