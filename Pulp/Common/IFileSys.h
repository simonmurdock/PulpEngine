#pragma once

#ifndef _X_FILE_SYSTEM_I_H_
#define _X_FILE_SYSTEM_I_H_

// why the fuck is this here?
// #include "io.h"

#include "Util\Delegate.h"
#include "Containers\Array.h"

#if X_ENABLE_FILE_STATS
#include "Time\TimeVal.h"
#endif // !X_ENABLE_FILE_STATS

// i need the definition :|
#include X_INCLUDE(../Core/FileSys/X_PLATFORM/OsFileAsyncOperation.h)

X_NAMESPACE_BEGIN(core)

static const size_t FS_MAX_VIRTUAL_DIR = 4;
static const size_t FS_MAX_PAK = 16;

X_DECLARE_FLAGS(FileFlag)
(
    READ,
    WRITE,
    APPEND,
    WRITE_FLUSH,
    RECREATE,
    SHARE,
    RANDOM_ACCESS,
    NOBUFFER
);

X_DECLARE_FLAGS(SeekMode)
(
    CUR,
    END,
    SET);

X_DECLARE_ENUM(VirtualDirectory)
(
    BASE,
    SAVE
);

typedef Flags<FileFlag> FileFlags;

X_DECLARE_FLAG_OPERATORS(FileFlags);

typedef core::XOsFileAsyncOperation XFileAsyncOperation;
typedef core::XOsFileAsyncOperationCompiltion XFileAsyncOperationCompiltion;

struct X_NO_DISCARD XFileAsync
{
    X_DECLARE_ENUM(Type)
    (DISK, VIRTUAL);

    typedef XOsFileAsyncOperation::ComplitionRotinue ComplitionRotinue;

    virtual ~XFileAsync(){};

    virtual Type::Enum getType(void) const X_ABSTRACT;

    virtual XFileAsyncOperation readAsync(void* pBuffer, size_t length, uint64_t position) X_ABSTRACT;
    virtual XFileAsyncOperation writeAsync(const void* pBuffer, size_t length, uint64_t position) X_ABSTRACT;

    virtual void cancelAll(void) const X_ABSTRACT;

    // Waits until the asynchronous operation has finished, and returns the number of transferred bytes.
    virtual size_t waitUntilFinished(const XFileAsyncOperation& operation) X_ABSTRACT;

    virtual uint64_t fileSize(void) const X_ABSTRACT;
    virtual void setSize(int64_t numBytes) X_ABSTRACT;
};

struct X_NO_DISCARD XFile
{
    virtual ~XFile() = default;
    virtual size_t read(void* pBuf, size_t Len) X_ABSTRACT;
    virtual size_t write(const void* pBuf, size_t Len) X_ABSTRACT;

    virtual void seek(int64_t position, SeekMode::Enum origin) X_ABSTRACT;

    template<typename T>
    inline size_t readObj(T& object)
    {
        return read(&object, sizeof(T));
    }

    template<typename T>
    inline size_t readObj(T* objects, size_t num)
    {
        return read(objects, sizeof(T) * num);
    }

    inline size_t readString(core::string& str)
    {
        // uggh
        char Char;
        str.clear();
        while (read(&Char, 1)) {
            if (Char == '\0') {
                break;
            }
            str += Char;
        }
        return str.length();
    }

    template<typename T>
    inline size_t writeObj(const T& object)
    {
        return write(&object, sizeof(T));
    }
    template<>
    inline size_t writeObj(const core::string& str)
    {
        return writeString(str);
    }

    template<typename T>
    inline size_t writeObj(const T* objects, size_t num)
    {
        return write(objects, (sizeof(T) * num));
    }
    inline size_t writeString(const core::string& str)
    {
        return write(str.c_str(), str.length() + 1);
    }
    inline size_t writeString(const char* str)
    {
        return write(str, (strlen(str) + 1));
    }
    inline size_t writeString(const char* str, size_t Length)
    {
        return write(str, Length);
    }

    inline size_t writeStringNNT(const core::string& str)
    {
        return write(str.c_str(), (str.length()));
    }
    inline size_t writeStringNNT(const char* str)
    {
        return write(str, (strlen(str)));
    }

    inline size_t printf(const char* fmt, ...)
    {
        char buf[2048]; // more? i think not!
        int length;

        va_list argptr;

        va_start(argptr, fmt);
        length = vsnprintf(buf, sizeof(buf) - 1, fmt, argptr);
        va_end(argptr);

        if (length < 0) {
            return 0;
        }

        return write(buf, length);
    }

