#include "stdafx.h"
#include "Converter.h"

#include <IModel.h>
#include <IAnimation.h>
#include <IConverterModule.h>
#include <IFileSys.h>
#include <IPhysics.h>

#include <Extension\IEngineFactory.h>
#include <Extension\EngineCreateClass.h>

#include <Containers\Array.h>

#include <String\HumanDuration.h>
#include <String\Json.h>
#include <Time\StopWatch.h>

#include <Hashing\sha1.h>

// need assetDB.
X_LINK_ENGINE_LIB("AssetDb")

X_NAMESPACE_BEGIN(converter)

Converter::Converter(assetDb::AssetDB& db, core::MemoryArenaBase* scratchArea) :
    scratchArea_(scratchArea),
    db_(db),
    pPhysLib_(nullptr),
    pPhysConverterMod_(nullptr),
    forceConvert_(false)
{
    core::zero_object(converters_);
}

Converter::~Converter()
{
    UnloadConverters();
}

void Converter::PrintBanner(void)
{
    X_LOG0("Converter", "=================== V0.1 ===================");
}

bool Converter::Init(void)
{
    if (!db_.OpenDB()) {
        X_ERROR("Converter", "Failed to open AssetDb");
        return false;
    }

    // open cache db.
    core::Path<char> dbPath;
    dbPath.append(assetDb::AssetDB::ASSET_DB_FOLDER);
    dbPath.ensureSlash();
    dbPath.append(assetDb::AssetDB::CACHE_DB_NAME);

    if (!cacheDb_.connect(dbPath.c_str(), sql::OpenFlag::CREATE | sql::OpenFlag::WRITE)) {
        X_ERROR("Converter", "Failed to open cache db");
        return false;
    }

    if (!cacheDb_.execute("PRAGMA synchronous = OFF; PRAGMA page_size = 4096; PRAGMA journal_mode=delete; PRAGMA foreign_keys = ON;")) {
        return false;
    }

    if (!CreateTables()) {
        return false;
    }

    return true;
}

bool Converter::SetMod(const core::string& modName)
{
    if (!db_.SetMod(modName)) {
        X_ERROR("Converter", "Failed to set mod");
        return false;
    }

    return true;
}

void Converter::forceConvert(bool force)
{
    forceConvert_ = force;
}

bool Converter::setConversionProfiles(const core::string& profileName)
{
    X_LOG0("Converter", "Applying conversion profile: \"%s\"", profileName.c_str());

    if (!loadConversionProfiles(profileName)) {
        X_ERROR("Converter", "Failed to apply conversion profile");
        return false;
    }

    return true;
}

bool Converter::Convert(AssetType::Enum assType, const core::string& name)
{
    X_LOG0("Converter", "Converting \"%s\" type: \"%s\"", name.c_str(), AssetType::ToString(assType));

    // early out if we don't have the con lib for this ass type.
    if (!EnsureLibLoaded(assType)) {
        X_ERROR("Converter", "Failed to convert, converter module missing for asset type.");
        return false;
    }

    assetDb::AssetId assetId = assetDb::INVALID_ASSET_ID;
    assetDb::AssetDB::ModId modId;
    if (!db_.AssetExists(assType, name, &assetId, &modId)) {
        X_ERROR("Converter", "Asset does not exists");
        return false;
    }

    core::Path<char> pathOut;
    if (!db_.GetOutputPathForAsset(modId, assType, name, pathOut)) {
        X_ERROR("Converter", "Failed to asset path");
        return false;
    }

    DataHash dataHash, argsHash;
    if (!db_.GetHashesForAsset(assetId, dataHash, argsHash)) {
        X_ERROR("Converter", "Failed to get hashes");
        return false;
    }

    // file exist already?
    if (!forceConvert_ && gEnv->pFileSys->fileExists(pathOut, core::VirtualDirectory::BASE)) {
        // see if stale.
        if (!IsAssetStale(assetId, assType, dataHash, argsHash)) {
            X_LOG1("Converter", "Skipping conversion, asset is not stale");
            return true;
        }
    }

    core::StopWatch timer;

    core::string argsStr;
    if (!db_.GetArgsForAsset(assetId, argsStr)) {
        X_ERROR("Converter", "Failed to get conversion args");
        return false;
    }

    // make sure out dir is valid.
    {
        core::Path<char> dir(pathOut);
        dir.removeFileName();
        if (!gEnv->pFileSys->directoryExists(dir, core::VirtualDirectory::BASE)) {
            if (!gEnv->pFileSys->createDirectoryTree(dir, core::VirtualDirectory::BASE)) {
                X_ERROR("Converter", "Failed to create output directory for asset");
                return false;
            }
        }
    }

    timer.Start();

    bool res = Convert_int(assType, assetId, argsStr, pathOut);
    if (res) {
        X_LOG1("Converter", "processing took: ^6%g ms", timer.GetMilliSeconds());
        OnAssetCompiled(assetId, assType, dataHash, argsHash);
    }
    return res;
}

