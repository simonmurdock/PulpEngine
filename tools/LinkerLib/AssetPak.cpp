#include "stdafx.h"
#include "AssetPak.h"

#include <IAssetPak.h>
#include <IFileSys.h>

#include <Compression\CompressorAlloc.h>
#include <Compression\DictBuilder.h>

#include <Time\StopWatch.h>
#include <String\HumanSize.h>
#include <String\HumanDuration.h>
#include <String\StringHash.h>
#include <Memory\MemCursor.h>

#include <Threading\JobSystem2.h>

#include <../../tools/AssetDB/AssetFileExtensions.h>

X_NAMESPACE_BEGIN(AssetPak)

namespace
{
    struct JobData
    {
        JobData(Asset* pAsset, const CompressionOptions* pCompOpt, core::MemoryArenaBase* scratchArena) :
            pAsset(pAsset),
            pCompOpt(pCompOpt),
            scratchArena(scratchArena)
        {
        }

        Asset* pAsset;
        const CompressionOptions* pCompOpt;
        core::MemoryArenaBase* scratchArena;
    };

    void compression_job(core::V2::JobSystem&, size_t threadIdx, core::V2::Job* job, void* jobData)
    {
        X_UNUSED(threadIdx, job);
        const JobData* pData = reinterpret_cast<const JobData*>(jobData);
        auto& asset = *pData->pAsset;
        auto& compOpt = *pData->pCompOpt;

        DataVec compData(pData->scratchArena);
        compData.reserve(1024 * 512);

        core::Compression::CompressorAlloc comp(compOpt.algo);

        comp->deflate(pData->scratchArena, asset.data, compData, core::Compression::CompressLevel::HIGH);

        float ratio = static_cast<float>(compData.size()) / static_cast<float>(asset.data.size());
        bool keep = ratio < compOpt.maxRatio;

        if (keep) {
            asset.data.resize(compData.size());
            std::memcpy(asset.data.data(), compData.begin(), compData.size());
        }

        core::HumanSize::Str sizeStr, sizeStr1;

        X_LOG0("AssetPak", "^5%-46s ^7orig: ^6%-10s^7 comp: ^6%-10s ^1%-6.2f %s", asset.name.c_str(),
            core::HumanSize::toString(sizeStr, asset.infaltedSize),
            core::HumanSize::toString(sizeStr1, compData.size()),
            ratio,
            keep ? "^8<keep>" : "^1<original>");
    }

    void hashAsset_job(Asset* pAssets, size_t num)
    {
        core::Hash::xxHash64 hasher;

        for (size_t i = 0; i < num; i++)
        {
            auto& asset = pAssets[i];

            hasher.reset();
            asset.hash = hasher.calc(asset.data.data(), asset.data.size());
        }
    }

} // namespace

Asset::Asset(AssetId id, AssetType::Enum type, const core::string& name,
    DataVec&& data, core::MemoryArenaBase* arena) :
    name(name),
    id(id),
    type(type),
    infaltedSize(data.size()),
    data(std::move(data)),
    hash(0)
{
    X_ASSERT(infaltedSize > 0, "size is zero")(infaltedSize, data.size(), this->data.size()); 
    X_UNUSED(arena);
}

SharedDict::SharedDict(core::MemoryArenaBase* arena) :
    dict(arena)
{
}

// -----------------------------------------------------