    virtual inline bool isEof(void) const
    {
        return remainingBytes() == 0;
    }

    virtual uint64_t remainingBytes(void) const X_ABSTRACT;
    virtual uint64_t tell(void) const X_ABSTRACT;
    virtual void setSize(int64_t numBytes) X_ABSTRACT;
};

// I don't like this.
// as it's taking ownership of buffer.
// and is simular functionaloty to XFileFixedBuf otherwise.
struct X_NO_DISCARD XFileMem : public XFile
{
    XFileMem(char* begin, char* end, core::MemoryArenaBase* arena) :
        arena_(arena),
        begin_(begin),
        current_(begin),
        end_(end)
    {
        X_ASSERT_NOT_NULL(begin);
        X_ASSERT_NOT_NULL(end);
        X_ASSERT_NOT_NULL(arena);
        X_ASSERT(end >= begin, "invalid buffer")(begin, end);
    }
    ~XFileMem() X_OVERRIDE
    {
        X_DELETE_ARRAY(begin_, arena_);
    }

    virtual size_t read(void* pBuf, size_t Len) X_FINAL
    {
        size_t size = core::Min<size_t>(Len, safe_static_cast<size_t, uint64_t>(remainingBytes()));

        memcpy(pBuf, current_, size);
        current_ += size;

        return size;
    }

    virtual size_t write(const void* pBuf, size_t Len) X_FINAL
    {
        X_UNUSED(pBuf);
        X_UNUSED(Len);
        X_ASSERT_NOT_IMPLEMENTED();
        return 0;
    }

    virtual void seek(int64_t position, SeekMode::Enum origin) X_FINAL
    {
        switch (origin) {
            case SeekMode::CUR:
                current_ += core::Min<int64_t>(position, remainingBytes());
                if (current_ < begin_) {
                    current_ = begin_;
                }
                break;
            case SeekMode::SET:
                current_ = begin_ + core::Min<int64_t>(position, getSize());
                if (current_ < begin_) {
                    current_ = begin_;
                }
                if (current_ > end_) {
                    current_ = end_;
                }
                break;
            case SeekMode::END:
                X_ASSERT_NOT_IMPLEMENTED();
                break;
        }
    }
    virtual uint64_t remainingBytes(void) const X_FINAL
    {
        return static_cast<size_t>(end_ - current_);
    }
    virtual uint64_t tell(void) const X_FINAL
    {
        return static_cast<size_t>(current_ - begin_);
    }
    virtual void setSize(int64_t numBytes) X_FINAL
    {
        X_UNUSED(numBytes);
        X_ASSERT_UNREACHABLE();
    }

    inline char* getBufferStart(void)
    {
        return begin_;
    }
    inline const char* getBufferStart(void) const
    {
        return begin_;
    }

    inline char* getBufferEnd(void)
    {
        return end_;
    }
    inline const char* getBufferEnd(void) const
    {
        return end_;
    }

    inline uint64_t getSize(void) const
    {
        return static_cast<size_t>(end_ - begin_);
    }

    inline MemoryArenaBase* getMemoryArena(void)
    {
        return arena_;
    }

    inline bool isEof(void) const X_FINAL
    {
        return remainingBytes() == 0;
    }

private:
    core::MemoryArenaBase* arena_;
    char* begin_;
    char* current_;
    char* end_;
};

struct XFileFixedBuf : public XFile
{
    XFileFixedBuf(const uint8_t* begin, const uint8_t* end) :
        begin_(begin),
        current_(begin),
        end_(end)
    {
        X_ASSERT_NOT_NULL(begin);
        X_ASSERT_NOT_NULL(end);
        X_ASSERT(end >= begin, "invalid buffer")(begin, end);
    }

    XFileFixedBuf(const char* begin, const char* end) :
        XFileFixedBuf(reinterpret_cast<const uint8_t*>(begin), reinterpret_cast<const uint8_t*>(end))
    {
    }

    ~XFileFixedBuf() X_OVERRIDE
    {
    }

    virtual size_t read(void* pBuf, size_t Len) X_FINAL
    {
        size_t size = core::Min<size_t>(Len, safe_static_cast<size_t, uint64_t>(remainingBytes()));

        memcpy(pBuf, current_, size);
        current_ += size;

        return size;
    }

    virtual size_t write(const void* pBuf, size_t Len) X_FINAL
    {
        X_UNUSED(pBuf);
        X_UNUSED(Len);
        X_ASSERT_NOT_IMPLEMENTED();
        return 0;
    }