bool Converter::Convert(assetDb::ModId modId)
{
    X_LOG0("Converter", "Converting all assets...");

    int32_t numAssets = 0;
    if (!db_.GetNumAssets(modId, numAssets)) {
        X_ERROR("Converter", "Failed to get asset count");
        return false;
    }

    X_LOG0("Converter", "%" PRIi32 " asset(s)", numAssets);

    assetDb::AssetDB::AssetDelegate func;
    func.Bind<Converter, &Converter::Convert>(this);

    core::StopWatch timer;

    if (!db_.IterateAssets(modId, func)) {
        X_ERROR("Converter", "Failed to convert all assets");
        return false;
    }

    core::HumanDuration::Str timeStr;
    X_LOG0("Converter", "Converted %" PRIi32 " asset(s) in ^6%s", numAssets,
        core::HumanDuration::toString(timeStr, timer.GetMilliSeconds()));
    return true;
}

bool Converter::Convert(assetDb::ModId modId, AssetType::Enum assType)
{
    X_LOG0("Converter", "Converting all \"%s\" assets ...", AssetType::ToString(assType));

    // early out.
    if (!EnsureLibLoaded(assType)) {
        X_ERROR("Converter", "Failed to convert, converter missing for asset type.");
        return false;
    }

    int32_t numAssets = 0;
    if (!db_.GetAssetTypeCount(modId, assType, numAssets)) {
        X_ERROR("Converter", "Failed to get asset count");
        return false;
    }

    X_LOG0("Converter", "%" PRIi32 " asset(s)", numAssets);

    assetDb::AssetDB::AssetDelegate func;
    func.Bind<Converter, &Converter::Convert>(this);

    core::StopWatch timer;

    if (!db_.IterateAssets(modId, assType, func)) {
        X_ERROR("Converter", "Failed to convert \"%s\" assets", AssetType::ToString(assType));
        return false;
    }

    core::HumanDuration::Str timeStr;
    X_LOG0("Converter", "Converted %" PRIi32 " asset(s) in ^6%s", numAssets,
        core::HumanDuration::toString(timeStr, timer.GetMilliSeconds()));
    return true;
}

bool Converter::Convert(AssetType::Enum assType)
{
    X_LOG0("Converter", "Converting all \"%s\" assets ...", AssetType::ToString(assType));

    if (!EnsureLibLoaded(assType)) {
        X_ERROR("Converter", "Failed to convert, converter missing for asset type.");
        return false;
    }

    int32_t numAssets = 0;
    if (!db_.GetNumAssets(assType, numAssets)) {
        X_ERROR("Converter", "Failed to get asset count");
        return false;
    }

    X_LOG0("Converter", "%" PRIi32 " asset(s)", numAssets);

    assetDb::AssetDB::AssetDelegate func;
    func.Bind<Converter, &Converter::Convert>(this);

    core::StopWatch timer;

    if (!db_.IterateAssets(assType, func)) {
        X_ERROR("Converter", "Failed to convert \"%s\" assets", AssetType::ToString(assType));
        return false;
    }
    
    core::HumanDuration::Str timeStr;
    X_LOG0("Converter", "Converted %" PRIi32 " asset(s) in ^6%s", numAssets,
        core::HumanDuration::toString(timeStr, timer.GetMilliSeconds()));
    return true;
}

bool Converter::ConvertAll(void)
{
    X_LOG0("Converter", "Converting all assets...");

    int32_t numAssets = 0;
    if (!db_.GetNumAssets(numAssets)) {
        X_ERROR("Converter", "Failed to get asset count");
        return false;
    }

    X_LOG0("Converter", "%" PRIi32 " asset(s)", numAssets);

    assetDb::AssetDB::AssetDelegate func;
    func.Bind<Converter, &Converter::Convert>(this);

    core::StopWatch timer;

    if (!db_.IterateAssets(func)) {
        X_ERROR("Converter", "Failed to convert all assets");
        return false;
    }

    core::HumanDuration::Str timeStr;
    X_LOG0("Converter", "Converted %" PRIi32 " asset(s) in ^6%s", numAssets,
        core::HumanDuration::toString(timeStr, timer.GetMilliSeconds()));
    return true;
}

