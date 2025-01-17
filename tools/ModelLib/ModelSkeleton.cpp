#include "stdafx.h"
#include "ModelSkeleton.h"

#include <IModel.h>
#include <IFileSys.h>

#include <Memory\MemCursor.h>

X_NAMESPACE_BEGIN(model)

// ==================================

ModelSkeleton::ModelSkeleton(core::MemoryArenaBase* arena) :
    arena_(arena),
    tagNames_(arena),

    nameIdx_(arena),
    tree_(arena),
    angles_(arena),
    positions_(arena)
{
    numBones_ = 0;
}

ModelSkeleton::~ModelSkeleton()
{
}

void ModelSkeleton::dumpToLog(void) const
{
    size_t longestName = 0;
    for (size_t i = 0; i < numBones_; i++) {
        const char* pName = getBoneName(i);
        longestName = core::Max(longestName, core::strUtil::strlen(pName));
    }

    ++longestName;    // one space
    longestName += 2; // " "

    for (size_t i = 0; i < numBones_; i++) {
        const char* pName = getBoneName(i);
        const auto pos = getBonePos(i);
        const auto ang = getBoneAngle(i);
        const auto parent = getBoneParent(i);

        core::StackString<model::MODEL_MAX_BONE_NAME_LENGTH> boneName;
        core::StackString<128> posStr, angStr;

        X_ASSERT(longestName > boneName.length(), "Incorrect longest name")(longestName, boneName.length()); 

        boneName.appendFmt("\"%s", pName);
        boneName.append('"', 1);
        boneName.append(' ', longestName - boneName.length());
        posStr.appendFmt("(^6%8.3f,%8.3f,%8.3f ^7)", pos.x, pos.y, pos.z);
        angStr.appendFmt("(^6%6.3f,%6.3f,%6.3f - %6.3f^7)", ang.v.x, ang.v.y, ang.v.z, ang.w);

        X_LOG0("Skeleton", "%s pos: %s ang: %s parent: ^6%" PRIuS, boneName.c_str(), posStr.c_str(), angStr.c_str(), parent);
    }
}

bool ModelSkeleton::LoadCompiledSkeleton(const core::Path<char>& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }

    core::Path<char> path(filePath);
    path.setExtension(model::MODEL_FILE_EXTENSION);

    core::FileFlags mode;
    mode.Set(core::FileFlag::READ);
    mode.Set(core::FileFlag::RANDOM_ACCESS);

    core::XFileScoped file;
    if (!file.openFile(path, mode)) {
        X_ERROR("Model", "Failed to open model for skeleton load");
        return false;
    }

    ModelHeader hdr;

    // read header;
    if (file.readObj(hdr) != sizeof(hdr)) {
        X_ERROR("Model", "failed to read model header");
        return false;
    }

    if (!hdr.isValid()) {
        X_ERROR("Model", "model header is invalid");
        return false;
    }

    if (!hdr.flags.IsSet(ModelFlags::LOOSE)) {
        X_ERROR("Model", "can't load model skeleton from none loose model");
        return false;
    }

    // we only care about none black bones.
    if (hdr.numBones > 0) {
        // read bone data.
        nameIdx_.resize(hdr.numBones);
        tree_.resize(hdr.numBones);
        angles_.resize(hdr.numBones);
        positions_.resize(hdr.numBones);

        numBones_ = hdr.numBones;

        // the tag names are after the material names.
        const long tagNameOffset = sizeof(hdr) + hdr.materialNameDataSize;
        // start of name index and tree
        const long tagDataOffset = sizeof(hdr) + hdr.materialNameDataSize + hdr.tagNameDataSize;

        char TagNameBuf[model::MODEL_MAX_BONE_NAME_LENGTH * model::MODEL_MAX_BONES];

        file.seek(tagNameOffset, core::SeekMode::SET);
        if (file.read(TagNameBuf, hdr.tagNameDataSize) != hdr.tagNameDataSize) {
            X_ERROR("Model", "failed to read tag buf");
            return false;
        }

        core::MemCursor tag_name_cursor(TagNameBuf, hdr.tagNameDataSize);
        core::StackString<MODEL_MAX_BONE_NAME_LENGTH> name;

        for (size_t i = 0; i < hdr.numBones; i++) {
            while (!tag_name_cursor.isEof() && tag_name_cursor.get<char>() != '\0') {
                name.append(tag_name_cursor.getSeek<char>(), 1);
            }

            if (!tag_name_cursor.isEof()) {
                ++tag_name_cursor;
            }

            tagNames_.append(name);

            name.clear();
        }

        file.seek(tagDataOffset, core::SeekMode::SET);

        if (!file.readObjs(nameIdx_.ptr(), nameIdx_.size())) {
            X_ERROR("Model", "failed to read name indexes");
            return false;
        }
        if (!file.readObjs(tree_.ptr(), tree_.size())) {
            X_ERROR("Model", "failed to read tree");
            return false;
        }

        core::Array<XQuatCompressedf> compressedAngles(arena_, hdr.numBones);
        if (!file.readObjs(compressedAngles.ptr(), compressedAngles.size())) {
            X_ERROR("Model", "failed to read angles");
            return false;
        }

        // uncompress them
        X_ASSERT(angles_.size() == compressedAngles.size(), "Angle arrays size should match, source code erorr")
        (
            angles_.size(), compressedAngles.size());

        for (size_t i = 0; i < compressedAngles.size(); i++) {
            angles_[i] = compressedAngles[i].asQuat();
        }

        if (!file.readObjs(positions_.ptr(), positions_.size())) {
            X_ERROR("Model", "failed to read pos data");
            return false;
        }
    }

    return true;
}

