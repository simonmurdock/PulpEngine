#include "stdafx.h"
#include "Session.h"

#include "Lobby.h"
#include "Vars\SessionVars.h"

#include <Util/FloatIEEEUtil.h>

#include <I3DEngine.h>
#include <IPrimitiveContext.h>
#include <IFont.h>
#include <ITimer.h>
#include <IFrameData.h>

#include <UserCmdMan.h>

X_NAMESPACE_BEGIN(net)


Session::PendingPeer::PendingPeer(net::SystemHandle sysHandle, net::NetGUID guid, core::TimeVal connectTime) :
    sysHandle(sysHandle),
    guid(guid),
    connectTime(connectTime)
{

}

Session::PendingConnection::PendingConnection(LobbyType::Enum type, net::SystemAddress address) :
    type(type),
    address(address)
{

}

Session::ConnectedPeer::ConnectedPeer(net::SystemHandle sysHandle, net::NetGUID guid, LobbyFlags initalLobbys) :
    sysHandle(sysHandle),
    guid(guid),
    flags(initalLobbys)
{

}

Session::ConnectedPeer::ConnectedPeer(net::SystemHandle sysHandle, net::NetGUID guid, LobbyType::Enum initialLobbyType) :
    ConnectedPeer(sysHandle, guid, typeToFlag(initialLobbyType))
{

}

LobbyFlag::Enum Session::ConnectedPeer::typeToFlag(LobbyType::Enum type)
{
    static_assert(LobbyType::ENUM_COUNT == 2, "More enums?");

    switch (type)
    {
        case LobbyType::Party:
            return LobbyFlag::Party;
        case LobbyType::Game:
            return LobbyFlag::Game;
        default:
            X_NO_SWITCH_DEFAULT;
    }
}


// ------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------

X_DISABLE_WARNING(4355) // 'this': used in base member initializer list

Session::Session(SessionVars& vars, IPeer* pPeer, IGameCallbacks* pGameCallbacks, core::MemoryArenaBase* arena) :
    vars_(vars),
    pPeer_(X_ASSERT_NOT_NULL(pPeer)),
    pGameCallbacks_(X_ASSERT_NOT_NULL(pGameCallbacks)),
    arena_(X_ASSERT_NOT_NULL(arena)),
    snapAllocator_(),
    snapArena_(&snapAllocator_, "SnapShotArena"),
    state_(SessionState::Idle),
    lobbys_{ {
        {vars_, this, pPeer, pGameCallbacks, LobbyType::Party, arena, &snapArena_},
        {vars_, this, pPeer, pGameCallbacks, LobbyType::Game, arena, &snapArena_}
    }},
    pendingJoins_(arena),
    pendingConnections_(arena),
    peers_(arena),
    numSnapsReceived_(0),
    snapInterpolationTimeMS_(0),
    snapInterpolationResidual_(0.f)
{
    arena->addChildArena(&snapArena_);

    X_ASSERT(lobbys_[LobbyType::Party].getType() == LobbyType::Party, "Incorrect type")();
    X_ASSERT(lobbys_[LobbyType::Game].getType() == LobbyType::Game, "Incorrect type")();

}

X_ENABLE_WARNING(4355)

Session::~Session()
{
    arena_->removeChildArena(&snapArena_);

    receivedSnaps_.clear();
}

void Session::update(void)
{
    X_PROFILE_BEGIN("Session", core::profiler::SubSys::NETWORK);
    ttZone(gEnv->ctx, "(Net/Session) Update");

    // do a network tick.
    pPeer_->runUpdate();

    // potentially we have now received packets.
    readPackets();

    lobbys_[LobbyType::Party].handleState();
    lobbys_[LobbyType::Game].handleState();

    handleState();

    processPendingPeers();
}

