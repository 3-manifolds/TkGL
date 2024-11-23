/*
 * tkglGLX.c --
 *
 * Copyright (C) 2024, Marc Culler, Nathan Dunfield, Matthias Goerner
 *
 * This file is part of the TkGL project.  TkGL is derived from Togl, which
 * was written by Brian Paul, Ben Bederson and Greg Couch.  TkGL is licensed
 * under the Tcl license.  The terms of the license are described in the file
 * "license.terms" which should be included with this distribution.
 */

/*
  This file contains implementations of the following platform specific
  functions declared in tkgl.h.  They comprise the platform interface.

void Tkgl_Update(const Tkgl *tkglPtr);
Window Tkgl_MakeWindow(Tk_Window tkwin, Window parent, void* instanceData);
void Tkgl_MapWidget(void *instanceData);
void Tkgl_UnmapWidget(void *instanceData);
void Tkgl_WorldChanged(void* instanceData);
void Tkgl_MakeCurrent(const Tkgl *tkglPtr);
void Tkgl_SwapBuffers(const Tkgl *tkglPtr);
int Tkgl_TakePhoto(Tkgl *tkglPtr, Tk_PhotoHandle photo);
int Tkgl_CopyContext(const Tkgl *from, const Tkgl *to, unsigned mask);
int Tkgl_CreateGLContext(Tkgl *tkglPtr);
const char* Tkgl_GetExtensions(Tkgl *TkglPtr);
void Tkgl_FreeResources(Tkgl *TkglPtr);
*/

#include <stdbool.h>
#include "tkgl.h"
#include "tkglPlatform.h"
#include "tkInt.h"  /* for TkWindow */

static Colormap get_rgb_colormap(Display *dpy, int scrnum,
		    const XVisualInfo *visinfo, Tk_Window tkwin);

static const int attributes_2_1[] = {
  GLX_CONTEXT_MAJOR_VERSION_ARB, 2,
  GLX_CONTEXT_MINOR_VERSION_ARB, 1,
  None
};

static const int attributes_3_2[] = {
  GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
  GLX_CONTEXT_MINOR_VERSION_ARB, 2,
  None
};

static const int attributes_4_1[] = {
  GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
  GLX_CONTEXT_MINOR_VERSION_ARB, 1,
  None
};

#define ALL_EVENTS_MASK         \
   (KeyPressMask                \
   |KeyReleaseMask              \
   |ButtonPressMask             \
   |ButtonReleaseMask           \
   |EnterWindowMask             \
   |LeaveWindowMask             \
   |PointerMotionMask           \
   |ExposureMask                \
   |VisibilityChangeMask        \
   |FocusChangeMask             \
   |PropertyChangeMask          \
   |ColormapChangeMask)

/*
 * These static function pointers are set to the address of the
 * corresponding GLX function.  This setup avoids compiler warnings.
 */

static PFNGLXCHOOSEFBCONFIGPROC chooseFBConfig = NULL;
static PFNGLXGETFBCONFIGATTRIBPROC getFBConfigAttrib = NULL;
static PFNGLXGETVISUALFROMFBCONFIGPROC getVisualFromFBConfig = NULL;
static PFNGLXCREATEPBUFFERPROC createPbuffer = NULL;
static PFNGLXCREATEGLXPBUFFERSGIXPROC createPbufferSGIX = NULL;
static PFNGLXDESTROYPBUFFERPROC destroyPbuffer = NULL;
static PFNGLXQUERYDRAWABLEPROC queryPbuffer = NULL;
static Bool hasMultisampling = False;
static Bool hasPbuffer = False;

struct FBInfo
{
    int     acceleration;
    int     samples;
    int     depth;
    int     colors;
    GLXFBConfig fbcfg;
};
typedef struct FBInfo FBInfo;

static void getFBInfo(
   Display *display,
   GLXFBConfig cfg,
   FBInfo *info)
{
    info->fbcfg = cfg;
    /* GLX_NONE < GLX_SLOW_CONFIG < GLX_NON_CONFORMANT_CONFIG */
    getFBConfigAttrib(display, cfg, GLX_CONFIG_CAVEAT, &info->acceleration);
    /* Number of bits per color */
    getFBConfigAttrib(display, cfg, GLX_BUFFER_SIZE, &info->colors);
    /* Number of bits per depth value. */
    getFBConfigAttrib(display, cfg, GLX_DEPTH_SIZE, &info->depth);
    /* Number of samples per pixesl when multisampling. */
    getFBConfigAttrib(display, cfg, GLX_SAMPLES, &info->samples);
}