bool ModelSkeleton::LoadRawModelSkeleton(const core::Path<char>& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }

    core::Path<char> path(filePath);
    path.setExtension(model::MODEL_FILE_EXTENSION);

    core::FileFlags mode;
    mode.Set(core::FileFlag::READ);

    core::XFileScoped file;
    if (!file.openFile(path, mode)) {
        X_ERROR("Model", "Failed to open raw model for skeleton load");
        return false;
    }

    core::Array<char> fileData(arena_);
    {
        size_t fileSize = safe_static_cast<size_t, uint64_t>(file.remainingBytes());

        // lets limit it to 16kb
        fileSize = core::Min<size_t>(fileSize, 1024 * 16);

        fileData.resize(fileSize);

        const size_t bytesRead = file.read(fileData.ptr(), fileSize);
        if (bytesRead != fileSize) {
            X_ERROR("RawModel", "failed to read file data. got: %" PRIuS " requested: %" PRIuS, bytesRead, fileSize);
            return false;
        }
    }

    core::XLexer lex(fileData.begin(), fileData.end());

    return LoadRawModelSkeleton_int(lex);
}

bool ModelSkeleton::LoadRawModelSkeleton(const core::Array<uint8_t>& data)
{
    const char* pBegin = reinterpret_cast<const char*>(data.begin());
    const char* pEnd = reinterpret_cast<const char*>(data.end());

    core::XLexer lex(pBegin, pEnd);

    return LoadRawModelSkeleton_int(lex);
}

bool ModelSkeleton::LoadRawModelSkeleton_int(core::XLexer& lex)
{
    int32_t version, numLods, numBones;

    if (!ReadheaderToken(lex, "VERSION", version)) {
        return false;
    }
    if (!ReadheaderToken(lex, "LODS", numLods)) {
        return false;
    }
    if (!ReadheaderToken(lex, "BONES", numBones)) {
        return false;
    }

    if (!ReadBones(lex, numBones)) {
        X_ERROR("RawModel", "failed to parse bone data");
        return false;
    }

    return true;
}