void Session::handleSnapShots(core::FrameTimeData& timeInfo)
{
    // This needs to:
    // - decide to apply the next snapshot.
    // - set the interpolation values in game for this frame.
    // - Look at how big our snapshot buffer is in terms of time.
    // - speed up or slow down interpolation speed based on this.

    // if we have interpolated past current snap delta and have new snaps process them.
    while (snapInterpolationTimeMS_ > snapTime_.deltaMS && receivedSnaps_.isNotEmpty())
    {
        snapInterpolationTimeMS_ -= snapTime_.deltaMS;
        processSnapShot();
    }

    if (snapInterpolationTimeMS_ > snapTime_.deltaMS)
    {
        X_WARNING("Session", "Ran out snaps, extrapolation required.");
    }

    // clamp it for current frame
    snapInterpolationTimeMS_ = math<int32_t>::clamp(snapInterpolationTimeMS_, 0, snapTime_.deltaMS);

    float fraction = static_cast<float>(snapInterpolationTimeMS_) / static_cast<float>(snapTime_.deltaMS);
    int32_t serverTimeMS = lerp(snapTime_.previousMS, snapTime_.currentMS, fraction);

    pGameCallbacks_->setInterpolation(fraction, serverTimeMS, snapTime_.previousMS, snapTime_.currentMS);

    // do we need to slow down?
    auto bufferedTime = calculateSnapShotBufferTime();

    // work out what our effective rate should be.
    // so if the server is sending 100ms of data but we get it over 110ms we strech the buffer locally.
    // so our effective rate would be 1.1f;
    auto effectiveSnapRate = static_cast<float>(bufferedTime.totalTimeMS) / static_cast<float>(bufferedTime.totalRecvTimeMS);

    int32_t desiredBufferedMS = 100;
    int32_t desiredBufferedWindowMS = 100;

    float snapRateScale = 1.0f; // right on the nail!

    if (bufferedTime.timeLeftMS < desiredBufferedMS)
    {
        // steady on trevor...
        snapRateScale = vars_.snapFallbackRate();
    }
    else if (bufferedTime.timeLeftMS > desiredBufferedMS + desiredBufferedWindowMS)
    {
        // to slow motherfucker!
        snapRateScale = vars_.snapCatchupRate();
    }

    // need to work out how much interpolation time to advance by.
    float gameFrameDeltaMS = timeInfo.unscaledDeltas[core::Timer::GAME].GetMilliSeconds();

    float interpolateDeltaMS = (gameFrameDeltaMS * snapRateScale * effectiveSnapRate) + snapInterpolationResidual_;
    if (!core::FloatUtil::isValid(interpolateDeltaMS))
    {
        interpolateDeltaMS = 0.f;
    }

    snapInterpolationResidual_ = math<float>::frac(interpolateDeltaMS);
    if (!core::FloatUtil::isValid(snapInterpolationResidual_))
    {
        snapInterpolationResidual_ = 0.f;
    }

    if (vars_.snapDebug())
    {
        X_LOG0("Session", "SnapUpdate - gameFrameDelta: %g snapRateScale: %g effectiveSnapRate: %g interDeltaMS: %g snapInterResidual: %g buffTime: %" PRIi32,
            gameFrameDeltaMS, snapRateScale, effectiveSnapRate, interpolateDeltaMS, snapInterpolationResidual_, bufferedTime.timeLeftMS);
    }

    snapInterpolationTimeMS_ += static_cast<int32_t>(interpolateDeltaMS);
}

Session::SnapBufferTimeInfo Session::calculateSnapShotBufferTime(void) const
{
    int32_t lastSnapTimeMS = snapTime_.currentMS;
    int32_t lastSnapRecvTimeMS = snapRecvTime_.currentMS;

    int32_t totalTimeMS = snapTime_.deltaMS;
    int32_t totalRecvTimeMS = snapRecvTime_.deltaMS;

    // look at all the pending snaps. (might be none)
    for (const auto& snap : receivedSnaps_)
    {
        auto snapTime = snap.getTimeMS();
        auto snapRecvTime = snap.getRecvTimeMS();

        X_ASSERT(snapTime > lastSnapTimeMS && snapRecvTime >= lastSnapRecvTimeMS, "Previous snap is ahead")(snapTime, lastSnapTimeMS, snapRecvTime, lastSnapRecvTimeMS);

        totalTimeMS += (snapTime - lastSnapTimeMS);
        totalRecvTimeMS += (snapRecvTime - lastSnapRecvTimeMS);

        lastSnapTimeMS = snapTime;
        lastSnapRecvTimeMS = snapRecvTime;
    }

    // remove time we have interpolated.
    int32_t timeLeftMS = totalTimeMS - core::Min(snapTime_.deltaMS, snapInterpolationTimeMS_);

    return { timeLeftMS, totalTimeMS, totalRecvTimeMS };
}

void Session::processSnapShot(void)
{
    ttZone(gEnv->ctx, "(Net/Session) SnapShot");

    if (receivedSnaps_.isEmpty()) {
        X_ERROR("Session", "No snapshot to process");
        return;
    }

    auto& snap = receivedSnaps_.peek();

    snapTime_.rotate(snap.getTimeMS());
    snapRecvTime_.rotate(snap.getRecvTimeMS());

    if (vars_.snapDebug()) {
        X_LOG0("Session", "Processing snapshot. ServerDelta: %" PRIi32 " RecvDelta %" PRIi32, snapTime_.deltaMS, snapRecvTime_.deltaMS);
    }

    pGameCallbacks_->applySnapShot(snap);

    receivedSnaps_.pop();
}

void Session::connect(SystemAddress address)
{
    // biiiiitcooooonnneeeecttt!!!!
    quitToMenu();

    // we always try to connect to party lobby.
    // If the party is already in game, it will tell us to join the game lobby.


    lobbys_[LobbyType::Party].connectTo(address);
    setState(SessionState::ConnectAndMoveToParty);
}

void Session::disconnect(void)
{
    quitToMenu();
}

SessionStatus::Enum Session::getBackStatus(void) const
{
    switch (getStatus())
    {
        case SessionStatus::InGame:
        case SessionStatus::Loading:
            return SessionStatus::GameLobby;

        case SessionStatus::GameLobby:
            return SessionStatus::PartyLobby;

        case SessionStatus::PartyLobby:
            return SessionStatus::Idle;

        default:
            break;
    }

    return SessionStatus::Idle;
}

void Session::cancel(void)
{
    if (state_ == SessionState::Idle) {
        return;
    }

    switch(getBackStatus())
    {
        case SessionStatus::GameLobby:
            X_ASSERT_NOT_IMPLEMENTED();
            break;

        case SessionStatus::PartyLobby:
        {
            auto& party = lobbys_[LobbyType::Party];

            if (party.isHost())
            {
                party.notifyPeersLeavingGameLobby();
                
                auto& gameLobby = lobbys_[LobbyType::Game];
                gameLobby.reset();

                setState(SessionState::PartyLobbyHost);
            }
            else
            {

                X_ASSERT_NOT_IMPLEMENTED();
            }
            break;
        }
        case SessionStatus::Idle:
            quitToMenu();
            break;

        default:
            break;
    }
}

