#include "stdafx.h"
#include "XPeer.h"
#include "XNet.h"

#include <ITimer.h>

#include <Hashing\sha1.h>
#include <Memory\VirtualMem.h>
#include <String\HumanDuration.h>
#include <String\HumanSize.h>
#include <Random\MultiplyWithCarry.h>
#include <Threading\JobSystem2.h>
#include <Time\TimeLiterals.h>

#include "Sockets\Socket.h"

X_NAMESPACE_BEGIN(net)

using namespace core::string_view_literals;

namespace
{
    template <typename T, size_t Length, size_t ... Index>
    constexpr T HexString(const char(&Input)[Length], const std::index_sequence<Index...>&)
    {
        return T{ core::strUtil::HexChar(Input[(Index * 2)], Input[((Index * 2) + 1)])... };
    }

    template <typename T, size_t Length>
    constexpr T HexString(const char(&Input)[Length])
    {
        return HexString<T>(Input, std::make_index_sequence<(Length / 2)>{});
    }


    // start of packet to mark offline msg.
    const std::array<uint8_t, 8> OFFLINE_MSG_ID = {
        0x00, 0x35, 0x33, 0x24, 0xbb, 0xa5, 0x38, 0x85};

    const size_t POOL2_ALLOCATION_SIZE = sizeof(ReliablePacket);
    const size_t POOL2_ALLOCATION_ALIGN = X_ALIGN_OF(ReliablePacket);
    const size_t POOL2_ALLOC_MAX = 1024 * 128;

    const size_t POOL_ALLOC_MAX = 1024 * 128; // packets and buffered commands
    const size_t POOL_ALLOCATION_SIZE = core::Max(sizeof(BufferdCommand), sizeof(Packet));
    const size_t POOL_ALLOCATION_ALIGN = core::Max(X_ALIGN_OF(BufferdCommand), X_ALIGN_OF(Packet));

} // namespace

PingAndClockDifferential::PingAndClockDifferential()
{
    pingTime = UNDEFINED_PING;
}

// -----------------------------------

RemoteSystem::RemoteSystem(NetVars& vars, core::MemoryArenaBase* arena,
    core::MemoryArenaBase* packetDataArena, core::MemoryArenaBase* packetPool) :
    relLayer(vars, arena, packetDataArena, packetPool)
{
    isActive = false;
    weStartedconnection = false;
    systemHandle = INVALID_SYSTEM_HANDLE;

    connectState = ConnectState::NoAction;
    lowestPing = UNDEFINED_PING;
    MTUSize = MAX_MTU_SIZE;

    pNetSocket = nullptr;
}

void RemoteSystem::free(void)
{
    closeConnection();

    relLayer.free();
}

void RemoteSystem::closeConnection(void)
{
    isActive = false;
    weStartedconnection = false;

    connectState = ConnectState::NoAction;
    lowestPing = UNDEFINED_PING;
    MTUSize = MAX_MTU_SIZE;

    pNetSocket = nullptr;

    guid = UNASSIGNED_NET_GUID;

    // we don't reset reliability layer, memory is kept.
    // this is so buffers can be kept for new connection.
    // meaning new connections get warm buffers.
}

bool RemoteSystem::canSend(void) const
{
    if (!isActive) {
        return false;
    }

    // in a mode we want to send to?
    switch (connectState) {
        case ConnectState::DisconnectAsap:
        case ConnectState::DisconnectAsapSilent:
        case ConnectState::DisconnectOnNoAck:
            return false;
        default:
            break;
    }

    return true;
}

ConnectionState::Enum RemoteSystem::getConnectionState(void) const
{
    if (!isActive) {
        return ConnectionState::Disconnected;
    }

    static_assert(ConnectState::ENUM_COUNT == 8, "Additional states? this logic needs updating");
    switch (connectState) {
        case ConnectState::DisconnectAsap:
        case ConnectState::DisconnectOnNoAck:
            return ConnectionState::Disconnecting;
        case ConnectState::DisconnectAsapSilent:
            return ConnectionState::DisconnectingSilently;

        case ConnectState::RequestedConnection:
        case ConnectState::HandlingConnectionRequest:
        case ConnectState::UnverifiedSender:
            return ConnectionState::Connecting;

        case ConnectState::Connected:
            return ConnectionState::Connected;

        case ConnectState::NoAction:
            break;
    }

    return ConnectionState::Disconnected;
}

int32_t RemoteSystem::getAveragePing(void) const
{
    int32_t sum = 0;
    int32_t num = 0;

    for (const auto& ping : pings) {
        if (ping.isValid()) {
            sum += ping.pingTime;
            ++num;
        }
    }

    // aint no div by zero today son!
    if (num) {
        return sum / num;
    }

    return -1;
}

int32_t RemoteSystem::getLastPing(void) const
{
    return pings[lastPingIdx].pingTime;
}

SystemHandle RemoteSystem::getHandle(void) const
{
    return systemHandle;
}

void RemoteSystem::setHandle(SystemHandle handle)
{
    systemHandle = handle;
}

void RemoteSystem::onConnected(const SystemAddressEx& externalSysId, const SystemAddArr& localIps,
    core::TimeVal sendPingTime, core::TimeVal sendPongTime)
{
    myExternalSystemAddress = externalSysId;
    thierInternalSystemAddress = localIps;
    connectState = ConnectState::Connected;

    onPong(sendPingTime, sendPongTime);

    IPStr ipStr;

    X_LOG0("Net", "RemoteSystem ^2Connected^7: myExternalIp: \"%s\"", externalSysId.toString(ipStr));
    X_LOG_BULLET;
    for (auto ip : localIps) {
        X_LOG0("Net", "Remote LocalIp: \"%s\"", ip.toString(ipStr));
    }
}

void RemoteSystem::onPong(core::TimeVal sendPingTime, core::TimeVal sendPongTime)
{
    core::TimeVal timeNow = gEnv->pTimer->GetTimeNowReal();
    core::TimeVal ping = timeNow - sendPingTime;

    uint16_t pingMS = safe_static_cast<uint16_t>(ping.GetMilliSecondsAsInt64());
    int64_t diff = sendPongTime.GetMilliSecondsAsInt64() - (timeNow.GetMilliSecondsAsInt64() / 2 + sendPingTime.GetMilliSecondsAsInt64() / 2);

    pings[lastPingIdx].pingTime = pingMS;
    pings[lastPingIdx].clockDifferential = core::TimeVal(diff);
    lastPingIdx = (++lastPingIdx % PING_HISTORY_COUNT);

    if (lowestPing == UNDEFINED_PING || pingMS < lowestPing) {
        lowestPing = pingMS;
    }
}

// -----------------------------------

const std::array<uint32_t, 3> XPeer::MTUSizesArr = {
    MAX_MTU_SIZE, 
    1200, 
    576 // this is the guaranteed minimum hardware must support for IPV4 (IPV6 is 1280)
};

XPeer::XPeer(NetVars& vars, const SystemAddArr& localAddress, core::MemoryArenaBase* arena) :
    vars_(vars),
    ipList_(localAddress),
    sockets_(arena),
    socketThreads_(arena),
    remoteSystems_(arena),
    activeRemoteSystems_(arena),
    remoteSystemLookup_(arena),
    bufferdCmds_(arena),
    packetQue_(arena),
    recvDataQue_(arena),
    connectionReqs_(arena),
    bans_(arena),
    arena_(arena),
    poolAllocator_(PoolArena::getMemoryRequirement(POOL_ALLOCATION_SIZE) * POOL_ALLOC_MAX,
        core::VirtualMem::GetPageSize() * 4,
        0,
        PoolArena::getMemoryRequirement(POOL_ALLOCATION_SIZE),
        PoolArena::getMemoryAlignmentRequirement(POOL_ALLOCATION_ALIGN),
        PoolArena::getMemoryOffsetRequirement()),
    poolArena_(&poolAllocator_, "PacketPool"),
    pool2Allocator_(
        PoolArena::getMemoryRequirement(POOL2_ALLOCATION_SIZE) * POOL2_ALLOC_MAX,
        core::VirtualMem::GetPageSize() * 4,
        0,
        PoolArena::getMemoryRequirement(POOL2_ALLOCATION_SIZE),
        PoolArena::getMemoryAlignmentRequirement(POOL2_ALLOCATION_ALIGN),
        PoolArena::getMemoryOffsetRequirement()),
    pool2Arena_(&pool2Allocator_, "ReliablePool"),
    blockArena_(&blockAlloc_, "BlockArena")
{
    X_ASSERT(arena_->isThreadSafe(), "Peer arena must be thread safe")(arena_->isThreadSafe());

    arena->addChildArena(&poolArena_);
    arena->addChildArena(&pool2Arena_);
    arena->addChildArena(&blockArena_);

    remoteSystems_.getAllocator().setBaseAlignment(core::Max(16_sz, X_ALIGN_OF(RemoteSystem)));
    remoteSystems_.setGranularity(1);

    sockets_.setGranularity(4);

    guid_ = XNet::generateGUID();

    unreliableTimeOut_ = core::TimeVal::fromMS(1000 * 10);

    defaultMTU_ = MAX_MTU_SIZE;
    maxIncommingConnections_ = 0;
    maxPeers_ = 0;
    drainSockets_ = false;

    pJobSys_ = gEnv->pJobSys;

    bufferdCmds_.reserve(256);
    packetQue_.reserve(256);
    recvDataQue_.reserve(256);

    connectionReqs_.setGranularity(8);

    ttSetLockName(gEnv->ctx, &connectionReqsCS_, "ConnectionReqLock");
}

XPeer::~XPeer()
{
    shutdown(0_tv, OrderingChannel::Default, PacketPriority::High);

    arena_->removeChildArena(&poolArena_);
    arena_->removeChildArena(&pool2Arena_);
    arena_->removeChildArena(&blockArena_);
}

