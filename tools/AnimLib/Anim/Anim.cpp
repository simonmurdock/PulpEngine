#include "stdafx.h"
#include "Anim.h"

#include <Math\XQuatCompressed.h>
#include <Memory\MemCursor.h>

#include <Time\TimeLiterals.h>

X_NAMESPACE_BEGIN(anim)

Bone::Bone()
{
    pName_ = nullptr;
    numAngles_ = 0;
    numPos_ = 0;
    pAngleData_ = nullptr;
    pAngleFrames_ = nullptr;
    pPosScalers_ = nullptr;
}

void Bone::setName(const char* pName)
{
    pName_ = pName;
}

void Bone::load(core::MemCursor& cursor)
{
    flags_ = cursor.getSeek<BoneFlags>();

    // angles
    numAngles_ = cursor.getSeek<uint16_t>();

    if (numAngles_ > 0) {
        if (numAngles_ > 1 && !flags_.IsSet(BoneFlag::AngFullFrame)) {
            // frame numbers.
            pAngleFrames_ = cursor.postSeekPtr<uint8_t>(numAngles_);
        }

        pAngleData_ = cursor.postSeekPtr<XQuatCompressedf>(numAngles_);
    }

    // positions.
    numPos_ = cursor.getSeek<uint16_t>();

    if (numPos_ > 0) {
        if (numPos_ == 1) {
            posMin_ = cursor.getSeek<Vec3f>();
        }
        else {
            if (numPos_ > 1 && !flags_.IsSet(BoneFlag::PosFullFrame)) {
                // frame numbers.
                pPosFrames_ = cursor.postSeekPtr<uint8_t>(numPos_);
            }

            if (flags_.IsSet(BoneFlag::LargePosScalers)) {
                pPosScalersLarge_ = cursor.postSeekPtr<Vec3<uint16_t>>(numPos_);
            }
            else {
                pPosScalers_ = cursor.postSeekPtr<Vec3<uint8_t>>(numPos_);
            }

            posMin_ = cursor.getSeek<Vec3f>();
            posRange_ = cursor.getSeek<Vec3f>();
        }
    }
}

void Bone::decodeFrame(Transformf& trans, int32_t frame) const
{
    decodePos(trans.pos, frame);
    decodeAngle(trans.quat, frame);
}

void Bone::decodePos(Vec3f& pos, int32_t frame) const
{
    // position
    if (numPos_) {
        if (numPos_ == 1) {
            pos = posMin_;
        }
        else {
            if (flags_.IsSet(BoneFlag::PosLargeFrames)) {
                X_ASSERT_NOT_IMPLEMENTED();
            }

            if (flags_.IsSet(BoneFlag::PosFullFrame)) {
                pos = GetPostion(frame);
            }
            else {
                // do we have any data for you yet?
                if (pPosFrames_[0] > frame) {
                    return;
                }

                // we have multiple pos.
                // find a frame for us.
                int32_t posFrameIdx = 0;
                while (posFrameIdx < numPos_ && pPosFrames_[posFrameIdx] < frame) {
                    ++posFrameIdx;
                }

                // we have data
                if (posFrameIdx == numPos_) {
                    pos = GetPostion(numPos_ - 1);
                }
                else if (pPosFrames_[posFrameIdx] == frame) {
                    pos = GetPostion(posFrameIdx);
                }
                else // if(posFrameIdx != 0)
                {
                    // interpolate.
                    int32_t firstIdx = posFrameIdx - 1;
                    int32_t lastIdx = posFrameIdx;

                    X_ASSERT(firstIdx >= 0 && firstIdx < numPos_, "invalid index")(firstIdx, numPos_); 
                    X_ASSERT(lastIdx >= 0 && lastIdx < numPos_, "invalid index")(lastIdx, numPos_); 

                    int32_t first = pPosFrames_[firstIdx];
                    int32_t last = pPosFrames_[lastIdx];

                    int32_t offset = frame - first;
                    int32_t range = last - first;

                    // if range is 10 and offset 5 i want o.5 yo

                    // calculate Fraction
                    float fraction = static_cast<float>(offset) / range;

                    auto firstPos = GetPostion(firstIdx);
                    auto lastPos = GetPostion(lastIdx);

                    pos = firstPos.lerp(fraction, lastPos);
                }
            }
        }
    }
}