void Session::finishedLoading(void)
{
    // we finished loading.
    // noice.
    X_ASSERT(state_ == SessionState::Loading, "Can't finish loading if not loading")(state_);

    auto& gameLobby = lobbys_[LobbyType::Game];

    gameLobby.finishedLoading();

    auto flags = gameLobby.getMatchFlags();
    if (flags.IsSet(MatchFlag::Online))
    {
        if (gameLobby.isHost())
        {
            if (vars_.waitForPlayers())
            {
                X_LOG0("Session", "Waiting for players to load");
            }
            else
            {
                setState(SessionState::InGame);
            }
        }
        else
        {
            gameLobby.sendToHost(MessageID::LoadingDone);
        }
    }
    else
    {
        X_ASSERT(!gameLobby.isPeer() && gameLobby.getNumUsers() < 2, "Can't be a peer of a none online lobby")(gameLobby.isPeer(), gameLobby.getNumUsers());

        setState(SessionState::InGame);
    }
}

bool Session::hasFinishedLoading(void) const
{
    return lobbys_[LobbyType::Game].hasFinishedLoading();
}

void Session::quitToMenu(void)
{
    if (state_ == SessionState::Idle) {
        return;
    }

    for (auto& lobby : lobbys_)
    {
        lobby.reset();
    }

    for (auto& p : peers_)
    {
        X_WARNING("Session", "Dangling peer");
        pPeer_->closeConnection(p.sysHandle, true, OrderingChannel::Default, PacketPriority::High);
    }

    peers_.clear();

    nextUserCmdSendTime_.SetValue(0);
    nextSnapshotSendTime_.SetValue(0);
    numSnapsReceived_ = 0;

    setState(SessionState::Idle);
}

void Session::quitMatch(void)
{
    // So long, farewell, auf Wiedersehen!
    endGame(true);
}

void Session::createPartyLobby(const MatchParameters& params)
{
    X_ASSERT(state_ == SessionState::Idle, "Must be idle")(state_);

    // host a new 'party' lobby.
    lobbys_[LobbyType::Party].startHosting(params);

    setState(SessionState::CreateAndMoveToPartyLobby);
}

void Session::createMatch(const MatchParameters& params)
{
    // host a new 'game'
    lobbys_[LobbyType::Game].startHosting(params);

    setState(SessionState::CreateAndMoveToGameLobby);
}

void Session::startMatch(void)
{
    X_ASSERT(state_ == SessionState::GameLobbyHost, "Can't start match without been a game host")(state_);

    startLoading();
}

void Session::sendUserCmd(const UserCmdMan& userCmdMan, int32_t localIdx, core::FrameTimeData& timeInfo)
{
    X_ASSERT(state_ == SessionState::InGame, "Should only send user cmd if in game")(state_);

    if (timeInfo.startTimeRealative < nextUserCmdSendTime_) {
        return;
    }

    ttZone(gEnv->ctx, "(Net/Session) UserCmds");

    auto rateMS = vars_.userCmdRateMs();
    nextUserCmdSendTime_ = timeInfo.startTimeRealative + core::TimeVal::fromMS(rateMS);

    // we send N cmds, but this should be rate limited by 'net_ucmd_rate_ms'
    using UserCmdBS = core::FixedBitStreamStack<(sizeof(UserCmd) * MAX_USERCMD_SEND) + 0x100>;

    UserCmdBS bs;
    bs.write(net::MessageID::UserCmd);
    userCmdMan.writeUserCmdToBs(bs, MAX_USERCMD_SEND, localIdx);

    static_assert(UserCmdBS::BUF_SIZE < MAX_MTU_SIZE, "Can't fit usercmd buffer in single packet.");

    auto& gameLobby = lobbys_[LobbyType::Game];

    X_ASSERT(gameLobby.isPeer(), "Trying to send userCmds when not a peer")(gameLobby.isPeer(), gameLobby.isHost(), gameLobby.isActive());

    gameLobby.sendUserCmd(bs);

    pPeer_->runUpdate();
}

void Session::sendSnapShot(core::FrameTimeData& timeInfo)
{
    X_ASSERT(state_ == SessionState::InGame, "Should only send snapshot if in game")(state_);
    ttZone(gEnv->ctx, "(Net/Session) Send SnapShot");

    if (timeInfo.startTimeRealative < nextSnapshotSendTime_) {
        return;
    }

    auto rateMS = vars_.snapRateMs();
    nextSnapshotSendTime_ = timeInfo.startTimeRealative + core::TimeVal::fromMS(rateMS);

    SnapShot snap(&snapArena_);
    pGameCallbacks_->buildSnapShot(snap);

    sendSnapShot(std::move(snap));
}

void Session::sendSnapShot(SnapShot&& snap)
{
    X_ASSERT(state_ == SessionState::InGame, "Should only send snapshot if in game")(state_);
    X_ASSERT(snap.getTimeMS() >= 0, "Shapshot time is not valid")(snap.getTimeMS());

    // to all peers.
    lobbys_[LobbyType::Game].sendSnapShot(snap);

    pPeer_->runUpdate();
}