// TODO: make this handle been called multiple times better.
StartupResult::Enum XPeer::init(int32_t maxConnections, core::span<const SocketDescriptor> socketDescriptors)
{
    ttZone(gEnv->ctx, "(Net) peer init");

    if (maxConnections < 1) {
        return StartupResult::InvalidMaxCon;
    }
    if (socketDescriptors.size() < 1) {
        return StartupResult::InvalidSocketDescriptors;
    }

    for (auto& sd : socketDescriptors)
    {
        if (sd.getPort() == 0)
        {
            return StartupResult::InvalidPort;
        }
    }

    BindParameters bindParam;
    bindParam.nonBlockingSocket = false;
    bindParam.IPHdrIncl = false;
    bindParam.broadCast = true;

    for (int32_t i = 0; i < socketDescriptors.size(); i++) {
        const SocketDescriptor& socketDiscriptor = socketDescriptors[i];

        NetSocket socket(vars_);
        bindParam.hostAddr = socketDiscriptor.getHostAdd();
        bindParam.port = socketDiscriptor.getPort();
        bindParam.socketFamily = socketDiscriptor.getSocketFamiley();
        bindParam.socketType = SocketType::Dgram;

        BindResult::Enum res = socket.bind(bindParam);
        if (res != BindResult::Success) {
            static_assert(BindResult::ENUM_COUNT == 4, "More bind results?");

            // map to startup result.
            if (res == BindResult::AddrInUse) {
                return StartupResult::SocketPortInUse;
            }
            if (res == BindResult::FailedToBind) {
                return StartupResult::SocketFailedToBind;
            }
            if (res == BindResult::SendTestFailed) {
                return StartupResult::SocketFailedToTestSend;
            }

            X_ASSERT_UNREACHABLE();
            return StartupResult::Error;
        }

        sockets_.emplace_back(std::move(socket));
    }

    if (!cryptRnd_.init()) {
        X_ERROR("Net", "Failed to init rnd");
        return StartupResult::Error;
    }

    X_ASSERT(maxPeers_ == 0, "Already init")(maxPeers_);

    maxPeers_ = maxConnections;

    if (maxIncommingConnections_ > maxConnections) {
        maxIncommingConnections_ = maxConnections;
    }

    {
        core::MemoryArenaBase* packetPool = &pool2Arena_;

        remoteSystems_.reserve(maxPeers_);
        activeRemoteSystems_.reserve(maxPeers_);
        remoteSystemLookup_.reserve(maxPeers_);
        for (int32_t i = 0; i < maxPeers_; i++) {
            remoteSystems_.emplace_back(vars_, arena_, &blockArena_, packetPool);
            remoteSystems_[i].setHandle(i);
        }
    }

    socketThreads_.resize(sockets_.size());
    for (size_t i = 0; i < socketThreads_.size(); i++) {
        auto& socket = sockets_[i];
        auto& socketThread = socketThreads_[i];

        core::StackString256 threadName;
        threadName.appendFmt("netSocketRecv_%i", i);

        ThreadArr::Type::FunctionDelagate del;
        del.Bind<XPeer, &XPeer::socketRecvThreadProc>(this);

        socketThread.create(threadName.c_str(), 1024 * 4);
        socketThread.setData(&socket);
        socketThread.start(del);
    }

    // set out local address port as that of 1st bound socket.
    if (sockets_.isNotEmpty()) {
        auto port = sockets_.front().getBoundAdd().getPort();
        for (auto& ip : ipList_) {
            ip.setPortFromHostByteOrder(port);
        }
    }


    if (vars_.debugEnabled() > 1) {
        NetGuidStr guidStr;
        X_LOG0("Net", "ProtoVersion: ^5%" PRIu8 ".%" PRIu8, PROTO_VERSION_MAJOR, PROTO_VERSION_MINOR);
        X_LOG0("Net", "Max peers: ^5%" PRIi32, maxPeers_);
        X_LOG0("Net", "Max incoming connections: ^5%" PRIi32, maxIncommingConnections_);
        X_LOG0("Net", "GUID: ^5%s", guid_.toString(guidStr));
        {
            X_LOG0("Net", "Listening on ^5%" PRIuS " endpoints", sockets_.size());
            X_LOG_BULLET;
            for (auto& s : sockets_) {
                IPStr boundAddStr;
                s.getBoundAdd().toString(boundAddStr);
                X_LOG0("Net", "bound address: \"%s\"", boundAddStr.c_str());
            }
        }

        listLocalAddress();
    }

    return StartupResult::Started;
}

void XPeer::shutdown(core::TimeVal blockDuration, OrderingChannel::Enum orderingChannel,
    PacketPriority::Enum disconnectionNotificationPriority)
{
    X_LOG0("Net", "Shutting down peer");

    if (blockDuration.GetValue() > 0) {
        bool anyActive = false;

        for (auto& rs : remoteSystems_) {
            if (rs.isActive) {
                notifyAndFlagForShutdown(rs, orderingChannel, disconnectionNotificationPriority);
                anyActive |= true;
            }
        }

        if (anyActive) {
            auto timeNow = gEnv->pTimer->GetTimeNowReal();
            auto startTime = timeNow;

            // send out the packets.
            BitStreamMTU updateBS;
            remoteReliabilityTick(updateBS, timeNow);

            // scale so if block is low we don't sleep much
            auto sleepTime = safe_static_cast<uint32_t>(core::Max(blockDuration.GetMilliSecondsAsInt64() / 100, 1ll));
            sleepTime = core::Min(50u, sleepTime);

            // spin till all closed or timeout.
            while (blockDuration > (timeNow - startTime)) {
                anyActive = false;
                for (auto& rs : remoteSystems_) {
                    anyActive |= rs.isActive;
                }

                if (!anyActive) {
                    break;
                }

                core::Thread::sleep(sleepTime);

                timeNow = gEnv->pTimer->GetTimeNowReal();

                remoteReliabilityTick(updateBS, timeNow);

                // re get time..?
            }

            if (anyActive) {
                X_WARNING("Net", "Timed out waiting for disconnect notification dispatch.");
            }
            else {
                X_LOG0_IF(vars_.debugEnabled(), "Net", "All disconnect notifications, dispatched.");
            }
        }
    }

    for (auto& socketThread : socketThreads_) {
        socketThread.stop();
    }

    for (size_t i = 0; i < socketThreads_.size(); i++) {
        auto& socket = sockets_[i];
        auto& socketThread = socketThreads_[i];

        if (!socketThread.hasFinished()) {
            if (socket.sendSendTest()) {
                socketThread.join();
            }
            else {
                // if the send test failed potentially the thread won't join.
                X_WARNING("Net", "Failed to sent shutdown msg to thread");
            }
        }
    }

    sockets_.clear();
    socketThreads_.clear();

    {
        core::CriticalSection::ScopedLock lock(connectionReqsCS_);
        for (auto* pConReq : connectionReqs_) {
            freeConnectionRequest(pConReq);
        }
    }

    // cleans up all memory for reliability layers and remoteSystems.
    remoteSystems_.clear();
    activeRemoteSystems_.clear();
    remoteSystemLookup_.clear();

    bufferdCmds_.popAll([this](BufferdCommand* pCmd) {
        freeBufferdCmd(pCmd);
    });

    packetQue_.popAll([this](Packet* pPacket) {
        freePacket(pPacket);
    });

    recvDataQue_.popAll([this](RecvData* pData) {
        freeRecvData(pData);
    });

    ipList_.clear();
    sendReceiptSerial_ = 0;
}

void XPeer::runUpdate(void)
{
    ttZone(gEnv->ctx, "(Net) peer update");

    BitStreamMTU updateBS;
    core::TimeVal timeNow = gEnv->pTimer->GetTimeNowReal();

    // would be nice if we could split this into jobs.
    // basically need to group incoming packets by target system
    // maybe i'll have some initial single thread logic that processes all the packets by systemAddress.
    // then once that's been done jobs can be created to pass the data to the reliability layers where the processing is actually done.
    processRecvData(updateBS, timeNow);
    processConnectionRequests(updateBS, timeNow);
    processBufferdCommands(updateBS, timeNow);

    // if just one remote better to not make jobs.
    // as we can reuse current updateBS which should be hot in cache.
    const bool singlethreadRemoteTick = activeRemoteSystems_.size() < 2;

    if (singlethreadRemoteTick || !pJobSys_) {
        remoteReliabilityTick(updateBS, timeNow);
    }
    else {
        // we can update all the peers in different threads.
        core::Delegate<void(RemoteSystem**, uint32_t)> del;
        del.Bind<XPeer, &XPeer::Job_remoteReliabilityTick>(this);

        // create a job for each remote.
        auto* pJob = pJobSys_->parallel_for_member<XPeer>(del,
            activeRemoteSystems_.data(),
            safe_static_cast<uint32_t>(activeRemoteSystems_.size()),
            core::V2::CountSplitter32(1)
                JOB_SYS_SUB_ARG(core::profiler::SubSys::NETWORK));

        pJobSys_->Run(pJob);
        pJobSys_->Wait(pJob);
    }
}

void XPeer::setPassword(const PasswordStr& pass)
{
    password_ = pass;
}

// connection api
ConnectionAttemptResult::Enum XPeer::connect(const HostStr& host, Port remotePort, const PasswordStr& password, uint32_t retryCount,
    core::TimeVal retryDelay, core::TimeVal timeoutTime)
{
    X_ASSERT(retryDelay > 0_tv, "Delay can't be zero")(retryDelay);

    // resolve the address.
    SystemAddressEx systemAddress;
    if (!systemAddress.fromHost(host, remotePort)) {
        return ConnectionAttemptResult::FailedToResolve;
    }

    // log what host resolved it?
    // 'porky-pork-scratchings.ru'

    return connect(systemAddress, password, retryCount, retryDelay, timeoutTime);
}

ConnectionAttemptResult::Enum XPeer::connect(const IPStr& ip, Port remotePort, const PasswordStr& password, uint32_t retryCount,
    core::TimeVal retryDelay, core::TimeVal timeoutTime)
{
    X_ASSERT(retryDelay > 0_tv, "Delay can't be zero")(retryDelay);

    // need to work out the address.
    SystemAddressEx systemAddress;
    if (!systemAddress.fromIP(ip, remotePort)) {
        return ConnectionAttemptResult::FailedToResolve;
    }

    return connect(systemAddress, password, retryCount, retryDelay, timeoutTime);
}

ConnectionAttemptResult::Enum XPeer::connect(const SystemAddress& sysAdd, const PasswordStr& password, uint32_t retryCount,
    core::TimeVal retryDelay, core::TimeVal timeoutTime)
{
    X_ASSERT(retryDelay > 0_tv, "Delay can't be zero")(retryDelay);

    const SystemAddressEx& systemAddress = static_cast<const SystemAddressEx&>(sysAdd);

    uint8_t socketIdx = 0; // hard coded socket idx for now

    if (socketIdx >= sockets_.size()) {
        return ConnectionAttemptResult::InvalidParam;
    }

    if (sysAdd.getIPVersion() == IpVersion::Ipv6) {
        auto socketIpVer = sockets_[socketIdx].getBoundAdd().getIPVersion();

        if (socketIpVer != IpVersion::Ipv6) {
            X_ERROR("Net", "Can't connect to a ipv6 address over a ipv4 socket");
            return ConnectionAttemptResult::InvalidParam;
        }
    }

    // are we already connected?
    if (getRemoteSystem(systemAddress, true)) {
        return ConnectionAttemptResult::AlreadyConnected;
    }

    RequestConnection* pConReq = allocConnectionRequest();
    pConReq->systemAddress = systemAddress;
    pConReq->nextRequestTime = gEnv->pTimer->GetTimeNowReal();
    pConReq->timeoutTime = timeoutTime;
    pConReq->retryDelay = retryDelay;
    pConReq->numRequestsMade = 0;
    pConReq->retryCount = retryCount;
    pConReq->socketIdx = socketIdx;
    pConReq->MTUIdxShift = 0;
    pConReq->password = password;

    // only push if not trying to connect already.
    auto matchSysAddFunc = [&systemAddress](const RequestConnection* pOth) {
        return pOth->systemAddress == systemAddress;
    };

    {
        core::CriticalSection::ScopedLock lock(connectionReqsCS_);

        if (std::find_if(connectionReqs_.begin(), connectionReqs_.end(), matchSysAddFunc) != connectionReqs_.end()) {
            freeConnectionRequest(pConReq);
            return ConnectionAttemptResult::AlreadyInProgress;
        }

        connectionReqs_.emplace_back(pConReq);
    }

    IPStr strBuf;
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Started Connection request to add: \"%s\" port: ^5%" PRIu16, systemAddress.toString(strBuf),
        systemAddress.getPort());

    return ConnectionAttemptResult::Started;
}

void XPeer::closeConnection(SystemHandle systemHandle, bool sendDisconnectionNotification,
    OrderingChannel::Enum orderingChannel, PacketPriority::Enum notificationPriority)
{
    X_ASSERT(systemHandle != INVALID_SYSTEM_HANDLE, "Invalid system handle passed")(systemHandle);

    X_LOG0_IF(vars_.debugEnabled(), "Net", "Queued CloseConnection, send notification: %" PRIi32, static_cast<int32_t>(sendDisconnectionNotification));

    BufferdCommand* pCmd = allocBufferdCmd(BufferdCommand::Cmd::CloseConnection, 0);
    pCmd->priority = notificationPriority;
    pCmd->orderingChannel = orderingChannel;
    pCmd->systemHandle = systemHandle;
    pCmd->systemAddress = UNASSIGNED_SYSTEM_ADDRESS;
    pCmd->sendDisconnectionNotification = sendDisconnectionNotification;
    bufferdCmds_.push(pCmd);
}

