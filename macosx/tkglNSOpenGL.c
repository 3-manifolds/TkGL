/*
 * tkglNSOpenGL.c --
 *
 * Copyright (C) 2024, Marc Culler, Nathan Dunfield, Matthias Goerner
 *
 * This file is part of the TkGL project.  TkGL is derived from Togl, which
 * was written by Brian Paul, Ben Bederson and Greg Couch.  TkGL is licensed
 * under the Tcl license.  The terms of the license are described in the file
 * "license.terms" which should be included with this distribution.
 */

#include "tkgl.h"
#include <OpenGL/glext.h>
#include <OpenGL/gl.h>

#include <Foundation/Foundation.h>   /* for NSRect */
#include <OpenGL/OpenGL.h>
#include <AppKit/NSOpenGL.h>
#include <AppKit/NSView.h>
#include <tkMacOSXInt.h>              /* for MacDrawable */
#include <ApplicationServices/ApplicationServices.h>
#define Tkgl_MacOSXGetDrawablePort(tkgl) \
    TkMacOSXGetDrawablePort((Drawable) ((TkWindow *) tkgl->TkWin)->privatePtr)

static NSOpenGLPixelFormat *
tkgl_pixelFormat(Tkgl *tkgl)
{
    NSOpenGLPixelFormatAttribute   attribs[32];
    int     na = 0;
    NSOpenGLPixelFormat *pix;

#if 0
    if (tkgl->multisampleFlag && !hasMultisampling) {
        Tcl_SetResult(tkgl->interp,
                "multisampling not supported", TCL_STATIC);
        return NULL;
    }
#endif

    if (tkgl->pBufferFlag && !tkgl->rgbaFlag) {
        Tcl_SetResult(tkgl->interp,
                "puffer must be RGB[A]", TCL_STATIC);
        return NULL;
    }

    attribs[na++] = NSOpenGLPFAMinimumPolicy;
    /* ask for hardware-accelerated onscreen */
    /* This is not needed, and can break virtual machines.
       Accelerated rendering is always preferred.
    attribs[na++] = NSOpenGLPFAAccelerated;
    attribs[na++] = NSOpenGLPFANoRecovery;
    */
    if (tkgl->rgbaFlag) {
        /* RGB[A] mode */
        attribs[na++] = NSOpenGLPFAColorSize;
	attribs[na++] = tkgl->rgbaRed + tkgl->rgbaGreen + tkgl->rgbaBlue;
	/* NSOpenGL does not take separate red,green,blue sizes. */
        if (tkgl->alphaFlag) {
            attribs[na++] = NSOpenGLPFAAlphaSize;
            attribs[na++] = tkgl->alphaSize;
        }
    } else {
        /* Color index mode */
        Tcl_SetResult(tkgl->interp,
                "Color index mode not supported", TCL_STATIC);
        return NULL;
    }
    if (tkgl->depthFlag) {
        attribs[na++] = NSOpenGLPFADepthSize;
        attribs[na++] = tkgl->depthSize;
    }
    if (tkgl->doubleFlag) {
        attribs[na++] = NSOpenGLPFADoubleBuffer;
    }
    if (tkgl->stencilFlag) {
        attribs[na++] = NSOpenGLPFAStencilSize;
        attribs[na++] = tkgl->stencilSize;
    }
    if (tkgl->accumFlag) {
        attribs[na++] = NSOpenGLPFAAccumSize;
        attribs[na++] = (tkgl->accumRed + tkgl->accumGreen + tkgl->accumBlue
			 + (tkgl->alphaFlag ? tkgl->accumAlpha : 0));
    }
    if (tkgl->multisampleFlag) {
        attribs[na++] = NSOpenGLPFAMultisample;
        attribs[na++] = NSOpenGLPFASampleBuffers;
        attribs[na++] = 1;
        attribs[na++] = NSOpenGLPFASamples;
        attribs[na++] = 2;
    }
    if (tkgl->auxNumber != 0) {
        attribs[na++] = NSOpenGLPFAAuxBuffers;
        attribs[na++] = tkgl->auxNumber;
    }
    if (tkgl->stereo == TKGL_STEREO_NATIVE) {
        attribs[na++] = NSOpenGLPFAStereo;
    }
    if (tkgl->fullscreenFlag) {
        Tcl_SetResult(tkgl->interp,
                "FullScreen mode not supported.", TCL_STATIC);
        return NULL;
    }
    switch(tkgl->profile) {
    case PROFILE_LEGACY:
	attribs[na++] = NSOpenGLPFAOpenGLProfile;
	attribs[na++] = NSOpenGLProfileVersionLegacy;
	break;
    case PROFILE_3_2:
	attribs[na++] = NSOpenGLPFAOpenGLProfile;
	attribs[na++] = NSOpenGLProfileVersion3_2Core;
	break;
    case PROFILE_SYSTEM:
    case PROFILE_4_1:
	attribs[na++] = NSOpenGLPFAOpenGLProfile;
	attribs[na++] = NSOpenGLProfileVersion4_1Core;
	break;
    }
    attribs[na++] = 0;	/* End of attributes. */

    pix = [[NSOpenGLPixelFormat alloc] initWithAttributes:attribs];
    if (pix == nil) {
        Tcl_SetResult(tkgl->interp, "couldn't choose pixel format",
                TCL_STATIC);
        return NULL;
    }
    return pix;
}