bool Converter::CleanAll(const char* pMod)
{
    if (pMod) {
        X_LOG0("Converter", "Cleaning all compiled assets for mod: \"%s\"", pMod);
    }
    else {
        X_LOG0("Converter", "Cleaning all compiled assets");
    }

    // this is for cleaning all asset that don't have a entry.
    // optionally limit it to a mod.
    if (pMod) {
        assetDb::AssetDB::ModId modId;
        if (!db_.ModExists(core::string(pMod), &modId)) {
            X_ERROR("Converter", "Can't clean mod \"%s\" does not exist", pMod);
            return false;
        }

        assetDb::AssetDB::Mod mod;

        if (!db_.GetModInfo(modId, mod)) {
            X_ERROR("Converter", "Failed to get mod info");
            return false;
        }

        return CleanMod(modId, mod.name, mod.outDir);
    }

    assetDb::AssetDB::ModDelegate func;
    func.Bind<Converter, &Converter::CleanMod>(this);

    return db_.IterateMods(func);
}

bool Converter::CleanAll(assetDb::ModId modId)
{
    assetDb::AssetDB::Mod mod;

    if (!db_.GetModInfo(modId, mod)) {
        X_ERROR("Converter", "Failed to get mod info");
        return false;
    }

    return CleanMod(modId, mod.name, mod.outDir);
}

bool Converter::CleanMod(assetDb::AssetDB::ModId modId, const core::string& name, const core::Path<char>& outDir)
{
    // mark all the assets for this mod stale.
    if (!MarkAssetsStale(modId)) {
        X_ERROR("Converter", "Failed to mark mod \"%s\" assets as state", name.c_str());
        return false;
    }

    X_LOG0("Converter", "Cleaning all compiled assets for mod: \"%s\"", name.c_str());

    core::IFileSys* pFileSys = gEnv->pFileSys;

    // nuke the output directory. BOOM!
    // if they put files in here that are custom. RIP.
    if (pFileSys->directoryExists(outDir, core::VirtualDirectory::BASE)) {
        // lets be nice and only clear dir's we actually populate.
        for (int32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
            AssetType::Enum assType = static_cast<AssetType::Enum>(i);

            core::Path<char> assetPath;
            assetDb::AssetDB::GetOutputPathForAssetType(assType, outDir, assetPath);

            if (pFileSys->directoryExists(assetPath, core::VirtualDirectory::BASE)) {
                if (!pFileSys->deleteDirectoryContents(assetPath, core::VirtualDirectory::BASE)) {
                    X_ERROR("Converter", "Failed to clear mod \"%s\" \"%s\" assets directory", assetPath.c_str(), AssetType::ToString(assType));
                    return false;
                }
            }
        }

        X_LOG0("Converter", "Cleaning complete");
    }
    else {
        X_WARNING("Converter", "mod dir not fnd cleaning skipped: \"\"", outDir.c_str());
    }

    return true;
}

bool Converter::CleanThumbs(void)
{
    X_LOG0("Converter", "Cleaning thumbs");

    return db_.CleanThumbs();
}

bool Converter::GenerateThumbs(void)
{
    X_LOG0("Converter", "Generating thumbs");

    for (int32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
        AssetType::Enum assType = static_cast<AssetType::Enum>(i);

        if (!EnsureLibLoaded(assType)) {
            X_LOG0("Converter", "Skipping \"%s\" thumb generation, no converter module found", AssetType::ToString(assType));
            continue;
        }

        IConverter* pCon = GetConverter(assType);
        X_ASSERT_NOT_NULL(pCon);

        if (!pCon->thumbGenerationSupported()) {
            X_LOG0("Converter", "Skipping \"%s\" thumb generation, not supported", AssetType::ToString(assType));
            continue;
        }

        int32_t numAssets = 0;
        if (!db_.GetNumAssets(assType, numAssets)) {
            X_ERROR("Converter", "Failed to get asset count");
            return false;
        }

        X_LOG0("Converter", "Processing \"%s\" Generating ^6%" PRIi32 "^7 thumb(s)", AssetType::ToString(assType), numAssets);

        assetDb::AssetDB::AssetDelegate func;
        func.Bind<Converter, &Converter::GenerateThumb>(this);

        core::StopWatch timer;

        if (!db_.IterateAssets(assType, func)) {
            X_ERROR("Converter", "Failed to generate thumbs for assetType \"%s\"", AssetType::ToString(assType));
            return false;
        }

        core::HumanDuration::Str timeStr;
        X_LOG0("Converter", "Generated thumbs for %" PRIi32 " asset(s) in ^6%s", numAssets,
            core::HumanDuration::toString(timeStr, timer.GetMilliSeconds()));
    }

    return true;
}