    virtual void seek(int64_t position, SeekMode::Enum origin) X_FINAL
    {
        switch (origin) {
            case SeekMode::CUR:
                current_ += core::Min<int64_t>(position, remainingBytes());
                if (current_ < begin_) {
                    current_ = begin_;
                }
                break;
            case SeekMode::SET:
                current_ = begin_ + core::Min<int64_t>(position, getSize());
                if (current_ < begin_) {
                    current_ = begin_;
                }
                if (current_ > end_) {
                    current_ = end_;
                }
                break;
            case SeekMode::END:
                X_ASSERT_NOT_IMPLEMENTED();
                break;
        }
    }
    virtual uint64_t remainingBytes(void) const X_FINAL
    {
        return static_cast<size_t>(end_ - current_);
    }
    virtual uint64_t tell(void) const X_FINAL
    {
        return static_cast<size_t>(current_ - begin_);
    }
    virtual void setSize(int64_t numBytes) X_FINAL
    {
        X_UNUSED(numBytes);
        X_ASSERT_UNREACHABLE();
    }

    inline const uint8_t* getBufferStart(void) const
    {
        return begin_;
    }

    inline const uint8_t* getBufferEnd(void) const
    {
        return end_;
    }

    inline uint64_t getSize(void) const
    {
        return static_cast<uint64_t>(end_ - begin_);
    }

    inline bool isEof(void) const X_FINAL
    {
        return remainingBytes() == 0;
    }

private:
    const uint8_t* begin_;
    const uint8_t* current_;
    const uint8_t* end_;
};

struct XFileStream : public XFile
{
    typedef core::Array<uint8_t, core::ArrayAllocator<uint8_t>, core::growStrat::Multiply> DataVec;

    XFileStream(core::MemoryArenaBase* arena) :
        buf_(arena)
    {
        X_ASSERT_NOT_NULL(arena);
    }
    ~XFileStream() X_OVERRIDE
    {
    }

    virtual size_t read(void* pBuf, size_t len) X_FINAL
    {
        X_UNUSED(pBuf);
        X_UNUSED(len);
        X_ASSERT_NOT_IMPLEMENTED();
        return 0;
    }

    virtual size_t write(const void* pBuf, size_t len) X_FINAL
    {
        const size_t offset = buf_.size();

        buf_.resize(offset + len);

        std::memcpy(&buf_[offset], pBuf, len);
        return len;
    }

    virtual void seek(int64_t position, SeekMode::Enum origin) X_FINAL
    {
        X_UNUSED(position);
        X_UNUSED(origin);
        X_ASSERT_NOT_IMPLEMENTED();
    }

    virtual uint64_t remainingBytes(void) const X_FINAL
    {
        return 0;
    }
    virtual uint64_t tell(void) const X_FINAL
    {
        return buf_.size();
    }
    virtual void setSize(int64_t numBytes) X_FINAL
    {
        X_UNUSED(numBytes);
        X_ASSERT_UNREACHABLE();
    }

    inline uint64_t getSize(void) const
    {
        return buf_.size();
    }

    inline bool isEof(void) const X_FINAL
    {
        return remainingBytes() == 0;
    }

    inline const DataVec& buffer(void) const
    {
        return buf_;
    }

private:
    DataVec buf_;
};

struct XFileByteStream : public XFile
{
    XFileByteStream(core::ByteStream& stream) :
        stream_(stream)
    {
    }
    ~XFileByteStream() X_OVERRIDE
    {
    }

    virtual size_t read(void* pBuf, size_t len) X_FINAL
    {
        stream_.read(reinterpret_cast<char*>(pBuf), len);
        return len;
    }

    virtual size_t write(const void* pBuf, size_t len) X_FINAL
    {
        stream_.write(reinterpret_cast<const char*>(pBuf), len);
        return len;
    }

    virtual void seek(int64_t position, SeekMode::Enum origin) X_FINAL
    {
        X_UNUSED(position);
        X_UNUSED(origin);

        X_ASSERT_NOT_IMPLEMENTED();
    }
    virtual uint64_t remainingBytes(void) const X_FINAL
    {
        return stream_.size();
    }
    virtual uint64_t tell(void) const X_FINAL
    {
        return stream_.tell();
    }
    virtual void setSize(int64_t numBytes) X_FINAL
    {
        X_UNUSED(numBytes);
        X_ASSERT_NOT_IMPLEMENTED();
    }

    inline uint64_t getSize(void) const
    {
        return stream_.tellWrite();
    }

    inline bool isEof(void) const X_FINAL
    {
        return stream_.isEos();
    }

private:
    core::ByteStream& stream_;
};

