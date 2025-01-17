#pragma once

#include <IAssetDb.h>

#include "StackString.h"

X_NAMESPACE_BEGIN(core)

class AssetName : public StackString<256, char>
{
    typedef StackString<256, char> BaseType;

public:
    static const size_t BUF_SIZE = MAX_PATH;

    static const char ASSET_NAME_SLASH = assetDb::ASSET_NAME_SLASH;
    static const char ASSET_NAME_INVALID_SLASH = assetDb::ASSET_NAME_INVALID_SLASH;

public:
    AssetName();
    AssetName(assetDb::AssetType::Enum type, const core::string_view name);
    AssetName(assetDb::AssetType::Enum type, core::string_view name, core::string_view ext);
    AssetName(assetDb::AssetType::Enum type, const core::string& name);
    AssetName(assetDb::AssetType::Enum type, const core::string& name, const char* pExt);
    AssetName(const AssetName& oth);
    template<size_t N>
    explicit AssetName(const core::StackString<N, char>& oth);

    explicit AssetName(const char* const str);
    AssetName(const char* const beginInclusive, const char* const endExclusive);

public:
    void replaceSeprators(void);

    void set(assetDb::AssetType::Enum type, const core::string& name);
    void set(assetDb::AssetType::Enum type, const core::string_view name);
    bool stripAssetFolder(assetDb::AssetType::Enum type);

    const char* extension(bool incDot) const;
    void setExtension(const char* pExtension);
    void setExtension(core::string_view extension);
    void removeExtension(void);
};

X_NAMESPACE_END

#include "AssetName.inl"