bool Converter::Chkdsk(void)
{
    X_LOG0("Converter", "Chkdsk");

    return db_.Chkdsk(false);
}

bool Converter::CleanupOldRawFiles(void)
{
    X_LOG0("Converter", "CleanupOldRawFiles");

    return db_.CleanupOldRawFiles();
}

bool Converter::Repack(void)
{
    X_LOG0("Converter", "Repack");

    for (int32_t i = 0; i < AssetType::ENUM_COUNT; i++) {
        AssetType::Enum assType = static_cast<AssetType::Enum>(i);

        if (!EnsureLibLoaded(assType)) {
            X_LOG0("Converter", "Skipping \"%s\" repack, no converter module found", AssetType::ToString(assType));
            continue;
        }

        IConverter* pCon = GetConverter(assType);
        X_ASSERT_NOT_NULL(pCon);

        if (!pCon->repackSupported()) {
            X_LOG0("Converter", "Skipping \"%s\" repack, not supported", AssetType::ToString(assType));
            continue;
        }

        int32_t numAssets = 0;
        if (!db_.GetNumAssets(assType, numAssets)) {
            X_ERROR("Converter", "Failed to get asset count");
            return false;
        }

        X_LOG0("Converter", "Processing \"%s\" Repacking ^6%" PRIi32 "^7 assets(s)", AssetType::ToString(assType), numAssets);

        assetDb::AssetDB::AssetDelegate func;
        func.Bind<Converter, &Converter::RepackAsset>(this);

        core::StopWatch timer;

        if (!db_.IterateAssets(assType, func)) {
            X_ERROR("Converter", "Failed to repack assets for assetType \"%s\"", AssetType::ToString(assType));
            return false;
        }

        core::HumanDuration::Str timeStr;
        X_LOG0("Converter", "Repacked %" PRIi32 " asset(s) in ^6%s", numAssets,
            core::HumanDuration::toString(timeStr, timer.GetMilliSeconds()));
    }

    return true;
}


bool Converter::GenerateThumb(AssetType::Enum assType, const core::string& name)
{
    assetDb::AssetId assetId = assetDb::INVALID_ASSET_ID;
    if (!db_.AssetExists(assType, name, &assetId)) {
        X_ERROR("Converter", "Asset does not exists");
        return false;
    }

    if (!forceConvert_ && db_.AssetHasThumb(assetId)) {
        return true;
    }

    IConverter* pCon = GetConverter(assType);
    // this is a private member which should have this stuff validated
    // before calling this.
    X_ASSERT_NOT_NULL(pCon);
    X_ASSERT(pCon->thumbGenerationSupported(), "thumb generatino not supported")(); 

    core::StopWatch timer;

    if (!pCon->CreateThumb(*this, assetId, Vec2i(64, 64))) {
        X_ERROR("Converter", "Failed to generate thumb for \"%s\" \"%s\"", name.c_str(), AssetType::ToString(assType));
        return false;
    }

    core::HumanDuration::Str timeStr;
    X_LOG0("Converter", "generated thumb for \"%s\" in ^6%s", name.c_str(),
        core::HumanDuration::toString(timeStr, timer.GetMilliSeconds()));
    return true;
}