// Buffers small write calls using local buffer.
template<size_t BUF_SIZE>
struct XFileBufWriteIO : public core::XFile
{
    static constexpr size_t SKIP_BUF_SIZE = BUF_SIZE / 2;

    static_assert(core::bitUtil::IsPowerOfTwo(BUF_SIZE));
    static_assert(SKIP_BUF_SIZE <= BUF_SIZE, "Skip buffer size must be less than buffer size");

public:
    XFileBufWriteIO(core::XFile* pFile) :
        pFile_(pFile),
        bufSize_(0)
    {
    }

    ~XFileBufWriteIO() X_FINAL {
        flushBuffer();
    }


    size_t read(void* pBuf, size_t len) X_FINAL
    {
        flushBuffer();

        return pFile_->read(pBuf, len);
    }

    size_t write(const void* pBuf, size_t len) X_FINAL
    {
        // if above certain size write direct.
        if (len >= SKIP_BUF_SIZE) {
            flushBuffer();
            return pFile_->write(pBuf, len);
        }

        size_t spaceInBuffer = BUF_SIZE - bufSize_;
        if (spaceInBuffer < len) {
            flushBuffer();
        }

        std::memcpy(&buf_[bufSize_], pBuf, len);
        bufSize_ += len;

        return len;
    }

    X_INLINE void seek(int64_t position, core::SeekMode::Enum origin) X_FINAL
    {
        flushBuffer();

        pFile_->seek(position, origin);
    }

    X_INLINE uint64_t remainingBytes(void) const X_FINAL
    {
        return pFile_->remainingBytes();
    }

    X_INLINE uint64_t tell(void) const X_FINAL
    {
        return pFile_->tell();
    }

    X_INLINE void setSize(int64_t numBytes) X_FINAL
    {
        pFile_->setSize(numBytes);
    }

private:

    X_INLINE void flushBuffer(void)
    {
        if (bufSize_ == 0) {
            return;
        }

        pFile_->write(buf_, bufSize_);
        bufSize_ = 0;
    }

private:
    core::XFile* pFile_;

    size_t bufSize_;
    uint8_t buf_[BUF_SIZE];
};



// stuff for io requests
X_DECLARE_ENUM(IoRequest)
(
    // these are ordered based on priority.
    // aka READ requests are higest priority.
    CLOSE,
    OPEN,
    OPEN_WRITE_ALL,
    OPEN_READ_ALL,
    WRITE,
    READ);

typedef uint32_t RequestHandle;
static const RequestHandle INVALID_IO_REQ_HANDLE = 0;

// I want to pass these into filesystem with one function call.
// but support different types.
// so many i should have a base type that contains the mode
// and we copy it into a internal buffer

struct IoRequestBase;

typedef core::Delegate<void(core::IFileSys&, const IoRequestBase*, core::XFileAsync*, uint32_t)> IoCallBack;

struct IoRequestBase
{
    X_INLINE IoRequest::Enum getType(void) const
    {
        return type;
    }
    template<typename T>
    X_INLINE T* getUserData(void) const
    {
        return reinterpret_cast<T*>(pUserData);
    }

#if X_ENABLE_FILE_STATS
    X_INLINE core::TimeVal getAddTime(void) const
    {
        return addTime;
    }

    X_INLINE void setAddTime(core::TimeVal time)
    {
        addTime = time;
    }
#endif // !X_ENABLE_FILE_STATS

    IoCallBack callback; // 8 bytes
    void* pUserData;

protected:
    IoRequest::Enum type; // 4 bytes

#if X_ENABLE_FILE_STATS
    core::TimeVal addTime;
#endif // !X_ENABLE_FILE_STATS
};

struct IoRequestOpen : public IoRequestBase
{
    IoRequestOpen()
    {
        pUserData = nullptr;
        type = IoRequest::OPEN;
    }

    FileFlags mode;
    core::Path<char> path;
};

struct IoRequestOpenRead : public IoRequestOpen
{
    IoRequestOpenRead()
    {
        pUserData = nullptr;
        type = IoRequest::OPEN_READ_ALL;
        arena = nullptr;
        pFile = nullptr;
        pBuf = nullptr;
        dataSize = 0;
    }

    core::MemoryArenaBase* arena; // the arena the buffer to read the whole file into is allocated from.
    // these are only valid if read completed.
    XFileAsync* pFile;
    uint8_t* pBuf;      // for this to be uniqueptr have to pass none const IoRequestBase in callbacks.
    uint32_t dataSize;
};