// connection util
SystemHandle XPeer::getSystemHandleForAddress(const SystemAddress& sysAdd) const
{
    const SystemAddressEx& systemAddress = static_cast<const SystemAddressEx&>(sysAdd);

    const RemoteSystem* pRemoteSys = getRemoteSystem(systemAddress, false);
    if (!pRemoteSys) {
        return INVALID_SYSTEM_HANDLE;
    }

    return pRemoteSys->systemHandle;
}


ConnectionState::Enum XPeer::getConnectionState(SystemHandle systemHandle) const
{
    X_ASSERT(systemHandle != INVALID_SYSTEM_HANDLE, "Invalid system handle passed")(systemHandle);

    const RemoteSystem* pRemoteSys = getRemoteSystem(systemHandle, false);
    if (!pRemoteSys) {
        return ConnectionState::NotConnected;
    }

    return pRemoteSys->getConnectionState();
}

ConnectionState::Enum XPeer::getConnectionState(const SystemAddress& systemAddress)
{
    const SystemAddressEx& sysAdd = static_cast<const SystemAddressEx&>(systemAddress);

    // pending?
    {
        auto matchSysAddFunc = [&sysAdd](const RequestConnection* pOth) {
            return pOth->systemAddress == sysAdd;
        };

        core::CriticalSection::ScopedLock lock(connectionReqsCS_);

        if (std::find_if(connectionReqs_.begin(), connectionReqs_.end(), matchSysAddFunc) != connectionReqs_.end()) {
            return ConnectionState::Pending;
        }
    }

    const RemoteSystem* pRemoteSys = getRemoteSystem(sysAdd, false);
    if (!pRemoteSys) {
        return ConnectionState::NotConnected;
    }

    return pRemoteSys->getConnectionState();
}

void XPeer::cancelConnectionAttempt(const SystemAddress& target)
{
    const SystemAddressEx& sysAdd = static_cast<const SystemAddressEx&>(target);

    IPStr addStr;
    sysAdd.toString(addStr);

    X_LOG0_IF(vars_.debugEnabled(), "Net", "Cancelling Connection request to host: \"%s\"", addStr.c_str());

    removeConnectionRequest(sysAdd);
}

SendReceipt XPeer::send(const uint8_t* pData, const size_t lengthBytes, PacketPriority::Enum priority,
    PacketReliability::Enum reliability, SystemHandle systemHandle,
    OrderingChannel::Enum orderingChannel, bool broadcast,
    SendReceipt forceReceiptNumber)
{
    X_ASSERT(systemHandle != INVALID_SYSTEM_HANDLE, "Invalid system handle passed")(systemHandle);

    if (!lengthBytes) {
        return INVALID_SEND_RECEIPT;
    }

    X_ASSERT_NOT_NULL(pData);

    SendReceipt usedSendReceipt = forceReceiptNumber;

    if (forceReceiptNumber == INVALID_SEND_RECEIPT) {
        usedSendReceipt = incrementNextSendReceipt();
    }

    if (!broadcast && isLoopbackHandle(systemHandle)) {
        sendLoopback(pData, lengthBytes);

        if (reliability == PacketReliability::UnReliableWithAck) {
            // send a fake ack to loopback
            X_PACK_PUSH(1);
            struct Msg
            {
                uint8_t ID;
                SendReceipt receipt;
            };
            X_PACK_POP;

            Msg m;
            m.ID = MessageID::SndReceiptAcked;
            m.receipt = usedSendReceipt;
            static_assert(sizeof(m) == sizeof(SendReceipt) + sizeof(uint8_t), "overflow");

            sendLoopback(reinterpret_cast<const uint8_t*>(&m), sizeof(m));
        }

        return usedSendReceipt;
    }

    sendBuffered(
        pData,
        safe_static_cast<BitSizeT>(lengthBytes * 8),
        priority,
        reliability,
        orderingChannel,
        systemHandle,
        broadcast,
        usedSendReceipt);

    return usedSendReceipt;
}

void XPeer::sendBuffered(const uint8_t* pData, BitSizeT numberOfBitsToSend, PacketPriority::Enum priority,
    PacketReliability::Enum reliability, OrderingChannel::Enum orderingChannel, SystemHandle systemHandle, bool broadcast, SendReceipt receipt)
{
    X_ASSERT(numberOfBitsToSend > 0, "Null request should not reach here")(numberOfBitsToSend);
    X_ASSERT(systemHandle != INVALID_SYSTEM_HANDLE, "Invalid system handle passed")(systemHandle);

    BufferdCommand* pCmd = allocBufferdCmd(BufferdCommand::Cmd::Send, numberOfBitsToSend);
    std::memcpy(pCmd->pData, pData, core::bitUtil::bitsToBytes(numberOfBitsToSend));
    pCmd->priority = priority;
    pCmd->reliability = reliability;
    pCmd->orderingChannel = orderingChannel;
    pCmd->broadcast = broadcast;
    pCmd->systemHandle = systemHandle;
    pCmd->receipt = receipt;

    bufferdCmds_.push(pCmd);
}

void XPeer::notifyAndFlagForShutdown(RemoteSystem& rs, OrderingChannel::Enum orderingChannel, PacketPriority::Enum notificationPriority)
{
    IPStr ipStr;
    X_LOG0_IF(vars_.debugEnabled(), "Net", "sending disconnectNotification to remoteSystem: \"%s\"", rs.systemAddress.toString(ipStr));

    core::FixedBitStreamStack<16> bsOut;
    bsOut.write(MessageID::DisconnectNotification);

    core::TimeVal now = gEnv->pTimer->GetTimeNowReal();

    rs.sendReliable(
        bsOut,
        PacketPriority::Immediate,
        PacketReliability::ReliableOrdered,
        orderingChannel,
        now);

    rs.connectState = ConnectState::DisconnectAsap;
}

// -------------------------------------------

void XPeer::sendLoopback(const uint8_t* pData, size_t lengthBytes)
{
    Packet* pPacket = allocPacket(core::bitUtil::bytesToBits(lengthBytes));
    std::memcpy(pPacket->pData, pData, lengthBytes);
    pPacket->guid = getMyGUID();
    pPacket->systemHandle = LOOPBACK_SYSTEM_HANDLE;

    pushBackPacket(pPacket);
}

Packet* XPeer::receive(void)
{
    if (packetQue_.isEmpty()) {
        return nullptr;
    }

    Packet* pPacket = nullptr;
    if (packetQue_.tryPop(pPacket)) {
        X_ASSERT_NOT_NULL(pPacket->pData);
        return pPacket;
    }

    return nullptr;
}

void XPeer::clearPackets(void)
{
    if (packetQue_.isEmpty()) {
        return;
    }

    packetQue_.popAll([this](Packet* pPacket) {
        freePacket(pPacket);
    });
}

const RemoteSystem* XPeer::getRemoteSystem(SystemHandle handle, bool onlyActive) const
{
    auto& rs = remoteSystems_[handle];

    if (!onlyActive || rs.isActive) {
        return &rs;
    }

    return nullptr;
}

const RemoteSystem* XPeer::getRemoteSystem(const SystemAddressEx& systemAddress, bool onlyActive) const
{
    if (systemAddress == UNASSIGNED_SYSTEM_ADDRESS) {
        X_WARNING("Net", "Tried to get remote for unassigned address");
        return nullptr;
    }

    if (onlyActive) // fast path for active lookup, uses more cache friendly lookup.
    {
        for (const auto& rslu : remoteSystemLookup_) {
            if (rslu.systemAddress == systemAddress) {
                return rslu.pRemoteSystem;
            }
        }

        return nullptr;
    }

    for (size_t i = 0; i < remoteSystems_.size(); i++) {
        auto& rs = remoteSystems_[i];
        if (rs.systemAddress == systemAddress) {
            // can be active or none active.
            return &rs;
        }
    }

    return nullptr;
}

const RemoteSystem* XPeer::getRemoteSystem(const NetGUID guid, bool onlyActive) const
{
    if (guid == UNASSIGNED_NET_GUID) {
        X_WARNING("Net", "Tried to get remote for unassigned guid");
        return nullptr;
    }

    if (onlyActive) {
        for (const auto& rslu : remoteSystemLookup_) {
            if (rslu.guid == guid) {
                return rslu.pRemoteSystem;
            }
        }

        return nullptr;
    }

    for (auto& rs : remoteSystems_) {
        if (rs.guid == guid) {
            return &rs;
        }
    }

    return nullptr;
}

RemoteSystem* XPeer::getRemoteSystem(SystemHandle handle, bool onlyActive)
{
    auto& rs = remoteSystems_[handle];

    if (!onlyActive || rs.isActive) {
        return &rs;
    }

    return nullptr;
}

RemoteSystem* XPeer::getRemoteSystem(const SystemAddressEx& systemAddress, bool onlyActive)
{
    if (systemAddress == UNASSIGNED_SYSTEM_ADDRESS) {
        X_WARNING("Net", "Tried to get remote for unassigned address");
        return nullptr;
    }

    if (onlyActive) // fast path for active lookup, uses more cache friendly lookup.
    {
        for (const auto& rslu : remoteSystemLookup_) {
            if (rslu.systemAddress == systemAddress) {
                return rslu.pRemoteSystem;
            }
        }

        return nullptr;
    }

    for (size_t i = 0; i < remoteSystems_.size(); i++) {
        auto& rs = remoteSystems_[i];
        if (rs.systemAddress == systemAddress) {
            // can be active or none active.
            return &rs;
        }
    }

    return nullptr;
}

RemoteSystem* XPeer::getRemoteSystem(const NetGUID guid, bool onlyActive)
{
    if (guid == UNASSIGNED_NET_GUID) {
        X_WARNING("Net", "Tried to get remote for unassigned guid");
        return nullptr;
    }

    if (onlyActive) {
        for (const auto& rslu : remoteSystemLookup_) {
            if (rslu.guid == guid) {
                return rslu.pRemoteSystem;
            }
        }

        return nullptr;
    }

    for (auto& rs : remoteSystems_) {
        if (rs.guid == guid) {
            return &rs;
        }
    }

    return nullptr;
}

void XPeer::pushBackPacket(const RemoteSystem& rs, ReliabilityLayer::PacketData& data)
{
    // need to decide if i want to pass this back as up.
    auto up = std::move(data.getUP());

    // currently we only allow data to de returned that was allocated from the blockArena.
    // this is so i don't need to pass arena back.
    // and we know which arena to delete the data with when we get the packet back.
    X_ASSERT(up.getArena() == &blockArena_, "Should be block arena")(up.getArena());

    // want to take ownership of the data.
    Packet* pPacket = X_NEW(Packet, &poolArena_, "Packet");
    pPacket->systemHandle = rs.systemHandle;
    pPacket->guid = rs.guid;
    pPacket->length = safe_static_cast<uint32_t>(core::bitUtil::bitsToBytes(data.getNumbBits()));
    pPacket->bitLength = data.getNumbBits();
    pPacket->pData = up.release();

    packetQue_.push(pPacket);
}

Packet* XPeer::allocPacket(size_t lengthBits)
{
    Packet* pPacket = X_NEW(Packet, &poolArena_, "Packet");
    pPacket->systemHandle = INVALID_SYSTEM_HANDLE;
    pPacket->length = safe_static_cast<uint32_t>(core::bitUtil::bitsToBytes(lengthBits));
    pPacket->bitLength = safe_static_cast<uint32_t>(lengthBits);
    pPacket->pData = allocPacketData(core::bitUtil::bitsToBytes(lengthBits));
    return pPacket;
}

void XPeer::freePacket(Packet* pPacket)
{
    freePacketData(pPacket->pData);
    X_DELETE(pPacket, &poolArena_);
}