static Bool isBetterFB(
    const FBInfo *x,
    const FBInfo *y)
{
    /* True if x is better than y */
    if (x->acceleration != y->acceleration)
        return (x->acceleration < y->acceleration);
    if (x->colors != y->colors)
        return (x->colors > y->colors);
    if (x->depth != y->depth)
        return (x->depth > y->depth);
    if (x->samples != y->samples)
        return (x->samples > y->samples);
    return false;
}

static Tcl_ThreadDataKey tkgl_XError;
struct ErrorData
{
    int     error_code;
    XErrorHandler prevHandler;
};
typedef struct ErrorData ErrorData;

static int
tkgl_HandleXError(Display *dpy, XErrorEvent * event)
{
    ErrorData *data = Tcl_GetThreadData(&tkgl_XError, (int) sizeof (ErrorData));

    data->error_code = event->error_code;
    return 0;
}

static void
tkgl_SetupXErrorHandler()
{
    ErrorData *data = Tcl_GetThreadData(&tkgl_XError, (int) sizeof (ErrorData));

    data->error_code = Success; /* 0 */
    data->prevHandler = XSetErrorHandler(tkgl_HandleXError);
}

static int
tkgl_CheckForXError(const Tkgl *tkglPtr)
{
    ErrorData *data = Tcl_GetThreadData(&tkgl_XError, (int) sizeof (ErrorData));

    XSync(tkglPtr->display, False);
    (void) XSetErrorHandler(data->prevHandler);
    return data->error_code;
}

static GLXPbuffer
tkgl_createPbuffer(Tkgl *tkglPtr)
{
    int     attribs[32];
    int     na = 0;
    GLXPbuffer pbuf;

    tkgl_SetupXErrorHandler();
    if (tkglPtr->largestPbufferFlag) {
        attribs[na++] = GLX_LARGEST_PBUFFER;
        attribs[na++] = True;
    }
    attribs[na++] = GLX_PRESERVED_CONTENTS;
    attribs[na++] = True;
    if (createPbuffer) {
        attribs[na++] = GLX_PBUFFER_WIDTH;
        attribs[na++] = tkglPtr->width;
        attribs[na++] = GLX_PBUFFER_HEIGHT;
        attribs[na++] = tkglPtr->width;
        attribs[na++] = None;
        pbuf = createPbuffer(tkglPtr->display, tkglPtr->fbcfg, attribs);
    } else {
        attribs[na++] = None;
        pbuf = createPbufferSGIX(tkglPtr->display, tkglPtr->fbcfg,
		   tkglPtr->width, tkglPtr->height, attribs);
    }
    if (tkgl_CheckForXError(tkglPtr) || pbuf == None) {
        Tcl_SetResult(tkglPtr->interp,
                      "unable to allocate pbuffer", TCL_STATIC);
        return None;
    }
    if (pbuf && tkglPtr->largestPbufferFlag) {
        unsigned int     tmp;

        queryPbuffer(tkglPtr->display, pbuf, GLX_WIDTH, &tmp);
        if (tmp != 0)
            tkglPtr->width = tmp;
        queryPbuffer(tkglPtr->display, pbuf, GLX_HEIGHT, &tmp);
        if (tmp != 0)
            tkglPtr->height = tmp;
    }
    return pbuf;
}