ILobby* Session::getLobby(LobbyType::Enum type)
{
    return &lobbys_[type];
}


void Session::setState(SessionState::Enum state)
{
    if (state_ == state) {
        X_WARNING("Session", "redundant state change: \"%s\"", SessionState::ToString(state));
        return;
    }

    X_LOG0_IF(vars_.sessionDebug(), "Session", "State changed: \"%s\" -> \"%s\"", SessionState::ToString(state_), SessionState::ToString(state));

    state_ = state;
}

ConnectionAttemptResult::Enum Session::connectToPeer(LobbyType::Enum type, SystemAddress sa)
{
    // we already connected to peer?
    auto systemHandle = pPeer_->getSystemHandleForAddress(sa);
    if (systemHandle != INVALID_SYSTEM_HANDLE) 
    {
        for(auto& p : peers_)
        {
            if (p.sysHandle == systemHandle) {
                auto flag = ConnectedPeer::typeToFlag(type);
                X_ASSERT(!p.flags.IsSet(flag), "Peer is already in lobby")(type);
                p.flags.Set(flag);
                return ConnectionAttemptResult::AlreadyConnected;
            }
        }
    }

    // new connection.
    // Bittttttcoooonnneeeeeeeeectt!!!!
    auto delay = core::TimeVal::fromMS(vars_.connectionRetryDelayMs());

    auto res = pPeer_->connect(sa, PasswordStr(), vars_.connectionAttemps(), delay);

    switch (res)
    {
        case ConnectionAttemptResult::AlreadyConnected:
            X_ASSERT_UNREACHABLE();
            break;

        case ConnectionAttemptResult::Started:
        case ConnectionAttemptResult::AlreadyInProgress:
            pendingConnections_.emplace_back(type, sa);
            break;

        default:
            X_ERROR("Lobby", "Failed to connectTo: \"%s\"", ConnectionAttemptResult::ToString(res));
    }

    return res;
}

void Session::closeConnection(LobbyType::Enum type, SystemHandle systemHandle)
{
    for (size_t i = 0; i < peers_.size(); i++)
    {
        auto& p = peers_[i];

        if (p.sysHandle == systemHandle) {
        
            auto flag = ConnectedPeer::typeToFlag(type);
            X_ASSERT(p.flags.IsSet(flag), "Peer is not in lobby")(type);

            p.flags.Remove(flag);
            if (!p.flags.IsAnySet()) {
                peers_.removeIndex(i);
                X_ERROR("Session", "Closing connection for peer");

                pPeer_->closeConnection(systemHandle, true, OrderingChannel::Default, PacketPriority::Low);
            }

            return;
        }
    }

    X_ERROR("Session", "Failed to find peer for removal");
}

void Session::onLostConnectionToHost(LobbyType::Enum type)
{
    X_UNUSED(type);
    X_LOG0("Session", "Lost connection to host");

    quitToMenu();
}

void Session::onReceiveSnapShot(SnapShot&& snap)
{
    X_ASSERT(snap.getTimeMS() >= 0, "Shapshot time is not valid")(snap.getTimeMS(), snap.getRecvTimeMS());
    X_ASSERT(snap.getRecvTimeMS() < 0, "Shapshot recvtime should not be set")(snap.getTimeMS(), snap.getRecvTimeMS());

    if (receivedSnaps_.freeSpace() == 0)
    {
        X_ERROR("Session", "Snapshot buffer full forcing snap apply");
        processSnapShot();
        X_ASSERT(receivedSnaps_.freeSpace() > 0, "Should have space for snapshot")();
    }

    auto time = gEnv->pTimer->GetTimeNowNoScale();
    snap.setRecvTime(time.GetMilliSecondsAsInt32());

    receivedSnaps_.emplace(std::move(snap));

    // process the snap shot.
    // not sure where i want to put this logic.
    // i think it been in the network session is fine.
    // rather than core, since snapshots are 'network' related.
    // core should not care if you have a networked game or not.
    // but all local games have a session, humm..
    ++numSnapsReceived_;

    X_LOG0_IF(vars_.snapDebug(), "Session", "Received snapshot: %" PRIi32, numSnapsReceived_);

    if (state_ != SessionState::InGame)
    {
        // the game needs two snapshots to interpolate before we can start.
        processSnapShot();

        if (numSnapsReceived_ < 2) {
            return;
        }

        auto& gameLobby = lobbys_[LobbyType::Game];
        X_ASSERT(gameLobby.isPeer(), "Getting snapshots when not a peer")(gameLobby.isHost(), gameLobby.isPeer());

        // meow.
        gameLobby.sendToHost(MessageID::InGame);

        setState(SessionState::InGame);
    }
}

void Session::connectAndMoveToLobby(LobbyType::Enum type, SystemAddress sa)
{
    auto& lobby = lobbys_[type];

    lobby.reset();
    lobby.connectTo(sa);

    switch (type)
    {
        case LobbyType::Party:
            setState(SessionState::ConnectAndMoveToParty);
            break;

        case LobbyType::Game:
            setState(SessionState::ConnectAndMoveToGame);
            break;

        default:
            X_NO_SWITCH_DEFAULT_ASSERT;
    }
}

