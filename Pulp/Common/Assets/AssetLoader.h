#pragma once

#include <IAssetDb.h>

#include <Util\UniquePointer.h>
#include <Containers\Array.h>
#include <Containers\Fifo.h>

X_NAMESPACE_BEGIN(core)

namespace V2 {
	struct Job;
	class JobSystem;
}
struct XFileAsync;
struct IoRequestBase;


struct IAssetLoadSink;
class AssetBase;

class AssetLoader
{
	typedef std::array<IAssetLoadSink*, assetDb::AssetType::ENUM_COUNT> AssetLoadSinksArr;
	typedef std::array<const char*, assetDb::AssetType::ENUM_COUNT> AssetExtArr;

	struct AssetLoadRequest
	{
		X_INLINE AssetLoadRequest(AssetBase* pAsset) :
			pFile(nullptr),
			pAsset(pAsset),
			dataSize(0)
		{
		}

		core::XFileAsync* pFile;
		AssetBase* pAsset;
		core::UniquePointer<char[]> data;
		uint32_t dataSize;
	};

	typedef core::Array<AssetLoadRequest*> AssetLoadRequestArr;
	typedef core::Fifo<AssetBase*> AssetQueue;

public:
	AssetLoader(core::MemoryArenaBase* arena, core::MemoryArenaBase* blockArena);

	void registerAssetType(assetDb::AssetType::Enum type, IAssetLoadSink* pSink, const char* pExt);

	void addLoadRequest(AssetBase* pAsset);
	bool waitForLoad(AssetBase* pAsset);

private:
	void dispatchPendingLoads(void);

	void queueLoadRequest(AssetBase* pAsset, core::CriticalSection::ScopedLock&);
	void dispatchLoad(AssetBase* pAsset, core::CriticalSection::ScopedLock&);
	bool dispatchPendingLoad(core::CriticalSection::ScopedLock&);
	void dispatchLoadRequest(AssetLoadRequest* pLoadReq);

	// load / processing
	void onLoadRequestFail(AssetLoadRequest* pLoadReq);
	void loadRequestCleanup(AssetLoadRequest* pLoadReq);

private:
	void IoRequestCallback(core::IFileSys&, const core::IoRequestBase*, core::XFileAsync*, uint32_t);

	void processData_job(core::V2::JobSystem& jobSys, size_t threadIdx, core::V2::Job* pJob, void* pData);


private:
	core::MemoryArenaBase*		arena_;
	core::MemoryArenaBase*		blockArena_;

	core::CriticalSection		loadReqLock_;
	core::ConditionVariable		loadCond_;

	AssetQueue					requestQueue_; // requests not yet currenty dispatched
	AssetLoadRequestArr			pendingRequests_; // active requests.

	AssetLoadSinksArr			assetsinks_;
	AssetExtArr					assetExt_;
};


X_NAMESPACE_END