
X_NAMESPACE_BEGIN(core)


X_INLINE consoleState::Enum XConsole::getVisState(void) const
{
    return consoleState_;
}

X_INLINE void XConsole::showConsole(consoleState::Enum state)
{
    if (state == consoleState::CLOSED) {
        resetHistoryPos();
    }

    consoleState_ = state;
}

X_INLINE bool XConsole::isVisible(void) const
{
    return consoleState_ != consoleState::CLOSED;
}

X_INLINE bool XConsole::isExpanded(void) const
{
    return consoleState_ == consoleState::EXPANDED;
}

X_INLINE void XConsole::toggleConsole(bool expand)
{
    if (isVisible()) {
        showConsole(consoleState::CLOSED);
    }
    else {
        if (expand) {
            showConsole(consoleState::EXPANDED);
        }
        else {
            showConsole(consoleState::OPEN);
        }
    }
}

X_INLINE bool XConsole::isAutocompleteVis(void)
{
    return autoCompleteNum_ > 0;
}

X_NAMESPACE_END