void Session::peerJoinedLobby(LobbyType::Enum type, SystemHandle handle)
{
    for (size_t i = 0; i < pendingJoins_.size(); i++)
    {
        auto& pp = pendingJoins_[i];

        if (pp.sysHandle == handle) {
            // they are now a peer.
            peers_.emplace_back(pp.sysHandle, pp.guid, type);

            pendingJoins_.removeIndex(i);
            return;
        }
    }

    auto flag = ConnectedPeer::typeToFlag(type);

    // they should be a exsiting peer, that has joined another lobby.
    for (auto& p : peers_)
    {
        if (p.sysHandle == handle) {
            X_ASSERT(!p.flags.IsSet(flag), "Peer is already in lobby")(type);
            p.flags.Set(flag);
            return;
        }
    }

    X_ERROR("Session", "Failed to find pending peer to remove");
}

void Session::leaveGameLobby(void)
{
    if(state_ != SessionState::GameLobbyPeer) {
        X_ERROR("Session", "Can't leave game lobby on request if not peer");
        return;
    }

    auto& lobby = lobbys_[LobbyType::Game];
    lobby.reset();

    setState(SessionState::PartyLobbyPeer);
}

void Session::endGame(bool early)
{
    X_UNUSED(early);

    auto& gameLobby = lobbys_[LobbyType::Game];

    if (gameLobby.isHost())
    {
        if (gameLobby.hasActivePeers())
        {
            // we ended the game :( tell the players.
            core::FixedBitStreamStack<0x10> bs;
            bs.write(MessageID::EndGame);
            bs.write(early);
            gameLobby.sendToPeers(bs.data(), bs.sizeInBytes());
        }
    }
    else if (early)
    {
        // we are a peer and the host left early.
        // tell the user the host left?

    }

    // TODO: put you back in lobby?
    quitToMenu();
}

void Session::processPendingPeers(void)
{
    if (pendingJoins_.isEmpty()) {
        return;
    }

    auto timeNow = gEnv->pTimer->GetTimeNowReal();
    auto timeOut = core::TimeVal::fromMS(vars_.joinLobbyTimeoutMs());

    // timeout any peers.
    for (auto it = pendingJoins_.begin(); it != pendingJoins_.end(); )
    {
        const auto& pp = *it;

        auto elapsed = timeNow - pp.connectTime;

        if (elapsed > timeOut)
        {
            X_WARNING("Session", "Dropping peer connection, peer is not in a lobby");
            pPeer_->closeConnection(pp.sysHandle, true, OrderingChannel::Default, PacketPriority::Low);
            it = pendingJoins_.erase(it);
            continue;
        }

        ++it;
    }
}

// --------------------------------------

bool Session::handleState(void)
{
    switch (state_)
    {
        case SessionState::Idle:
            return stateIdle();
        case SessionState::CreateAndMoveToPartyLobby:
            return stateCreateAndMoveToPartyLobby();
        case SessionState::CreateAndMoveToGameLobby:
            return stateCreateAndMoveToGameLobby();
        case SessionState::PartyLobbyHost:
            return statePartyLobbyHost();
        case SessionState::PartyLobbyPeer:
            return statePartyLobbyPeer();
        case SessionState::GameLobbyHost:
            return stateGameLobbyHost();
        case SessionState::GameLobbyPeer:
            return stateGameLobbyPeer();
        case SessionState::ConnectAndMoveToParty:
            return stateConnectAndMoveToParty();
        case SessionState::ConnectAndMoveToGame:
            return stateConnectAndMoveToGame();
        case SessionState::Loading:
            return stateLoading();
        case SessionState::InGame:
            return stateInGame();

        default:
            X_NO_SWITCH_DEFAULT_ASSERT;
    }
    return false;
}

// --------------------------------------

bool Session::stateIdle(void)
{

    return true;
}

bool Session::stateCreateAndMoveToPartyLobby(void)
{
    // wait for the lobby to create.
    if (hasLobbyCreateCompleted(lobbys_[LobbyType::Party]))
    {
        // Success
        setState(SessionState::PartyLobbyHost);

        return true;
    }

    return true;
}

bool Session::stateCreateAndMoveToGameLobby(void)
{
    if (hasLobbyCreateCompleted(lobbys_[LobbyType::Game]))
    {
        if (lobbys_[LobbyType::Party].isActive()) {
            lobbys_[LobbyType::Party].sendMembersToLobby(lobbys_[LobbyType::Game]);
        }

        // Success
        setState(SessionState::GameLobbyHost);
        return true;
    }

    return true;
}

bool Session::statePartyLobbyHost(void)
{
    return true;
}

bool Session::statePartyLobbyPeer(void)
{
    return true;
}

bool Session::stateGameLobbyHost(void)
{
    // click start plz.

    return true;
}

bool Session::stateGameLobbyPeer(void)
{
    // start the game already!
    // need a way for the lobby to tell me to start loading.
    // the lobby needs to handle the packet, to check it actually came from the host, no bad peers plz!
    auto& lobby = lobbys_[LobbyType::Game];

    X_ASSERT(lobby.isPeer(), "Should be game lobby peer")(lobby.isPeer(), lobby.isHost());

    if (lobby.shouldStartLoading())
    {
        lobby.beganLoading();

        setState(SessionState::Loading);
    }

    return true;
}