static int
tkgl_describePixelFormat(Tkgl *tkgl)
{
    NSOpenGLPixelFormat *pfmt = tkgl->pixelFormat;

    /* fill in RgbaFlag, DoubleFlag, and Stereo */
    GLint   has_rgba, has_doublebuf, has_depth, has_accum, has_alpha,
            has_stencil, has_stereo, has_multisample;

    GLint   vscr = 0;
    [pfmt getValues:&has_rgba forAttribute:NSOpenGLPFAColorSize forVirtualScreen:vscr];
    [pfmt getValues:&has_doublebuf forAttribute:NSOpenGLPFADoubleBuffer forVirtualScreen:vscr];
    [pfmt getValues:&has_depth forAttribute:NSOpenGLPFADepthSize forVirtualScreen:vscr];
    [pfmt getValues:&has_accum forAttribute:NSOpenGLPFAAccumSize forVirtualScreen:vscr];
    [pfmt getValues:&has_alpha forAttribute:NSOpenGLPFAAlphaSize forVirtualScreen:vscr];
    [pfmt getValues:&has_stencil forAttribute:NSOpenGLPFAStencilSize forVirtualScreen:vscr];
    [pfmt getValues:&has_stereo forAttribute:NSOpenGLPFAStereo forVirtualScreen:vscr];
    [pfmt getValues:&has_multisample forAttribute:NSOpenGLPFASampleBuffers forVirtualScreen:vscr];

    tkgl->rgbaFlag = (has_rgba != 0);
    tkgl->doubleFlag = (has_doublebuf != 0);
    tkgl->depthFlag = (has_depth != 0);
    tkgl->accumFlag = (has_accum != 0);
    tkgl->alphaFlag = (has_alpha != 0);
    tkgl->stencilFlag = (has_stencil != 0);
    tkgl->stereo = (has_stereo ? TKGL_STEREO_NATIVE : TKGL_STEREO_NONE);
    tkgl->multisampleFlag = (has_multisample != 0);
    return True;
}

#define isPow2(x) (((x) & ((x) - 1)) == 0)