bool Converter::RepackAsset(AssetType::Enum assType, const core::string& name)
{
    assetDb::AssetId assetId = assetDb::INVALID_ASSET_ID;
    if (!db_.AssetExists(assType, name, &assetId)) {
        X_ERROR("Converter", "Asset does not exists");
        return false;
    }

    IConverter* pCon = GetConverter(assType);
    X_ASSERT_NOT_NULL(pCon);
    X_ASSERT(pCon->repackSupported(), "repack not supported")();

    core::StopWatch timer;

    if (!pCon->Repack(*this, assetId)) {
        X_ERROR("Converter", "Failed to repack \"%s\" \"%s\"", name.c_str(), AssetType::ToString(assType));
        return false;
    }

    core::HumanDuration::Str timeStr;
    X_LOG0("Converter", "Repacked \"%s\" in ^6%s", name.c_str(),
        core::HumanDuration::toString(timeStr, timer.GetMilliSeconds()));
    return true;
}

bool Converter::Convert_int(AssetType::Enum assType, assetDb::AssetId assetId, ConvertArgs& args, const OutPath& pathOut)
{
    IConverter* pCon = GetConverter(assType);

    if (pCon) {
        return pCon->Convert(*this, assetId, args, pathOut);
    }

    return false;
}

bool Converter::GetAssetArgs(assetDb::AssetId assetId, ConvertArgs& args)
{
    if (!db_.GetArgsForAsset(assetId, args)) {
        X_ERROR("Converter", "Failed to get args for asset: %" PRIi32, assetId);
        return false;
    }

    return true;
}

bool Converter::GetAssetData(assetDb::AssetId assetId, DataArr& dataOut)
{
    if (!db_.GetRawFileDataForAsset(assetId, dataOut)) {
        X_ERROR("Converter", "Failed to get raw data for asset: %" PRIi32, assetId);
        return false;
    }

    return true;
}

bool Converter::GetAssetData(const char* pAssetName, AssetType::Enum assType, DataArr& dataOut)
{
    assetDb::AssetId assetId = assetDb::INVALID_ASSET_ID;
    if (!db_.AssetExists(assType, core::string(pAssetName), &assetId)) {
        X_ERROR("Converter", "Asset does not exists: \"%s\"", pAssetName);
        return false;
    }

    if (!db_.GetRawFileDataForAsset(assetId, dataOut)) {
        X_ERROR("Converter", "Failed to get raw data for: \"%s\"", pAssetName);
        return false;
    }

    return true;
}

bool Converter::GetAssetDataCompAlgo(assetDb::AssetId assetId, core::Compression::Algo::Enum& algoOut)
{
    if (!db_.GetRawFileCompAlgoForAsset(assetId, algoOut)) {
        X_ERROR("Converter", "Failed to get raw data algo for: %" PRIi32, assetId);
        return false;
    }

    return true;
}


bool Converter::AssetExists(const char* pAssetName, assetDb::AssetType::Enum assType, assetDb::AssetId* pIdOut)
{
    if (!db_.AssetExists(assType, core::string(pAssetName), pIdOut)) {
        return false;
    }

    return true;
}

bool Converter::AssetExists(assetDb::AssetId assetId, assetDb::AssetType::Enum& typeOut, core::string& nameOut)
{
    return db_.AssetExists(assetId, typeOut, nameOut);
}

bool Converter::UpdateAssetThumb(assetDb::AssetId assetId, Vec2i thumbDim, Vec2i srcDim, core::span<const uint8_t> data,
    core::Compression::Algo::Enum algo, core::Compression::CompressLevel::Enum lvl)
{
    auto res = db_.UpdateAssetThumb(assetId, thumbDim, srcDim, data, algo, lvl);
    if (res != assetDb::AssetDB::Result::OK) {
        return false;
    }

    return true;
}

bool Converter::UpdateAssetThumb(assetDb::AssetId assetId, Vec2i thumbDim, Vec2i srcDim, core::span<const uint8_t> compressedData)
{
    auto res = db_.UpdateAssetThumb(assetId, thumbDim, srcDim, compressedData);
    if (res != assetDb::AssetDB::Result::OK) {
        return false;
    }

    return true;
}

bool Converter::UpdateAssetRawFile(assetDb::AssetId assetId, const DataArr& data, core::Compression::Algo::Enum algo, core::Compression::CompressLevel::Enum lvl)
{
    auto result = db_.UpdateAssetRawFile(assetId, data, algo, lvl);

    return result == assetDb::AssetDB::Result::OK || result == assetDb::AssetDB::Result::UNCHANGED;
}

