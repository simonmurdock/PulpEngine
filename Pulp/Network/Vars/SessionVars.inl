

X_NAMESPACE_BEGIN(net)

X_INLINE bool SessionVars::sessionDebug(void) const
{
    return sessionDebug_ != 0;
}

X_INLINE bool SessionVars::lobbyDebug(void) const
{
    return lobbyDebug_ != 0;
}

X_INLINE int32_t SessionVars::drawLobbyDebug(void) const
{
    return drawLobbyDebug_;
}

X_INLINE int32_t SessionVars::connectionAttemps(void) const
{
    return connectionAttemps_;
}

X_INLINE int32_t SessionVars::connectionRetryDelayMs(void) const
{
    return connectionRetryDelayMs_;
}

X_INLINE int32_t SessionVars::joinLobbyTimeoutMs(void) const
{
    return joinLobbyTimeoutMs_;
}

X_INLINE bool SessionVars::snapDebug(void) const
{
    return snapDebug_ != 0;
}

X_INLINE bool SessionVars::snapFroceDrop(void)
{
    if (snapFroceDrop_ == 0) {
        return false;
    }

    snapFroceDrop_ = 0;
    return true;
}

X_INLINE int32_t SessionVars::snapMaxbufferedMs(void) const
{
    return snapMaxbufferedMs_;
}

X_INLINE int32_t SessionVars::snapRateMs(void) const
{
    return snapRateMs_;
}

X_INLINE float SessionVars::snapFallbackRate(void) const
{
    return snapFallbackRate_;
}

X_INLINE float SessionVars::snapCatchupRate(void) const
{
    return snapCatchupRate_;
}

X_INLINE int32_t SessionVars::userCmdRateMs(void) const
{
    return userCmdRateMs_;
}

X_INLINE int32_t SessionVars::waitForPlayers(void) const
{
    return waitForPlayers_;
}

X_NAMESPACE_END
