
X_NAMESPACE_BEGIN(texture)

X_INLINE const core::string& Texture::getName(void) const
{
    return name_;
}

X_INLINE Vec2i Texture::getDimensions(void) const
{
    return Vec2i(dimensions_.x, dimensions_.y);
}

X_INLINE int32_t Texture::getWidth(void) const
{
    return dimensions_.x;
}

X_INLINE int32_t Texture::getHeight(void) const
{
    return dimensions_.y;
}

X_INLINE int32_t Texture::getNumFaces(void) const
{
    return numFaces_;
}

X_INLINE int32_t Texture::getDepth(void) const
{
    return depth_;
}

X_INLINE int32_t Texture::getNumMips(void) const
{
    return static_cast<int32_t>(numMips_);
}

X_INLINE int32_t Texture::getDataSize(void) const
{
    return 0;
}

X_INLINE TextureType::Enum Texture::getTextureType(void) const
{
    return type_;
}

X_INLINE Texturefmt::Enum Texture::getFormat(void) const
{
    return format_;
}

X_INLINE render::BufUsage::Enum Texture::getUsage(void) const
{
    return usage_;
}

// IPixelBuffer
X_INLINE render::PixelBufferType::Enum Texture::getBufferType(void) const
{
    return pixelBufType_;
}

// ~IPixelBuffer

X_INLINE render::ColorBuffer& Texture::getColorBuf(void) const
{
    X_ASSERT(pixelBufType_ == render::PixelBufferType::COLOR, "Not a color pixel buffer")(pixelBufType_);
    return *pColorBuf_;
}

X_INLINE render::DepthBuffer& Texture::getDepthBuf(void) const
{
    X_ASSERT(pixelBufType_ == render::PixelBufferType::DEPTH, "Not a depth pixel buffer")(pixelBufType_);
    return *pDepthBuf_;
}

X_INLINE render::ShadowBuffer& Texture::getShadowBuf(void) const
{
    X_ASSERT(pixelBufType_ == render::PixelBufferType::SHADOW, "Not a shadow pixel buffer")(pixelBufType_);
    return *pShadowBuf_;
}

X_INLINE render::GpuResource& Texture::getGpuResource(void)
{
    return resource_;
}

X_INLINE const D3D12_CPU_DESCRIPTOR_HANDLE& Texture::getSRV(void) const
{
    return hCpuDescriptorHandle_;
}

X_INLINE const int32_t Texture::getID(void) const
{
    return id_;
}

X_INLINE void Texture::setID(int32_t id)
{
    id_ = id;
}

X_INLINE void Texture::setSRV(D3D12_CPU_DESCRIPTOR_HANDLE& srv)
{
    hCpuDescriptorHandle_ = srv;
}

X_INLINE void Texture::setFormat(Texturefmt::Enum fmt)
{
    format_ = fmt;
}

X_INLINE void Texture::setType(TextureType::Enum type)
{
    type_ = type;
}

X_INLINE void Texture::setUsage(render::BufUsage::Enum usage)
{
    usage_ = usage;
}

X_INLINE void Texture::setWidth(uint16_t width)
{
    dimensions_.x = width;
}

X_INLINE void Texture::setHeight(uint16_t height)
{
    dimensions_.y = height;
}

X_INLINE void Texture::setDepth(uint8_t depth)
{
    depth_ = depth;
}

X_INLINE void Texture::setNumFaces(uint8_t faces)
{
    numFaces_ = faces;
}

X_INLINE void Texture::setNumMips(uint8_t mips)
{
    numMips_ = mips;
}

X_NAMESPACE_END