// Takes a buffer and file, and attemps to write it all to a file.
// will then free the data after.
// used for dispatch and forget style writes
// mode is: RECREATE | WRITE
struct IoRequestOpenWrite : public IoRequestBase
{
    IoRequestOpenWrite(core::Array<uint8_t>&& arr) :
        data(std::move(arr))
    {
        pUserData = nullptr;
        type = IoRequest::OPEN_WRITE_ALL;
        pFile = nullptr;
    }

    core::Path<char> path;
    XFileAsync* pFile;
    core::Array<uint8_t> data;
};

struct IoRequestClose : public IoRequestBase
{
    IoRequestClose()
    {
        core::zero_this(this);
        type = IoRequest::CLOSE;
    }

    XFileAsync* pFile;
};

struct IoRequestRead : public IoRequestBase
{
    IoRequestRead()
    {
        core::zero_this(this);
        type = IoRequest::READ;
    }

    XFileAsync* pFile;
    void* pBuf;
    uint64_t offset;   // support files >4gb.
    uint32_t dataSize; // don't support reading >4gb at once.
};

struct IoRequestWrite : public IoRequestBase
{
    IoRequestWrite()
    {
        core::zero_this(this);
        type = IoRequest::WRITE;
    }

    XFileAsync* pFile;
    void* pBuf;
    uint64_t offset;   // support files >4gb.
    uint32_t dataSize; // don't support reading >4gb at once.
};

struct FindData
{
    X_DECLARE_FLAGS(AttrFlag)(
        DIRECTORY    
    );

    typedef Flags<AttrFlag> AttrFlags;

    AttrFlags attrib;
    int64_t size;
    core::Path<char> name;
};

typedef intptr_t findhandle;
static const findhandle INVALID_FIND_HANDLE = -1;

struct X_NO_DISCARD FindPair
{
    FindPair(findhandle handle, bool valid) :
        handle(handle),
        valid(valid)
    {
    }

    FindPair() :
        FindPair(INVALID_FIND_HANDLE, false)
    {
    }

    findhandle handle;
    bool valid;         // true is we opened a handle to the path.
};

struct IFileSys
{
    typedef FileFlag FileFlag;
    typedef Flags<FileFlag> FileFlags;
    typedef SeekMode SeekMode;
    typedef core::Path<char> PathT;
    typedef core::Path<wchar_t> PathWT;
    typedef FindData FindData;

    static const findhandle INVALID_FIND_HANDLE = INVALID_FIND_HANDLE;

    virtual ~IFileSys() = default;

    virtual void registerVars(void) X_ABSTRACT;
    virtual void registerCmds(void) X_ABSTRACT;

    virtual bool init(const CoreInitParams& params) X_ABSTRACT;
    virtual bool initWorker(void) X_ABSTRACT;
    virtual void shutDown(void) X_ABSTRACT;

    virtual bool getWorkingDirectory(core::Path<char>& pathOut) const X_ABSTRACT;
    // TODO: remove engine should only deal with utf-8
    virtual bool getWorkingDirectory(core::Path<wchar_t>& pathOut) const X_ABSTRACT;

    // folders - there is only one game dirtory.
    // but other folders can be added with 'addModDir' to add to the virtual directory.
    virtual bool setBaseDir(const PathWT& osPath) X_ABSTRACT;
    virtual bool setSaveDir(const PathWT& osPath) X_ABSTRACT;
    virtual bool addModDir(const PathWT& osPath) X_ABSTRACT;

    // Open Files
    virtual XFile* openFileOS(const PathWT& osPath, FileFlags mode) X_ABSTRACT;
    virtual XFile* openFileOS(const PathT& osPath, FileFlags mode) X_ABSTRACT;
    virtual XFile* openFile(const PathT& relPath, FileFlags mode, VirtualDirectory::Enum dir = VirtualDirectory::BASE) X_ABSTRACT;
    virtual void closeFile(XFile* file) X_ABSTRACT;

    // async
    virtual XFileAsync* openFileAsync(const PathT& relPath, FileFlags mode, VirtualDirectory::Enum dir = VirtualDirectory::BASE) X_ABSTRACT;
    virtual void closeFileAsync(XFileAsync* file) X_ABSTRACT;

    // loads the whole file into memory.
    virtual XFileMem* openFileMem(const PathT& relPath, FileFlags mode, VirtualDirectory::Enum dir = VirtualDirectory::BASE) X_ABSTRACT;
    virtual void closeFileMem(XFileMem* file) X_ABSTRACT;

