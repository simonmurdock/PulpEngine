#ifndef _MPxGlBuffer
#define _MPxGlBuffer
//-
// ==========================================================================
// Copyright (C) 1995 - 2006 Autodesk, Inc., and/or its licensors.  All
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
// CLASS:    MPxGlBuffer
//
// ****************************************************************************
//
// CLASS DESCRIPTION (MPxGlBuffer)
//
//  MPxGlBuffer allows the user to create OpenGL buffers that Maya
//	can draw into.  The base class as is defined will create a hardware
//	accellerated off-screen buffer.
//
//  To create a custom buffer, derive from this class and override the
//  beginBufferNotify and endBufferNotify methods.
//
// ****************************************************************************

#if defined __cplusplus

// ****************************************************************************
// INCLUDED HEADER FILES


#include <maya/MStatus.h>
#include <maya/MTypes.h>

#if defined(__unix) || defined(LINUX)
#include <GL/glx.h>
#endif

// ****************************************************************************
// DECLARATIONS

class MString;
class M3dView;

// ****************************************************************************
// CLASS DECLARATION (MPxGlBuffer)

//! \ingroup OpenMayaUI MPx
//! \brief \obsolete
/*!
\deprecated
Use MHWRender::MRenderOverride and MHWRender::MRenderTarget instead.

Historically this class was used to created offscreen buffers on
Linux.  Users should refrain from using this class in the traditional
pbuffer use case outlined in the deprecated section below as results
may be undefined.  Instead users should not override any of the
function call below and simply invoke openFbo() method. The contents
of the frame buffer object (FBO) can be read back by using the bindFbo()
method and OpenGl calls to read pixels. After rendering and reading
pixels, the frame buffer object can be destroyed by calling
closeFbo(). The blastCmd API example has been updated to illustrate
how to render offscreen.

Note, by using a frame buffer object it is now possible to utilize
this class in on non-Linux platforms.

Using this class in pbuffer or externally defined window is DEPRECATED. 
*/
class OPENMAYAUI_EXPORT MPxGlBuffer
{
public:
	MPxGlBuffer();
	MPxGlBuffer( M3dView &view );
	virtual ~MPxGlBuffer();

	MStatus		openFbo( short width, short height, M3dView & );
	MStatus		closeFbo( M3dView & ); 
	MStatus		bindFbo(); 
	MStatus		unbindFbo(); 
	
#if defined(__unix) || defined(LINUX)
	virtual	MStatus			open( short width, short height,
								  GLXContext shareCtx = NULL );
	virtual GLXDrawable	    drawable( MStatus * ReturnStatus = NULL );
	virtual	GLXContext      context( MStatus * ReturnStatus = NULL );
	virtual	Display *       display( MStatus * ReturnStatus = NULL );
	virtual XVisualInfo *   visual( MStatus * ReturnStatus = NULL );
	virtual int *   		attributeList( MStatus * ReturnStatus = NULL );

	virtual MStatus         setUseExternalDrawable( bool state );
	virtual MStatus         setDoubleBuffer( bool state );
	virtual MStatus         setDisplay( Display * disp );
	virtual MStatus         setDrawable( GLXDrawable drawable );
	virtual MStatus         setVisual( XVisualInfo * vis );

#endif // __unix

	virtual void			beginBufferNotify( );
	virtual void			endBufferNotify( );
	virtual	MStatus			close();

	static	const char*		className();

protected:
	bool					hasColorIndex;
	bool					hasAlphaBuffer;
	bool					hasDepthBuffer;
	bool					hasAccumulationBuffer;
	bool					hasDoubleBuffer;
	bool					hasStencilBuffer;

private:
	void   setData( void* );
	void * 	data;

};

#endif /* __cplusplus */
#endif /* _MPxGlBuffer */