bool Session::stateConnectAndMoveToParty(void)
{
    return handleConnectAndMoveToLobby(lobbys_[LobbyType::Party]);
}

bool Session::stateConnectAndMoveToGame(void)
{
    return handleConnectAndMoveToLobby(lobbys_[LobbyType::Game]);
}

bool Session::stateLoading(void)
{
    // hurry the fuck up!
    auto& gameLobby = lobbys_[LobbyType::Game];

    if (!gameLobby.hasFinishedLoading()) {
        return false;
    }

    // if not online, we should of switched out of this state if hasFinishedLoading().
    if (!gameLobby.getMatchFlags().IsSet(MatchFlag::Online)) {
        X_ASSERT_UNREACHABLE();
    }

    if (gameLobby.isHost())
    {
        if (gameLobby.hasActivePeers())
        {
            if (!gameLobby.allPeersLoaded()) {
                X_LOG0("Session", "Waiting for peers to load..");
                return false;
            }
            
            // TODO: timeout peers loading very slow.
            // ...
            X_LOG0("Session", "All peers loaded...");
        }
        else
        {
            X_LOG0("Session", "No peers, starting game...");
        }
    }
    else
    {
        // we will keep coming back here until we have synced world with host.
        // so check we are still connected.
        if (gameLobby.getNumConnectedPeers() < 1)
        {
            X_ERROR("Session", "No peers");
            quitToMenu();
            return false;
        }

        return false;
    }

    setState(SessionState::InGame);
    return true;
}

bool Session::stateInGame(void)
{
    // packets, packets, packets!
    return true;
}


// --------------------------------------

bool Session::hasLobbyCreateCompleted(Lobby& lobby)
{
    if (lobby.getState() == LobbyState::Idle) {
        return true;
    }

    // check for failed state.

    return false;
}

bool Session::handleConnectAndMoveToLobby(Lobby& lobby)
{
    if (lobby.getState() == LobbyState::Error) {
        X_ERROR("Session", "Failed to connect to \"%s\" lobby", LobbyType::ToString(lobby.getType()));
        handleConnectionFailed(lobby);
        return true;
    }

    // busy.
    if (lobby.getState() != LobbyState::Idle) {
        return true;
    }

    X_LOG0_IF(vars_.sessionDebug(), "Session", "Connected to \"%s\" lobby", LobbyType::ToString(lobby.getType()));

    // we connected!
    switch (lobby.getType())
    {
        case LobbyType::Game:
            setState(SessionState::GameLobbyPeer);
            break;
        case LobbyType::Party:
            setState(SessionState::PartyLobbyPeer);
            break;
        default:
            X_NO_SWITCH_DEFAULT_ASSERT;
    }

    return true;
}

void Session::handleConnectionFailed(Lobby& lobby)
{
    X_ASSERT(state_ == SessionState::ConnectAndMoveToGame || state_ == SessionState::ConnectAndMoveToParty, "Unexpected state")();

    quitToMenu();
}

void Session::startLoading(void)
{
    auto& gameLobby = lobbys_[LobbyType::Game];

    // should only be called if we are the host.
    X_ASSERT(gameLobby.isHost(), "Cant start loading if we are not the host")(gameLobby.isHost());
       

    const auto& params = gameLobby.getMatchParams();
    if (params.mapName.isEmpty()) {
        X_ERROR("Session", "Can't start map name is empty");
        return;
    }

    if (gameLobby.getMatchFlags().IsSet(MatchFlag::Online))
    {
        gameLobby.sendToPeers(MessageID::LoadingStart);
    }

    // Weeeeeeeeeeee!!
    setState(SessionState::Loading);
}

bool Session::readPackets(void)
{
    Packet* pPacket = nullptr;
    for (pPacket = pPeer_->receive(); pPacket; pPeer_->freePacket(pPacket), pPacket = pPeer_->receive())
    {
        core::FixedBitStreamNoneOwning bs(pPacket->begin(), pPacket->end(), true);

        auto msg = pPacket->getID();

        switch (msg)
        {
            // these are transport loss, so tell all lobby.
            case MessageID::ConnectionClosed:
            case MessageID::DisconnectNotification:
                handleTransportConnectionTermPacket(pPacket);
                break;

            case MessageID::AlreadyConnected:
            case MessageID::ConnectionRequestFailed:
            case MessageID::ConnectionBanned:
            case MessageID::ConnectionNoFreeSlots:
            case MessageID::ConnectionRateLimited:
            case MessageID::InvalidPassword:
            case MessageID::ConnectionRequestAccepted:
                handleTransportConnectionResponse(pPacket);
                break;

            case MessageID::ConnectionRequestHandShake:
                handleTransportConnectionHandShake(pPacket);
                break; 

            // Lobby type prefixed.
            case MessageID::LobbyJoinRequest:
            case MessageID::LobbyJoinAccepted:
            case MessageID::LobbyJoinNoFreeSlots:
            case MessageID::LobbyJoinRejected:
            case MessageID::LobbyUsersConnected:
            case MessageID::LobbyUsersDiconnected:
            case MessageID::LobbyGameParams:
            case MessageID::LobbyLeaveGameLobby:
            case MessageID::LobbyConnectAndMove:
            case MessageID::LobbyChatMsg:
                sendPacketToDesiredLobby(pPacket);
                break;

            // Only valid if we are in game lobby.
            case MessageID::LoadingStart:
            case MessageID::LoadingDone:
            case MessageID::InGame:
            case MessageID::EndGame:

            case MessageID::SnapShot:
            case MessageID::UserCmd:
                sendPacketToLobbyIfGame(pPacket);
                break;
          
            default:
                if (!pGameCallbacks_->handlePacket(pPacket)) {
                    X_ERROR("Session", "Unhandled message: %s", MessageID::ToString(msg));
                }
                break;
        }

    }

    return true;
}

