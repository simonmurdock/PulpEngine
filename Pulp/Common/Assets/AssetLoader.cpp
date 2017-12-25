#include "EngineCommon.h"
#include "AssetLoader.h"
#include "AssetBase.h"

#include <IFileSys.h>
#include <Threading\JobSystem2.h>

X_NAMESPACE_BEGIN(core)


AssetLoader::AssetLoader(core::MemoryArenaBase* arena, core::MemoryArenaBase* blockArena) :
	arena_(arena),
	blockArena_(blockArena),
	requestQueue_(arena),
	pendingRequests_(arena)
{
	requestQueue_.reserve(64);
	pendingRequests_.setGranularity(32);

	assetsinks_.fill(nullptr);
	assetExt_.fill("");
}

void AssetLoader::registerAssetType(assetDb::AssetType::Enum type, IAssetLoadSink* pSink, const char* pExt)
{

	assetsinks_[type] = pSink;
	assetExt_[type] = pExt;
}


void AssetLoader::addLoadRequest(AssetBase* pAsset)
{
	X_ASSERT(assetsinks_[pAsset->getType()], "Asset type doest not have a registered handler")(assetDb::AssetType::ToString(pAsset->getType()));

	core::CriticalSection::ScopedLock lock(loadReqLock_);

//	pAsset->addReference(); // prevent instance sweep
	pAsset->setStatus(core::LoadStatus::Loading);

	// queue it if over 16 active requests.
	const int32_t maxReq = 16;
	if (maxReq == 0 || safe_static_cast<int32_t>(pendingRequests_.size()) < maxReq)
	{
		dispatchLoad(pAsset, lock);
	}
	else
	{
		queueLoadRequest(pAsset, lock);
	}
}

bool AssetLoader::waitForLoad(AssetBase* pAsset)
{
	if (pAsset->getStatus() == core::LoadStatus::Complete) {
		return true;
	}

	{
		// TODO: work out if this lock is really required.
		core::CriticalSection::ScopedLock lock(loadReqLock_);
		while (pAsset->getStatus() == core::LoadStatus::Loading)
		{
			dispatchPendingLoads();

			loadCond_.Wait(loadReqLock_);
		}
	}

	// did we fail? or never sent a dispatch?
	auto status = pAsset->getStatus();
	if (status == core::LoadStatus::Complete)
	{
		return true;
	}
	else if (status == core::LoadStatus::Error)
	{
		return false;
	}
	else if (status == core::LoadStatus::NotLoaded)
	{
		// request never sent?
	}

	X_ASSERT_UNREACHABLE();
	return false;
}

void AssetLoader::dispatchPendingLoads(void)
{
	core::CriticalSection::ScopedLock lock(loadReqLock_);

	while (dispatchPendingLoad(lock)) {
		// ...
	}
}


void AssetLoader::queueLoadRequest(AssetBase* pAsset, core::CriticalSection::ScopedLock&)
{
	X_ASSERT(pAsset->getName().isNotEmpty(), "Asset name is empty")();

	// can we know it's not in this queue just from status?
	// like if it's complete it could be in this status
	auto status = pAsset->getStatus();
	if (status == core::LoadStatus::Complete || status == core::LoadStatus::Loading)
	{
		X_WARNING("AssetLoader", "Redundant load request requested: \"%s\"", pAsset->getName().c_str());
		return;
	}

	X_ASSERT(!requestQueue_.contains(pAsset), "Queue already contains asset")(pAsset);

	requestQueue_.push(pAsset);
}

void AssetLoader::dispatchLoad(AssetBase* pAsset, core::CriticalSection::ScopedLock&)
{
	X_ASSERT(pAsset->getStatus() == core::LoadStatus::Loading || pAsset->getStatus() == core::LoadStatus::NotLoaded, "Incorrect status")();

	auto loadReq = core::makeUnique<AssetLoadRequest>(arena_, pAsset);

	// dispatch IO 
	dispatchLoadRequest(loadReq.get());

	pendingRequests_.emplace_back(loadReq.release());
}

bool AssetLoader::dispatchPendingLoad(core::CriticalSection::ScopedLock& lock)
{
	int32_t maxReq = 16;

	if (requestQueue_.isNotEmpty() && (maxReq == 0 || safe_static_cast<int32_t>(pendingRequests_.size()) < maxReq))
	{
		X_ASSERT(requestQueue_.peek()->getStatus() == core::LoadStatus::Loading, "Incorrect status")();
		dispatchLoad(requestQueue_.peek(), lock);
		requestQueue_.pop();
		return true;
	}

	return false;
}