static NSOpenGLPixelBuffer *
tkgl_createPbuffer(Tkgl *tkgl)
{
    GLint   min_size[2], max_size[2];
    Bool    hasPbuffer;
    const char *extensions;
    GLint   target;
    GLint   virtualScreen;
    NSOpenGLPixelBuffer *pbuf;

    extensions = (const char *) glGetString(GL_EXTENSIONS);
    hasPbuffer = (strstr(extensions, "GL_APPLE_pixel_buffer") != NULL);
    if (!hasPbuffer) {
        Tcl_SetResult(tkgl->interp,
                "pbuffers are not supported", TCL_STATIC);
        return NULL;
    }
    glGetIntegerv(GL_MIN_PBUFFER_VIEWPORT_DIMS_APPLE, min_size);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, max_size);
    virtualScreen = [tkgl->context currentVirtualScreen];
    for (;;) {
        /* make sure we don't exceed the maximum size because if we do,
         * NSOpenGLPixelBuffer allocationmay succeed and later uses of
	 * the pbuffer fail
	 */
        if (tkgl->width < min_size[0])
            tkgl->width = min_size[0];
        else if (tkgl->width > max_size[0]) {
            if (tkgl->largestPbufferFlag)
                tkgl->width = max_size[0];
            else {
                Tcl_SetResult(tkgl->interp,
                        "pbuffer too large", TCL_STATIC);
                return NULL;
            }
        }
        if (tkgl->height < min_size[1])
            tkgl->height = min_size[1];
        else if (tkgl->height > max_size[1]) {
            if (tkgl->largestPbufferFlag)
                tkgl->height = max_size[1];
            else {
                Tcl_SetResult(tkgl->interp,
                        "pbuffer too large", TCL_STATIC);
                return NULL;
            }
        }

        if (isPow2(tkgl->width) && isPow2(tkgl->height))
            target = GL_TEXTURE_2D;
        else
            target = GL_TEXTURE_RECTANGLE_ARB;

	pbuf = [[NSOpenGLPixelBuffer alloc] initWithTextureTarget:target
		textureInternalFormat:(tkgl->alphaFlag ? GL_RGBA : GL_RGB)
		textureMaxMipMapLevel:0
		pixelsWide:tkgl->width pixelsHigh:tkgl->height];
        if (pbuf != nil) {
            /* setPixelBuffer allocates the framebuffer space */
	  [tkgl->context setPixelBuffer:pbuf cubeMapFace:0 mipMapLevel:0
	   currentVirtualScreen:virtualScreen];
	  return pbuf;
	}
        if (!tkgl->largestPbufferFlag
                || tkgl->width == min_size[0] || tkgl->height == min_size[1]) {
            Tcl_SetResult(tkgl->interp,
                    "unable to create pbuffer", TCL_STATIC);
            return NULL;
        }
        /* largest unavailable, try something smaller */
        tkgl->width = tkgl->width / 2 + tkgl->width % 2;
        tkgl->height = tkgl->width / 2 + tkgl->height % 2;
    }
}

void
tkgl_destroyPbuffer(Tkgl *tkgl)
{
    [tkgl->pbuf release];
}

/* Declarations that Apple leaves out of gl.h */
extern const GLubyte *glGetStringi(GLenum name, GLuint index);
extern void glGetIntegerv(GLenum pname, GLint  *params);
#define GL_NUM_EXTENSIONS 0x821D

const char* Tkgl_GetExtensions(
    Tkgl *tkglPtr)
{
    char *buffer = NULL;
    int bufsize = 0;
    int strsize = 0;
    int num;

    if (tkglPtr->profile == PROFILE_LEGACY) {
	return (const char *)glGetString(GL_EXTENSIONS);
    }
    if (tkglPtr->extensions) {
	return tkglPtr->extensions;
    }
    glGetIntegerv(GL_NUM_EXTENSIONS, &num);
    buffer = ckalloc(1536);
    if (!buffer) {
	return NULL;
    }
    bufsize = 1536;
    for (int i = 0; i < num; i++) {
	char *ext = (char *)glGetStringi(GL_EXTENSIONS, i);
	int len = strlen(ext);
	if (strsize + len > bufsize) {
	    buffer = ckrealloc(buffer, len + 2*bufsize);
	    if (buffer == 0) {
		strsize = bufsize = 0;
		return NULL;
	    }
	    bufsize = len + 2*bufsize;
	}
	strsize += strlcpy(buffer + strsize, ext, bufsize);
	strsize += strlcpy(buffer + strsize, " ", bufsize);
    }
    buffer[strsize - 1] = '\0';
    tkglPtr->extensions = buffer;
    return tkglPtr->extensions;
}

/*
 *  Tkgl_Update
 *
 *    Called by TkglDisplay.  On macOS this sets the size of the NSView being
 *    used as the OpenGL drawing surface.  Also, if the widget's NSView has
 *    not been assigned to its NSOpenGLContext, that will be done here.  This
 *    step is not needed on other platforms, where the surface is managed by
 *    the window.
 */