bool Converter::SetDependencies(assetDb::AssetId assetId, core::span<AssetDep> dependencies)
{
    // clear even if new list not empty.
    if (!ClearDependencies(assetId)) {
        return false;
    }

    if (dependencies.empty()) {
        return true;
    }

    // make unique
    AssetDepArr depUnique(scratchArea_);
    depUnique.reserve(dependencies.size());
    for (auto& dep : dependencies) {
        depUnique.emplace_back(dep);
    }

    std::sort(depUnique.begin(), depUnique.end());
    auto last = std::unique(depUnique.begin(), depUnique.end());
    depUnique.erase(last, depUnique.end());

    sql::SqlLiteTransaction trans(cacheDb_);

    for (auto& dep : depUnique)
    {
        X_ASSERT(dep.name.isNotEmpty(), "Empty asset name")();

        sql::SqlLiteCmd cmd(cacheDb_, "INSERT INTO dependencies (assetId, type, name) VALUES(?,?,?)");
        cmd.bind(1, assetId);
        cmd.bind(2, dep.type);
        cmd.bind(3, dep.name.data(), dep.name.length());

        if (cmd.execute() != sql::Result::OK) {
            X_ERROR("Converter", "Failed to insert dependencies for asset: %" PRIi32, assetId);
            return false;
        }
    }

    trans.commit();
    return true;
}

bool Converter::GetDependencies(assetDb::AssetId assetId, core::Array<AssetDep>& dependencies)
{
    dependencies.clear();

    sql::SqlLiteQuery qry(cacheDb_, "SELECT type, name FROM dependencies WHERE assetId = ?");
    qry.bind(1, assetId);

    for (auto it = qry.begin(); it != qry.end(); ++it) {
        auto row = *it;

        auto type = static_cast<assetDb::AssetType::Enum>(row.get<int32_t>(0));
        const char* pName = row.get<const char*>(1);
        const size_t nameLength = row.columnBytes(1);

        dependencies.emplace_back(type, core::string(pName, pName + nameLength));
    }

    return true;
}

bool Converter::ClearDependencies(assetDb::AssetId assetId)
{
    sql::SqlLiteCmd cmd(cacheDb_, "DELETE FROM dependencies WHERE assetId = ?");
    cmd.bind(1, assetId);

    if (cmd.execute() != sql::Result::OK) {
        X_ERROR("Converter", "Failed to clear dependencies for asset: %" PRIi32, assetId);
        return false;
    }

    return true;
}

bool Converter::GetAssetRefsFrom(assetDb::AssetId assetId, AssetIdArr& refsOut)
{
    return db_.GetAssetRefsFrom(assetId, refsOut);
}

bool Converter::getConversionProfileData(assetDb::AssetType::Enum type, core::string& strOut)
{
    if (conversionProfiles_[type].profile.isEmpty()) {
        return false;
    }

    strOut = conversionProfiles_[type].profile;
    return true;
}

core::MemoryArenaBase* Converter::getScratchArena(void)
{
    return scratchArea_;
}


bool Converter::CreateTables(void)
{
    if (!cacheDb_.execute("CREATE TABLE IF NOT EXISTS convert_cache ("
        "assetId INTEGER PRIMARY KEY,"
        "hash BLOB NOT NULL,"
        "precedence INTEGER NOT NULL,"
        "profileHash BLOB NOT NULL,"
        "lastUpdateTime TIMESTAMP NOT NULL"
        ");")) {
        X_ERROR("Converter", "Failed to create 'convert_cache' table");
        return false;
    }

    if (!cacheDb_.execute("CREATE TABLE IF NOT EXISTS dependencies ("
        "id INTEGER PRIMARY KEY,"
        "assetId INTEGER,"
        "type INTEGER NOT NULL,"
        "name TEXT COLLATE NOCASE NOT NULL"
        ");")) {
        X_ERROR("Converter", "Failed to create 'dependencies' table");
        return false;
    }

    return true;
}

bool Converter::MarkAssetsStale(assetDb::ModId modId)
{
    sql::SqlLiteTransaction trans(cacheDb_);

    sql::SqlLiteCmd cmd(cacheDb_, "DELETE FROM convert_cache WHERE mod_id = ?");
    cmd.bind(1, modId);

    sql::Result::Enum res = cmd.execute();
    if (res != sql::Result::OK) {
        return false;
    }

    trans.commit();
    return true;
}