void Session::handleTransportConnectionTermPacket(Packet* pPacket)
{
    auto msg = pPacket->getID();

    // MessageID::ConnectionClosed:
    // MessageID::DisconnectNotification:

    for (int32_t i = 0; i < LobbyType::ENUM_COUNT; i++)
    {
        if (lobbys_[i].isActive()) {
            lobbys_[i].handlePacket(pPacket);
        }
    }

    for (size_t i = 0; i < peers_.size(); i++) {
        auto& p = peers_[i];
        if (p.sysHandle == pPacket->systemHandle) {
            peers_.removeIndex(i);
        }
    }
}

void Session::handleTransportConnectionResponse(Packet* pPacket)
{
    if (state_ == SessionState::Idle) {
        X_ERROR("Session", "Received connection failure packet while idle. Msg: \"%s\"", MessageID::ToString(pPacket->getID()));
        X_ASSERT_UNREACHABLE(); // we tried to connect to a peer but are idle?
        return;
    }

    // Fail:
    // case MessageID::AlreadyConnected:
    // case MessageID::ConnectionRequestFailed:
    // case MessageID::ConnectionBanned:
    // case MessageID::ConnectionNoFreeSlots:
    // case MessageID::ConnectionRateLimited:
    // case MessageID::InvalidPassword:
    // Ok:
    // case MessageID::ConnectionRequestAccepted:
    auto msg = pPacket->getID();
    const bool failed = msg != MessageID::ConnectionRequestAccepted;

    // we should have pending connections
    net::SystemAddress sa;

    if (pPacket->systemHandle != net::INVALID_SYSTEM_HANDLE) {
        sa = pPeer_->getAddressForHandle(pPacket->systemHandle);
    }
    else {
        // read it from the packet
        core::FixedBitStreamNoneOwning packetBs(pPacket->begin(), pPacket->end(), true);
        X_ASSERT(packetBs.sizeInBytes() >= sa.serializedSize(), "Packet should have a systemAdd")();

        sa.fromBitStream(packetBs);
    }

    LobbyFlags flags;

    const auto numPending = pendingConnections_.size();
    for (auto it = pendingConnections_.begin(); it != pendingConnections_.end(); )
    {
        auto& pc = *it;

        if (pc.address == sa)
        {
            flags.Set(ConnectedPeer::typeToFlag(pc.type));

            lobbys_[pc.type].handlePacket(pPacket);
            it = pendingConnections_.erase(it);
            continue;
        }

        ++it;
    }

    // we had no pending connections for this peer.
    const auto processed = numPending - pendingConnections_.size();
    if (processed == 0)
    {
        X_ASSERT_UNREACHABLE();
    }

    if (!failed) {
        X_ASSERT(flags.IsAnySet(), "No flags set")();
        peers_.emplace_back(pPacket->systemHandle, pPacket->guid, flags);
    }
}

void Session::handleTransportConnectionHandShake(Packet* pPacket)
{
    if (state_ == SessionState::Idle) {
        // we got a packet when idle, should this ever happen?
        // This means we have set the transport to allow incoming connections.
        // we should tell them to go away?
        X_ERROR("Session", "Received packet while idle. Msg: \"%s\"", MessageID::ToString(pPacket->getID()));
        // close connections for now when this happens
        pPeer_->closeConnection(pPacket->systemHandle, true, OrderingChannel::Default, PacketPriority::Low);
        return;
    }

    X_ASSERT(pPacket->getID() == net::MessageID::ConnectionRequestHandShake, "Incorrect message")();

    NetGuidStr guidStr;
    X_LOG0_IF(vars_.sessionDebug(), "Session", "Peer connected to us: %s", pPacket->guid.toString(guidStr));

    // a peer just connected to us, if they don't join a lobby time them out.
    auto timeNow = gEnv->pTimer->GetTimeNowReal();

    pendingJoins_.emplace_back(pPacket->systemHandle, pPacket->guid, timeNow);
}

void Session::sendPacketToLobbyIfGame(Packet* pPacket)
{
    switch (state_)
    {
        case SessionState::ConnectAndMoveToGame:
        case SessionState::GameLobbyHost:
        case SessionState::GameLobbyPeer:
        case SessionState::Loading:
        case SessionState::InGame:
            lobbys_[LobbyType::Game].handlePacket(pPacket);
            break;

        default:
            // TODO: if we just leave a game, can prevent incoming snapshots hitting this?
            X_WARNING("Session", "Received Game packet when not in game state: %s", SessionState::ToString(state_));
            break;
    }
}