BufferdCommand* XPeer::allocBufferdCmd(BufferdCommand::Cmd::Enum type, size_t lengthBits)
{
    BufferdCommand* pCmd = X_NEW(BufferdCommand, &poolArena_, "BufferedCmd");
    pCmd->cmd = type;
    pCmd->numberOfBitsToSend = safe_static_cast<BitSizeT>(lengthBits);

    if (lengthBits) {
        pCmd->pData = allocPacketData(core::bitUtil::bitsToBytes(lengthBits));
    }
    else {
        pCmd->pData = nullptr;
    }
    return pCmd;
}

void XPeer::freeBufferdCmd(BufferdCommand* pBufCmd)
{
    if (pBufCmd->pData) {
        freePacketData(pBufCmd->pData);
    }
    X_DELETE(pBufCmd, &poolArena_);
}

uint8_t* XPeer::allocPacketData(size_t lengthBytes)
{
    return X_NEW_ARRAY_ALIGNED(uint8_t, lengthBytes, &blockArena_, "PacketData", 16);
}

void XPeer::freePacketData(uint8_t* pPacketData)
{
    X_DELETE_ARRAY(pPacketData, &blockArena_);
}

RecvData* XPeer::allocRecvData(void)
{
    return X_NEW(RecvData, arena_, "RecvData");
}

void XPeer::freeRecvData(RecvData* pRecvData)
{
    X_DELETE(pRecvData, arena_);
}

RequestConnection* XPeer::allocConnectionRequest(void)
{
    return X_NEW(RequestConnection, arena_, "connectionReq");
}

void XPeer::freeConnectionRequest(RequestConnection* pConReq)
{
    X_DELETE(pConReq, arena_);
}

void XPeer::removeConnectionRequest(const SystemAddressEx& sysAdd)
{
    {
        core::CriticalSection::ScopedLock lock(connectionReqsCS_);

        auto matchSysAddFunc = [&sysAdd](const RequestConnection* pOth) {
            return pOth->systemAddress == sysAdd;
        };

        auto it = std::find_if(connectionReqs_.begin(), connectionReqs_.end(), matchSysAddFunc);
        if (it != connectionReqs_.end()) {
            freeConnectionRequest(*it);
            connectionReqs_.erase(it);
            return;
        }
    }
    
    IPStr remoteStr;
    X_WARNING("Net", "Failed to find connection request for removal for Addr: \"%s\"", sysAdd.toString(remoteStr));
}

void XPeer::setDrainSockets(bool drainSocket)
{
    drainSockets_ = drainSocket;
}

// connection limits
void XPeer::setMaximumIncomingConnections(uint16_t numberAllowed)
{
    maxIncommingConnections_ = numberAllowed;

    if (maxIncommingConnections_ > maxPeers_) {
        X_ERROR("Net", "Tried to set max incoming connections(%" PRIu16 ") above max peers(%" PRIi32 "), capping", numberAllowed, maxPeers_);
        maxIncommingConnections_ = maxPeers_;
    }

    X_LOG0_IF(vars_.debugEnabled() > 1, "Net", "Set maxIncomingConnections to: ^5%" PRIu16, numberAllowed);
}

uint16_t XPeer::numberOfConnections(void) const
{
    // number of open connections.
    uint16_t num = 0;

    for (const auto& rs : remoteSystems_) {
        if (rs.isActive && rs.connectState == ConnectState::Connected) {
            ++num;
        }
    }

    return num;
}

// Ping
void XPeer::ping(const SystemHandle handle)
{
    X_ASSERT(handle != INVALID_SYSTEM_HANDLE, "Invalid system handle passed")(handle);

    core::FixedBitStreamStack<64> bsOut;

    core::TimeVal now = gEnv->pTimer->GetTimeNowReal();
    bsOut.write(MessageID::ConnectedPing);
    bsOut.write(now.GetValue());

    sendBuffered(
        bsOut,
        PacketPriority::Immediate,
        PacketReliability::UnReliable,
        OrderingChannel::Default,
        handle,
        false,
        0);
}

bool XPeer::ping(const HostStr& host, Port remotePort, bool onlyReplyOnAcceptingConnections,
    uint32_t connectionSocketIndex)
{
    X_ASSERT(connectionSocketIndex < sockets_.size(), "Socket index out of range")(connectionSocketIndex, sockets_.size());

    core::TimeVal now = gEnv->pTimer->GetTimeNowReal();

    core::FixedBitStreamStack<64> bsOut;
    if (onlyReplyOnAcceptingConnections) {
        bsOut.write(MessageID::UnConnectedPingOpenConnections);
    }
    else {
        bsOut.write(MessageID::UnConnectedPing);
    }
    bsOut.write(OFFLINE_MSG_ID);
    bsOut.write(now.GetValue());
    bsOut.write(guid_);

    NetSocket& socket = sockets_[connectionSocketIndex];
    SendParameters sp;
    sp.setData(bsOut);
    if (!sp.systemAddress.fromHost(host, remotePort, socket.getBoundAdd().getIPVersion())) {
        return false;
    }

    auto result = socket.send(sp);
    return result > 0;
}

void XPeer::sendPing(RemoteSystem& rs, PacketReliability::Enum rel)
{
    core::FixedBitStreamStack<64> bsOut;

    core::TimeVal now = gEnv->pTimer->GetTimeNowReal();
    bsOut.write(MessageID::ConnectedPing);
    bsOut.write(now.GetValue());

    rs.sendReliable(
        bsOut,
        PacketPriority::Immediate,
        rel,
        OrderingChannel::Default,
        now);
}

// bans at connection level.
void XPeer::addToBanList(const IPStr& ip, core::TimeVal timeout)
{
    if (ip.isEmpty()) {
        X_WARNING("Net", "Passed empty ip to ban list");
        return;
    }

    // check if the ip is valid format?
    if (!SystemAddressEx::isValidIP(ip, IpVersion::Any)) {
        X_ERROR("Net", "Can't add ban for \"%s\" it's a invalid address", ip.c_str());
        return;
    }

    SystemAddressEx sysAdd;
    if (!sysAdd.fromIP(ip)) {
        return;
    }

    addToBanList(sysAdd, timeout);
}

void XPeer::addToBanList(const SystemAddressEx& sysAdd, core::TimeVal timeout)
{
    IPStr ipStr;
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Adding ban: \"%s\" timeout: %" PRIi64 "ms", sysAdd.toString(ipStr, false), timeout.GetMilliSecondsAsInt64());

    core::TimeVal timeNow = gEnv->pTimer->GetTimeNowReal();

    auto assignBanTime = [timeNow](Ban& ban, core::TimeVal timeout) {
        if (timeout == 0_tv) {
            ban.timeOut = 0_tv;
        }
        else {
            ban.timeOut = timeNow + timeout;
        }
    };

    for (auto& ban : bans_) {
        if (ban.sysAdd.equalExcludingPort(sysAdd)) {
            assignBanTime(ban, timeout);
            return;
        }
    }

    if (bans_.size() == MAX_BANS) {
        X_ERROR("Net", "Can't add ban for \"%s\" max ban limit(%" PRIu32 ") reached", sysAdd.toString(ipStr, false), MAX_BANS);
        return;
    }

    auto& ban = bans_.AddOne();
    ban.sysAdd = sysAdd;
    assignBanTime(ban, timeout);
}

void XPeer::removeFromBanList(const IPStr& ip)
{
    if (ip.isEmpty()) {
        return;
    }

    X_LOG0_IF(vars_.debugEnabled(), "Net", "Removing ban: \"%s\"", ip.c_str());

    if (!SystemAddressEx::isValidIP(ip, IpVersion::Any)) {
        X_ERROR("Net", "Can't remove ban for \"%s\" it's a invalid address", ip.c_str());
        return;
    }

    SystemAddressEx sysAdd;
    if (!sysAdd.fromIP(ip)) {
        return;
    }

    auto findBanIP = [&sysAdd](const Ban& oth) {
        return oth.sysAdd.equalExcludingPort(sysAdd);
    };

    auto it = std::find_if(bans_.begin(), bans_.end(), findBanIP);
    if (it != bans_.end()) {
        bans_.erase(it);
    }
    else {
        X_LOG0_IF(vars_.debugEnabled(), "Net", "Failed to remove ban, no entry for \"%s\" found", ip.c_str());
    }
}

bool XPeer::isBanned(const IPStr& ip)
{
    if (bans_.isEmpty()) {
        return false;
    }

    SystemAddressEx sysAdd;
    if (!sysAdd.fromIP(ip)) {
        X_ERROR("Net", "BanCheck, failed to load address from ip");
        return false; // say they are banned if we can't check?
    }

    return isBanned(sysAdd);
}

bool XPeer::isBanned(const SystemAddressEx& sysAdd)
{
    if (bans_.isEmpty()) {
        return false;
    }

    for (auto it = bans_.begin(); it != bans_.end(); /* ++it */) {
        auto& ban = (*it);

        // expired?
        if (ban.timeOut.GetValue() != 0) {
            core::TimeVal time = gEnv->pTimer->GetTimeNowReal();
            if (time > ban.timeOut) {
                it = bans_.erase(it);
                continue;
            }
        }

        if (ban.sysAdd.equalExcludingPort(sysAdd)) {
            return true;
        }

        ++it;
    }

    return false;
}

void XPeer::clearBanList(void)
{
    bans_.clear();
}

void XPeer::listBans(void) const
{
    core::TimeVal timeNow = gEnv->pTimer->GetTimeNowReal();

    for (const auto& ban : bans_) {
        int64_t msLeft = -1;

        if (ban.timeOut.GetValue() != 0) {
            msLeft = (ban.timeOut - timeNow).GetMilliSecondsAsInt64();
            if (msLeft < 0) {
                msLeft = 0;
            }
        }

        IPStr ipStr;
        X_LOG0("Net", "Ban: \"%s\" timeLeftMS: ^5%" PRIi64, ban.sysAdd.toString(ipStr, false), msLeft);
    }
}

void XPeer::listLocalAddress(void) const
{
    X_LOG0("Net", "LocalAdd ^5%" PRIuS, ipList_.size());
    X_LOG_BULLET;
    for (auto& ip : ipList_) {
        IPStr addStr;
        X_LOG0("Net", "local address: \"%s\"", ip.toString(addStr));
    }
}

bool XPeer::getPingInfo(SystemHandle systemHandle, PingInfo& info) const
{
    X_ASSERT(systemHandle != INVALID_SYSTEM_HANDLE, "Invalid system handle passed")(systemHandle);

    const RemoteSystem* pRemoteSys = getRemoteSystem(systemHandle, false);
    if (!pRemoteSys) {
        return false;
    }

    info.cur = pRemoteSys->getLastPing();
    info.avg = pRemoteSys->getAveragePing();
    info.min = pRemoteSys->lowestPing;
    return true;
}

SystemAddress XPeer::getMyBoundAddress(void) const
{
    if (sockets_.isEmpty()) {
        return SystemAddress();
    }

    return sockets_.front().getBoundAdd();
}


// MTU for a given system
int32_t XPeer::getMTUSize(SystemHandle systemHandle) const
{
    X_ASSERT(systemHandle != INVALID_SYSTEM_HANDLE, "Invalid system handle passed")(systemHandle);

    if (systemHandle == INVALID_SYSTEM_HANDLE) {
        return defaultMTU_;
    }

    auto* pRemoteSys = getRemoteSystem(systemHandle, false);
    if (pRemoteSys) {
        return pRemoteSys->MTUSize;
    }

    X_ERROR("Net", "Failed to find remote system for MTU size returning default");
    return defaultMTU_;
}

SystemAddress XPeer::getAddressForHandle(SystemHandle systemHandle) const
{
    X_ASSERT(systemHandle != INVALID_SYSTEM_HANDLE, "Invalid system handle passed")(systemHandle);

    if (systemHandle == INVALID_SYSTEM_HANDLE) {
        return ipList_.front();
    }

    auto* pRemoteSys = getRemoteSystem(systemHandle, false);
    if (pRemoteSys) {
        return pRemoteSys->systemAddress;
    }

    X_ERROR("Net", "Failed to find remote system for MTU size returning default");
    return SystemAddress();
}