void Tkgl_Update(const Tkgl *tkglPtr)
{
    // The coordinates of the frame of an NSView are in points, but the
    // coordinates of the bounds of an NSView managed by an
    // NSOpenGLContext are in pixels.  (There are 2.0 pixels per point on
    // a retina display.)  Coordinates can be converted with
    // [NSView convertRectToBacking:(NSRect)rect];

    NSRect newFrame;
    TkWindow *widget = (TkWindow *) tkglPtr->tkwin;
    NSView *contentView = Tk_MacOSXGetNSViewForDrawable((Drawable)widget->privatePtr);

    if (tkglPtr->context && [tkglPtr->context view] != tkglPtr->nsview) {
	[tkglPtr->context setView:tkglPtr->nsview];
    }

    newFrame.size.width = Tk_Width(widget);
    newFrame.size.height = Tk_Height(widget);
    newFrame.origin.x = widget->privatePtr->xOff;
    newFrame.origin.y = ([contentView bounds].size.height -
			 newFrame.size.height -
			 widget->privatePtr->yOff);

    if (!NSEqualRects(newFrame, [tkglPtr->nsview frame])) {
	[tkglPtr->nsview setFrame:newFrame];
    }
    [tkglPtr->context update];
}

/*
 *  Tkgl_CreateGLContext
 *
 *  Creates an NSOpenGLContext and assigns it to tkglPtr->context.
 *  The pixelFormat index is saved ins TkglPtr->pixelFormat.  Also
 *  creates and NSView to serve as the rendering surface.  The NSView
 *  is assigned as a subview and occupies the rectangle in the content
 *  view which is assigned as to the Tkgl widget.
 *
 *  Returns a standard Tcl result.
 */

int
Tkgl_CreateGLContext(Tkgl *tkglPtr)
{
    if (tkglPtr->context) {
	return TCL_OK;
    }
    //// FIX ME - the sharing does not make sense.
    // shareList and shareContext are supposed to be mutually exclusive.
    if (tkglPtr->shareList) {
        /* We will share the display lists with an existing tkgl widget. */
        Tkgl *shareWith = FindTkgl(tkglPtr, tkglPtr->shareList);
        if (shareWith) {
	    tkglPtr->pixelFormat = shareWith->pixelFormat;
            tkglPtr->context = shareWith->context;
            tkglPtr->contextTag = shareWith->contextTag;
        } else {
	    Tcl_SetResult(tkglPtr->interp,
		"Invalid widget specified in the sharelist option.",
		TCL_STATIC);
	  return TCL_ERROR;
	}
    } else if (tkglPtr->shareContext) {
        /* We will share the OpenGL context of an existing Tkgl widget. */
        Tkgl *shareWith = FindTkgl(tkglPtr, tkglPtr->shareContext);
	if (shareWith == NULL) {
	    Tcl_SetResult(tkglPtr->interp,
		"Invalid widget specified in the sharecontext option.",
		TCL_STATIC);
	  return TCL_ERROR;
	}
	tkglPtr->pixelFormat = shareWith->pixelFormat;
	tkglPtr->context = [[NSOpenGLContext alloc]
	    initWithCGLContextObj: (CGLContextObj) shareWith->context];
    } else {
	tkglPtr->context = [NSOpenGLContext alloc];
	tkglPtr->pixelFormat = tkgl_pixelFormat(tkglPtr);
	[tkglPtr->context initWithFormat:tkglPtr->pixelFormat
			    shareContext:nil];
	if (tkglPtr->context== nil){
	    [tkglPtr->pixelFormat release];
	    tkglPtr->pixelFormat = nil;
	    Tcl_SetResult(tkglPtr->interp,
		"Could not create OpenGL context", TCL_STATIC);
	    return TCL_ERROR;
	}
	// Make the new context current.  This ensures that there is
	// always a current context whenever a Tkgl exists, so GL
	// calls made before mapping the widget will not crash.
	[tkglPtr->context makeCurrentContext];
    }
    return TCL_OK;
}

/*
 * Tkgl_MakeWindow
 *
 *   Window creation function, invoked as a callback from Tk_MakeWindowExist.
 */