bool Converter::IsAssetStale(assetDb::AssetId assetId, AssetType::Enum type, DataHash dataHash, DataHash argsHash)
{
    sql::SqlLiteQuery qry(cacheDb_, "SELECT hash, precedence, profileHash FROM convert_cache WHERE assetId = ?");
    qry.bind(1, assetId);

    const auto it = qry.begin();
    if (it == qry.end()) {
        return true;
    }

    auto row = *it;

    core::Hash::SHA1Digest hash, curHash;
    if (!GetHash(assetId, type, dataHash, argsHash, curHash)) {
        X_ERROR("Converter", "Failed to get hash for stale check");
        return true; // assume stale
    }

    const void* pHash = row.get<void const*>(0);
    const size_t hashBlobSize = row.columnBytes(0);
    const int32_t precedence = row.get<int32_t>(1);
    const ProfileHashVal profileHash = static_cast<ProfileHashVal>(row.get<int64_t>(2));

    if (hashBlobSize != sizeof(hash.bytes)) {
        X_ERROR("Converter", "Cache hash incorrect size: %" PRIuS, hashBlobSize);
        return true;// assume stale
    }

    std::memcpy(hash.bytes, pHash, sizeof(hash.bytes));

    if (hash != curHash) {
        return true;
    }

    // if the compiled precedence is less it's stale.
    if (precedence < conversionProfiles_[type].precedence) {
        return true;
    }

    // if the precedence is same but hash different stale.
    if (precedence == conversionProfiles_[type].precedence) {
        if (profileHash != conversionProfiles_[type].hash) {
            return true;
        }
    }

    return false;
}

bool Converter::OnAssetCompiled(assetDb::AssetId assetId, AssetType::Enum type, DataHash dataHash, DataHash argsHash)
{
    core::Hash::SHA1Digest hash;
    if (!GetHash(assetId, type, dataHash, argsHash, hash)) {
        return false;
    }

    sql::SqlLiteCmd cmd(cacheDb_, "INSERT OR REPLACE INTO convert_cache(assetId, hash, precedence, profileHash, lastUpdateTime) VALUES(?,?,?,?,DateTime('now'))");
    cmd.bind(1, assetId);
    cmd.bind(2, &hash, sizeof(hash));
    cmd.bind(3, conversionProfiles_[type].precedence);
    cmd.bind(4, static_cast<int64_t>(conversionProfiles_[type].hash));

    sql::Result::Enum res = cmd.execute();
    if (res != sql::Result::OK) {
        return false;
    }

    return true;
}

bool Converter::GetHash(assetDb::AssetId assetId, AssetType::Enum type, DataHash dataHash, DataHash argsHash, core::Hash::SHA1Digest& hashOut)
{
    core::Hash::SHA1 hash;
    hash.update(dataHash);
    hash.update(argsHash);

    // HACK: for assuming all anim refs are needed in compile.
    // sort something out more generic if other assets need this.
    // this just adds the hashes of all refs.
    if (type == AssetType::ANIM) {

        AssetIdArr refs(scratchArea_);
        if (!db_.GetAssetRefsFrom(assetId, refs)) {
            X_ERROR("Converter", "Failed to get asset refs for hash");
            return false;
        }

        for (auto refId : refs) {
            DataHash refDataHash, refArgsHash;
            if(!db_.GetHashesForAsset(refId, refDataHash, refArgsHash)) {   
                X_ERROR("Converter", "Failed to get asset ref hash");
                return false;
            }

            hash.update(refDataHash);
            hash.update(refArgsHash);
        }
    }

    hashOut = hash.finalize();
    return true;
}

bool Converter::loadConversionProfiles(const core::string& profileName)
{
    core::string profileData;
    int32_t precedence;

    if (!db_.GetProfileData(profileName, profileData, precedence)) {
        return false;
    }

    clearConversionProfiles();

    // we have a json doc with objects for each asset type.
    core::json::Document d;
    d.ParseInsitu(const_cast<char*>(profileData.c_str()));

    for (auto it = d.MemberBegin(); it != d.MemberEnd(); ++it) {
        const auto& name = it->name;
        const auto& val = it->value;

        if (val.GetType() != core::json::kObjectType) {
            X_ERROR("Converter", "Conversion profile contains invalid data, expected object got: %" PRIu32, val.GetType());
            return false;
        }

        // now try match the name to a type.
        core::StackString<64, char> nameStr(name.GetString(), name.GetString() + name.GetStringLength());
        nameStr.toUpper();

        assetDb::AssetType::Enum type;
        int32_t i;
        for (i = 0; i < assetDb::AssetType::ENUM_COUNT; i++) {
            const char* pTypeStr = assetDb::AssetType::ToString(i);
            if (nameStr.isEqual(pTypeStr)) {
                type = static_cast<assetDb::AssetType::Enum>(i);
                break;
            }
        }

        if (i == assetDb::AssetType::ENUM_COUNT) {
            X_ERROR("Converter", "Conversion profile contains unkown asset type \"%s\"", nameStr.c_str());
            return false;
        }

        X_ASSERT(type >= 0 && static_cast<uint32_t>(type) < assetDb::AssetType::ENUM_COUNT, "Invalid type")(type); 

        // now we want to split this into a separate doc.
        core::json::StringBuffer s;
        core::json::Writer<core::json::StringBuffer> writer(s);

        val.Accept(writer);

        conversionProfiles_[type].profile = core::string(s.GetString(), s.GetSize());
        conversionProfiles_[type].hash = ProfileHash::calc(s.GetString(), s.GetSize());
        conversionProfiles_[type].precedence = precedence;
    }

    return true;
}