AssetPakBuilder::AssetPakBuilder(core::MemoryArenaBase* arena) :
    arena_(arena),
    assets_(arena),
    assetLookup_(arena),
    deflatedAssetSize_(0),
    infaltedAssetSize_(0)
{
    assets_.reserve(1024);
    assetLookup_.reserve(1024);

    compression_[AssetType::ANIM].enabled = true;
    compression_[AssetType::ANIM].algo = core::Compression::Algo::LZ4HC;
    compression_[AssetType::MODEL].enabled = true;
    compression_[AssetType::MODEL].algo = core::Compression::Algo::LZ4HC;
    // compression_[AssetType::MATERIAL].enabled = true;
    // compression_[AssetType::MATERIAL].algo = core::Compression::Algo::LZ4;
    compression_[AssetType::WEAPON].enabled = true;
    compression_[AssetType::WEAPON].algo = core::Compression::Algo::LZ4HC;

    compression_[AssetType::IMG].enabled = true;
    compression_[AssetType::IMG].maxRatio = 0.85f;
    compression_[AssetType::IMG].algo = core::Compression::Algo::LZ4HC;

    compression_[AssetType::FONT].enabled = true;
    compression_[AssetType::FONT].maxRatio = 0.85f;
    compression_[AssetType::FONT].algo = core::Compression::Algo::LZ4HC;

    compression_[AssetType::LEVEL].enabled = true;
    compression_[AssetType::LEVEL].maxRatio = 0.90f;
    compression_[AssetType::LEVEL].algo = core::Compression::Algo::LZ4HC;

    compression_[AssetType::TECHDEF].enabled = true;
    compression_[AssetType::TECHDEF].maxRatio = 0.90f;
    compression_[AssetType::TECHDEF].algo = core::Compression::Algo::LZ4HC;

    compression_[AssetType::SCRIPT].enabled = true;
    compression_[AssetType::SCRIPT].maxRatio = 0.85f;
    compression_[AssetType::SCRIPT].algo = core::Compression::Algo::LZ4HC;

    compression_[AssetType::RAW].enabled = true;
    compression_[AssetType::RAW].maxRatio = 0.90f;
    compression_[AssetType::RAW].algo = core::Compression::Algo::LZ4HC;


    // per asset shared dictionary.
    dictonaries_.fill(nullptr);
    // dictonaries_[AssetType::MODEL] = X_NEW(SharedDict, arena, "CompressionDict")(arena);

    assetCounts_.fill(0);
    compressedAssetCounts_.fill(0);
}

AssetPakBuilder::~AssetPakBuilder()
{
    for (auto* pDict : dictonaries_) {
        if (pDict) {
            X_DELETE(pDict, arena_);
        }
    }
}

void AssetPakBuilder::setFlags(PakBuilderFlags flags)
{
    flags_ = flags;
}