static XVisualInfo *
tkgl_pixelFormat(
    Tkgl *tkglPtr,
    int scrnum)
{
    int attribs[256];
    int na = 0;
    int i;
    XVisualInfo *visinfo;
    int dummy, major, minor;
    const char *extensions;

    /*
     * Make sure OpenGL's GLX extension is supported.
     */

    if (!glXQueryExtension(tkglPtr->display, &dummy, &dummy)) {
      Tcl_SetResult(tkglPtr->interp,
                    "X server is missing OpenGL GLX extension",
                    TCL_STATIC);
      return NULL;
    }

#ifdef DEBUG_GLX
    (void) XSetErrorHandler(fatal_error);
#endif

    glXQueryVersion(tkglPtr->display, &major, &minor);
    extensions = glXQueryExtensionsString(tkglPtr->display, scrnum);

    if (major == 1 && minor < 4) {
	Tcl_SetResult(tkglPtr->interp,
	    "Tkgl 3.0 requires GLX 1.4 or newer.", TCL_STATIC);
	return NULL;
    }
    chooseFBConfig = glXChooseFBConfig;
    getFBConfigAttrib = glXGetFBConfigAttrib;
    getVisualFromFBConfig = glXGetVisualFromFBConfig;
    createPbuffer = glXCreatePbuffer;
    destroyPbuffer = glXDestroyPbuffer;
    queryPbuffer = glXQueryDrawable;
    hasPbuffer = True;

    if (hasPbuffer && !chooseFBConfig) {
      hasPbuffer = False;
    }

    if (strstr(extensions, "GLX_ARB_multisample") != NULL
        || strstr(extensions, "GLX_SGIS_multisample") != NULL) {
      hasMultisampling = True;
    }

    if (tkglPtr->multisampleFlag && !hasMultisampling) {
        Tcl_SetResult(tkglPtr->interp,
                      "multisampling not supported", TCL_STATIC);
        return NULL;
    }

    if (tkglPtr->pBufferFlag && !hasPbuffer) {
        Tcl_SetResult(tkglPtr->interp,
                      "pbuffers are not supported", TCL_STATIC);
        return NULL;
    }

    if (chooseFBConfig) {
        int     count;
        GLXFBConfig *cfgs;

        attribs[na++] = GLX_RENDER_TYPE;
        if (tkglPtr->rgbaFlag) {
            /* RGB[A] mode */
            attribs[na++] = GLX_RGBA_BIT;
            attribs[na++] = GLX_RED_SIZE;
            attribs[na++] = tkglPtr->rgbaRed;
            attribs[na++] = GLX_GREEN_SIZE;
            attribs[na++] = tkglPtr->rgbaGreen;
            attribs[na++] = GLX_BLUE_SIZE;
            attribs[na++] = tkglPtr->rgbaBlue;
            if (tkglPtr->alphaFlag) {
                attribs[na++] = GLX_ALPHA_SIZE;
                attribs[na++] = tkglPtr->alphaSize;
            }
        } else {
            /* Color index mode */
            attribs[na++] = GLX_COLOR_INDEX_BIT;
            attribs[na++] = GLX_BUFFER_SIZE;
            attribs[na++] = 1;
        }
        if (tkglPtr->depthFlag) {
            attribs[na++] = GLX_DEPTH_SIZE;
            attribs[na++] = tkglPtr->depthSize;
        }
        if (tkglPtr->doubleFlag) {
            attribs[na++] = GLX_DOUBLEBUFFER;
            attribs[na++] = True;
        }
        if (tkglPtr->stencilFlag) {
            attribs[na++] = GLX_STENCIL_SIZE;
            attribs[na++] = tkglPtr->stencilSize;
        }
        if (tkglPtr->accumFlag) {
            attribs[na++] = GLX_ACCUM_RED_SIZE;
            attribs[na++] = tkglPtr->accumRed;
            attribs[na++] = GLX_ACCUM_GREEN_SIZE;
            attribs[na++] = tkglPtr->accumGreen;
            attribs[na++] = GLX_ACCUM_BLUE_SIZE;
            attribs[na++] = tkglPtr->accumBlue;
            if (tkglPtr->alphaFlag) {
                attribs[na++] = GLX_ACCUM_ALPHA_SIZE;
                attribs[na++] = tkglPtr->accumAlpha;
            }
        }
        if (tkglPtr->stereo == TKGL_STEREO_NATIVE) {
            attribs[na++] = GLX_STEREO;
            attribs[na++] = True;
        }
        if (tkglPtr->multisampleFlag) {
            attribs[na++] = GLX_SAMPLE_BUFFERS_ARB;
            attribs[na++] = 1;
            attribs[na++] = GLX_SAMPLES_ARB;
            attribs[na++] = 2;
        }
        if (tkglPtr->pBufferFlag) {
            attribs[na++] = GLX_DRAWABLE_TYPE;
            attribs[na++] = GLX_WINDOW_BIT | GLX_PBUFFER_BIT;
        }
        if (tkglPtr->auxNumber != 0) {
            attribs[na++] = GLX_AUX_BUFFERS;
            attribs[na++] = tkglPtr->auxNumber;
        }
        attribs[na++] = None;

        cfgs = chooseFBConfig(tkglPtr->display, scrnum, attribs, &count);
        if (cfgs == NULL || count == 0) {
            Tcl_SetResult(tkglPtr->interp, "Couldn't choose pixel format.",
			  TCL_STATIC);
            return NULL;
        }

        /*
         * Pick the best available pixel format.
         */

	FBInfo bestFB, nextFB;
	getFBInfo(tkglPtr->display, cfgs[0], &bestFB);
	for (i=1; i < count; i++) {
	    getFBInfo(tkglPtr->display, cfgs[i], &nextFB);
	    if (isBetterFB(&nextFB, &bestFB)) {
		bestFB = nextFB;
	    }
	}
#if 0
	printf(" acc: %d ", bestFB.acceleration);
	printf(" colors: %d ", bestFB.colors);
	printf(" depth: %d ", bestFB.depth);
	printf(" samples: %d\n", bestFB.samples);
#endif
	tkglPtr->fbcfg = bestFB.fbcfg;
	visinfo = getVisualFromFBConfig(tkglPtr->display, bestFB.fbcfg);
    }
    if (visinfo == NULL) {
        Tcl_SetResult(tkglPtr->interp,
                      "couldn't choose pixel format", TCL_STATIC);
        return NULL;
    }
    return visinfo;
}

