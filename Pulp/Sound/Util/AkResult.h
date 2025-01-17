#pragma once

X_NAMESPACE_BEGIN(sound)

namespace AkResult
{
    using Description = core::StackString<128, char>;

    const char* ToString(AKRESULT res, Description& desc);
} // namespace AkResult

X_NAMESPACE_END