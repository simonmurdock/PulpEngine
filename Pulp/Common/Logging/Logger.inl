


template<class FilterPolicy, class FormatPolicy, class WritePolicy>
Logger<FilterPolicy, FormatPolicy, WritePolicy>::Logger(void) :
    LoggerBase(),
    filter_(),
    formatter_(),
    writer_()
{
    filter_.Init();
    formatter_.Init();
    writer_.Init();
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
Logger<FilterPolicy, FormatPolicy, WritePolicy>::Logger(const FilterPolicy& filter, const FormatPolicy& formatter, const WritePolicy& writer) :
    LoggerBase(),
    filter_(filter),
    formatter_(formatter),
    writer_(writer)
{
    filter_.Init();
    formatter_.Init();
    writer_.Init();
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
Logger<FilterPolicy, FormatPolicy, WritePolicy>::~Logger(void)
{
    filter_.Exit();
    formatter_.Exit();
    writer_.Exit();
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
void Logger<FilterPolicy, FormatPolicy, WritePolicy>::DoLog(X_SOURCE_INFO_LOG_CA(const SourceInfo& sourceInfo) const char* pChannel, int verbosity, const char* pFormat, va_list args)
{
    if (filter_.Filter(LogType::INFO, X_SOURCE_INFO_LOG_CA(sourceInfo) pChannel, verbosity, pFormat, args)) {
        LoggerBase::Line line;
        const uint32_t length = formatter_.Format(line, pLog_->GetIndentation(), LogType::INFO, X_SOURCE_INFO_LOG_CA(sourceInfo) pChannel, verbosity, pFormat, args);
        writer_.WriteLog(line, length);
    }
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
void Logger<FilterPolicy, FormatPolicy, WritePolicy>::DoWarning(X_SOURCE_INFO_LOG_CA(const SourceInfo& sourceInfo) const char* pChannel, const char* pFormat, va_list args)
{
    if (filter_.Filter(LogType::WARNING, X_SOURCE_INFO_LOG_CA(sourceInfo) pChannel, 0, pFormat, args)) {
        LoggerBase::Line line;
        const uint32_t length = formatter_.Format(line, pLog_->GetIndentation(), LogType::WARNING, X_SOURCE_INFO_LOG_CA(sourceInfo) pChannel, 0, pFormat, args);
        writer_.WriteWarning(line, length);
    }
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
void Logger<FilterPolicy, FormatPolicy, WritePolicy>::DoError(X_SOURCE_INFO_LOG_CA(const SourceInfo& sourceInfo) const char* pChannel, const char* pFormat, va_list args)
{
    if (filter_.Filter(LogType::ERROR, X_SOURCE_INFO_LOG_CA(sourceInfo) pChannel, 0, pFormat, args)) {
        LoggerBase::Line line;
        const uint32_t length = formatter_.Format(line, pLog_->GetIndentation(), LogType::ERROR, X_SOURCE_INFO_LOG_CA(sourceInfo) pChannel, 0, pFormat, args);
        writer_.WriteError(line, length);
    }
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
void Logger<FilterPolicy, FormatPolicy, WritePolicy>::DoFatal(X_SOURCE_INFO_LOG_CA(const SourceInfo& sourceInfo) const char* pChannel, const char* pFormat, va_list args)
{
    if (filter_.Filter(LogType::FATAL, X_SOURCE_INFO_LOG_CA(sourceInfo) pChannel, 0, pFormat, args)) {
        LoggerBase::Line line;
        const uint32_t length = formatter_.Format(line, pLog_->GetIndentation(), LogType::FATAL, X_SOURCE_INFO_LOG_CA(sourceInfo) pChannel, 0, pFormat, args);
        writer_.WriteFatal(line, length);
    }
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
void Logger<FilterPolicy, FormatPolicy, WritePolicy>::DoAssert(X_SOURCE_INFO_LOG_CA(const SourceInfo& sourceInfo) const char* pFormat, va_list args)
{
    if (filter_.Filter(LogType::ASSERT, X_SOURCE_INFO_LOG_CA(sourceInfo) "Assert", 0, pFormat, args)) {
        LoggerBase::Line line;
        const uint32_t length = formatter_.Format(line, pLog_->GetIndentation(), LogType::ASSERT, X_SOURCE_INFO_LOG_CA(sourceInfo) "Assert", 0, pFormat, args);
        writer_.WriteAssert(line, length);
    }
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
void Logger<FilterPolicy, FormatPolicy, WritePolicy>::DoAssertVariable(X_SOURCE_INFO_LOG_CA(const SourceInfo& sourceInfo) const char* pFormat, va_list args)
{
    if (filter_.Filter(LogType::ASSERT, X_SOURCE_INFO_LOG_CA(sourceInfo) "Assert", 0, pFormat, args)) {
        LoggerBase::Line line;
        const uint32_t length = formatter_.Format(line, pLog_->GetIndentation(), LogType::ASSERT, X_SOURCE_INFO_LOG_CA(sourceInfo) "Assert", 0, pFormat, args);
        writer_.WriteAssertVariable(line, length);
    }
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
const FilterPolicy& Logger<FilterPolicy, FormatPolicy, WritePolicy>::GetFilterPolicy(void) const
{
    return filter_;
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
const FormatPolicy& Logger<FilterPolicy, FormatPolicy, WritePolicy>::GetFormatPolicy(void) const
{
    return formatter_;
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
const WritePolicy& Logger<FilterPolicy, FormatPolicy, WritePolicy>::GetWritePolicy(void) const
{
    return writer_;
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
FilterPolicy& Logger<FilterPolicy, FormatPolicy, WritePolicy>::GetFilterPolicy(void)
{
    return filter_;
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
FormatPolicy& Logger<FilterPolicy, FormatPolicy, WritePolicy>::GetFormatPolicy(void)
{
    return formatter_;
}



template<class FilterPolicy, class FormatPolicy, class WritePolicy>
WritePolicy& Logger<FilterPolicy, FormatPolicy, WritePolicy>::GetWritePolicy(void)
{
    return writer_;
}