static int
tkgl_describePixelFormat(Tkgl *tkglPtr)
{
    int tmp = 0;

    /* Set flags in the widget record based on the pixel format.*/
    (void) glXGetConfig(tkglPtr->display, tkglPtr->visInfo, GLX_RGBA,
               &tkglPtr->rgbaFlag);
    (void) glXGetConfig(tkglPtr->display, tkglPtr->visInfo, GLX_DOUBLEBUFFER,
               &tkglPtr->doubleFlag);
    (void) glXGetConfig(tkglPtr->display, tkglPtr->visInfo, GLX_DEPTH_SIZE,
	       &tmp);
    tkglPtr->depthFlag = (tmp != 0);
    (void) glXGetConfig(tkglPtr->display, tkglPtr->visInfo, GLX_ACCUM_RED_SIZE,
	       &tmp);
    tkglPtr->accumFlag = (tmp != 0);
    (void) glXGetConfig(tkglPtr->display, tkglPtr->visInfo, GLX_ALPHA_SIZE,
	       &tmp);
    tkglPtr->alphaFlag = (tmp != 0);
    (void) glXGetConfig(tkglPtr->display, tkglPtr->visInfo, GLX_STENCIL_SIZE,
	       &tmp);
    tkglPtr->stencilFlag = (tmp != 0);
    (void) glXGetConfig(tkglPtr->display, tkglPtr->visInfo, GLX_STEREO, &tmp);
    tkglPtr->stereo = tmp ? TKGL_STEREO_NATIVE : TKGL_STEREO_NONE;
    if (hasMultisampling) {
        (void) glXGetConfig(tkglPtr->display, tkglPtr->visInfo, GLX_SAMPLES, &tmp);
        tkglPtr->multisampleFlag = (tmp != 0);
    }
    return True;
}

/*
 * Tkgl_CreateGLContext
 *
 * Creates an OpenGL rendering context for the Tkgl widget.  This is called
 * when the widget is created, before it is mapped. Creating a rendering
 * context also requires creating the rendering surface.  For GLX The surface
 * is an an X "window" (i.e. an X widget) managed by the Tkgl widget.  The
 * visual id of the visualInfo, which plays the role of a pixel format, is
 * saved in the pixelFormat field of the widget record.
 *
 * This function is called as an idle task, or as a timer handler.
 */