NetGUID XPeer::getGuidForHandle(SystemHandle systemHandle) const
{
    X_ASSERT(systemHandle != INVALID_SYSTEM_HANDLE, "Invalid system handle passed")(systemHandle);

    auto* pRemoteSys = getRemoteSystem(systemHandle, false);
    if (pRemoteSys) {
        return pRemoteSys->guid;
    }

    X_ERROR("Net", "Failed to find remote system for Guid");
    return NetGUID();
}

bool XPeer::getStatistics(const NetGUID guid, NetStatistics& stats)
{
    auto* pRemoteSys = getRemoteSystem(guid, false);

    if (!pRemoteSys) {
        X_WARNING("Net", "Failed to find remote system for stat retrieval");
        return false;
    }

    stats.connectionStartTime = pRemoteSys->connectionTime;

    pRemoteSys->relLayer.getStatistics(stats);
    return true;
}

NetBandwidthStatistics XPeer::getBandwidthStatistics(void) const
{
    NetBandwidthStatistics stats;
    core::zero_object(stats);

#if X_ENABLE_NET_STATS

    for (const auto& socket : sockets_) {
        stats += socket.getStats();
    }

#endif // !X_ENABLE_NET_STATS

    return stats;
}

// ~IPeer

size_t XPeer::getNumRemoteInitiatedConnections(void) const
{
    size_t num = 0;

    for (const auto& rc : remoteSystems_) {
        if (rc.connectState == ConnectState::Connected && rc.isActive && !rc.weStartedconnection) {
            ++num;
        }
    }

    return num;
}

void XPeer::listRemoteSystems(bool verbose) const
{
    auto timeNow = gEnv->pTimer->GetTimeNowReal();

    for (const auto& rs : remoteSystems_) {
        if (!rs.isActive) {
            continue;
        }

        auto connectionElapsed = timeNow - rs.connectionTime;

        IPStr ipStr;
        core::HumanDuration::Str durStr;

        if (!verbose) {
            X_LOG0("Net", "Host: \"%s\" connectionTime: ^5%s^7 state: ^5%s",
                rs.systemAddress.toString(ipStr),
                core::HumanDuration::toString(durStr, connectionElapsed.GetMilliSecondsAsInt64()),
                ConnectionState::ToString(rs.getConnectionState()));
        }
        else {
            core::HumanSize::Str sizeStr;

            NetStatistics stats;
            rs.relLayer.getStatistics(stats);

            X_LOG0("Net", "Host: \"%s\" connectionTime: ^5%s^7 state: ^5%s^7 memUsage: ^5%s",
                rs.systemAddress.toString(ipStr),
                core::HumanDuration::toString(durStr, connectionElapsed.GetMilliSecondsAsInt64()),
                ConnectionState::ToString(rs.getConnectionState()),
                core::HumanSize::toString(sizeStr, stats.internalMemUsage));
            X_LOG_BULLET;
            X_LOG0("Net", "bytesSent: ^5%s", core::HumanSize::toString(sizeStr, stats.runningMetrics[NetStatistics::Metric::BytesSent]));
            X_LOG0("Net", "bytesResent: ^5%s", core::HumanSize::toString(sizeStr, stats.runningMetrics[NetStatistics::Metric::BytesResent]));
            X_LOG0("Net", "bytesPushed: ^5%s", core::HumanSize::toString(sizeStr, stats.runningMetrics[NetStatistics::Metric::BytesPushed]));
            X_LOG0("Net", "bytesReceivedIgnored: ^5%s", core::HumanSize::toString(sizeStr, stats.runningMetrics[NetStatistics::Metric::BytesReceivedIgnored]));
            X_LOG0("Net", "bytesReceivedProcessed: ^5%s", core::HumanSize::toString(sizeStr, stats.runningMetrics[NetStatistics::Metric::BytesReceivedProcessed]));
            X_LOG0("Net", "actualBytesSent: ^5%s", core::HumanSize::toString(sizeStr, stats.runningMetrics[NetStatistics::Metric::ActualBytesSent]));
            X_LOG0("Net", "actualBytesReceived: ^5%s", core::HumanSize::toString(sizeStr, stats.runningMetrics[NetStatistics::Metric::ActualBytesReceived]));
        }
    }
}

void XPeer::processConnectionRequests(UpdateBitStream& updateBS, core::TimeVal timeNow)
{
    if (connectionReqs_.isEmpty()) {
        return;
    }

    core::CriticalSection::ScopedLock lock(connectionReqsCS_);

    for (auto it = connectionReqs_.begin(); it != connectionReqs_.end(); /* ++it */) {
        RequestConnection& cr = *(*it);

        // time for a request?
        if (cr.nextRequestTime > timeNow) {
            ++it;
            continue;
        }

        IPStr addStr;

        // give up?
        if (cr.numRequestsMade == cr.retryCount) {
            X_LOG0_IF(vars_.debugEnabled(), "Net", "Reached max connection retry count for: \"%s\"", cr.systemAddress.toString(addStr));

            auto sa = cr.systemAddress; // copy before free
            freeConnectionRequest(*it);
            it = connectionReqs_.erase(it);

            // send packet.
            pushPacket(MessageID::ConnectionRequestFailed, sa);
            continue;
        }

        X_LOG0_IF(vars_.debugEnabled(), "Net", "Dispatching open connection request(%" PRIu8 "): \"%s\"",
            cr.numRequestsMade, cr.systemAddress.toString(addStr));

        ++cr.numRequestsMade;
        cr.nextRequestTime = timeNow + cr.retryDelay;

        updateBS.reset();
        updateBS.write(MessageID::OpenConnectionRequest);
        updateBS.write(OFFLINE_MSG_ID);
        updateBS.write<uint8_t>(PROTO_VERSION_MAJOR);
        updateBS.write<uint8_t>(PROTO_VERSION_MINOR);

        // lets be cute and send the build revision.
        // this will just be a hex string.
        constexpr auto digest = HexString<std::array<uint8_t, core::Hash::SHA1Digest::NUM_BYTES>>(X_STRINGIZE(X_ENGINE_BUILD_REF));
        updateBS.write<uint8_t>(digest.data(), digest.size());

        // devide the mtu array by retry count, so that we try mtu index 0 for 1/3 of request if mtusizes is 3.
        size_t mtuIdx = cr.numRequestsMade / (cr.retryCount / MTUSizesArr.size());
        mtuIdx = core::Min(mtuIdx + cr.MTUIdxShift, MTUSizesArr.size() - 1);

        size_t MTUSize = MTUSizesArr[mtuIdx] - UDP_HEADER_SIZE;
        updateBS.zeroPadToLength(MTUSize);

        core::TimeVal timeSend = gEnv->pTimer->GetTimeNowReal();

        NetSocket& socket = sockets_[cr.socketIdx];
        SendParameters sp;
        sp.setData(updateBS);
        sp.systemAddress = cr.systemAddress;
        if (socket.send(sp) == -WSAEMSGSIZE) // A message sent on a datagram socket was larger than the internal message buffer
        {
            // skip this MTU size.
            cr.MTUIdxShift++;
            cr.nextRequestTime = timeNow;

            X_LOG0_IF(vars_.debugEnabled(), "Net", "Moving to next MTU size for connection request");
        }
        else {
            core::TimeVal timeSendFin = gEnv->pTimer->GetTimeNowReal();
            core::TimeVal timeToSend = timeSendFin - timeSend;
            if (timeToSend > core::TimeVal::fromMS(100)) {
                // if you took more than 100ms drop, to lowest MTU size.
                cr.MTUIdxShift = safe_static_cast<uint8_t>(MTUSizesArr.size() - 1);

                X_LOG0_IF(vars_.debugEnabled(), "Net", "Moving to last MTU size for connection request");
            }
        }

        ++it;
    }
}

void XPeer::processBufferdCommands(UpdateBitStream& updateBS, core::TimeVal timeNow)
{
    if (bufferdCmds_.isEmpty()) {
        return;
    }

    BufferdCommand* pBufCmd;
    while (bufferdCmds_.tryPop(pBufCmd)) {
        X_ASSERT_NOT_NULL(pBufCmd); // no null ref plz!
        auto& cmd = *pBufCmd;

        if (cmd.cmd == BufferdCommand::Cmd::Send) {
            X_ASSERT(cmd.orderingChannel < MAX_ORDERED_STREAMS, "Invalid channel")(cmd.orderingChannel);

            if (cmd.broadcast) {
                X_ASSERT_NOT_IMPLEMENTED();

                // for broadcast pRemoteSystem is allowed to be null.
                // it's used for exclusions.
                RemoteSystem* pRemoteSystem = nullptr;
                if (cmd.systemHandle != INVALID_SYSTEM_HANDLE) {
                    pRemoteSystem = getRemoteSystem(cmd.systemHandle, true);
                }

                // broadcast too all but pRemoteSystem.
                for (auto* pRemote : activeRemoteSystems_) {
                    if (pRemote == pRemoteSystem) {
                        continue;
                    }

                    if (!pRemoteSystem->sendReliable(
                            cmd.pData,
                            cmd.numberOfBitsToSend,
                            true,
                            cmd.priority,
                            cmd.reliability,
                            cmd.orderingChannel,
                            timeNow,
                            cmd.receipt)) {
                        X_WARNING("Net", "Failed to send reliable packet, when broadcasting");
                    }
                }

                cmd.pData = nullptr;
            }
            else {
                RemoteSystem* pRemoteSystem = getRemoteSystem(cmd.systemHandle, true);
                if (!pRemoteSystem) {
                    freeBufferdCmd(pBufCmd);
                    continue;
                }

                if (!pRemoteSystem->canSend()) {
                    X_WARNING_IF(vars_.debugEnabled(), "Net", "Tried to send data to remote, where sending is currently disabled");
                    freeBufferdCmd(pBufCmd);
                    continue;
                }

                if (!pRemoteSystem->sendReliable(
                        cmd.pData,
                        cmd.numberOfBitsToSend,
                        true,
                        cmd.priority,
                        cmd.reliability,
                        cmd.orderingChannel,
                        timeNow,
                        cmd.receipt)) {
                    X_WARNING("Net", "Failed to send reliable packet");
                }

                cmd.pData = nullptr;
            }
        }
        else if (cmd.cmd == BufferdCommand::Cmd::CloseConnection) {
            RemoteSystem* pRemoteSystem = nullptr;

            if (cmd.systemHandle != INVALID_SYSTEM_HANDLE) {
                pRemoteSystem = getRemoteSystem(cmd.systemHandle, true);
            }
            else {
                pRemoteSystem = getRemoteSystem(cmd.systemAddress, true);

                // is this a connection request?
                if (!pRemoteSystem)
                {
                    core::CriticalSection::ScopedLock lock(connectionReqsCS_);

                    for (size_t i = 0; i < connectionReqs_.size(); i++) {
                        RequestConnection* pReq = connectionReqs_[i];

                        if (pReq->systemAddress == cmd.systemAddress) {
                            X_WARNING("Net", "Connection was closed for a pending connectionRequest");

                            freeConnectionRequest(pReq);
                            connectionReqs_.removeIndex(i);
                            pushPacket(MessageID::ConnectionRequestFailed, cmd.systemAddress);
                            continue;
                        }
                    }
                }
            }

            if (!pRemoteSystem) {
                X_WARNING("Net", "Failed to find system for connection close");
                freeBufferdCmd(pBufCmd);
                continue;
            }

            if (!pRemoteSystem->isActive || pRemoteSystem->connectState != ConnectState::Connected) {
                X_LOG0_IF(vars_.debugEnabled(), "Net", "Skipping closeConnection request, the remote is already disconnecting/disconnected. state: \"%s\"",
                    ConnectState::ToString(pRemoteSystem->connectState));
                freeBufferdCmd(pBufCmd);
                continue;
            }

            if (cmd.sendDisconnectionNotification) {
                X_ASSERT(cmd.orderingChannel < MAX_ORDERED_STREAMS, "Invalid channel")(cmd.orderingChannel);

                // don't disconnect yet.
                // send notification and close after.
                notifyAndFlagForShutdown(*pRemoteSystem, cmd.orderingChannel, cmd.priority);
            }
            else {
                X_ASSERT_NOT_NULL(pRemoteSystem);
                // tell the game now.
                pushPacket(MessageID::ConnectionClosed, *pRemoteSystem);

                // close it.
                disconnectRemote(*pRemoteSystem);
            }
        }
        else {
            X_ASSERT_UNREACHABLE();
        }

        freeBufferdCmd(pBufCmd);
    }
}