void Session::sendPacketToDesiredLobby(Packet* pPacket)
{
    // message should have a lobby prefix.
    auto lobbyType = static_cast<LobbyType::Enum>(pPacket->pData[1]);

    // ignore the packet if idle.
    // should we send them a packet to tell them, or let them timeout?
    if (state_ == SessionState::Idle) {
        X_WARNING("Session", "Received lobby packet when idle. LobbyType: %" PRIu8, LobbyType::ToString(lobbyType));
        return;
    }

    switch (lobbyType)
    {
        case LobbyType::Party:
        case LobbyType::Game:
            break;

        default:
            X_ERROR("Session", "Received packet with invalid lobbyType: %s Type: %" PRIu8, SessionState::ToString(state_), lobbyType);
            X_ASSERT_UNREACHABLE();
            return;
    }

    auto& lobby = lobbys_[lobbyType];

    if (!lobby.isActive()) {
        X_WARNING("Session", "Received packet for inactive lobby. LobbyType: %" PRIu8, LobbyType::ToString(lobbyType));
        return;
    }

    lobby.handlePacket(pPacket);
}

void Session::getSessionInfo(SessionInfo& info) const
{
    info.status = getStatus();
    info.isHost = isHost();
}


bool Session::isHost(void) const
{
    // not sure what states I will allowthis to be caleld form yet.
    //X_ASSERT(getStatus() == SessionStatus::InGame, "Unexpected status")(getStatus());

    return lobbys_[LobbyType::Game].isHost();
}

SessionStatus::Enum Session::getStatus(void) const
{
    static_assert(SessionState::ENUM_COUNT == 11, "Enums changed?");
    static_assert(SessionStatus::ENUM_COUNT == 6, "Enums changed?");

    switch(state_)
    {
        case SessionState::Idle:
            return SessionStatus::Idle;
        case SessionState::CreateAndMoveToPartyLobby:
            return SessionStatus::Connecting;
        case SessionState::CreateAndMoveToGameLobby:
            return SessionStatus::Connecting;
        case SessionState::PartyLobbyHost:
            return SessionStatus::PartyLobby;
        case SessionState::PartyLobbyPeer:
            return SessionStatus::PartyLobby;
        case SessionState::GameLobbyHost:
            return SessionStatus::GameLobby;
        case SessionState::GameLobbyPeer:
            return SessionStatus::GameLobby;
        case SessionState::ConnectAndMoveToParty:
            return SessionStatus::Connecting;
        case SessionState::ConnectAndMoveToGame:
            return SessionStatus::Connecting;
        case SessionState::Loading:
            return SessionStatus::Loading;
        case SessionState::InGame:
            return SessionStatus::InGame;
        default:
            X_NO_SWITCH_DEFAULT_ASSERT;
    }

    return SessionStatus::Idle;
}

const MatchParameters& Session::getMatchParams(void) const
{
    return lobbys_[LobbyType::Game].getMatchParams();
}

bool Session::getPingInfo(SystemHandle systemHandle, PingInfo& info) const
{
    return pPeer_->getPingInfo(systemHandle, info);
}

Vec2f Session::drawDebug(Vec2f base, engine::IPrimitiveContext* pPrim) const
{
    const auto debugLvl = vars_.drawLobbyDebug();

    if (debugLvl <= 0) {
        return Vec2f::zero();
    }

    const float spacing = 20.f;

    Vec2f size;

    // session debug.
    if (debugLvl >= 1)
    {
        font::TextDrawContext con;
        con.col = Col_Whitesmoke;
        con.size = Vec2f(16.f, 16.f);
        con.effectId = 0;
        con.pFont = gEnv->pFontSys->getDefault();


        core::StackString<512> txt;
        txt.setFmt("Session - %s PendingJoins: %" PRIuS " PendingCon: %" PRIuS " Peers: %" PRIuS " \n",
            SessionState::ToString(state_), pendingJoins_.size(), pendingConnections_.size(), peers_.size());

        const float width = 750.f;
        float height = 30.f;

        pPrim->drawQuad(base, width, height, Color8u(20, 20, 20, 150));
        pPrim->drawText(base.x + 2.f, base.y + 2.f, con, txt.begin(), txt.end());

        base.y += height;

        size = Vec2f(width, height + spacing);

        if(peers_.isNotEmpty())
        {
            txt.clear();

            height = 30.f * peers_.size();

            NetGuidStr guidStr;
            LobbyFlags::Description flagDsc;
            for (size_t i=0; i<peers_.size(); i++)
            {
                const auto& p = peers_[i];
                txt.setFmt("Peer%" PRIuS " ^1%s^7 Lobby: [^6%s^7]\n", i, p.guid.toString(guidStr), p.flags.ToString(flagDsc));
            }

            pPrim->drawQuad(base, width, height, Color8u(20, 20, 20, 150));
            pPrim->drawText(base.x + 2.f, base.y + 2.f, con, txt.begin(), txt.end());
        }
        else
        {
            height = 0.f;
        }

        base.y += height + spacing;
        size.y += height;
    }

    if (debugLvl >= 2)
    {
        auto s0 = lobbys_[LobbyType::Party].drawDebug(base, pPrim);

        float height = s0.y + spacing;

        base.y += height;

        size.x = core::Max(size.x, s0.x);
        size.y += height;
    }

    if (debugLvl >= 1)
    {
        auto s1 = lobbys_[LobbyType::Game].drawDebug(base, pPrim);

        size.y += s1.y;
        size.x = core::Max(size.x, s1.x);
    }

    return size;
}


X_NAMESPACE_END