static void CreateRenderingSurface(
     void *clientData)
{
    Tkgl *tkglPtr = (Tkgl *) clientData;
    Tk_Window tkwin = tkglPtr->tkwin;
    Display *dpy;
    Colormap cmap;
    int     scrnum;
    Window parent = Tk_WindowId(Tk_Parent(tkwin)); 
    Window  window = None;
    XSetWindowAttributes swa;
 
    /*
     * We can't create the rendering surface until the parent window exists.
     */

    if (parent == None) {
	/* Reschedule this idle task to run again later. */
	if (tkglPtr->timerToken) {
	    Tcl_DeleteTimerHandler(tkglPtr->timerToken);
	    tkglPtr->timerToken = None;
	}
	tkglPtr->timerToken = Tcl_CreateTimerHandler(20,
		    CreateRenderingSurface, clientData);
	tkglPtr->badWindow++;
	return;
    }
    if (tkglPtr->badWindow > 5) {
	/* Still no parent after 5 attempts.  Give up. */
        tkglPtr->surface = Tk_MakeWindow(tkwin, parent);
	return;
    }

    /* for color index mode photos */
    if (tkglPtr->redMap) {
        free(tkglPtr->redMap);
    }
    if (tkglPtr->greenMap) {
        free(tkglPtr->greenMap);
    }
    if (tkglPtr->blueMap) {
        free(tkglPtr->blueMap);
    }
    tkglPtr->redMap = tkglPtr->greenMap = tkglPtr->blueMap = NULL;
    tkglPtr->mapSize = 0;

    dpy = Tk_Display(tkwin);
    scrnum = Tk_ScreenNumber(tkwin);

    /*
     * Use the visualid stored in the pixelformat field to choose
     * igure out which OpenGL context to use
     */

    if (tkglPtr->pixelFormat) {
        XVisualInfo template;
        int     count = 0;

	// The -pixelformat option was set or we are being remapped.
        template.visualid = tkglPtr->pixelFormat;
        tkglPtr->visInfo = XGetVisualInfo(dpy, VisualIDMask, &template, &count);
        if (tkglPtr->visInfo == NULL) {
            Tcl_SetResult(tkglPtr->interp,
                    "visual information not available", TCL_STATIC);
            goto error;
        }
        if (!tkgl_describePixelFormat(tkglPtr)) {
            Tcl_SetResult(tkglPtr->interp,
                "No consistent pixel format is available.", TCL_STATIC);
            goto error;
        }
    } else {
        tkglPtr->visInfo = tkgl_pixelFormat(tkglPtr, scrnum);
        if (tkglPtr->visInfo == NULL)
            goto error;
    }
    if (tkglPtr->shareList) {
        /* We are sharing resourcess of an existing tkgl widget */
        Tkgl   *shareWith = FindTkgl(tkglPtr, tkglPtr->shareList);
        GLXContext shareCtx;
        int     error_code;

        if (shareWith) {
            shareCtx = shareWith->context;
            tkglPtr->contextTag = shareWith->contextTag;
        } else {
            shareCtx = None;
        }
        if (shareCtx) {
            tkgl_SetupXErrorHandler();
        }
        if (shareCtx && (error_code = tkgl_CheckForXError(tkglPtr))) {
            char    buf[256];

            tkglPtr->context = NULL;
            XGetErrorText(dpy, error_code, buf, sizeof buf);
            Tcl_AppendResult(tkglPtr->interp,
                    "unable to share display lists: ", buf, NULL);
            goto error;
        }
    } else {
        if (tkglPtr->shareContext && FindTkgl(tkglPtr, tkglPtr->shareContext)) {
            /* We are using the OpenGL context of an existing Tkgl widget */
            Tkgl   *shareWith = FindTkgl(tkglPtr, tkglPtr->shareContext);

            if (tkglPtr->visInfo->visualid != shareWith->visInfo->visualid) {
                Tcl_SetResult(tkglPtr->interp,
                        "Unable to share the requested OpenGL context.",
                        TCL_STATIC);
                goto error;
            }
            tkglPtr->context = shareWith->context;
        } else {
            /* We can't share the context so clear the flag. */
            tkglPtr->shareContext = False;
	}
    }
    if (tkglPtr->context == NULL) {
        Tcl_SetResult(tkglPtr->interp,
                "could not create rendering context", TCL_STATIC);
        goto error;
    }
    if (tkglPtr->pBufferFlag) {
        /* Don't need a colormap, nor overlay, nor be displayed */
        tkglPtr->pbuf = tkgl_createPbuffer(tkglPtr);
        if (!tkglPtr->pbuf) {
            /* A Tcl result will have been set in tkgl_createPbuffer */
            goto error;
        }
        tkglPtr->surface = window;
	return;
    }

    /*
     * find a colormap
     */
    if (tkglPtr->rgbaFlag) {
        /* Colormap for RGB mode */
        cmap = get_rgb_colormap(dpy, scrnum, tkglPtr->visInfo, tkwin);
    } else {
        /* Colormap for CI mode */
        if (tkglPtr->privateCmapFlag) {
            /* need read/write colormap so user can store own color entries */
            cmap = XCreateColormap(dpy,
		       XRootWindow(dpy, tkglPtr->visInfo->screen),
                       tkglPtr->visInfo->visual, AllocAll);
        } else {
            if (tkglPtr->visInfo->visual == DefaultVisual(dpy, scrnum)) {
                /* share default/root colormap */
                cmap = Tk_Colormap(tkwin);
            } else {
                /* make a new read-only colormap */
                cmap = XCreateColormap(dpy,
                        XRootWindow(dpy, tkglPtr->visInfo->screen),
                        tkglPtr->visInfo->visual, AllocNone);
            }
        }
    }

    /*
     * Make sure Tk knows to switch to the new colormap when the cursor is over
     * this window when running in color index mode.
     */

    (void) Tk_SetWindowVisual(tkwin, tkglPtr->visInfo->visual,
            tkglPtr->visInfo->depth, cmap);
    swa.background_pixmap = None;
    swa.border_pixel = 0;
    swa.colormap = cmap;
    swa.event_mask = ALL_EVENTS_MASK;

    /*
     * Create the Tkgl X window with width and height equal to 1.
     * It will be resized when the widget is mapped.
     */

    window = XCreateWindow(dpy, parent, 0, 0, 1, 1, //width, height,
            0, tkglPtr->visInfo->depth, InputOutput, tkglPtr->visInfo->visual,
            CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask, &swa);

    /*
     * Ask the window manager to install our colormap
     */
    
    (void) XSetWMColormapWindows(dpy, window, &window, 1);

    /*
     * See if we requested single buffering but had to accept a double
     * buffered visual.  If so, set the GL draw buffer to be the front buffer
     * to simulate single buffering.
     */

    if (!tkglPtr->doubleFlag) {
        int     dbl_flag;

        if (glXGetConfig(dpy, tkglPtr->visInfo, GLX_DOUBLEBUFFER, &dbl_flag)) {
            if (dbl_flag) {
                glXMakeCurrent(dpy, window, tkglPtr->context);
                glDrawBuffer(GL_FRONT);
                glReadBuffer(GL_FRONT);
            }
        }
    }

#if TKGL_USE_OVERLAY
    if (tkglPtr->overlayFlag) {
        if (SetupOverlay(tkgl) == TCL_ERROR) {
            fprintf(stderr, "Warning: couldn't setup overlay.\n");
            tkglPtr->overlayFlag = False;
        }
    }
#endif

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
    if (tkglPtr->stereo == TKGL_STEREO_NATIVE) {
        if (!tkglPtr->as_initialized) {
            const char *autostereod;

            tkglPtr->as_initialized = True;
            if ((autostereod = getenv("AUTOSTEREOD")) == NULL)
                autostereod = AUTOSTEREOD;
            if (autostereod && *autostereod) {
                if (ASInitialize(tkglPtr->display, autostereod) == Success) {
                    tkglPtr->ash = ASCreatedStereoWindow(dpy);
                }
            }
        } else {
            tkglPtr->ash = ASCreatedStereoWindow(dpy);
        }
    }
#endif

    tkglPtr->surface = window;
    return;

  error:
    tkglPtr->badWindow = True;
}