void XPeer::remoteReliabilityTick(UpdateBitStream& updateBS, core::TimeVal timeNow)
{
    for (auto* pRS : activeRemoteSystems_) {
        X_ASSERT_NOT_NULL(pRS);
        remoteReliabilityTick(*pRS, updateBS, timeNow);
    }
}

void XPeer::Job_remoteReliabilityTick(RemoteSystem** pRemoteSystems, uint32_t count)
{
    BitStreamMTU updateBS;
    core::TimeVal timeNow = gEnv->pTimer->GetTimeNowReal();

    for (uint32_t i = 0; i < count; i++) {
        X_ASSERT_NOT_NULL(pRemoteSystems[i]);

        remoteReliabilityTick(*pRemoteSystems[i], updateBS, timeNow);
    }
}

void XPeer::remoteReliabilityTick(RemoteSystem& rs, UpdateBitStream& updateBS, core::TimeVal timeNow)
{
    X_ASSERT(rs.isActive, "System not active")();

    rs.relLayer.update(updateBS, *rs.pNetSocket, rs.systemAddress, rs.MTUSize, timeNow);

    const bool deadConnection = rs.relLayer.isConnectionDead();
    const bool disconnecting = rs.connectState == ConnectState::DisconnectAsap || rs.connectState == ConnectState::DisconnectAsapSilent;
    const bool disconnectingAfterAck = rs.connectState == ConnectState::DisconnectOnNoAck;
    const bool waitingforConnection = rs.connectState == ConnectState::RequestedConnection || rs.connectState == ConnectState::HandlingConnectionRequest || rs.connectState == ConnectState::UnverifiedSender;

    // has this connection not completed yet?
    const bool connectionOpenTimeout = (waitingforConnection && timeNow > (rs.connectionTime + core::TimeVal::fromMS(vars_.dropPartialConnectionsMS())));
    const bool dissconectNoAck = (disconnectingAfterAck && !rs.relLayer.isWaitingForAcks());
    const bool disconnectingNoData = disconnecting && !rs.relLayer.pendingOutgoingData() && rs.relLayer.numMsgInResendBuffer() == 0;
    const bool socketClosed = false;

    if (deadConnection || disconnectingNoData || connectionOpenTimeout || dissconectNoAck || socketClosed) {
        if (vars_.debugEnabled()) {
            auto closeReason = "<ukn>"_sv;

            if (deadConnection) {
                // did we timeout waiting for acks to send?
                if (disconnectingAfterAck) {
                    closeReason = "Disconnect noack timeout"_sv;
                }
                else {
                    closeReason = "Connection timeout"_sv;
                }
            }
            else if (disconnectingNoData) {
                closeReason = "Disconnection request"_sv;
            }
            else if (connectionOpenTimeout) {
                closeReason = "Partial connection timeout"_sv;
            }
            else if (dissconectNoAck) {
                closeReason = "Disconnect request(noack)"_sv;
            }

            IPStr ipStr;
            X_LOG0("Net", "Closing connection for remote system: \"%s\" reason: \"%.*s\" State: \"%s\"", 
                rs.systemAddress.toString(ipStr), closeReason.length(), closeReason.data(), ConnectState::ToString(rs.connectState));
        }

        if (rs.connectState != ConnectState::DisconnectAsapSilent)
        {
            MessageID::Enum msgId = MessageID::DisconnectNotification;
            if (rs.connectState == ConnectState::RequestedConnection) {
                msgId = MessageID::ConnectionRequestFailed;
            }
            else if (rs.connectState == ConnectState::Connected) {
                msgId = MessageID::ConnectionClosed;
            }

            pushPacket(msgId, rs);
        }

        disconnectRemote(rs);
        return;
    }

    if (rs.connectState == ConnectState::Connected && timeNow > rs.nextPingTime) {
        rs.nextPingTime = timeNow + core::TimeVal::fromMS(vars_.pingTimeMS());
        sendPing(rs, PacketReliability::UnReliable);
    }

    // do we have any packets?
    ReliabilityLayer::PacketData data(arena_);
    while (rs.relLayer.receive(data)) {
        // okay we got a packet :D!
        // the first byte should be msgId.
        if (data.getNumbBits() < 8) {
            continue;
        }

        core::FixedBitStreamNoneOwning stream(data.begin(), data.end(), true);
        MessageID::Enum msgId = stream.read<MessageID::Enum>();

        if (msgId < MessageID::ENUM_COUNT) {
            X_LOG0_IF(vars_.debugEnabled(), "Net", "Received reliable messageId: \"%s\"", MessageID::ToString(msgId));
        }
        else {
            // user packet data.
        }

        updateBS.reset();

        // okay process the msg!
        switch (msgId) {
            case MessageID::ConnectionRequest:
                handleConnectionRequest(updateBS, stream, rs, timeNow);
                break;
            case MessageID::ConnectionRequestAccepted:
                handleConnectionRequestAccepted(updateBS, stream, rs, timeNow);
                break;
            case MessageID::ConnectionRequestHandShake:
                handleConnectionRequestHandShake(updateBS, stream, rs);
                break;

            case MessageID::ConnectedPing:
                handleConnectedPing(updateBS, stream, rs, timeNow);
                break;
            case MessageID::ConnectedPong:
                handleConnectedPong(updateBS, stream, rs);
                break;

            case MessageID::DisconnectNotification:
                handleDisconnectNotification(updateBS, stream, rs);
                break;

            case MessageID::InvalidPassword:
                handleInvalidPassword(updateBS, stream, rs);
                break;

            default:
                // we send this out.
                pushBackPacket(rs, data);
                break;
        }
    }
}

void XPeer::processRecvData(UpdateBitStream& updateBS, core::TimeVal timeNow)
{
    X_UNUSED(timeNow);
    
    // For testing we drain the recv buffer.
    if (drainSockets_) {
        for (auto& socket : sockets_) {
            socket.waitTillRecvDrained();
        }
    }

    // use a single BS instance for processing all the data, to try improve cache hits.
    RecvData* pRecvData = nullptr;
    while (recvDataQue_.tryPop(pRecvData)) {
        updateBS.reset();
        processRecvData(updateBS, pRecvData, 0);

        freeRecvData(pRecvData);
    }
}

void XPeer::processRecvData(core::FixedBitStreamBase& updateBS, RecvData* pData, int32_t byteOffset)
{
    X_ASSERT_NOT_NULL(pData);
    X_ASSERT_NOT_NULL(pData->pSrcSocket);
    X_ASSERT(pData->bytesRead > 0, "RecvData with no data should not reach here")(pData, pData->bytesRead);

    if (isBanned(pData->systemAddress)) {
        updateBS.write(MessageID::ConnectionBanned);
        updateBS.write(OFFLINE_MSG_ID);
        updateBS.write(guid_);

        SendParameters sp;
        sp.setData(updateBS);
        sp.systemAddress = pData->systemAddress;
        pData->pSrcSocket->send(sp);
        return;
    }

    uint8_t* pBegin = (pData->data + byteOffset);
    uint8_t* pEnd = (pData->data + pData->bytesRead);
    X_ASSERT(pBegin < pEnd, "Stream is empty")(pBegin, pEnd);

    size_t dataLength = union_cast<size_t>(pEnd - pBegin);

    // is this a offline msg.
    if (dataLength == 1) {
        if (static_cast<MessageID::Enum>(*pBegin) == MessageID::SendTest) {
            return;
        }
    }
    else if (dataLength >= OFFLINE_MSG_ID.size()) {
        if (std::memcmp(pBegin + 1, OFFLINE_MSG_ID.data(), OFFLINE_MSG_ID.size()) == 0) {
            processOfflineMsg(updateBS, pData, pBegin, pEnd);
            return;
        }
        // fall through
    }

    RemoteSystem* pRemoteSys = getRemoteSystem(pData->systemAddress, true);
    if (!pRemoteSys) {
        IPStr ipStr;
        X_ERROR("Net", "Received reliable message for none connected client: \"%s\"", pData->systemAddress.toString(ipStr, true));

        // temp ban them.
        addToBanList(pData->systemAddress, core::TimeVal::fromMS(vars_.unexpectedMsgBanTime()));
        return;
    }

    bool res = pRemoteSys->relLayer.recv(
        pData->data + byteOffset,
        pData->bytesRead,
        *pData->pSrcSocket,
        pData->systemAddress,
        pData->timeRead,
        pRemoteSys->MTUSize);

    if (!res) {
        X_ERROR("Net", "Error processing received packet");
    }
}

void XPeer::processOfflineMsg(UpdateBitStream& updateBS, RecvData* pData,
    uint8_t* pBegin, uint8_t* pEnd)
{
    core::FixedBitStreamNoneOwning stream(pBegin, pEnd, true);
    X_ASSERT(stream.sizeInBytes() > OFFLINE_MSG_ID.size(), "Called with too small buffer")(stream.sizeInBytes());

    MessageID::Enum msgId = stream.read<MessageID::Enum>();
    stream.skipBytes(OFFLINE_MSG_ID.size());
    X_ASSERT(msgId < MessageID::ENUM_COUNT, "Invalid msg id")(msgId);

    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received offline messageId: \"%s\"", MessageID::ToString(msgId));

    switch (msgId) {
        case MessageID::ProtcolVersionIncompatible:
        case MessageID::ConnectionRequestFailed:
        case MessageID::ConnectionBanned:
        case MessageID::ConnectionNoFreeSlots:
        case MessageID::ConnectionRateLimited:
        case MessageID::InvalidPassword:
            handleConnectionFailure(updateBS, pData, stream, msgId);
            break;

        case MessageID::OpenConnectionRequest:
            handleOpenConnectionRequest(updateBS, pData, stream);
            break;
        case MessageID::OpenConnectionRequestStage2:
            handleOpenConnectionRequestStage2(updateBS, pData, stream);
            break;
        case MessageID::OpenConnectionResponse:
            handleOpenConnectionResponse(updateBS, pData, stream);
            break;
        case MessageID::OpenConnectionResponseStage2:
            handleOpenConnectionResponseStage2(updateBS, pData, stream);
            break;

            // hello.
        case MessageID::UnConnectedPing:
        case MessageID::UnConnectedPingOpenConnections:
            handleUnConnectedPing(updateBS, pData, stream, msgId == MessageID::UnConnectedPingOpenConnections);
            return;

            // hello.
        case MessageID::SendTest:
            return;

            // one day.
        case MessageID::StuNotFnd:
            return;

            break;
        default:
            X_ERROR("Net", "Unhandled message: \"%s\"", MessageID::ToString(msgId));
            break;
    }
}

