#pragma once

#include <ITexture.h>

#include "GpuResource.h"

X_NAMESPACE_DECLARE(render,
                    class DescriptorAllocator;
                    class PixelBuffer;
                    class ColorBuffer;
                    class DepthBuffer;
                    class ShadowBuffer);

X_NAMESPACE_BEGIN(texture)

class TextureManager;

// we don't align this type, the allocator aligns it.
// since we inherit this type.

// not using core::BaseAsset currently but it can work with it.
// as this texture has like no disk activity etc.
class Texture : public render::IPixelBuffer // ::texture::ITexture
{
    friend TextureManager;
    friend class render::PixelBuffer;
    friend class render::ColorBuffer;
    friend class render::DepthBuffer;
    friend class render::ShadowBuffer;

public:
    Texture(core::string_view name);
    ~Texture();

    void destroy(void);

    // temp maybe
    X_INLINE const TexID getTexID(void) const X_FINAL
    {
        return id_;
    };

    X_INLINE const core::string& getName(void) const X_FINAL;
    X_INLINE Vec2i getDimensions(void) const X_FINAL;
    X_INLINE int32_t getWidth(void) const X_FINAL;
    X_INLINE int32_t getHeight(void) const X_FINAL;
    X_INLINE int32_t getNumFaces(void) const X_FINAL;
    X_INLINE int32_t getDepth(void) const X_FINAL;
    X_INLINE int32_t getNumMips(void) const X_FINAL;
    X_INLINE int32_t getDataSize(void) const X_FINAL;

    X_INLINE TextureType::Enum getTextureType(void) const X_FINAL;
    X_INLINE Texturefmt::Enum getFormat(void) const X_FINAL;
    X_INLINE render::BufUsage::Enum getUsage(void) const;

    DXGI_FORMAT getFormatDX(void) const;

    void setProperties(const XTextureFile& imgFile) X_FINAL;

    // IPixelBuffer
    X_INLINE render::PixelBufferType::Enum getBufferType(void) const X_FINAL;
    // ~IPixelBuffer

    X_INLINE const D3D12_CPU_DESCRIPTOR_HANDLE& getSRV(void) const;

    X_INLINE const int32_t getID(void) const;
    X_INLINE void setID(int32_t id);

    X_INLINE render::GpuResource& getGpuResource(void);

    // pixel buffers.
    X_INLINE render::PixelBuffer& getPixelBuf(void) const;
    X_INLINE render::ColorBuffer& getColorBuf(void) const;
    X_INLINE render::DepthBuffer& getDepthBuf(void) const;
    X_INLINE render::ShadowBuffer& getShadowBuf(void) const;

protected:
    X_INLINE void setSRV(D3D12_CPU_DESCRIPTOR_HANDLE& srv);

    X_INLINE void setFormat(Texturefmt::Enum fmt);
    X_INLINE void setType(TextureType::Enum type);
    X_INLINE void setUsage(render::BufUsage::Enum usage);
    X_INLINE void setWidth(uint16_t width);
    X_INLINE void setHeight(uint16_t height);
    X_INLINE void setDepth(uint8_t depth);
    X_INLINE void setNumFaces(uint8_t faces);
    X_INLINE void setNumMips(uint8_t mips);

    void setPixelBuffer(render::PixelBufferType::Enum type, render::PixelBuffer* pInst);

private:
    render::GpuResource resource_;

protected:
    D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescriptorHandle_; // SRV

protected:
    core::string name_;
    TexID id_;
    Vec2<uint16_t> dimensions_;
    uint32_t datasize_; // size of the higest mip in bytes.
    TextureType::Enum type_;
    Texturefmt::Enum format_;
    uint8_t numMips_;
    uint8_t depth_;
    uint8_t numFaces_;
    uint8_t _pad[7];

    render::BufUsage::Enum usage_;
    render::PixelBufferType::Enum pixelBufType_;
    union
    {
        render::ColorBuffer* pColorBuf_;
        render::DepthBuffer* pDepthBuf_;
        render::ShadowBuffer* pShadowBuf_;
        render::PixelBuffer* pPixelBuffer_;
    };
};

X_ENSURE_LE(sizeof(Texture), 96, "Try keep texture <= 96 bytes plz!")


X_NAMESPACE_END

#include "Texture.inl"