Window
Tkgl_MakeWindow(Tk_Window tkwin, Window parent, void* instanceData)
{
    Tkgl   *tkglPtr = (Tkgl *) instanceData;
    Display *display;
    Colormap cmap;
    int     scrnum;
    Window  window = None;

    if (tkglPtr->badWindow) {
        return Tk_MakeWindow(tkwin, parent);
    }

    /* for color index mode photos */
    if (tkglPtr->redMap)
        free(tkglPtr->redMap);
    if (tkglPtr->greenMap)
        free(tkglPtr->greenMap);
    if (tkglPtr->blueMap)
        free(tkglPtr->blueMap);
    tkglPtr->redMap = tkglPtr->greenMap = tkglPtr->blueMap = NULL;
    tkglPtr->mapSize = 0;

    display = Tk_Display(tkwin);
    scrnum = Tk_ScreenNumber(tkwin);

    /* ????
     * Windows and Mac OS X need the window created before OpenGL context
     * is created.  So do that now and set the window variable.
     */
    window = Tk_MakeWindow(tkwin, parent);

    /* if (!tkglPtr->pBufferFlag) { */
    /*     printf("Tkgl_MakeWindow: calling XMapWindow\n"); */
    /*     (void) XMapWindow(display, window); */
    /* } */

    if (tkglPtr->pixelFormat) {
        if (!tkgl_describePixelFormat(tkglPtr)) {
            Tcl_SetResult(tkglPtr->interp,
                    "couldn't choose pixel format", TCL_STATIC);
            goto error;
        }
    } else {
        tkglPtr->pixelFormat = (void *)tkgl_pixelFormat(tkglPtr);
        if (tkglPtr->pixelFormat == nil) {
            goto error;
        }
    }

    if (tkglPtr->visInfo == NULL) {
        Visual *visual= DefaultVisual(display, scrnum);
        tkglPtr->visInfo = (XVisualInfo *) calloc(1, sizeof (XVisualInfo));
        tkglPtr->visInfo->screen = scrnum;
        tkglPtr->visInfo->visual = visual;
        tkglPtr->visInfo->visualid = visual->visualid;
#  if defined(__cplusplus) || defined(c_plusplus)
        tkglPtr->visInfo->c_class = visual->c_class;
#  else
        tkglPtr->visInfo->class = visual->class;
#  endif
        tkglPtr->visInfo->depth = visual->bits_per_rgb;
    }

    /*
     * We should already have a context, but ...
     */
    if (Tkgl_CreateGLContext(tkglPtr) != TCL_OK) {
         goto error;
    }
    if (!tkglPtr->pBufferFlag) {
      tkglPtr->nsview = [[NSView alloc] initWithFrame:NSZeroRect];
      [tkglPtr->nsview setWantsBestResolutionOpenGLSurface:NO];
      MacDrawable *d = ((TkWindow *) tkglPtr->tkwin)->privatePtr;
      NSView *topview = d->toplevel->view;
      [topview addSubview:tkglPtr->nsview];

      /* TODO: Appears setView has to be deferred until the window is mapped,
       * or it gives "invalid drawable" error.  But MapNotify doesn't happen.
       * I think toplevel is already mapped.  Iconifying and deiconifying
       * the main window makes the graphics work.
       */
      /*      [tkglPtr->context setView:tkglPtr->nsview];*/
    }
    if (tkglPtr->context == NULL) {
        Tcl_SetResult(tkglPtr->interp,
            "could not create rendering context", TCL_STATIC);
        goto error;
    }
    if (tkglPtr->pBufferFlag) {
        tkglPtr->pbuf = tkgl_createPbuffer(tkglPtr);
        if (!tkglPtr->pbuf) {
            /* tcl result set in tkgl_createPbuffer */
            if (!tkglPtr->shareContext) {
	        [tkglPtr->context release];
		[tkglPtr->pixelFormat release];
            }
            tkglPtr->context = NULL;
            tkglPtr->pixelFormat = nil;
            goto error;
        }
        return window;
    }

    /*
     * find a colormap
     */
    if (tkglPtr->rgbaFlag) {
        /* Colormap for RGB mode */
        cmap = DefaultColormap(display, scrnum);
    } else {
        /* Colormap for CI mode */
        if (tkglPtr->privateCmapFlag) {
            /* need read/write colormap so user can store own color entries */
            /* need to figure out how to do this correctly on Mac... */
            cmap = DefaultColormap(display, scrnum);
        } else {
            if (tkglPtr->visInfo->visual == DefaultVisual(display, scrnum)) {
                /* share default/root colormap */
                cmap = Tk_Colormap(tkwin);
            } else {
                /* make a new read-only colormap */
                cmap = XCreateColormap(display,
                        XRootWindow(display, tkglPtr->visInfo->screen),
                        tkglPtr->visInfo->visual, AllocNone);
            }
        }
    }

    /* Make sure Tk knows to switch to the new colormap when the cursor is
     * over this window when running in color index mode. */
    (void) Tk_SetWindowVisual(tkwin, tkglPtr->visInfo->visual,
            tkglPtr->visInfo->depth, cmap);

#if TKGL_USE_OVERLAY
    if (tkglPtr->OverlayFlag) {
        if (SetupOverlay(tkglPtr) == TCL_ERROR) {
            fprintf(stderr, "Warning: couldn't setup overlay.\n");
            tkglPtr->OverlayFlag = False;
        }
    }
#endif

    /* Request the X window to be displayed */
    (void) XMapWindow(display, window);
    if (!tkglPtr->rgbaFlag) {
        int     index_size;
        GLint   index_bits;

        glGetIntegerv(GL_INDEX_BITS, &index_bits);
        index_size = 1 << index_bits;
        if (tkglPtr->mapSize != index_size) {
            if (tkglPtr->redMap)
                free(tkglPtr->redMap);
            if (tkglPtr->greenMap)
                free(tkglPtr->greenMap);
            if (tkglPtr->blueMap)
                free(tkglPtr->blueMap);
            tkglPtr->mapSize = index_size;
            tkglPtr->redMap = (GLfloat *) calloc(index_size, sizeof (GLfloat));
            tkglPtr->greenMap = (GLfloat *) calloc(index_size, sizeof (GLfloat));
            tkglPtr->blueMap = (GLfloat *) calloc(index_size, sizeof (GLfloat));
        }
    }
#ifdef HAVE_AUTOSTEREO
    if (tkglPtr->Stereo == TKGL_STEREO_NATIVE) {
        if (!tkglPtr->as_initialized) {
            const char *autostereod;

            tkglPtr->as_initialized = True;
            if ((autostereod = getenv("AUTOSTEREOD")) == NULL)
                autostereod = AUTOSTEREOD;
            if (autostereod && *autostereod) {
                if (ASInitialize(tkglPtr->display, autostereod) == Success) {
                    tkglPtr->ash = ASCreatedStereoWindow(display);
                }
            }
        } else {
            tkglPtr->ash = ASCreatedStereoWindow(display);
        }
    }
#endif
    return window;

  error:
    tkglPtr->badWindow = True;
    return window;
}