    // Find util
    virtual FindPair findFirst(const PathT& path, FindData& findinfo) X_ABSTRACT;
    virtual FindPair findFirstOS(const PathWT& osPath, FindData& findinfo) X_ABSTRACT;
    virtual bool findNext(findhandle handle, FindData& findinfo) X_ABSTRACT;
    virtual void findClose(findhandle handle) X_ABSTRACT;

    // Delete
    virtual bool deleteFile(const PathT& relPath, VirtualDirectory::Enum dir) const X_ABSTRACT;
    virtual bool deleteDirectory(const PathT& relPath, VirtualDirectory::Enum dir, bool recursive) const X_ABSTRACT;
    virtual bool deleteDirectoryContents(const PathT& relPath, VirtualDirectory::Enum dir) X_ABSTRACT;

    // Create
    virtual bool createDirectory(const PathT& relPath, VirtualDirectory::Enum dir) const X_ABSTRACT;
    virtual bool createDirectoryOS(const PathWT& osPath) const X_ABSTRACT;
    virtual bool createDirectoryTree(const PathT& relPath, VirtualDirectory::Enum dir) const X_ABSTRACT;
    virtual bool createDirectoryTreeOS(const PathWT& osPath) const X_ABSTRACT;
    virtual bool createDirectoryTreeOS(const PathT& osPath) const X_ABSTRACT;

    // exists.
    virtual bool fileExists(const PathT& relPath) const X_ABSTRACT;
    virtual bool fileExists(const PathT& relPath, VirtualDirectory::Enum dir) const X_ABSTRACT;
    virtual bool fileExistsOS(const PathWT& osPath) const X_ABSTRACT;
    virtual bool fileExistsOS(const PathT& osPath) const X_ABSTRACT;
    virtual bool directoryExists(const PathT& relPath) const X_ABSTRACT;
    virtual bool directoryExists(const PathT& relPath, VirtualDirectory::Enum dir) const X_ABSTRACT;
    virtual bool directoryExistsOS(const PathWT& osPath) const X_ABSTRACT;

    // does not error, when it's a file or not exist.
    virtual bool isDirectory(const PathT& relPath, VirtualDirectory::Enum dir) const X_ABSTRACT;
    virtual bool isDirectoryOS(const PathWT& osPath) const X_ABSTRACT;

    // rename
    virtual bool moveFile(const PathT& relPath, const PathT& newPathRel, VirtualDirectory::Enum dir) const X_ABSTRACT;
    virtual bool moveFileOS(const PathWT& osPath, const PathWT& osPathNew) const X_ABSTRACT;

    // returns the min sector size for all virtual directories.
    // so if game folder is drive A and mod is drive B
    // yet drive B has a larger sector size, it will return the largest.
    virtual size_t getMinimumSectorSize(void) const X_ABSTRACT;

    virtual RequestHandle AddCloseRequestToQue(core::XFileAsync* pFile) X_ABSTRACT;
    virtual RequestHandle AddIoRequestToQue(IoRequestBase& request) X_ABSTRACT;

    virtual void waitForRequest(RequestHandle handle) X_ABSTRACT;
};

class XFileMemScoped
{
public:
    XFileMemScoped() :
        pFile_(nullptr)
    {
        X_ASSERT_NOT_NULL(gEnv);
        X_ASSERT_NOT_NULL(gEnv->pFileSys);
        pFileSys_ = gEnv->pFileSys;
    }

    ~XFileMemScoped()
    {
        close();
    }

    inline bool openFile(const IFileSys::PathT& path, IFileSys::FileFlags mode, VirtualDirectory::Enum dir = VirtualDirectory::BASE)
    {
        pFile_ = pFileSys_->openFileMem(path, mode, dir);
        return pFile_ != nullptr;
    }

    inline void close(void)
    {
        if (pFile_) {
            pFileSys_->closeFileMem(pFile_);
            pFile_ = nullptr;
        }
    }

    inline operator bool() const
    {
        return pFile_ != nullptr;
    }

    inline bool IsOpen(void) const
    {
        return pFile_ != nullptr;
    }

    inline size_t read(void* pBuf, size_t Len)
    {
        X_ASSERT_NOT_NULL(pFile_); // catch bad use of this class. "not checking open return val"
        return pFile_->read(pBuf, Len);
    }

    template<typename T>
    inline size_t read(T& object)
    {
        return read(&object, sizeof(T));
    }

    template<typename T>
    inline size_t readObj(T& object)
    {
        return read(&object, sizeof(T));
    }

    template<typename T>
    inline size_t readObjs(T* objects, size_t num)
    {
        return read(objects, sizeof(T) * num) / sizeof(T);
    }

