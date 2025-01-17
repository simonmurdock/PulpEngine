#pragma once

#include "AssetPak.h"

#include <../AssetDB/AssetDB.h>
#include <../ConverterLib/ConverterLib.h>

X_NAMESPACE_BEGIN(linker)

struct BuildOptions
{
    core::Path<char> assetList;
    core::Path<char> outFile;
    core::string level;
    core::string mod;
    AssetPak::PakBuilderFlags flags;
};

struct DirEntry;

class Linker
{
    typedef core::Array<uint8_t> DataVec;

public:
    LINKERLIB_EXPORT Linker(assetDb::AssetDB& db, core::MemoryArenaBase* scratchArea);
    LINKERLIB_EXPORT ~Linker();

    LINKERLIB_EXPORT void PrintBanner(void);
    LINKERLIB_EXPORT bool Init(void);

    LINKERLIB_EXPORT bool dumpMetaOS(core::Path<>& osPath);

    LINKERLIB_EXPORT bool Build(BuildOptions& options);

private:
    bool AddEntDesc(core::Path<char>& inputFile);
    bool AddAssetList(core::Path<char>& inputFile);
    bool AddAssetDir(const DirEntry& entry, const core::Path<>& relPath, const core::Path<>& dirPath, int32_t& numAdded);
    bool AddAssetAndDepenency(assetDb::AssetType::Enum assType, const core::string& name);
    bool AddAsset(assetDb::AssetType::Enum assType, const core::string& name);
    bool AddAssetFromDisk(assetDb::AssetType::Enum assType, const core::string& name, const core::Path<char>& path);

    static bool LoadFile(const core::Path<char>& path, core::Array<uint8_t>& dataOut);


private:
    core::MemoryArenaBase* scratchArea_;
    assetDb::AssetDB& db_;
    converter::Converter converter_;

    AssetPak::AssetPakBuilder builder_;
};

X_NAMESPACE_END