bool AssetPakBuilder::process(void)
{
    X_LOG0("AssetPak", "===== Processing %" PRIuS " asset(s) =====", assets_.size());

    // dict training.
    if (flags_.IsSet(PakBuilderFlag::SHARED_DICT)) {
        const size_t maxDictSize = std::numeric_limits<uint16_t>::max() - sizeof(core::Compression::SharedDictHdr);

        DataVec sampleData(arena_);

        core::Array<size_t> sampleSizes(arena_);
        sampleSizes.setGranularity(256);

        core::HumanSize::Str sizeStr, sizeStr1;

        for (uint32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
            auto type = static_cast<AssetType::Enum>(i);

            if (!dictonaries_[type] || !compression_[type].enabled) {
                continue;
            }

            // work out required buffer size.
            const auto sampleDataSize = core::accumulate(assets_.begin(), assets_.end(), 0_sz, [type](const Asset& a) -> size_t {
                if (a.type != type) {
                    return 0;
                }
                return core::Min<size_t>(a.data.size(), core::Compression::DICT_SAMPLER_SIZE_MAX);
            });

            sampleSizes.clear();
            sampleData.resize(sampleDataSize);

            size_t currentOffset = 0;

            for (const auto& a : assets_) {
                if (a.type != type) {
                    continue;
                }

                const size_t sampleSize = core::Min<size_t>(a.data.size(), core::Compression::DICT_SAMPLER_SIZE_MAX);

                std::memcpy(&sampleData[currentOffset], a.data.data(), sampleSize);

                sampleSizes.push_back(sampleSize);
                currentOffset += sampleSize;
            }

            X_ASSERT(currentOffset == sampleDataSize, "Failed to write all sample data")(currentOffset, sampleDataSize); 

            // train.
            X_LOG0("AssetPak", "Training for assetType \"%s\" with ^6%s^7 sample data, ^6%" PRIuS "^7 files, avg size: ^6%s",
                AssetType::ToString(type),
                core::HumanSize::toString(sizeStr, sampleData.size()), sampleSizes.size(),
                core::HumanSize::toString(sizeStr1, sampleData.size() / sampleSizes.size()));

            core::StopWatch timer;

            if (!core::Compression::trainDictionary(sampleData, sampleSizes, dictonaries_[type]->dict, maxDictSize)) {
                X_ERROR("AssetPak", "Fail to train dictionary for assetType: \"%s\"", AssetType::ToString(type));
                return false;
            }

            core::Compression::SharedDictHdr& hdr = *(core::Compression::SharedDictHdr*)dictonaries_[type]->dict.data();
            hdr.magic = core::Compression::SharedDictHdr::MAGIC;
            hdr.sharedDictId = gEnv->xorShift.rand() & 0xFFFF;

            const auto size = dictonaries_[type]->dict.size() - sizeof(hdr);

            hdr.size = safe_static_cast<uint32_t>(size);

            const float trainTime = timer.GetMilliSeconds();
            core::HumanDuration::Str timeStr;
            X_LOG0("AssetPak", "Train took: ^6%s", core::HumanDuration::toString(timeStr, trainTime));
        }
    }

    auto* pJobSys = gEnv->pJobSys;

    // hash the assets.
    {
        X_LOG1("AssetPak", "Calculating raw hashes");

        auto* pJob = pJobSys->parallel_for(assets_.data(), assets_.size(), hashAsset_job, core::V2::CountSplitter(4)
            JOB_SYS_SUB_ARG(core::profiler::SubSys::TOOL));

        pJobSys->RunAndWait(pJob);
    }


    if (flags_.IsSet(PakBuilderFlag::COMPRESSION)) {

        // compression.
        for (uint32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
            auto type = static_cast<AssetType::Enum>(i);
            const auto& compOpt = compression_[type];

            if (!compOpt.enabled) {
                continue;
            }

            if (assetCounts_[type] == 0) {
                continue;
            }

            X_LOG0("AssetPak", "===== Compressing type \"%s\" maxRatio: ^1%.2f^7 =====", AssetType::ToString(type), compOpt.maxRatio);

            auto* pRoot = pJobSys->CreateEmtpyJob(JOB_SYS_SUB_ARG_SINGLE(core::profiler::SubSys::TOOL));

            for (auto& a : assets_) {
                if (a.type != type) {
                    continue;
                }

                JobData data(&a, &compOpt, arena_);

                auto* pJob = pJobSys->CreateJobAsChild<JobData>(pRoot, compression_job, data JOB_SYS_SUB_ARG(core::profiler::SubSys::TOOL));
                pJobSys->Run(pJob);
            }

            pJobSys->RunAndWait(pRoot);
        }
    }

    X_LOG1("AssetPak", "Sorting assets");

    // sort them?
    std::sort(assets_.begin(), assets_.end(), [](const Asset& a, const Asset& b) {
        return a.data.size() < b.data.size();
    });

    X_LOG1("AssetPak", "Calculating sizes");

    {
        uint64_t deflatedSize = 0;
        uint64_t inflatedSize = 0;

        for (const auto& a : assets_) {
            inflatedSize += a.infaltedSize;
            deflatedSize += a.data.size();

            int32_t isCompressed = (a.infaltedSize > a.data.size());

            compressedAssetCounts_[a.type] += isCompressed;
        }

        deflatedAssetSize_ = deflatedSize;
        infaltedAssetSize_ = inflatedSize;
    }

    if (!flags_.IsSet(PakBuilderFlag::COMPRESSION))
    {
        X_ASSERT(deflatedAssetSize_ == infaltedAssetSize_, "Should be identical")(deflatedAssetSize_, infaltedAssetSize_);
    }

    return true;
}

