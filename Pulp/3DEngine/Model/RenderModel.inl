
X_NAMESPACE_BEGIN(model)

X_INLINE const XRenderMesh& RenderModel::getLodRenderMesh(size_t idx) const
{
    X_ASSERT(idx < static_cast<size_t>(getNumLods()), "invalid lod index")(getNumLods(), idx);
    return renderMeshes_[idx];
}

X_NAMESPACE_END