void XPeer::handleConnectionFailure(UpdateBitStream& bsBuf, RecvData* pData, RecvBitStream& bs, MessageID::Enum failureType)
{
    NetGUID guid;
    bs.read(guid);

    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received connection failure: \"%s\"", MessageID::ToString(failureType));

    Packet* pPacket = nullptr;

    size_t packetSize = sizeof(MessageID::Enum) + SystemAddress::serializedSize();
    if (failureType == MessageID::ConnectionRateLimited) {
        packetSize += sizeof(uint32_t);
    }

    pPacket = allocPacket(core::bitUtil::bytesToBits(packetSize));

    core::FixedBitStreamNoneOwning packetBs(pPacket->pData, pPacket->pData + pPacket->length, false);
    packetBs.write(failureType);
    pData->systemAddress.writeToBitStream(packetBs);

    // rip connection.
    if (failureType == MessageID::ConnectionRateLimited) {
        uint32_t waitMS = bs.read<uint32_t>();
        packetBs.write(waitMS);
    }

    // remove connection request.
    removeConnectionRequest(pData->systemAddress);

    pPacket->guid = guid;
    pushBackPacket(pPacket);
}

void XPeer::handleOpenConnectionRequest(UpdateBitStream& bsOut, RecvData* pData, RecvBitStream& bs)
{
    // hello, pleb.
    uint8_t protoVersionMajor = bs.read<uint8_t>();
    uint8_t protoVersionMinor = bs.read<uint8_t>();

    // the build revision.
    core::Hash::SHA1Digest revision;
    bs.read(revision.bytes, revision.NUM_BYTES);

    // TODO: log?
    X_UNUSED(revision);

    IPStr ipStr;
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received open connection request from \"%s\" with proto version: ^5%" PRIu8 ".%" PRIu8,
        pData->systemAddress.toString(ipStr), protoVersionMinor, protoVersionMajor);

    if (protoVersionMajor != PROTO_VERSION_MAJOR || protoVersionMinor != PROTO_VERSION_MINOR) {
        // we don't support you.
        bsOut.write(MessageID::ProtcolVersionIncompatible);
        bsOut.write(OFFLINE_MSG_ID);
        bsOut.write<uint8_t>(PROTO_VERSION_MAJOR);
        bsOut.write<uint8_t>(PROTO_VERSION_MINOR);
        bsOut.write(guid_);

        SendParameters sp;
        sp.setData(bsOut);
        sp.systemAddress = pData->systemAddress;
        pData->pSrcSocket->send(sp);
        return;
    }

    // hey i like you!
    bsOut.write(MessageID::OpenConnectionResponse);
    bsOut.write(OFFLINE_MSG_ID);
    bsOut.write(guid_);
    bsOut.write<uint16_t>(MAX_MTU_SIZE); // i'll show you mine if you show me your's...

    SendParameters sp;
    sp.setData(bsOut);
    sp.systemAddress = pData->systemAddress;
    pData->pSrcSocket->send(sp);
}

void XPeer::handleOpenConnectionResponse(UpdateBitStream& bsOut, RecvData* pData, RecvBitStream& bs)
{
    // hello almighty server.
    NetGUID serverGuid;
    uint16_t mtu;

    bs.read(serverGuid);
    bs.read(mtu);

    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received open connection response");

    // find this fuck.
    {
        core::CriticalSection::ScopedLock lock(connectionReqsCS_);

        for (auto* pReq : connectionReqs_) {
            if (pReq->systemAddress == pData->systemAddress) {
                // oh it was me, send stage2
                bsOut.write(MessageID::OpenConnectionRequestStage2);
                bsOut.write(OFFLINE_MSG_ID);
                bsOut.write(guid_);
                pReq->systemAddress.writeToBitStream(bsOut);
                bsOut.write<uint16_t>(MAX_MTU_SIZE);

                SendParameters sp;
                sp.setData(bsOut);
                sp.systemAddress = pData->systemAddress;
                pData->pSrcSocket->send(sp);
                return;
            }
        }
    }

    IPStr remoteStr;
    pData->systemAddress.toString(remoteStr);
    X_ERROR("Net", "Received connection response for remote system we are not trying to connect to: \"%s\"", remoteStr.c_str());
}

void XPeer::handleOpenConnectionRequestStage2(UpdateBitStream& bsOut, RecvData* pData, RecvBitStream& bs)
{
    SystemAddressEx bindingAdd;
    NetGUID clientGuid;
    uint16_t mtu;

    bs.read(clientGuid);
    bindingAdd.fromBitStream(bs);
    bs.read(mtu);

    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received open connection request2");

    // right lets check the list.
    const RemoteSystem* pSys = getRemoteSystem(bindingAdd, true);
    const RemoteSystem* pSysGuid = getRemoteSystem(clientGuid, true);

    // response matrix:
    // IPAddrInUse GuidInUse	response
    // true		   true	 		already connected
    // false       true     	already connected.
    // true		   false	 	already connected
    // false	   false	 	allow connection

    if (pSys || pSysGuid) {
        bsOut.write(MessageID::AlreadyConnected);
        bsOut.write(OFFLINE_MSG_ID);
        bsOut.write(guid_);
    }
    else {
        if (!accpetingIncomingConnections()) {
            // stuff like reserved slots needs to be handled outside network layer.
            bsOut.write(MessageID::ConnectionNoFreeSlots);
            bsOut.write(OFFLINE_MSG_ID);
            bsOut.write(guid_);
        }
        else {
            core::TimeVal lastConnectDelta;
            if (vars_.rlConnectionsPerIP() && isIpConnectSpamming(pData->systemAddress, &lastConnectDelta)) {
                IPStr addStr;
                X_LOG0_IF(vars_.debugEnabled(), "Net", "Received connection req from \"%s\" %gms ago, rate limiting",
                    pData->systemAddress.toString(addStr), lastConnectDelta.GetMilliSeconds());

                // i don't care if this ratelimit is almost over and you could technically connect again sooner.
                uint32_t timeMs = vars_.rlConnectionsPerIPBanTimeMS();

                bsOut.write(MessageID::ConnectionRateLimited);
                bsOut.write(OFFLINE_MSG_ID);
                bsOut.write(guid_);
                bsOut.write(timeMs);
            }
            else {
                auto* pRemoteSys = addRemoteSystem(pData->systemAddress, clientGuid, mtu, pData->pSrcSocket, bindingAdd, ConnectState::UnverifiedSender);
                X_UNUSED(pRemoteSys);

                bsOut.write(MessageID::OpenConnectionResponseStage2);
                bsOut.write(OFFLINE_MSG_ID);
                bsOut.write(guid_);
                pData->systemAddress.writeToBitStream(bsOut);
                bsOut.write<uint16_t>(mtu);

                // generate a nonce, for password if required.
                pRemoteSys->nonce = generateNonce();

                bsOut.write(pRemoteSys->nonce);
            }
        }
    }

    SendParameters sp;
    sp.setData(bsOut);
    sp.systemAddress = pData->systemAddress;
    pData->pSrcSocket->send(sp);
}

void XPeer::handleOpenConnectionResponseStage2(UpdateBitStream& bsOut, RecvData* pData, RecvBitStream& bs)
{
    // meow.
    // if we here the response was valid annd == OpenConnectionResponseStage2
    SystemAddressEx bindingAdd;
    NetGUID clientGuid;
    uint16_t mtu;
    NonceHash::Digest nonce;

    bs.read(clientGuid);
    bindingAdd.fromBitStream(bs);
    bs.read(mtu);
    bs.read(nonce);

    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received connection response2");

    // find it.
    {
        core::CriticalSection::ScopedLock lock(connectionReqsCS_);

        for (auto& pReq : connectionReqs_) {
            if (pReq->systemAddress == pData->systemAddress) {
                RemoteSystem* pSys = getRemoteSystem(bindingAdd, true);
                if (!pSys) {
                    // add systen
                    pSys = addRemoteSystem(pData->systemAddress, clientGuid, mtu, pData->pSrcSocket, bindingAdd, ConnectState::UnverifiedSender);
                }
                if (pSys) {
                    pSys->connectState = ConnectState::RequestedConnection;
                    pSys->weStartedconnection = true;

                    core::TimeVal timeNow = gEnv->pTimer->GetTimeNowReal();

                    // send connectionRequest now.
                    bsOut.write(MessageID::ConnectionRequest);
                    bsOut.write(guid_);
                    bsOut.write(timeNow.GetValue());
                    bsOut.write<bool>(pReq->password.isNotEmpty());
                    if (pReq->password.isNotEmpty()) {
                        auto cnonce = generateNonce();

                        PasswdHash hash;
                        hash.update(cnonce);
                        hash.update(nonce);
                        hash.update(pReq->password.c_str(), pReq->password.length());
                        auto digest = hash.finalize();

                        bsOut.writeAligned(cnonce);
                        bsOut.writeAligned(digest);
                    }

                    // send the request to the remote.
                    pSys->sendReliable(
                        bsOut,
                        PacketPriority::Immediate,
                        PacketReliability::Reliable,
                        OrderingChannel::Default,
                        timeNow);
                }
                else {
                    // failed to add remote sys.
                    X_ERROR("Net", "Failed to add new remote system to internal list");

                    pushPacket(MessageID::ConnectionRequestFailed, pData->systemAddress, clientGuid);
                }

                break;
            }
        }
    }

    // remove the req.
    removeConnectionRequest(pData->systemAddress);
}

void XPeer::handleUnConnectedPing(UpdateBitStream& bsOut, RecvData* pData, RecvBitStream& bs, bool openConnectionsRequired)
{
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received unConnectedPing");

    if (openConnectionsRequired && !accpetingIncomingConnections()) {
        X_LOG0_IF(vars_.debugEnabled(), "Net", "Ignoring ping, not accepting incoming connections");
        return;
    }

    int64_t timeStamp;
    NetGUID clientGuid;

    bs.read(timeStamp);
    bs.read(clientGuid);

    // respond.
    bsOut.write(MessageID::UnConnectedPong);
    bsOut.write(timeStamp);
    bsOut.write(guid_);

    SendParameters sp;
    sp.setData(bsOut);
    sp.systemAddress = pData->systemAddress;
    pData->pSrcSocket->send(sp);
}

void XPeer::handleUnConnectedPong(UpdateBitStream& bsOut, RecvData* pData, RecvBitStream& bs)
{
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received unConnectedPong");

    int64_t timeStamp;
    NetGUID clientGuid;

    bs.read(timeStamp);
    bs.read(clientGuid);

    // tell the game.
    Packet* pPacket = allocPacket(core::bitUtil::bytesToBits(sizeof(MessageID::Enum) + sizeof(int64_t) + SystemAddress::serializedSize()));
    pPacket->guid = clientGuid;

    core::FixedBitStreamNoneOwning packetBs(pPacket->pData, pPacket->pData + pPacket->length, false);
    packetBs.write(MessageID::UnConnectedPong);
    packetBs.write(timeStamp);
    pData->systemAddress.writeToBitStream(packetBs);

    pushBackPacket(pPacket);
}

// ----------------------------------------------------