void Converter::clearConversionProfiles(void)
{
    for (auto& p : conversionProfiles_) {
        p.clear();
    }
}

IConverter* Converter::GetConverter(AssetType::Enum assType)
{
    if (!EnsureLibLoaded(assType)) {
        X_ERROR("Converter", "Failed to load convert for asset type: \"%s\"", AssetType::ToString(assType));
        return nullptr;
    }

    return converters_[assType];
}

physics::IPhysLib* Converter::GetPhsicsLib(void)
{
    if (pPhysLib_) {
        return pPhysLib_;
    }

    // load it :|
    IConverter* pConverterInstance = nullptr;

    // ideally we should be requesting the physics interface by guid directly.
    // will need to make the core api support that.
    bool result = gEnv->pCore->InitializeLoadedConverterModule(X_ENGINE_OUTPUT_PREFIX "Physics", "Engine_PhysLib",
        &pPhysConverterMod_, &pConverterInstance);

    if (!result) {
        return nullptr;
    }

    pPhysLib_ = static_cast<physics::IPhysLib*>(pConverterInstance);

    return pPhysLib_;
}

bool Converter::EnsureLibLoaded(AssetType::Enum assType)
{
    // this needs to be more generic.
    // might move to a single interface for all converter libs.
    if (converters_[assType]) {
        return true;
    }

    return IntializeConverterModule(assType);
}

bool Converter::IntializeConverterModule(AssetType::Enum assType)
{
    core::StackString<128> dllName(X_ENGINE_OUTPUT_PREFIX);
    core::StackString<128> className("Engine_");

    {
        core::StackString<64> typeName(AssetType::ToString(assType));
        typeName.toLower();
        if (typeName.isNotEmpty()) {
            typeName[0] = ::toupper(typeName[0]);
        }

        dllName.append(typeName.c_str());
        className.append(typeName.c_str());
    }

    dllName.append("Lib");
    className.append("Lib");

    return IntializeConverterModule(assType, dllName.c_str(), className.c_str());
}

bool Converter::IntializeConverterModule(AssetType::Enum assType, const char* pDllName, const char* pModuleClassName)
{
    X_ASSERT(converters_[assType] == nullptr, "converter already init")(pDllName, pModuleClassName); 

    IConverterModule* pConvertModuleOut = nullptr;
    IConverter* pConverterInstance = nullptr;

    bool result = gEnv->pCore->InitializeLoadedConverterModule(pDllName, pModuleClassName, &pConvertModuleOut, &pConverterInstance);
    if (!result) {
        return false;
    }

    // save for cleanup.
    converterModules_[assType] = pConvertModuleOut;
    converters_[assType] = pConverterInstance;

    return true;
}

void Converter::UnloadConverters(void)
{
    for (uint32_t i = 0; i < assetDb::AssetType::ENUM_COUNT; i++) {
        if (converters_[i]) {
            X_ASSERT(converterModules_[i] != nullptr, "Have a converter interface without a corresponding moduleInterface")(); 

            // con modules are ref counted so we can't free ourselves.
            gEnv->pCore->FreeConverterModule(converterModules_[i]);

            converterModules_[i] = nullptr;
            converters_[i] = nullptr;
        }
    }

    if (pPhysConverterMod_) {
        gEnv->pCore->FreeConverterModule(pPhysConverterMod_);
        pPhysConverterMod_ = nullptr;
    }
}

X_NAMESPACE_END