void Bone::decodeAngle(Quatf& ang, int32_t frame) const
{
    if (numAngles_) {
        if (numAngles_ == 1) {
            ang = GetAngle(0);
        }
        else {
            if (flags_.IsSet(BoneFlag::AngLargeFrames)) {
                X_ASSERT_NOT_IMPLEMENTED();
            }

            if (flags_.IsSet(BoneFlag::AngFullFrame)) {
                ang = GetAngle(frame);
            }
            else {
                if (pAngleFrames_[0] > frame) {
                    return;
                }

                int32_t angFrameIdx = 0;
                while (angFrameIdx < numAngles_ && pAngleFrames_[angFrameIdx] < frame) {
                    ++angFrameIdx;
                }

                if (angFrameIdx == numAngles_) {
                    ang = GetAngle(numAngles_ - 1);
                }
                else if (pAngleFrames_[angFrameIdx] == frame) {
                    ang = GetAngle(angFrameIdx);
                }
                else {
                    int32_t firstIdx = angFrameIdx - 1;
                    int32_t lastIdx = angFrameIdx;

                    X_ASSERT(firstIdx >= 0 && firstIdx < numAngles_, "invalid index")(firstIdx, numAngles_); 
                    X_ASSERT(lastIdx >= 0 && lastIdx < numAngles_, "invalid index")(lastIdx, numAngles_); 

                    int32_t first = pAngleFrames_[firstIdx];
                    int32_t last = pAngleFrames_[lastIdx];

                    int32_t offset = frame - first;
                    int32_t range = last - first;

                    float fraction = static_cast<float>(offset) / range;

                    auto firstAng = GetAngle(firstIdx);
                    auto lastAng = GetAngle(lastIdx);

                    ang = firstAng.slerp(fraction, lastAng);
                }
            }
        }
    }
}

Quatf Bone::GetAngle(int32_t idx) const
{
    X_ASSERT(idx < numAngles_, "Invalid idx")(idx, numAngles_); 

    return pAngleData_[idx].asQuat();
}

Vec3f Bone::GetPostion(int32_t idx) const
{
    X_ASSERT(idx < numPos_, "Invalid idx")(idx, numPos_); 

    Vec3f pos;
    if (flags_.IsSet(BoneFlag::LargePosScalers)) {
        Vec3<uint16_t>& scale = pPosScalersLarge_[idx];

        pos[0] = (posMin_.x + (posRange_.x * (scale[0] / 65535.f)));
        pos[1] = (posMin_.y + (posRange_.y * (scale[1] / 65535.f)));
        pos[2] = (posMin_.z + (posRange_.z * (scale[2] / 65535.f)));
    }
    else {
        Vec3<uint8_t>& scale = pPosScalers_[idx];

        pos[0] = (posMin_.x + (posRange_.x * (scale[0] / 255.f)));
        pos[1] = (posMin_.y + (posRange_.y * (scale[1] / 255.f)));
        pos[2] = (posMin_.z + (posRange_.z * (scale[2] / 255.f)));
    }

    return pos;
}

// ----------------------------------

Anim::Anim(core::string_view name, core::MemoryArenaBase* arena) :
    core::AssetBase(name, assetDb::AssetType::ANIM),
    bones_(arena),
    pNoteHdr_(nullptr),
    pHdr_(nullptr)
{
    id_ = 0;
    status_ = core::LoadStatus::NotLoaded;
}

Anim::~Anim()
{
}

void Anim::timeToFrame(core::TimeVal time, int32_t cycles, FrameBlend& frame) const
{
    int32_t numFrames = getNumFrames();
    int32_t fps = getFps();

    // this turns a elapsed time into frame info.
    if (numFrames <= 1) {
        frame.cylces = 0;
        frame.frame1 = 0;
        frame.frame2 = 0;
        frame.backlerp = 0.0f;
        frame.frontlerp = 1.0f;
        return;
    }

    if (time <= 0_tv) {
        frame.cylces = 0;
        frame.frame1 = 0;
        frame.frame2 = 1;
        frame.backlerp = 0.0f;
        frame.frontlerp = 1.0f;
        return;
    }

    // multiple time by fps then device by 1 second.
    const core::TimeVal oneSecond = 1000_ms;

    core::TimeVal frameTime = core::TimeVal(time.GetValue() * fps);
    int32_t frameNum = safe_static_cast<int32_t>((frameTime / oneSecond).GetValue());

    frame.cylces = frameNum / (numFrames);

    // clamp
    if (cycles > 0 && frame.cylces >= cycles) {
        frame.cylces = cycles - 1;
        frame.frame1 = numFrames - 1;
        frame.frame2 = frame.frame1;
        frame.backlerp = 0.0f;
        frame.frontlerp = 1.0f;
        return;
    }

    frame.frame1 = frameNum % (numFrames);
    frame.frame2 = frame.frame1 + 1;
    if (frame.frame2 >= numFrames) {
        if (cycles == 0) {
            frame.frame2 = 0;
        }
        else {
            frame.frame1 = numFrames - 1;
            frame.frame2 = frame.frame1;
            frame.backlerp = 0.0f;
            frame.frontlerp = 1.0f;
            return;
        }
    }

    // work out lerps.
    // we turn range of 1 second.
    auto offset = (frameTime % oneSecond);

    frame.backlerp = offset.GetMilliSeconds() / oneSecond.GetMilliSeconds();
    frame.frontlerp = 1.0f - frame.backlerp;

    X_ASSERT(frame.backlerp >= 0 && frame.backlerp <= 1.f && ((frame.backlerp + frame.frontlerp) == 1.f), "Invalid lerp values")(frame.backlerp, frame.frontlerp, (frame.backlerp + frame.frontlerp)); 
}

