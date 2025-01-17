#pragma once

#include <maya\MColor.h>
#include <maya\MFloatVector.h>
#include <maya\MFloatPoint.h>
#include <maya\MMatrix.h>
#include <maya\MDistance.h>

namespace MayaUtil
{
    void SetStartOfBlock(bool start);
    void SetVerbose(bool verbose);
    bool IsVerbose(void);

    void MayaPrintError(const char* fmt, ...);
    void MayaPrintWarning(const char* fmt, ...);
    void MayaPrintMsg(const char* fmt, ...);
    void MayaPrintVerbose(const char* fmt, ...);

    void PrintMatrix(const char* pName, const Matrix33f& mat);
    void PrintMatrix(const char* pName, const MMatrix& mat);

    void SetProgressCtrl(const MString& str);
    MStatus ShowProgressDlg(int32_t progressMin, int32_t progressMax);
    MStatus SetProgressRange(int32_t progressMin, int32_t progressMax);
    MStatus IncProcess(void);
    MStatus HideProgressDlg(void);
    void SetProgressText(const MString& str, bool advProgress = true);

    MString RemoveNameSpace(const MString& str);

    const char* GetMDistinceUnitStr(MDistance::Unit unit);

    X_INLINE ::std::ostream& operator<<(::std::ostream& os, const Vec3f& bar)
    {
        return os << "(" << bar.x << ", " << bar.y << ", " << bar.z << ")";
    }

    X_INLINE Matrix33f ConvertToGameSpace(const Matrix33f& m)
    {
        Matrix33f mat;

        mat.m00 = m.m00;
        mat.m01 = m.m02;
        mat.m02 = m.m01;

        mat.m10 = m.m10;
        mat.m11 = m.m12;
        mat.m12 = m.m11;

        mat.m20 = m.m20;
        mat.m21 = m.m22;
        mat.m22 = m.m21;
        return mat;
    }

    X_INLINE Vec3f XVec(const MFloatVector& point)
    {
        return Vec3f(point[0], point[1], point[2]);
    }

    X_INLINE Vec3f XVec(const MFloatPoint& point)
    {
        return Vec3f(point[0], point[1], point[2]);
    }

    X_INLINE Color XVec(const MColor& col)
    {
        return Color(col[0], col[1], col[2], col[3]);
    }

    template<typename T = float>
    X_INLINE Vec3<T> XVec(const MMatrix& matrix)
    {
        return Vec3<T>((T)matrix[3][0], (T)matrix[3][1], (T)matrix[3][2]);
    }

    X_INLINE Matrix33f XMat(const MMatrix& matrix)
    {
        Matrix33f mat;

        for (int32_t j = 0; j < 3; j++) {
            for (int32_t k = 0; k < 3; k++) {
                mat.at(j, k) = static_cast<float>(matrix(j, k));
            }
        }

        return mat;
    }

} // namespace MayaUtil