void XPeer::handleConnectionRequest(UpdateBitStream& bsOut, RecvBitStream& bs, RemoteSystem& rs, core::TimeVal timeNow)
{
    NetGUID clientGuid;
    int64_t timeStamp;
    bool passwordInc;

    // must be a unverified sender trying to connect.
    if (rs.connectState != ConnectState::UnverifiedSender) {
        X_ERROR("Net", "Received unexpected connection request from client, ignoring.");

        addToBanList(rs.systemAddress, core::TimeVal::fromMS(vars_.unexpectedMsgBanTime()));

        disconnectRemote(rs);
        return;
    }

    bs.read(clientGuid);
    bs.read(timeStamp);
    bs.read(passwordInc);

    NonceHash::Digest cnonce;
    PasswdHash::Digest passwordDisget;
    if (passwordInc) {
        bs.readAligned(cnonce);
        bs.readAligned(passwordDisget);
    }

    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received connection request. timeStamp: %" PRId64, timeStamp);

    // optional don't allow clients that give us a password when one not required.
    const bool clientMustNotSendPassword = false;
    // if this is true will force the hash check, which will fail.
    const bool invalidPassord = (passwordInc && clientMustNotSendPassword);

    if (password_.isNotEmpty() || invalidPassord) {
        PasswdHash hash;
        hash.update(cnonce);
        hash.update(rs.nonce);
        hash.update(password_.c_str(), password_.length());
        auto serverPassDigest = hash.finalize();

        if (passwordDisget != serverPassDigest) {
            bsOut.write(MessageID::InvalidPassword);
            bsOut.write(guid_);

            rs.sendReliable(
                bsOut,
                PacketPriority::Immediate,
                PacketReliability::Reliable,
                OrderingChannel::Default,
                timeNow);

            rs.connectState = ConnectState::DisconnectAsapSilent;
            return;
        }
    }

    // hey hey!
    rs.connectState = ConnectState::HandlingConnectionRequest;

    bsOut.write(MessageID::ConnectionRequestAccepted);
    rs.systemAddress.writeToBitStream(bsOut);
    bsOut.write<uint8_t>(safe_static_cast<uint8_t>(ipList_.size()));
    for (auto& ip : ipList_) {
        ip.writeToBitStream(bsOut);
    }
    bsOut.write(timeStamp);
    bsOut.write(timeNow.GetValue());

    rs.sendReliable(
        bsOut,
        PacketPriority::Immediate,
        PacketReliability::Reliable,
        OrderingChannel::Default,
        timeNow);
}

void XPeer::handleConnectionRequestAccepted(UpdateBitStream& bsOut, RecvBitStream& bs, RemoteSystem& rs, core::TimeVal timeNow)
{
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received connection request accepted");

    if (rs.connectState != ConnectState::RequestedConnection) {
        X_ERROR("Net", "Received unexpected connection request accept, ignoring.");
        return;
    }

    // mmmm
    SystemAddressEx externalSysId;
    uint8_t numInternal = 0;
    RemoteSystem::SystemAddArr localIps;
    int64_t sendPingTime;
    int64_t sendPongTime;

    externalSysId.fromBitStream(bs);
    bs.read(numInternal);
    X_ASSERT(numInternal < localIps.capacity(), "Peer sent too many internal addresses")(numInternal, localIps.capacity());

    localIps.resize(numInternal);
    for (size_t i = 0; i < numInternal; i++) {
        localIps[i].fromBitStream(bs);
    }
    bs.read(sendPingTime);
    bs.read(sendPongTime);

    // welcome to the club.
    rs.onConnected(externalSysId, localIps, core::TimeVal(sendPingTime), core::TimeVal(sendPongTime));

    // --------- Lets shake on it.. -------------

    bsOut.write(MessageID::ConnectionRequestHandShake);
    rs.systemAddress.writeToBitStream(bsOut);
    bsOut.write<uint8_t>(safe_static_cast<uint8_t>(ipList_.size()));
    for (auto& ip : ipList_) {
        ip.writeToBitStream(bsOut);
    }
    bsOut.write(sendPongTime);
    bsOut.write(timeNow.GetValue());

    rs.sendReliable(
        bsOut,
        PacketPriority::Immediate,
        PacketReliability::Reliable,
        OrderingChannel::Default,
        timeNow);

    sendPing(rs, PacketReliability::UnReliable);

    // tell the game.
    pushPacket(MessageID::ConnectionRequestAccepted, rs);
}

void XPeer::handleConnectionRequestHandShake(UpdateBitStream& bsOut, RecvBitStream& bs, RemoteSystem& rs)
{
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received connection request handshake");

    if (rs.connectState != ConnectState::HandlingConnectionRequest) {
        X_ERROR("Net", "Received unexpected connection request handshake, ignoring.");
        return;
    }

    // same shit as above.
    SystemAddressEx externalSysId;
    uint8_t numInternal = 0;
    RemoteSystem::SystemAddArr localIps;
    int64_t sendPingTime;
    int64_t sendPongTime;

    externalSysId.fromBitStream(bs);
    bs.read(numInternal);
    X_ASSERT(numInternal < localIps.capacity(), "Peer sent too many internal addresses")(numInternal, localIps.capacity());
    localIps.resize(numInternal);

    for (size_t i = 0; i < numInternal; i++) {
        localIps[i].fromBitStream(bs);
    }
    bs.read(sendPingTime);
    bs.read(sendPongTime);

    rs.onConnected(externalSysId, localIps, core::TimeVal(sendPingTime), core::TimeVal(sendPongTime));

    sendPing(rs, PacketReliability::UnReliable);

    // tell the game.
    pushPacket(MessageID::ConnectionRequestHandShake, rs);
}

// ----------------------------------

void XPeer::handleConnectedPing(UpdateBitStream& bsOut, RecvBitStream& bs, RemoteSystem& rs, core::TimeVal timeNow)
{
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received ping");

    int64_t timeStamp;
    bs.read(timeStamp);

    // respond.
    bsOut.write(MessageID::ConnectedPong);
    bsOut.write(timeStamp);
    bsOut.write(timeNow);

    rs.sendReliable(
        bsOut,
        PacketPriority::Immediate,
        PacketReliability::UnReliable,
        OrderingChannel::Default,
        timeNow);
}

void XPeer::handleConnectedPong(UpdateBitStream& bsOut, RecvBitStream& bs, RemoteSystem& rs)
{
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received pong");

    int64_t sendPingTime;
    int64_t sendPongTime;

    bs.read(sendPingTime);
    bs.read(sendPongTime);

    rs.onPong(core::TimeVal(sendPingTime), core::TimeVal(sendPongTime));
}

void XPeer::handleDisconnectNotification(UpdateBitStream& bsOut, RecvBitStream& bs, RemoteSystem& rs)
{
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received disconnect notification");

    // if ack never sends disconnect.
    rs.connectState = ConnectState::DisconnectOnNoAck;
}

void XPeer::handleInvalidPassword(UpdateBitStream& bsOut, RecvBitStream& bs, RemoteSystem& rs)
{
    X_LOG0_IF(vars_.debugEnabled(), "Net", "Received invalid password notification");

    // tell the game.
    pushPacket(MessageID::InvalidPassword, rs);

    rs.connectState = ConnectState::DisconnectAsapSilent;
}

// ----------------------------------

// -------------------------------------------

RemoteSystem* XPeer::addRemoteSystem(const SystemAddressEx& sysAdd, NetGUID guid, int32_t remoteMTU,
    NetSocket* pSrcSocket, const SystemAddressEx& bindingAdd, ConnectState::Enum state)
{
    // hello hello.
    IPStr ipStr;
    NetGuidStr guidStr;

    X_LOG0_IF(vars_.debugEnabled(), "Net", "^6Adding remote system^7 sysAdd: \"%s\" guid: ^5%s",
        sysAdd.toString(ipStr), guid.toString(guidStr));

    for (auto& remoteSys : remoteSystems_) {
        if (!remoteSys.isActive) {
            core::TimeVal timeNow = gEnv->pTimer->GetTimeNowReal();

            remoteSys.systemAddress = sysAdd;
            remoteSys.myExternalSystemAddress = UNASSIGNED_SYSTEM_ADDRESS;
            remoteSys.thierInternalSystemAddress.clear();

            remoteSys.nextPingTime = core::TimeVal(0ll);
            remoteSys.lastReliableSend = timeNow;
            remoteSys.connectionTime = timeNow;

            remoteSys.pings.fill(PingAndClockDifferential());
            remoteSys.lastPingIdx = 0;

            remoteSys.relLayer.reset(remoteMTU);

            remoteSys.guid = guid;
            remoteSys.lowestPing = UNDEFINED_PING;
            remoteSys.MTUSize = remoteMTU;
            remoteSys.connectState = state;
            remoteSys.pNetSocket = pSrcSocket;
            remoteSys.isActive = true; // diverts all packets to reliability layer.

            activeRemoteSystems_.push_back(&remoteSys);

            // insert into a more cache friendly lookup.
            RemoteSystemLookup lu;
            lu.systemAddress = sysAdd;
            lu.guid = guid;
            lu.pRemoteSystem = &remoteSys;
            remoteSystemLookup_.push_back(lu);

            if (pSrcSocket->getBoundAdd() != bindingAdd) {
                // print warning.
                X_WARNING("Net", "Binding address is different to source socket");
            }

            return &remoteSys;
        }
    }

    // warning?
    return nullptr;
}

void XPeer::disconnectRemote(RemoteSystem& rs)
{
    rs.closeConnection();

    {
        auto it = std::find(activeRemoteSystems_.begin(), activeRemoteSystems_.end(), &rs);
        X_ASSERT(it != activeRemoteSystems_.end(), "Tried to remove remote system that is not active")();
        activeRemoteSystems_.erase(it);
    }
    {
        auto it = std::find_if(remoteSystemLookup_.begin(), remoteSystemLookup_.end(), [&rs](const RemoteSystemLookup& lu) { return lu.systemAddress == rs.systemAddress; });
        X_ASSERT(it != remoteSystemLookup_.end(), "Tried to remove remote system that is not active")();
        remoteSystemLookup_.erase(it);
    }
}

bool XPeer::isIpConnectSpamming(const SystemAddressEx& sysAdd, core::TimeVal* pDeltaOut)
{
    for (auto& remoteSys : remoteSystems_) {
        if (remoteSys.isActive && remoteSys.systemAddress.equalExcludingPort(sysAdd)) {
            core::TimeVal timeNow = gEnv->pTimer->GetTimeNowReal();
            core::TimeVal connectionAttemptDelta = timeNow - remoteSys.connectionTime;
            core::TimeVal rateLimitThresh = core::TimeVal::fromMS(vars_.rlConnectionsPerIPThreshMS());

            if (connectionAttemptDelta < rateLimitThresh) {
                if (pDeltaOut) {
                    *pDeltaOut = connectionAttemptDelta;
                }

                return true;
            }
        }
    }

    return false;
}

core::Thread::ReturnValue XPeer::socketRecvThreadProc(const core::Thread& thread)
{
    NetSocket* pSocket = reinterpret_cast<NetSocket*>(thread.getData());
    X_ASSERT_NOT_NULL(pSocket);

    RecvData* pData = allocRecvData();

    ttSetThreadName(gEnv->ctx, 0, "Socket Worker");
    ttSetThreadGroup(gEnv->ctx, 0, 2);

    while (thread.shouldRun()) {
        auto res = pSocket->recv(*pData);

        if (res == RecvResult::Success) {
            if (pData->bytesRead > 0) {
                onSocketRecv(pData);
                pData = allocRecvData();
            }
            else {
                core::Thread::sleep(0);
            }
        }
        else if (res == RecvResult::ConnectionReset) {
            // okay so we know a socket has been closed we don't need to wait for timeout.
            // we send buffered as we on different thread.
            BufferdCommand* pCmd = allocBufferdCmd(BufferdCommand::Cmd::CloseConnection, 0);
            pCmd->systemHandle = INVALID_SYSTEM_HANDLE;
            pCmd->systemAddress = pData->systemAddress;
            pCmd->sendDisconnectionNotification = false;
            bufferdCmds_.push(pCmd);
        }
        else if (res == RecvResult::Error) {
            // ...
            // for now ignore, have to see what errors we get and see if want to handle them.
            // they will get logged by socket code.
        }
    }

    freeRecvData(pData);
    return core::Thread::ReturnValue(0);
}

NonceHash::Digest XPeer::generateNonce(void)
{
    core::TimeVal timeNow = gEnv->pTimer->GetTimeNowReal();

    std::array<uint8_t, NonceHash::Digest::NUM_BYTES> randBytes;
    cryptRnd_.genBytes(randBytes.data(), randBytes.size());

    NonceHash hash;
    hash.update(timeNow.GetValue());
    hash.update(randBytes.data(), randBytes.size());
    hash.update(core::TimeStamp::getSystemTime());
    hash.update(core::Thread::getCurrentID());

    return hash.finalize();
}


X_NAMESPACE_END