int
Tkgl_CreateGLContext(
    Tkgl *tkglPtr)
{
    GLXContext context = NULL;
    GLXContext shareCtx = NULL;
    Bool direct = true;  /* If this is false, GLX reports GLXBadFBConfig. */

    if (tkglPtr->fbcfg == NULL) {
	int scrnum = Tk_ScreenNumber(tkglPtr->tkwin);
	tkglPtr->visInfo = tkgl_pixelFormat(tkglPtr, scrnum);
    }
    switch(tkglPtr->profile) {
    case PROFILE_LEGACY:
	context = glXCreateContextAttribsARB(tkglPtr->display, tkglPtr->fbcfg,
	    shareCtx, direct, attributes_2_1);
	break;
    case PROFILE_3_2:
	context = glXCreateContextAttribsARB(tkglPtr->display, tkglPtr->fbcfg,
	    shareCtx, direct, attributes_3_2);
	break;
    case PROFILE_4_1:
	context = glXCreateContextAttribsARB(tkglPtr->display, tkglPtr->fbcfg,
	    shareCtx, direct, attributes_4_1);
	break;
    default:
	context = glXCreateContext(tkglPtr->display, tkglPtr->visInfo,
	    shareCtx, direct);
	break;
    }
    if (context == NULL) {
	Tcl_SetResult(tkglPtr->interp,
            "Failed to create GL rendering context", TCL_STATIC);
	return TCL_ERROR;
    }
    tkglPtr->context = context;
    Tcl_DoWhenIdle(CreateRenderingSurface, (void *)tkglPtr);
    return TCL_OK;
}