bool ModelSkeleton::ReadBones(core::XLexer& lex, int32_t numBones)
{
    numBones_ = numBones;
    nameIdx_.resize(numBones);
    tree_.resize(numBones);
    angles_.resize(numBones);
    positions_.resize(numBones);
    tagNames_.resize(numBones);

    core::XLexToken token(nullptr, nullptr);

    for (int32_t i = 0; i < numBones; i++) {
        if (!lex.ReadToken(token)) {
            X_ERROR("RawModel", "Failed to read 'BONE' token");
            return false;
        }

        if (!token.isEqual("BONE")) {
            X_ERROR("RawModel", "Failed to read 'BONE' token");
            return false;
        }

        // read the parent idx.
        int32_t parIdx;
        if (!lex.ParseInt(parIdx)) {
            X_ERROR("RawModel", "Failed to read parent index");
            return false;
        }

        if (parIdx >= 0) {
            tree_[i] = safe_static_cast<uint8_t>(parIdx);
        }
        else {
            tree_[i] = 0;
        }

        // read the string
        if (!lex.ReadTokenOnLine(token)) {
            X_ERROR("RawModel", "Failed to read bone name token");
            return false;
        }

        if (token.GetType() != core::TokenType::STRING) {
            X_ERROR("RawModel", "Bone name token is invalid");
            return false;
        }

        tagNames_[i] = core::StackString<model::MODEL_MAX_BONE_NAME_LENGTH>(token.begin(), token.end());

        // pos
        if (!lex.ReadToken(token)) {
            X_ERROR("RawModel", "Failed to read 'POS' token");
            return false;
        }

        if (!token.isEqual("POS")) {
            X_ERROR("RawModel", "Failed to read 'POS' token");
            return false;
        }

        if (!lex.Parse1DMatrix(3, &positions_[i][0])) {
            X_ERROR("RawModel", "Failed to read 'POS' token data");
            return false;
        }

        // scale
        if (!lex.ReadToken(token)) {
            X_ERROR("RawModel", "Failed to read 'SCALE' token");
            return false;
        }

        if (!token.isEqual("SCALE")) {
            X_ERROR("RawModel", "Failed to read 'SCALE' token");
            return false;
        }

        Vec3f scale;
        if (!lex.Parse1DMatrix(3, &scale[0])) {
            X_ERROR("RawModel", "Failed to read 'SCALE' token data");
            return false;
        }

        // ang
        if (!lex.ReadToken(token)) {
            X_ERROR("RawModel", "Failed to read 'ANG' token");
            return false;
        }

        if (!token.isEqual("ANG")) {
            X_ERROR("RawModel", "Failed to read 'ANG' token");
            return false;
        }

        Matrix33f rot;
        if (!lex.Parse2DMatrix(3, 3, &rot[0])) {
            X_ERROR("RawModel", "Failed to read 'ANG' token data");
            return false;
        }

        angles_[i] = Quatf(rot);
    }

    return true;
}

bool ModelSkeleton::ReadheaderToken(core::XLexer& lex, const char* pName, int32_t& valOut)
{
    valOut = 0;

    if (!lex.SkipUntilString(pName)) {
        X_ERROR("RawModel", "Failed to find '%s' token", pName);
        return false;
    }

    // get value
    if (!lex.ParseInt(valOut)) {
        X_ERROR("RawModel", "Failed to read '%s' value", pName);
        return false;
    }
    
    return true;
}

void ModelSkeleton::scale(float scale)
{
    // scale me bones!
    for (auto& pos : positions_) {
        pos *= scale;
    }
}

size_t ModelSkeleton::getNumBones(void) const
{
    return numBones_;
}

const char* ModelSkeleton::getBoneName(size_t idx) const
{
    return tagNames_[idx].c_str();
}

const Quatf ModelSkeleton::getBoneAngle(size_t idx) const
{
    return angles_[idx];
}

const Vec3f ModelSkeleton::getBonePos(size_t idx) const
{
    return positions_[idx];
}

const size_t ModelSkeleton::getBoneParent(size_t idx) const
{
    return tree_[idx];
}

X_NAMESPACE_END