    inline size_t readString(core::string& str)
    {
        return pFile_->readString(str);
    }

    inline size_t write(const void* pBuf, size_t Len)
    {
        X_ASSERT_NOT_NULL(pFile_);
        return pFile_->write(pBuf, Len);
    }

    inline size_t writeString(const core::string& str)
    {
        return pFile_->writeString(str);
    }
    inline size_t writeString(const char* str)
    {
        return pFile_->writeString(str);
    }
    inline size_t writeString(const char* str, size_t Length)
    {
        return pFile_->writeString(str, Length);
    }

    inline size_t writeStringNNT(const core::string& str)
    {
        return pFile_->writeStringNNT(str);
    }
    inline size_t writeStringNNT(const char* str)
    {
        return pFile_->writeStringNNT(str);
    }

    template<typename T>
    inline size_t writeObj(T& object)
    {
        return write(&object, sizeof(T));
    }
    template<>
    inline size_t writeObj(const core::string& str)
    {
        return writeString(str);
    }

    template<typename T>
    inline size_t writeObj(const T* objects, size_t num)
    {
        return write(objects, (sizeof(T) * num));
    }

    template<typename T>
    inline size_t writeObjs(T* objects, size_t num)
    {
        return write(objects, sizeof(T) * num) / sizeof(T);
    }

    template<typename T>
    inline size_t write(const T& object)
    {
        return write(&object, sizeof(T));
    }

    size_t printf(const char* fmt, ...)
    {
        char buf[2048];
        int32_t length;

        va_list argptr;

        va_start(argptr, fmt);
        length = vsnprintf_s(buf, 2048 - 1, fmt, argptr);
        va_end(argptr);

        if (length < 0) {
            return 0;
        }

        return write(buf, length);
    }

    inline void seek(int64_t position, SeekMode::Enum origin)
    {
        X_ASSERT_NOT_NULL(pFile_);
        pFile_->seek(position, origin);
    }

    inline uint64_t tell(void) const
    {
        X_ASSERT_NOT_NULL(pFile_);
        return pFile_->tell();
    }

    inline void setSize(int64_t numBytes)
    {
        X_ASSERT_NOT_NULL(pFile_);
        return pFile_->setSize(numBytes);
    }

    inline uint64_t remainingBytes(void) const
    {
        X_ASSERT_NOT_NULL(pFile_);
        return pFile_->remainingBytes();
    }

    inline XFileMem* GetFile(void) const
    {
        return pFile_;
    }

    X_INLINE XFileMem* operator->(void)
    {
        return pFile_;
    }
    X_INLINE const XFileMem* operator->(void)const
    {
        return pFile_;
    }
    X_INLINE operator XFileMem*(void)
    {
        return pFile_;
    }
    X_INLINE operator const XFileMem*(void)const
    {
        return pFile_;
    }

private:
    XFileMem* pFile_;
    IFileSys* pFileSys_;
};

// makes using the file more easy as you don't have to worrie about closing it.
class XFileScoped
{
public:
    XFileScoped() :
        pFile_(nullptr)
    {
        X_ASSERT_NOT_NULL(gEnv);
        X_ASSERT_NOT_NULL(gEnv->pFileSys);
        pFileSys_ = gEnv->pFileSys;
    }

    ~XFileScoped()
    {
        close();
    }

    inline bool openFile(const IFileSys::PathT& path, IFileSys::FileFlags mode, VirtualDirectory::Enum dir = VirtualDirectory::BASE)
    {
        X_ASSERT(pFile_ == nullptr, "File already open")();
        pFile_ = pFileSys_->openFile(path, mode, dir);
        return pFile_ != nullptr;
    }
    inline bool openFileOS(const IFileSys::PathT& path, IFileSys::FileFlags mode)
    {
        X_ASSERT(pFile_ == nullptr, "File already open")();
        pFile_ = pFileSys_->openFileOS(path, mode);
        return pFile_ != nullptr;
    }
    inline bool openFileOS(const IFileSys::PathWT& path, IFileSys::FileFlags mode)
    {
        X_ASSERT(pFile_ == nullptr, "File already open")();
        pFile_ = pFileSys_->openFileOS(path, mode);
        return pFile_ != nullptr;
    }

    inline void close(void)
    {
        if (pFile_) {
            pFileSys_->closeFile(pFile_);
            pFile_ = nullptr;
        }
    }

    inline operator bool() const
    {
        return pFile_ != nullptr;
    }

    inline bool IsOpen(void) const
    {
        return pFile_ != nullptr;
    }