bool AssetPakBuilder::save(const core::Path<char>& path)
{
    if (assets_.size() > PAK_MAX_ASSETS) {
        X_ERROR("AssetPak", "Pak contains too many assets %" PRIuS " max: " PRIu32, assets_.size(), PAK_MAX_ASSETS);
        return false;
    }

    if (assets_.isEmpty()) {
        X_ERROR("AssetPak", "No assets in pak");
        return false;
    }

    core::XFileScoped file;
    core::FileFlags mode;
    mode.Set(core::FileFlag::RECREATE);
    mode.Set(core::FileFlag::WRITE);

    // ensure correct extension.
    core::Path<char> pathExt(path);
    pathExt.setExtension(PAK_FILE_EXTENSION);
    pathExt.replaceSeprators();

    if (!file.openFile(pathExt, mode)) {
        X_ERROR("AssetPak", "Failed to open file for saving");
        return false;
    }

    APakHeader hdr;
    core::zero_object(hdr.pad);
    hdr.algos.fill(core::Compression::Algo::STORE);
    hdr.magic = PAK_MAGIC;
    hdr.version = PAK_VERSION;
    hdr.flags.Clear();
    hdr.unused = 0;
    hdr.size = 0;
    hdr.inflatedSize = 0;
    hdr.numAssets = safe_static_cast<uint32_t>(assets_.size());

    if (flags_.IsSet(PakBuilderFlag::TIMESTAMP)) {
        hdr.modified = core::DateTimeStampSmall::systemDateTime();
    }

    if (flags_.IsSet(PakBuilderFlag::HINT_MEMORY)) {
        hdr.flags.Set(APakFlag::HINT_MEMORY);
    }

    if (flags_.IsSet(PakBuilderFlag::COMPRESSION))
    {
        for (uint32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
            auto type = static_cast<AssetType::Enum>(i);

            X_ASSERT(compressedAssetCounts_[type] <= assetCounts_[type], "More compressed assets than assets")(compressedAssetCounts_[type], assetCounts_[type]);

            if (!assetCounts_[type]) {
                continue;
            }

            if (!compression_[type].enabled) {
                continue;
            }

            if (!compressedAssetCounts_[type]) {
                X_WARNING("AssetPak", "Compression was enabled for \"%s\" but no compressed versions where kept", AssetType::ToString(type));
                continue;
            }

            hdr.algos[type] = compression_[type].algo;
        }
    }

    {
        bool compressed = (core::accumulate(compressedAssetCounts_.begin(), compressedAssetCounts_.end(), 0_sz) > 0);
        X_ASSERT(compressed == hdr.hasCompressedAssets(), "Logic error")(compressed, hdr.hasCompressedAssets());
    }
    

    core::ByteStream strings(arena_);
    core::ByteStream entries(arena_);
    core::ByteStream sharedDictsData(arena_);
    core::ByteStream hashes(arena_);
    core::ByteStream data(arena_);

    // write all the strings.
    uint64_t stringDataSize = 0;
    uint64_t dataSize = 0;

    {
        std::array<core::StackString<32>, assetDb::AssetType::ENUM_COUNT> assetPrefixes;
        std::array<core::StackString<32>, assetDb::AssetType::ENUM_COUNT> assetExt;

        for (size_t i = 0; i < assetCounts_.size(); i++) {
            if (!assetCounts_[i]) {
                continue;
            }

            auto type = static_cast<AssetType::Enum>(i);

            auto& prefix = assetPrefixes[i];
            prefix.append(AssetType::ToString(type));
            prefix.append('s', 1);
            prefix.toLower();
            prefix.append(assetDb::ASSET_NAME_SLASH, 1);

            stringDataSize += (assetCounts_[i] * prefix.length());

            assetExt[i].setFmt(".%s", assetDb::getAssetTypeExtension(type));
        }


        for (const auto& a : assets_) {
            stringDataSize += core::strUtil::StringBytes(a.name);

            if (a.id != assetDb::INVALID_ASSET_ID) {
                stringDataSize += assetExt[a.type].length();
            }

            stringDataSize += sizeof(NameLengthType);
        }

        stringDataSize = core::bitUtil::RoundUpToMultiple<uint64_t>(stringDataSize, PAK_BLOCK_PADDING);

        strings.reserve(safe_static_cast<size_t>(stringDataSize));

        core::StackString<assetDb::ASSET_NAME_MAX_LENGTH> name;

        for (const auto& a : assets_) {
            const auto& prefix = assetPrefixes[a.type];

            name.clear();
            name.append(prefix.data(), prefix.length());
            name.append(a.name.data(), core::strUtil::StringBytes(a.name));

            if (a.id != assetDb::INVALID_ASSET_ID) {
                const auto& ext = assetExt[a.type];
                name.append(ext.data(), ext.length());
            }

            auto length = safe_static_cast<NameLengthType>(name.length());

            strings.write<NameLengthType>(length);
            strings.write(name.data(), name.length());
        }

        strings.alignWrite(PAK_BLOCK_PADDING);
        X_ASSERT(strings.size() == stringDataSize, "Size calculation invalid")(strings.size(), stringDataSize);
    }

    // write all the asset entries.
    const uint64_t entryTablesize = sizeof(APakEntry) * assets_.size();

    {
        entries.reserve(safe_static_cast<size_t>(entryTablesize));

        uint64_t assetOffset = 0;

        for (const auto& a : assets_) {
            X_ASSERT_ALIGNMENT(assetOffset, PAK_ASSET_PADDING, 0);

            APakEntry entry;
            entry._pad = 0xffff;
            entry.id = a.id;
            entry.type = a.type;
            entry.flags.Clear();
            entry.offset = assetOffset;
            entry.size = safe_static_cast<uint32_t>(a.data.size());
            entry.inflatedSize = safe_static_cast<uint32_t>(a.infaltedSize);

            entries.write(entry);

            assetOffset += core::bitUtil::RoundUpToMultiple<uint64_t>(a.data.size(), PAK_ASSET_PADDING);
        }

        entries.alignWrite(PAK_BLOCK_PADDING);
    }

    // shared dic
    if (std::any_of(dictonaries_.begin(), dictonaries_.end(), [](const SharedDict* pDict) { return pDict != nullptr && pDict->dict.isNotEmpty(); })) {
        hdr.flags.Set(APakFlag::SHARED_DICTS);

        // not sure if want this in header or not.
        APakDictInfo dictInfo;
        core::zero_object(dictInfo.sharedHdrs);

        uint32_t offset = sizeof(dictInfo);

        for (uint32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
            auto type = static_cast<AssetType::Enum>(i);
            if (dictonaries_[type] && compression_[type].enabled) {
                const auto* pDict = dictonaries_[type];

                dictInfo.sharedHdrs[type].size = safe_static_cast<uint32_t>(pDict->dict.size());
                dictInfo.sharedHdrs[type].offset = offset;

                offset = safe_static_cast<uint32_t>(offset + pDict->dict.size());
            }
        }

        sharedDictsData.reserve(offset);
        sharedDictsData.write(dictInfo);

        for (uint32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
            auto type = static_cast<AssetType::Enum>(i);
            if (dictonaries_[type] && compression_[type].enabled) {
                const auto* pDict = dictonaries_[type];

                sharedDictsData.write(pDict->dict.data(), pDict->dict.size());
            }
        }

        X_ASSERT(offset == sharedDictsData.size(), "Size calculation errro")(offset, sharedDictsData.size()); 

        sharedDictsData.alignWrite(PAK_ASSET_PADDING);
    }

    // assets hashes.
    {
        hashes.reserve(sizeof(AssetHash) * assets_.size());

        for (const auto& a : assets_) {
            hashes.write(a.hash);
        }

        hashes.alignWrite(PAK_BLOCK_PADDING);
    }

    // write all the asset data.
    {
        for (const auto& a : assets_) {
            if (a.data.size() > PAK_MAX_ASSET_SIZE) {
                X_ERROR("AssetPak", "Asset in pack is too large %" PRIuS " max: " PRIu32, a.data.size(), PAK_MAX_ASSET_SIZE);
                return false;
            }

            dataSize += core::bitUtil::RoundUpToMultiple<uint64_t>(a.data.size(), PAK_ASSET_PADDING);
        }

        data.reserve(safe_static_cast<size_t>(dataSize));

        for (const auto& a : assets_) {
            X_ASSERT_ALIGNMENT(data.size(), PAK_ASSET_PADDING, 0);

            data.write(a.data.data(), a.data.size());
            data.alignWrite(PAK_ASSET_PADDING);
        }
    }

    // calculate the offsets of the data.
    const uint64_t entryTableOffset = core::bitUtil::RoundUpToMultiple(sizeof(hdr) + strings.size(), PAK_BLOCK_PADDING);
    const uint64_t dictsOffset = core::bitUtil::RoundUpToMultiple<uint64_t>(entryTableOffset + entryTablesize, PAK_BLOCK_PADDING);
    const uint64_t hashesOffset = core::bitUtil::RoundUpToMultiple<uint64_t>(dictsOffset + sharedDictsData.size(), PAK_BLOCK_PADDING);
    const uint64_t dataOffset = core::bitUtil::RoundUpToMultiple<uint64_t>(hashesOffset + hashes.size(), PAK_BLOCK_PADDING);

    uint64_t totalFileSize = 0;
    totalFileSize += sizeof(hdr);
    totalFileSize = core::bitUtil::RoundUpToMultiple<uint64_t>(totalFileSize + stringDataSize, PAK_BLOCK_PADDING);
    totalFileSize = core::bitUtil::RoundUpToMultiple<uint64_t>(totalFileSize + entryTablesize, PAK_BLOCK_PADDING);
    totalFileSize = core::bitUtil::RoundUpToMultiple<uint64_t>(totalFileSize + sharedDictsData.size(), PAK_BLOCK_PADDING);
    totalFileSize = core::bitUtil::RoundUpToMultiple<uint64_t>(totalFileSize + hashes.size(), PAK_BLOCK_PADDING);
    totalFileSize += dataSize;

    if (totalFileSize > PAK_MAX_SIZE) {
        X_ERROR("AssetPak", "Asset exceeds max size %" PRIuS " max: " PRIu32, totalFileSize, PAK_MAX_SIZE);
        return false;
    }

    if (entryTableOffset > std::numeric_limits<decltype(hdr.entryTableOffset)>::max()) {
        X_ERROR("AssetPak", "Invalid entry table offset");
        return false;
    }
    if (dataOffset > std::numeric_limits<decltype(hdr.dataOffset)>::max()) {
        X_ERROR("AssetPak", "Invalid data offset");
        return false;
    }

    file.setSize(static_cast<int64_t>(totalFileSize));

    hdr.size = totalFileSize;
    hdr.inflatedSize = totalFileSize;
    hdr.stringDataOffset = sizeof(hdr);
    hdr.entryTableOffset = safe_static_cast<uint32_t>(entryTableOffset);
    hdr.dictOffset = safe_static_cast<uint32_t>(dictsOffset);
    hdr.hashesOffset = safe_static_cast<uint32_t>(hashesOffset);
    hdr.dataOffset = safe_static_cast<uint32_t>(dataOffset);

    if (totalFileSize > 1024 * 1024 * 100) {
        core::HumanSize::Str sizeStr;
        X_LOG0("AssetPak", "Writing ^6%s^7 pak...", core::HumanSize::toString(sizeStr, totalFileSize));
    }

    if (file.writeObj(hdr) != sizeof(hdr)) {
        X_ERROR("AssetPak", "Failed to write header");
        return false;
    }

    X_ASSERT_ALIGNMENT(file.tell(), PAK_BLOCK_PADDING, 0);

    if (file.write(strings.data(), strings.size()) != strings.size()) {
        X_ERROR("AssetPak", "Failed to write string data");
        return false;
    }

    X_ASSERT_ALIGNMENT(file.tell(), PAK_BLOCK_PADDING, 0);

    if (file.write(entries.data(), entries.size()) != entries.size()) {
        X_ERROR("AssetPak", "Failed to write entry data");
        return false;
    }

    if (hdr.flags.IsSet(APakFlag::SHARED_DICTS)) {
        X_ASSERT_ALIGNMENT(file.tell(), PAK_BLOCK_PADDING, 0);

        if (file.write(sharedDictsData.data(), sharedDictsData.size()) != sharedDictsData.size()) {
            X_ERROR("AssetPak", "Failed to write data");
            return false;
        }
    }

    X_ASSERT_ALIGNMENT(file.tell(), PAK_BLOCK_PADDING, 0);

    if (file.write(hashes.data(), hashes.size()) != hashes.size()) {
        X_ERROR("AssetPak", "Failed to write data");
        return false;
    }

    X_ASSERT_ALIGNMENT(file.tell(), PAK_BLOCK_PADDING, 0);

    if (file.write(data.data(), data.size()) != data.size()) {
        X_ERROR("AssetPak", "Failed to write data");
        return false;
    }

    if (file.tell() != totalFileSize) {
        X_ERROR("AssetPak", "File size mismatch actual %" PRIu64" Calculated %" PRIu64, file.tell(), totalFileSize);
        return false;
    }

    // some stats.
    X_LOG0("AssetPak", "Stats:");
    X_LOG_BULLET;
    if (flags_.IsSet(PakBuilderFlag::COMPRESSION))
    {
        core::HumanSize::Str sizeStr0, sizeStr1;
        X_LOG0("AssetPak", "RawAssetSize:        ^6%s", core::HumanSize::toString(sizeStr0, infaltedAssetSize_));
        X_LOG0("AssetPak", "CompressedAssetSize: ^6%s", core::HumanSize::toString(sizeStr1, deflatedAssetSize_));
    }

    X_LOG0("AssetPak", "%-16s %-8s", "Type", "Num");

    for (uint32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
        auto type = static_cast<AssetType::Enum>(i);

        if (!assetCounts_[i]) {
            continue;
        }

        X_LOG0("AssetPak", "^5%-16s ^6%-8" PRIi32, AssetType::ToString(type), assetCounts_[i]);
    }

    return true;
}