/*
 * Tkgl_MapWidget
 *
 *    Called when MapNotify events are received.
 */

void
Tkgl_MapWidget(void *instanceData)
{
    Tkgl *tkgl = (Tkgl *) instanceData;
    if (!tkgl->pBufferFlag) {
	[tkgl->context setView:tkgl->nsview];
	[tkgl->nsview setHidden:NO];
	Tkgl_Update(tkgl);
    }
}

/*
 * Tkgl_UnmapWidget
 *
 *    Called when UnmapNotify events are received.
 */

void
Tkgl_UnmapWidget(void *instanceData)
{
    Tkgl *tkgl = (Tkgl *) instanceData;
//    [tkgl->context clearDrawable];
    [tkgl->nsview setHidden:YES];
}

/*
 * Tkgl_WorldChanged
 *
 *    Add support for setgrid option.
 */
void
Tkgl_WorldChanged(ClientData instanceData)
{
    Tkgl   *tkgl = (Tkgl *) instanceData;
    int     width;
    int     height;

    if (tkgl->pBufferFlag)
        width = height = 1;
    else {
        width = tkgl->width;
        height = tkgl->height;
    }
    Tk_GeometryRequest(tkgl->tkwin, width, height);
    Tk_SetInternalBorder(tkgl->tkwin, 0);
    if (tkgl->setGrid > 0) {
        Tk_SetGrid(tkgl->tkwin,
		   width / tkgl->setGrid,
		   height / tkgl->setGrid,
		   tkgl->setGrid, tkgl->setGrid);
    } else {
        Tk_UnsetGrid(tkgl->tkwin);
    }
}

/*
 * Tkgl_TakePhoto
 *
 *   Take a photo image of the current OpenGL window.  May have problems
 *   if window is partially obscured, either by other windows or by the
 *   edges of the display.
 */