void Anim::getFrame(const FrameBlend& frame, TransformArr& boneTransOut, const IndexArr& indexes) const
{
    X_UNUSED(frame, boneTransOut, indexes);

    // we need to animate each bone in the model that we affect
    // the index map is same size as bones, and the index is the index of the bone in animation.
    X_ASSERT(boneTransOut.size() == indexes.size(), "Size mismatch")(); 
    X_ASSERT(frame.frame1 >= 0 && frame.frame1 < getNumFrames(), "Frame1 out of range")(frame.frame1, getNumFrames()); 
    X_ASSERT(frame.frame2 >= 0 && frame.frame2 < getNumFrames(), "Frame2 out of range")(frame.frame2, getNumFrames()); 

    TransformArr blendTrans(boneTransOut.getArena(), boneTransOut.size());
    IndexArr lerpIndex(boneTransOut.getArena());

    // decode all the joints.
    for (int32_t i = 0; i < safe_static_cast<int32_t>(indexes.size()); i++) {
        if (indexes[i] < 0) {
            continue;
        }

        int32_t animBoneIdx = indexes[i];

        auto& bone = bones_[animBoneIdx];

        bone.decodeFrame(boneTransOut[i], frame.frame1);
        bone.decodeFrame(blendTrans[i], frame.frame2);

        lerpIndex.append(i);
    }

    // blend them
    Util::blendBones(boneTransOut, blendTrans, lerpIndex, frame.backlerp);
}

void Anim::getOrigin(Vec3f& offset, core::TimeVal time, int32_t cycles) const
{
    FrameBlend frame;
    timeToFrame(time, cycles, frame);

    // todo.
    offset = Vec3f::zero();
}

void Anim::getNotes(core::TimeVal from, core::TimeVal to, NoteTrackValueArr& values) const
{
    if (!hasNotes()) {
        return;
    }

    int32_t numFrames = getNumFrames();
    int32_t fps = getFps();
    float totalSec = static_cast<float>(numFrames) / fps;

    float fromFract = from.GetSeconds() / totalSec;
    float toFract = to.GetSeconds() / totalSec;

    if (fromFract < 0.f) {
        return;
    }

    if (fromFract == toFract) {
        return;
    }

    auto* pNotes = getNotes();
    auto numNotes = getNumNotes();

    for (int32_t i = 0; i < numNotes; i++) {
        auto noteFrame = pNotes[i].frame;
        if (noteFrame >= toFract) {
            break;
        }

        if (noteFrame >= fromFract) {
            X_ASSERT(noteFrame <= toFract, "Frame out of range")(noteFrame, fromFract, toFract); 
            auto* pNoteValue = getNoteValue(i);
            values.push_back(pNoteValue);

            if (values.size() == values.capacity()) {
                X_WARNING("Anim", "Exceeded max note values");
                return;
            }
        }
    }
}

bool Anim::processData(core::UniquePointer<char[]> data, uint32_t dataSize)
{
    if (dataSize < sizeof(AnimHeader)) {
        return false;
    }

    AnimHeader& hdr = *reinterpret_cast<AnimHeader*>(data.get());

    if (!hdr.isValid()) {
        X_ERROR("Anim", "\"%s\" invalid header", name_.c_str());
        return false;
    }

    if (dataSize != (hdr.dataSize + sizeof(hdr))) {
        X_ERROR("Anim", "\"%s\" incompleted data", name_.c_str());
        return false;
    }

    bones_.resize(hdr.numBones);

    core::MemCursor cursor(data.ptr() + sizeof(hdr), hdr.dataSize);

    for (auto& bone : bones_) {
        bone.setName(cursor.getPtr<const char>());

        while (cursor.get<char>() != '\0') {
            cursor.seekBytes(1);
        }
        cursor.seekBytes(1);
    }

    for (auto& bone : bones_) {
        bone.load(cursor);
    }

    if (hdr.flags.IsSet(AnimFlag::NOTES)) {
        pNoteHdr_ = cursor.getSeekPtr<NoteTrackHdr>();
    }

    data_ = std::move(data);
    pHdr_ = reinterpret_cast<AnimHeader*>(data_.get());
    return true;
}

X_NAMESPACE_END