void AssetPakBuilder::addAsset(AssetId id, AssetType::Enum type, const core::string& name, DataVec&& data)
{
    X_ASSERT(name.isNotEmpty() && data.isNotEmpty(), "Empty name or data")(name.length(), data.size());

    assets_.emplace_back(id, type, name, std::move(data), arena_);
    assets_.back().name.replace(assetDb::ASSET_NAME_INVALID_SLASH, assetDb::ASSET_NAME_SLASH);
    assetLookup_.insert({ id, true });

    ++assetCounts_[type];
}

bool AssetPakBuilder::hasAsset(AssetId id) const
{
    return assetLookup_.find(id) != assetLookup_.end();
}


bool AssetPakBuilder::dumpMetaOS(core::Path<>& osPath)
{
    core::XFileScoped file;
    core::FileFlags mode;
    mode.Set(core::FileFlag::SHARE);
    mode.Set(core::FileFlag::READ);
    mode.Set(core::FileFlag::RANDOM_ACCESS);

    if (!file.openFileOS(osPath, mode)) {
        X_ERROR("AssetPak", "Failed to open file for saving");
        return false;
    }

    APakHeader hdr;
    if (file.readObj(hdr) != sizeof(hdr)) {
        X_ERROR("AssetPak", "Failed to open file for saving");
        return false;
    }

    if (!hdr.isValid()) {
        X_ERROR("AssetPak", "Invalid header");
        return false;
    }

    if (hdr.version != PAK_VERSION) {
        X_ERROR("AssetPak", "Version incorrect. got %" PRIu8 " require %" PRIu8, hdr.version, PAK_VERSION);
        return false;
    }

    std::array<int32_t, AssetType::ENUM_COUNT> assetCounts, compressedCounts;
    std::array<uint64_t, AssetType::ENUM_COUNT> assetSize;
    assetCounts.fill(0);
    compressedCounts.fill(0);
    assetSize.fill(0);

    core::Array<APakEntry> entries(arena_);
    entries.resize(hdr.numAssets);

    file.seek(hdr.entryTableOffset, core::SeekMode::SET);
    if (file.readObjs(entries.data(), entries.size()) != entries.size()) {
        X_ERROR("AssetPak", "Failed to read entry table");
        return false;
    }

    uint64_t totalInflatedSize = 0;

    for (const auto& ae : entries) {
        ++assetCounts[ae.type];

        if (ae.inflatedSize != ae.size) {
            ++compressedCounts[ae.type];
        }

        assetSize[ae.type] += ae.size;
        totalInflatedSize += ae.inflatedSize;
    }

    const auto numCompressed = core::accumulate(compressedCounts.begin(), compressedCounts.end(), 0_sz);

    core::HumanSize::Str sizeStr, sizeStr2;
    APakFlags::Description flagStr;
    X_LOG0("AssetPak", "^9PakMeta");
    X_LOG_BULLET;
    X_LOG0("AssetPak", "Pak: \"%s\" version: ^6%" PRIu8, osPath.fileName(), hdr.version);
    X_LOG0("AssetPak", "flags: \"%s\"", hdr.flags.ToString(flagStr));
    X_LOG0("AssetPak", "Size: ^6%s (%" PRIu64 ") ^7RawAssetSize: ^6%s (%" PRIu64 ") ^6%%%.2f",
        core::HumanSize::toString(sizeStr, hdr.size), hdr.size,
        core::HumanSize::toString(sizeStr2, totalInflatedSize), totalInflatedSize, 
        core::PercentageOf(hdr.size,totalInflatedSize));
    X_LOG0("AssetPak", "StringDataOffset: ^6%" PRIu32, hdr.stringDataOffset);
    X_LOG0("AssetPak", "EntryTableOffset: ^6%" PRIu32, hdr.entryTableOffset);
    X_LOG0("AssetPak", "DictOffset: ^6%" PRIu32, hdr.dictOffset);
    X_LOG0("AssetPak", "DataOffset: ^6%" PRIu32, hdr.dataOffset);
    X_LOG0("AssetPak", "NumAssets: ^6%" PRIu32 " ^7compressed: ^6%" PRIuS, hdr.numAssets, numCompressed);

    {
        X_LOG0("AssetPak", "^8AssetInfo");

        X_LOG_BULLET;
        X_LOG0("AssetPak", "%-16s %-8s %-10s %-16s %-8s", "Type", "Num", "Compressed", "Size", "Pak%");

        for (uint32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
            auto type = static_cast<AssetType::Enum>(i);

            if (assetCounts[i] == 0 && compressedCounts[i] == 0) {
                continue;
            }

            float sizePercent = core::PercentageOf(assetSize[type], hdr.size);

            X_LOG0("AssetPak", "^5%-16s ^6%-8" PRIi32 " %-10" PRIi32 " %-16s %-8.2f",
                AssetType::ToString(type), assetCounts[i], compressedCounts[i],
                core::HumanSize::toString(sizeStr, assetSize[type]), sizePercent);
        }
    }

    {
        X_LOG0("AssetPak", "^8CompressionInfo");
        X_LOG_BULLET;
        X_LOG0("AssetPak", "%-16s %-8s", "Type", "Algo");

        for (uint32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
            if (assetCounts[i] == 0) {
                continue;
            }

            X_LOG0("AssetPak", "^5%-16s ^6%s", AssetType::ToString(i), core::Compression::Algo::ToString(hdr.algos[i]));
        }
    }

    if (hdr.flags.IsSet(APakFlag::SHARED_DICTS)) {
        APakDictInfo dictInfo;

        file.seek(hdr.dictOffset, core::SeekMode::SET);
        file.readObj(dictInfo);

        X_LOG0("AssetPak", "^8Dictinfo");
        X_LOG_BULLET;
        X_LOG0("AssetPak", "%-16s %-8s %-10s", "Type", "Offset", "Size");

        for (uint32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
            if (assetCounts[i] == 0) {
                continue;
            }

            X_LOG0("AssetPak", "^5%-16s ^6%-8" PRIu32 " %-10" PRIu32,
                AssetType::ToString(i), dictInfo.sharedHdrs[i].offset, dictInfo.sharedHdrs[i].size);
        }
    }

    const size_t stringDataSize = hdr.entryTableOffset - hdr.stringDataOffset;

    core::Array<char> stringData(arena_);
    core::Array<core::string_view> strings(arena_);
    stringData.resize(stringDataSize);
    strings.reserve(hdr.numAssets);

    file.seek(hdr.stringDataOffset, core::SeekMode::SET);
    if (file.read(stringData.data(), stringData.size()) != stringData.size()) {
        X_ERROR("AssetPak", "Failed to read string data");
        return false;
    }

    size_t longestString = 0;

    {
        core::MemCursor stringsCursor(stringData.data(), stringData.size());

        for (uint32_t i = 0; i < hdr.numAssets; i++)
        {
            auto length = stringsCursor.getSeek<AssetPak::NameLengthType>();
            const char* pStringBegin = stringsCursor.getPtr<const char>();
            stringsCursor.seekBytes(length);

            strings.emplace_back(pStringBegin, length);
            longestString = core::Max<size_t>(longestString, length);
        }
    }

    // add some space.
    longestString += 2;

    X_LOG0("AssetPak", "^8Assets");
    X_LOG_BULLET;
    X_LOG0("AssetPak", "%-4s %-*s %-10s %-10s %-12s %s", "Idx", longestString, "Name", "Offset", "Size", "NameHash", "Type");

    for (size_t i = 0; i < strings.size(); i++) {
        const auto& name = strings[i];
        X_LOG0("AssetPak", "%-4" PRIuS " ^5%-*.*s ^6%-10" PRIu64 " %-10" PRIu32 " 0x%08" PRIx32 "   ^8%s", 
            i, longestString, name.length(), name.data(), (uint64_t)(entries[i].offset + hdr.dataOffset), entries[i].size, core::StrHash(name.data(), name.length()).hash(), AssetType::ToString(entries[i].type));
    }

    return true;
}

X_NAMESPACE_END