    inline size_t read(void* pBuf, size_t Len)
    {
        X_ASSERT_NOT_NULL(pFile_); // catch bad use of this class. "not checking open return val"
        return pFile_->read(pBuf, Len);
    }

    template<typename T>
    inline size_t read(T& object)
    {
        return read(&object, sizeof(T));
    }

    template<typename T>
    inline size_t readObj(T& object)
    {
        return read(&object, sizeof(T));
    }

    template<typename T>
    inline size_t readObjs(T* objects, size_t num)
    {
        return read(objects, sizeof(T) * num) / sizeof(T);
    }

    inline size_t readString(core::string& str)
    {
        return pFile_->readString(str);
    }

    inline size_t write(const void* pBuf, size_t Len)
    {
        X_ASSERT_NOT_NULL(pFile_);
        return pFile_->write(pBuf, Len);
    }

    inline size_t writeString(const core::string& str)
    {
        return pFile_->writeString(str);
    }
    inline size_t writeString(const char* str)
    {
        return pFile_->writeString(str);
    }
    inline size_t writeString(const char* str, size_t Length)
    {
        return pFile_->writeString(str, Length);
    }

    inline size_t writeStringNNT(const core::string& str)
    {
        return pFile_->writeStringNNT(str);
    }
    inline size_t writeStringNNT(const char* str)
    {
        return pFile_->writeStringNNT(str);
    }

    template<typename T>
    inline size_t writeObj(T& object)
    {
        return write(&object, sizeof(T));
    }
    template<>
    inline size_t writeObj(const core::string& str)
    {
        return writeString(str);
    }

    template<typename T>
    inline size_t writeObj(const T* objects, size_t num)
    {
        return write(objects, (sizeof(T) * num));
    }

    template<typename T>
    inline size_t writeObjs(T* objects, size_t num)
    {
        return write(objects, sizeof(T) * num) / sizeof(T);
    }

    template<typename T>
    inline size_t write(const T& object)
    {
        return write(&object, sizeof(T));
    }

    size_t printf(const char* fmt, ...)
    {
        char buf[2048];
        int32_t length;

        va_list argptr;

        va_start(argptr, fmt);
        length = vsnprintf_s(buf, 2048 - 1, fmt, argptr);
        va_end(argptr);

        if (length < 0) {
            return 0;
        }

        return write(buf, length);
    }

    inline void seek(int64_t position, SeekMode::Enum origin)
    {
        X_ASSERT_NOT_NULL(pFile_);
        pFile_->seek(position, origin);
    }

    inline uint64_t tell(void) const
    {
        X_ASSERT_NOT_NULL(pFile_);
        return pFile_->tell();
    }

    inline void setSize(int64_t numBytes)
    {
        X_ASSERT_NOT_NULL(pFile_);
        return pFile_->setSize(numBytes);
    }

    inline uint64_t remainingBytes(void) const
    {
        X_ASSERT_NOT_NULL(pFile_);
        return pFile_->remainingBytes();
    }

    inline XFile* GetFile(void) const
    {
        return pFile_;
    }

private:
    XFile* pFile_;
    IFileSys* pFileSys_;
};

class FindFirstScoped
{
public:
    X_INLINE FindFirstScoped()
    {
        pFileSys_ = gEnv->pFileSys;
    }

    X_INLINE ~FindFirstScoped()
    {
        if (fp_.handle != core::IFileSys::INVALID_FIND_HANDLE) {
            pFileSys_->findClose(fp_.handle);
        }
    }

    X_INLINE bool findFirst(const IFileSys::PathT& path)
    {
        fp_ = pFileSys_->findFirst(path, fd_);
        return fp_.handle != core::IFileSys::INVALID_FIND_HANDLE;
    }

    X_INLINE bool findNext(void)
    {
        X_ASSERT(fp_.handle != core::IFileSys::INVALID_FIND_HANDLE, "handle is invalid")();
        return pFileSys_->findNext(fp_.handle, fd_);
    }

    X_INLINE core::IFileSys::FindData& fileData(void)
    {
        return fd_;
    }
    X_INLINE const core::IFileSys::FindData& fileData(void) const
    {
        return fd_;
    }

    X_INLINE operator bool() const
    {
        return fp_.handle != core::IFileSys::INVALID_FIND_HANDLE;
    }

private:
    core::IFileSys::FindData fd_;
    core::IFileSys* pFileSys_;
    FindPair fp_;
};

X_NAMESPACE_END

#endif // !_X_FILE_SYSTEM_I_H_
