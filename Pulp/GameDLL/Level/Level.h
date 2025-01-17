#pragma once

#include <Util\UniquePointer.h>
#include <String\GrowingStringTable.h>
#include <Threading\Signal.h>

#include <Ilevel.h>

#include "Enity\EntitySystem.h"

X_NAMESPACE_DECLARE(core,
                    namespace V2 {
                        struct Job;
                        class JobSystem;
                    }

                    struct XFileAsync;

                    struct FrameData;)

X_NAMESPACE_DECLARE(engine,
                    struct IWorld3D;)

X_NAMESPACE_DECLARE(net,
                    class UserCmdMan;)

X_NAMESPACE_BEGIN(game)

struct UserNetMappings;
struct NetInterpolationState;
class Multiplayer;
class GameVars;

namespace entity
{
    class EntitySystem;

} // namespace entity

class Level
{
public:
    typedef core::StackString<assetDb::ASSET_NAME_MAX_LENGTH> MapNameStr;

public:
    Level(physics::IScene* pScene, engine::IWorld3D* p3DWorld, entity::EntitySystem& entSys, core::MemoryArenaBase* arena);
    ~Level();

    void update(core::FrameData& frame);

    void beginLoad(const MapNameStr& name);
    void clear(void);

    X_INLINE bool isLoaded(void) const;

private:
    void IoRequestCallback(core::IFileSys& fileSys, const core::IoRequestBase* pRequest,
        core::XFileAsync* pFile, uint32_t bytesTransferred);
    void IoRequestCallbackEntDesc(core::IFileSys& fileSys, const core::IoRequestBase* pRequest,
        core::XFileAsync* pFile, uint32_t bytesTransferred);

    void processHeader_job(core::V2::JobSystem& jobSys, size_t threadIdx, core::V2::Job* pJob, void* pData);
    void processData_job(core::V2::JobSystem& jobSys, size_t threadIdx, core::V2::Job* pJob, void* pData);
    bool processHeader(void);
    bool processData(void);
    bool processEnts(void);

private:
    core::MemoryArenaBase* arena_;
    core::V2::JobSystem* pJobSys_;
    core::IFileSys* pFileSys_;
    entity::EntitySystem& entSys_;

    bool loaded_;
    bool _pad[3];

    core::Path<char> path_;
    level::FileHeader fileHdr_;
    core::UniquePointer<uint8_t[]> levelData_;

    size_t entDescSize_;
    core::UniquePointer<char[]> entDescData_;
    core::Signal entDescLoadedSignal_;

    engine::IWorld3D* p3DWorld_;
    physics::IScene* pScene_;

    level::StringTable stringTable_;
};

X_INLINE bool Level::isLoaded(void) const
{
    return loaded_;
}

class World
{
public:
    typedef Level::MapNameStr MapNameStr;

public:
    World(GameVars& vars, net::SessionInfo& sessionInfo, physics::IPhysics* pPhys,
        weapon::WeaponDefManager& weaponDefs, Multiplayer* pMultiplayer, core::MemoryArenaBase* arena);
    ~World();

    bool loadMap(const MapNameStr& name);
    bool hasLoaded(void) const;

    void runUserCmdForPlayer(core::TimeVal dt, const net::UserCmd& cmd, int32_t playerIdx);
    void update(core::FrameData& frame, net::UserCmdMan& userCmdMan, const NetInterpolationState& netInterpolState, entity::EntityId localPlayerId);

    void createSnapShot(net::SnapShot& snap);
    void applySnapShot(const UserNetMappings& unm, const net::SnapShot& snap);

    void spawnPlayer(const UserNetMappings& unm, int32_t playerIdx, bool local);
    void removePlayer(int32_t playerIdx);

private:
    bool createPhysicsScene(physics::IPhysics* pPhys);

private:
    core::MemoryArenaBase* arena_;
    physics::IPhysics* pPhys_;
    physics::IScene* pScene_;
    engine::IWorld3D* p3DWorld_;

    entity::EntitySystem ents_;
    core::UniquePointer<Level> level_;
};

X_NAMESPACE_END