/*
 * Tkgl_MakeWindow
 *
 * This is a callback function which is called by Tk_MakeWindowExist
 * when the tkgl widget is mapped.  It sets up the widget record and
 * does other Tk-related initialization.  This function is not allowed
 * to fail.  It must return a valid X window identifier.  If something
 * goes wrong, it sets the badWindow flag in the widget record,
 * which is passed as the instanceData.  The actual work of creating
 * the window has already been done in TkglCreateGLContext.
 */

Window
Tkgl_MakeWindow(
    Tk_Window tkwin,
    Window parent,
    void* instanceData)
{
    Tkgl *tkglPtr = (Tkgl *) instanceData;
    Window result = tkglPtr->surface;
    if (result == None) {
	result = Tk_MakeWindow(tkwin, parent);
    }
    return result;
}


/*
 * Tkgl_MakeCurrent
 *
 * This is the key function of the Tkgl widget in its role as the
 * manager of an NSOpenGL rendering context.  Must be called by
 * a GL client before drawing into the widget.
 */

void
Tkgl_MakeCurrent(
    const Tkgl *tkglPtr)
{
    if (!tkglPtr->context) {
	return;
    }
    Display *display = tkglPtr ? tkglPtr->display : glXGetCurrentDisplay();
    if (!display) {
	return;
    }
    GLXDrawable drawable;

    if (!tkglPtr) {
	drawable = None;	
    } else if (tkglPtr->pBufferFlag) {
	drawable = tkglPtr->pbuf;
    } else if (tkglPtr->tkwin) {
	drawable = Tk_WindowId(tkglPtr->tkwin);
    } else {
	drawable = None;
    }
    (void) glXMakeCurrent(display, drawable, tkglPtr->context);
}


/*
 * Tkgl_SwapBuffers
 *
 * Called by the GL Client after updating the image.  If the Tkgl
 * is double-buffered it interchanges the front and back framebuffers.
 * otherwise it calls GLFlush.
 */

void
Tkgl_SwapBuffers(
    const Tkgl *tkglPtr){
    if (tkglPtr->doubleFlag) {
        glXSwapBuffers(Tk_Display(tkglPtr->tkwin),
		       Tk_WindowId(tkglPtr->tkwin));
    } else {
        glFlush();
    }
}

/*
 * TkglUpdate
 *
 * Called by TkglDisplay whenever the size of the Tkgl widget may
 * have changed.  On macOS it adjusts the frame of the NSView that
 * is being used as the rendering surface.  The other platforms
 * handle the size changes automatically.
 */

void
Tkgl_Update(
    const Tkgl *tkglPtr) {
}

/*
 * Tkgl_GetExtensions
 *
 * Queries the rendering context for its extension string, a
 * space-separated list of the names of all supported GL extensions.
 * The string is cached in the widget record and the cached
 * string is returned in subsequent calls.
 */

const char* Tkgl_GetExtensions(
    Tkgl *tkglPtr)
{
    int scrnum = Tk_ScreenNumber(tkglPtr->tkwin);
    return glXQueryExtensionsString(tkglPtr->display, scrnum);
}

void Tkgl_FreeResources(
    Tkgl *tkglPtr)
{
    (void) glXMakeCurrent(tkglPtr->display, None, NULL);
    if (tkglPtr->context) {
	if (FindTkglWithSameContext(tkglPtr) == NULL) {
	    glXDestroyContext(tkglPtr->display, tkglPtr->context);
	    XFree(tkglPtr->visInfo);
	}
	if (tkglPtr->pBufferFlag && tkglPtr->pbuf) {
	    glXDestroyPbuffer(tkglPtr->display, tkglPtr->pbuf);
	    tkglPtr->pbuf = 0;
	}
	tkglPtr->context = NULL;
	tkglPtr->visInfo = NULL;
    }
#  if TKGL_USE_OVERLAY
    if (tkgl->OverlayContext) {
	Tcl_HashEntry *entryPtr;
	TkWindow *winPtr = (TkWindow *) tkwin;
	if (winPtr) {
	    entryPtr = Tcl_FindHashEntry(&winPtr->dispPtr->winTable,
					 (const char *) tkgl->overlayWindow);
	    Tcl_DeleteHashEntry(entryPtr);
	}
	if (FindTkglWithSameOverlayContext(tkgl) == NULL)
	    glXDestroyContext(tkglPtr->display, tkglPtr->overlayContext);
	tkgl->overlayContext = NULL;
    }
#  endif
}

