#ifndef _MComponentDataIndexing
#define _MComponentDataIndexing
//-
// ==========================================================================
// Copyright (C) 2011 Autodesk, Inc., and/or its licensors.  All
// rights reserved.
//
// The coded instructions, statements, computer programs, and/or related
// material (collectively the "Data") in these files contain unpublished
// information proprietary to Autodesk, Inc. ("Autodesk") and/or its
// licensors,  which is protected by U.S. and Canadian federal copyright law
// and by international treaties.
//
// The Data may not be disclosed or distributed to third parties or be
// copied or duplicated, in whole or in part, without the prior written
// consent of Autodesk.
//
// The copyright notices in the Software and this entire statement,
// including the above license grant, this restriction and the following
// disclaimer, must be included in all copies of the Software, in whole
// or in part, and all derivative works of the Software, unless such copies
// or derivative works are solely in the form of machine-executable object
// code generated by a source language processor.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
// AUTODESK DOES NOT MAKE AND HEREBY DISCLAIMS ANY EXPRESS OR IMPLIED
// WARRANTIES INCLUDING, BUT NOT LIMITED TO, THE WARRANTIES OF
// NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE,
// OR ARISING FROM A COURSE OF DEALING, USAGE, OR TRADE PRACTICE. IN NO
// EVENT WILL AUTODESK AND/OR ITS LICENSORS BE LIABLE FOR ANY LOST
// REVENUES, DATA, OR PROFITS, OR SPECIAL, DIRECT, INDIRECT, OR
// CONSEQUENTIAL DAMAGES, EVEN IF AUTODESK AND/OR ITS LICENSORS HAS
// BEEN ADVISED OF THE POSSIBILITY OR PROBABILITY OF SUCH DAMAGES.
// ==========================================================================
//+
//
// CLASS: MComponentDataIndexing
//
// ****************************************************************************


#if defined __cplusplus

// ****************************************************************************
// INCLUDED HEADER FILES

#include <maya/MStatus.h>
#include <maya/MHWGeometry.h>
#include <maya/MDagPath.h>
#include <maya/MViewport2Renderer.h>
#include <maya/MUintArray.h>

// ****************************************************************************
// DECLARATIONS

class MString;
class MUserData;

// ****************************************************************************
// NAMESPACE

namespace MHWRender
{

// ****************************************************************************
// CLASS DECLARATION (MComponentDataIndexing)

//! \ingroup OpenMayaRender
//! \brief Class for storing index mapping when vertices are shared
/*!

*/
class OPENMAYARENDER_EXPORT MComponentDataIndexing
{
public:

    enum MComponentType
    {
        kFaceVertex = 0,
    };

    MUintArray& indices();
    const MUintArray& indices() const;

    const MComponentType componentType() const;
    void setComponentType(MComponentType type);

private:
    MComponentDataIndexing(MUintArray& indices, MComponentType type=kFaceVertex);
    MComponentDataIndexing(const MComponentDataIndexing&);
    ~MComponentDataIndexing();

    MComponentDataIndexing& operator=(const MComponentDataIndexing&);

    MUintArray& fIndices;
    MComponentType fComponentType;

    friend class MComponentDataIndexingList;
};

// ****************************************************************************
// CLASS DECLARATION (MIndexBufferDescriptorList)
//! \ingroup OpenMayaRender
//! \brief A list of MIndexBufferDescriptor objects
/*!
A list of MIndexBufferDescriptor objects.
*/
class OPENMAYARENDER_EXPORT MComponentDataIndexingList
{
public:
    MComponentDataIndexingList();
    ~MComponentDataIndexingList();

    int	length() const;
    const MComponentDataIndexing* operator[]( int index ) const;

    bool append(const MComponentDataIndexing& desc);
    bool removeAt(int index);
    void clear();

    static const char* className();

private:
    MComponentDataIndexingList(const MComponentDataIndexingList&) {}
    MComponentDataIndexingList& operator=(const MComponentDataIndexingList&) { return *this; }

    void* fData;
};

} // namespace MHWRender

#endif /* __cplusplus */
#endif /* _MComponentDataIndexing */