int
Tkgl_TakePhoto(Tkgl *tkglPtr, Tk_PhotoHandle photo)
{
    GLubyte *buffer;
    unsigned char *cp;
    int y, midy, width = tkglPtr->width, height = tkglPtr->height;
    buffer = (GLubyte *) ckalloc(width * height * 4);
    Tk_PhotoImageBlock photoBlock;
    photoBlock.pixelPtr = buffer;
    photoBlock.width = width;
    photoBlock.height = height;
    photoBlock.pitch = width * 4;
    photoBlock.pixelSize = 4;
    photoBlock.offset[0] = 0;
    photoBlock.offset[1] = 1;
    photoBlock.offset[2] = 2;
    photoBlock.offset[3] = 3;
    glPushAttrib(GL_PIXEL_MODE_BIT);
    if (tkglPtr->doubleFlag) {
        glReadBuffer(GL_FRONT);
    }
    if (!tkglPtr->rgbaFlag) {
        glPixelMapfv(GL_PIXEL_MAP_I_TO_R, tkglPtr->mapSize, tkglPtr->redMap);
        glPixelMapfv(GL_PIXEL_MAP_I_TO_G, tkglPtr->mapSize, tkglPtr->greenMap);
        glPixelMapfv(GL_PIXEL_MAP_I_TO_B, tkglPtr->mapSize, tkglPtr->blueMap);
    }
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);        /* guarantee performance */
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    /* OpenGL's origin is bottom-left, Tk Photo image's is top-left, so
     * reflect around the middle row. */
    midy = height / 2;
    cp = buffer;
    for (y = 0; y < midy; ++y) {
        int     m_y = height - 1 - y;   /* mirror y */
        unsigned char *m_cp = buffer + m_y * photoBlock.pitch;
        int     x;

        for (x = 0; x < photoBlock.pitch; ++x) {
            unsigned char c = *cp;

            *cp++ = *m_cp;
            *m_cp++ = c;
        }
    }
    Tk_PhotoPutBlock(tkglPtr->interp, photo, &photoBlock, 0, 0,
	width, height, TK_PHOTO_COMPOSITE_SET);
    glPopClientAttrib();
    glPopAttrib();    /* glReadBuffer */
    ckfree((char *) buffer);
    return TCL_OK;
}

/*
 * Tkgl_MakeCurrent
 *
 *   Bind the OpenGL rendering context to the specified
 *   Tkgl widget.  ????If given a NULL argument, then the
 *   OpenGL context is released without assigning a new one.????
 */

void
Tkgl_MakeCurrent(const Tkgl *tkglPtr)
{
    if (tkglPtr != NULL && tkglPtr->context != NULL) {
        [tkglPtr->context makeCurrentContext];
	// In case our context is in use by another view or pixel buffer,
	// reassign it to our view or pixel buffer.
        if (FindTkglWithSameContext(tkglPtr) != NULL) {
            if (!tkglPtr->pBufferFlag) {
	        [tkglPtr->context setView:tkglPtr->nsview];
            } else {
	        GLint virtualScreen = [tkglPtr->context currentVirtualScreen];
                [tkglPtr->context setPixelBuffer:tkglPtr->pbuf
				  cubeMapFace:0
				  mipMapLevel:0
			 currentVirtualScreen:virtualScreen];
            }
        }
    }
}

void
Tkgl_SwapBuffers(const Tkgl *tkglPtr)
{
    if (tkglPtr->doubleFlag) {
        [tkglPtr->context flushBuffer];
    } else {
        glFlush();
    }
}

int
Tkgl_CopyContext(const Tkgl *from, const Tkgl *to, unsigned mask)
{
    int same = (from->context == to->context);

    if (same) {
      [NSOpenGLContext clearCurrentContext];
    }
    [to->context copyAttributesFromContext:from->context withMask:mask];
    if (same)
        Tkgl_MakeCurrent(to);
    return TCL_OK;
}

void Tkgl_FreeResources(
    Tkgl *tkglPtr)
{
    [NSOpenGLContext clearCurrentContext];
    if (tkglPtr->extensions) {
	ckfree((void *)tkglPtr->extensions);
	tkglPtr->extensions = NULL;
    }
    if (tkglPtr->context) {
	if (FindTkglWithSameContext(tkglPtr) == NULL) {
	    [tkglPtr->context release];
	    tkglPtr->context = nil;
	}
	[tkglPtr->nsview removeFromSuperview];
	[tkglPtr->nsview release];
	tkglPtr->nsview = nil;
	free(tkglPtr->visInfo);
    }
    if (tkglPtr->pBufferFlag && tkglPtr->pbuf) {
	tkgl_destroyPbuffer(tkglPtr);
	tkglPtr->pbuf = 0;
    }
    tkglPtr->context = NULL;
    tkglPtr->visInfo = NULL;
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