/* 
 * Tkgl_MapWidget
 *
 *    Called when MapNotify events are received.
 */

void
Tkgl_MapWidget(
    void *instanceData)
{
    Tkgl *tkglPtr = (Tkgl *)instanceData;
    int width = Tk_Width(tkglPtr->tkwin);
    int height = Tk_Height(tkglPtr->tkwin);

    /*
     * The purpose of this unfortunate hack is to force the widget to be
     * rendered immediately after it is mapped.  Without this, the widget
     * appears blank when its window first opens, and the only way to make
     * it draw itself seems to be to resize the containing toplevel.
     */

    Tk_ResizeWindow(tkglPtr->tkwin, width, height + 1);
    Tk_ResizeWindow(tkglPtr->tkwin, width, height);
}

/* 
 * Tkgl_UnmapWidget
 *
 *    Called when UnmapNotify events are received.
 */

void
Tkgl_UnmapWidget(void *instanceData)
{
}

void
Tkgl_WorldChanged(
    void* instanceData){
#if 0
    printf("WorldChanged\n");
#endif
}

int
Tkgl_TakePhoto(
    Tkgl *tkglPtr,
    Tk_PhotoHandle photo)
{
#if 0
    printf("TakePhoto\n");
#endif
    return TCL_OK;
}

int
Tkgl_CopyContext(
    const Tkgl *from,
    const Tkgl *to,
    unsigned mask)
{
#if 0
    printf("CopyContext\n");
#endif
    return TCL_OK;
}

/*
 * Return an X colormap to use for OpenGL RGB-mode rendering.
 * Input:  dpy - the X display
 *         scrnum - the X screen number
 *         visinfo - the XVisualInfo as returned by glXChooseVisual()
 * Return:  an X Colormap or 0 if there's a _serious_ error.
 */
static Colormap
get_rgb_colormap(Display *dpy,
        int scrnum, const XVisualInfo *visinfo, Tk_Window tkwin)
{
    Atom    hp_cr_maps;
    Status  status;
    int     numCmaps;
    int     i;
    XStandardColormap *standardCmaps;
    Window  root = XRootWindow(dpy, scrnum);
    Bool    using_mesa;

    /*
     * First check if visinfo's visual matches the default/root visual.
     */
    if (visinfo->visual == Tk_Visual(tkwin)) {
        /* use the default/root colormap */
        Colormap cmap;

        cmap = Tk_Colormap(tkwin);
#  ifdef MESA_COLOR_HACK
        (void) get_free_color_cells(dpy, scrnum, cmap);
#  endif
        return cmap;
    }

    /*
     * Check if we're using Mesa.
     */
    if (strstr(glXQueryServerString(dpy, scrnum, GLX_VERSION), "Mesa")) {
        using_mesa = True;
    } else {
        using_mesa = False;
    }

    /*
     * Next, if we're using Mesa and displaying on an HP with the "Color
     * Recovery" feature and the visual is 8-bit TrueColor, search for a
     * special colormap initialized for dithering.  Mesa will know how to
     * dither using this colormap.
     */
    if (using_mesa) {
        hp_cr_maps = XInternAtom(dpy, "_HP_RGB_SMOOTH_MAP_LIST", True);
        if (hp_cr_maps
#  ifdef __cplusplus
                && visinfo->visual->c_class == TrueColor
#  else
                && visinfo->visual->class == TrueColor
#  endif
                && visinfo->depth == 8) {
            status = XGetRGBColormaps(dpy, root, &standardCmaps,
                    &numCmaps, hp_cr_maps);
            if (status) {
                for (i = 0; i < numCmaps; i++) {
                    if (standardCmaps[i].visualid == visinfo->visual->visualid) {
                        Colormap cmap = standardCmaps[i].colormap;

                        (void) XFree(standardCmaps);
                        return cmap;
                    }
                }
                (void) XFree(standardCmaps);
            }
        }
    }

    /*
     * If we get here, give up and just allocate a new colormap.
     */
    return XCreateColormap(dpy, root, visinfo->visual, AllocNone);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