void AssetLoader::dispatchLoadRequest(AssetLoadRequest* pLoadReq)
{
	auto* pAsset = pLoadReq->pAsset;
	auto type = pAsset->getType();

	core::Path<char> path;
	path /= assetDb::AssetType::ToString(pAsset->getType());
	path.append('s', 1);
	path /= pAsset->getName();
	path.setExtension(assetExt_[type]);
	path.toLower();

	// dispatch a read request baby!
	core::IoRequestOpen open;
	open.callback.Bind<AssetLoader, &AssetLoader::IoRequestCallback>(this);
	open.pUserData = pLoadReq;
	open.mode = core::fileMode::READ;
	open.path = path;

	gEnv->pFileSys->AddIoRequestToQue(open);
}


void AssetLoader::onLoadRequestFail(AssetLoadRequest* pLoadReq)
{
	auto* pAsset = pLoadReq->pAsset;
	auto type = pAsset->getType();

	pAsset->setStatus(core::LoadStatus::Error);

	assetsinks_[type]->onLoadRequestFail(pAsset);

	loadRequestCleanup(pLoadReq);
}

void AssetLoader::loadRequestCleanup(AssetLoadRequest* pLoadReq)
{
	auto status = pLoadReq->pAsset->getStatus();
	X_ASSERT(status == core::LoadStatus::Complete || status == core::LoadStatus::Error, "Unexpected load status")(status);

	{
		core::CriticalSection::ScopedLock lock(loadReqLock_);
		pendingRequests_.remove(pLoadReq);
	}

	if (pLoadReq->pFile) {
		gEnv->pFileSys->AddCloseRequestToQue(pLoadReq->pFile);
	}

	// release our ref.
	//releaseScript(static_cast<Script*>(pLoadReq->pAsset));

	X_DELETE(pLoadReq, arena_);

	loadCond_.NotifyAll();
}

void AssetLoader::IoRequestCallback(core::IFileSys& fileSys, const core::IoRequestBase* pRequest,
	core::XFileAsync* pFile, uint32_t bytesTransferred)
{
	core::IoRequest::Enum requestType = pRequest->getType();
	AssetLoadRequest* pLoadReq = pRequest->getUserData<AssetLoadRequest>();
	auto* pAsset = pLoadReq->pAsset;

	if (requestType == core::IoRequest::OPEN)
	{
		if (!pFile)
		{
			X_ERROR("AssetLoader", "Failed to load: %s -> \"%s\"", 
				assetDb::AssetType::ToString(pLoadReq->pAsset->getType()), pAsset->getName().c_str());
			onLoadRequestFail(pLoadReq);
			return;
		}

		pLoadReq->pFile = pFile;

		// read the whole file.
		uint32_t fileSize = safe_static_cast<uint32_t>(pFile->fileSize());
		X_ASSERT(fileSize > 0, "Datasize must be positive")(fileSize);
		pLoadReq->data = core::makeUnique<char[]>(blockArena_, fileSize, 16);
		pLoadReq->dataSize = fileSize;

		core::IoRequestRead read;
		read.callback.Bind<AssetLoader, &AssetLoader::IoRequestCallback>(this);
		read.pUserData = pLoadReq;
		read.pFile = pFile;
		read.dataSize = fileSize;
		read.offset = 0;
		read.pBuf = pLoadReq->data.ptr();
		fileSys.AddIoRequestToQue(read);
	}
	else if (requestType == core::IoRequest::READ)
	{
		const core::IoRequestRead* pReadReq = static_cast<const core::IoRequestRead*>(pRequest);

		X_ASSERT(pLoadReq->data.ptr() == pReadReq->pBuf, "Buffers don't match")();

		if (bytesTransferred != pReadReq->dataSize)
		{
			X_ERROR("AssetLoader", "Failed to read asset data. Got: 0x%" PRIx32 " need: 0x%" PRIx32, bytesTransferred, pReadReq->dataSize);
			onLoadRequestFail(pLoadReq);
			return;
		}

		gEnv->pJobSys->CreateMemberJobAndRun<AssetLoader>(this, &AssetLoader::processData_job,
			pLoadReq JOB_SYS_SUB_ARG(core::profiler::SubSys::CORE));
	}
}

void AssetLoader::processData_job(core::V2::JobSystem& jobSys, size_t threadIdx, core::V2::Job* pJob, void* pData)
{
	X_UNUSED(jobSys, threadIdx, pJob, pData);

	auto* pLoadReq = static_cast<AssetLoadRequest*>(X_ASSERT_NOT_NULL(pData));
	auto* pAsset = pLoadReq->pAsset;
	auto type = pAsset->getType();
	
	if(!assetsinks_[type]->processData(pAsset, std::move(pLoadReq->data), pLoadReq->dataSize))
	{
		onLoadRequestFail(pLoadReq);
		return;
	}

	pAsset->setStatus(core::LoadStatus::Complete);

	loadRequestCleanup(pLoadReq);
}





X_NAMESPACE_END