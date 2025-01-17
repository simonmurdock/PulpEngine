#pragma once

X_NAMESPACE_BEGIN(sound)

X_INLINE const AkVector& Vec3ToAkVector(const Vec3f& vec)
{
    return reinterpret_cast<const AkVector&>(vec);
}

X_INLINE AkSoundPosition TransToAkPos(const Transformf& trans)
{
    AkSoundPosition pos;
    // Wwise is left hand XYZ
    //  X: Vector that points to the right
    //  Y: Vector that points up
    //  Z: Vector that points forward.
    pos.SetPosition(trans.pos.x, trans.pos.z, trans.pos.y);

    // TODO: dunno what this is axis? TELL ME.
    pos.SetOrientation(
        0.f, 1.f, 0.f,
        0.f, 0.f, 1.f
    );

    return pos;
}

X_INLINE AkTimeMs ToAkTime(core::TimeVal time)
{
    return safe_static_cast<AkTimeMs>(time.GetMilliSecondsAsInt64());
}

X_INLINE AkCurveInterpolation ToAkCurveInterpolation(CurveInterpolation::Enum interpol)
{
    static_assert(AkCurveInterpolation_Log3 == CurveInterpolation::Log3, "!");
    static_assert(AkCurveInterpolation_Sine == CurveInterpolation::Sine, "!");
    static_assert(AkCurveInterpolation_Log1 == CurveInterpolation::Log1, "!");
    static_assert(AkCurveInterpolation_InvSCurve == CurveInterpolation::InvSCurve, "!");
    static_assert(AkCurveInterpolation_Linear == CurveInterpolation::Linear, "!");
    static_assert(AkCurveInterpolation_SCurve == CurveInterpolation::SCurve, "!");
    static_assert(AkCurveInterpolation_Exp1 == CurveInterpolation::Exp1, "!");
    static_assert(AkCurveInterpolation_SineRecip == CurveInterpolation::SineRecip, "!");
    // static_assert(AkCurveInterpolation_Exp3 == CurveInterpolation::Exp3, "!");
    static_assert(AkCurveInterpolation_LastFadeCurve == CurveInterpolation::LastFadeCurve, "!");
    static_assert(AkCurveInterpolation_Constant == CurveInterpolation::Constant, "!");

    return static_cast<AkCurveInterpolation>(interpol);
}

X_NAMESPACE_END