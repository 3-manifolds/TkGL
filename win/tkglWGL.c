/*
 * tkglWGL.h --
 *
 * Copyright (C) 2024, Marc Culler, Nathan Dunfield, Matthias Goerner
 *
 * This file is part of the TkGL project.  TkGL is derived from Togl, which
 * was written by Brian Paul, Ben Bederson and Greg Couch.  TkGL is licensed
 * under the Tcl license.  The terms of the license are described in the file
 * "license.terms" which hould be included with this distribution.
 */

/*
  This file implementats the following platform specific functions declared in
  tkgl.h.  They comprise the platform interface.

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
#include "tkWinInt.h" /* for TkWinDCState */
#include "tkIntPlatDecls.h" /* for TkWinChildProc */
#include "colormap.h"

#define TKGL_CLASS_NAME TEXT("Tkgl Class")
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#include <winnt.h>
#include <objbase.h>
#ifdef _MSC_VER
#  include <strsafe.h>
#  else
#    ifdef UNICODE
#      define StringCchPrintf snwprintf
#    else
#      define StringCchPrintf snprintf
#    endif
#endif

/*
 * These are static variables which cache pointers to extension procedures
 * provided by a graphics card device driver, rather than by the openGL
 * dynamic library.  They are initialized by calling wglGetProcAddress, which
 * requires a current rendering context. These procedures may or may not be
 * provided by any given driver.  If not provided wglGetProcAddress returns
 * NULL.
 */

static PFNWGLCREATECONTEXTATTRIBSARBPROC   createContextAttribs = NULL;
static PFNWGLGETEXTENSIONSSTRINGARBPROC    getExtensionsString = NULL;
static PFNWGLCHOOSEPIXELFORMATARBPROC      choosePixelFormat = NULL;
static PFNWGLGETPIXELFORMATATTRIBIVARBPROC getPixelFormatAttribiv = NULL;
static PFNWGLCREATEPBUFFERARBPROC          createPbuffer = NULL;
static PFNWGLDESTROYPBUFFERARBPROC         destroyPbuffer = NULL;
static PFNWGLGETPBUFFERDCARBPROC           getPbufferDC = NULL;
static PFNWGLRELEASEPBUFFERDCARBPROC       releasePbufferDC = NULL;
static PFNWGLQUERYPBUFFERARBPROC           queryPbuffer = NULL;

static int hasMultisampling = FALSE;
static int hasPbuffer = FALSE;
static int hasARBPbuffer = FALSE;

/*
 * initializeDeviceProcs
 *
 * This function initializes the pointers above.  It will have no
 * effect unless there is a current rendering context.  Some
 * pointers may remain NULL after initialization, if the graphics
 * driver does not provide the function.
 */

static void
initializeDeviceProcs()
{
    createContextAttribs = (PFNWGLCREATECONTEXTATTRIBSARBPROC)
	wglGetProcAddress("wglCreateContextAttribsARB");
    getExtensionsString = (PFNWGLGETEXTENSIONSSTRINGARBPROC)
	wglGetProcAddress("wglGetExtensionsStringARB");
    /* First try to get the ARB versions of choosePixelFormat */
    choosePixelFormat = (PFNWGLCHOOSEPIXELFORMATARBPROC)
	wglGetProcAddress("wglChoosePixelFormatARB");
    getPixelFormatAttribiv = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)
	wglGetProcAddress("wglGetPixelFormatAttribivARB");
    if (choosePixelFormat == NULL || getPixelFormatAttribiv == NULL) {
	choosePixelFormat = NULL;
	getPixelFormatAttribiv = NULL;
    }
    /* If that fails, fall back to the EXT versions, which have the same
     *  signature, ignoring const.
    `*/
    if (choosePixelFormat == NULL) {
	choosePixelFormat = (PFNWGLCHOOSEPIXELFORMATARBPROC)
	    wglGetProcAddress("wglChoosePixelFrmatEXT");
	getPixelFormatAttribiv = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)
	    wglGetProcAddress("wglGetPixelFormatAttribivEXT");
	if (choosePixelFormat == NULL || getPixelFormatAttribiv == NULL) {
	    choosePixelFormat = NULL;
	    getPixelFormatAttribiv = NULL;
	}
    }
    createPbuffer = (PFNWGLCREATEPBUFFERARBPROC)
	wglGetProcAddress("wglCreatePbufferARB");
    destroyPbuffer = (PFNWGLDESTROYPBUFFERARBPROC)
	wglGetProcAddress("wglDestroyPbufferARB");
    getPbufferDC = (PFNWGLGETPBUFFERDCARBPROC)
	wglGetProcAddress("wglGetPbufferDCARB");
    releasePbufferDC = (PFNWGLRELEASEPBUFFERDCARBPROC)
	wglGetProcAddress("wglReleasePbufferDCARB");
    queryPbuffer = (PFNWGLQUERYPBUFFERARBPROC)           
	wglGetProcAddress("wglQueryPbufferARB");
}

static Bool TkglClassInitialized = False;

static LRESULT CALLBACK
Win32WinProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT answer;
    Tkgl   *tkglPtr = (Tkgl *) GetWindowLongPtr(hwnd, 0);
    
    switch (message) {

      case WM_ERASEBKGND:
          /* We clear our own window */
          return 1;

      case WM_WINDOWPOSCHANGED:
          /* Should be processed by DefWindowProc, otherwise a double buffered
           * context is not properly resized when the corresponding window is
           * resized. */
          break;

      case WM_DESTROY:
          if (tkglPtr && tkglPtr->tkwin != NULL) {
              if (tkglPtr->setGrid > 0) {
                  Tk_UnsetGrid(tkglPtr->tkwin);
              }
              (void) Tcl_DeleteCommandFromToken(tkglPtr->interp,
			 tkglPtr->widgetCmd);
          }
          break;

      case WM_DISPLAYCHANGE:
          if (tkglPtr->pBufferFlag && hasARBPbuffer && !tkglPtr->pBufferLost) {
              queryPbuffer(tkglPtr->pbuf, WGL_PBUFFER_LOST_ARB,
                      &tkglPtr->pBufferLost);
          }
	  
      default:
          return TkWinChildProc(hwnd, message, wParam, lParam);
    }
    answer = DefWindowProc(hwnd, message, wParam, lParam);
    Tcl_ServiceAll();
    return answer;
}

/*
 * tkglCreateDummyWindow
 *
 * A peculiarity of WGL, the Weird GL, is that creating a rendering context
 * with prescribed attributes requires that a rendering context already
 * exist.  This Windows-only static function creates a hidden window
 * with a device context that supports a simple opengl configuration
 * which should be supported by any WGL implementation.  A pointer to
 * the hidden window is returned.  After creating
 * a rendering context for the hidden window and making it current it
 * becomes possible to query the opengl implementation to find out
 * which pixel formats are available and choose an appropriate one.
 * The hidden window can then be destroyed.
 */

static HWND
tkglCreateDummyWindow()
{
    static char ClassName[] = "TkglFakeWindow";
    WNDCLASS wc;
    HINSTANCE instance = GetModuleHandle(NULL);
    HWND    wnd;
    HDC     dc;
    PIXELFORMATDESCRIPTOR pfd;
    int     pixelFormat;
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = instance;
    wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = ClassName;
    if (!RegisterClass(&wc)) {
        DWORD   err = GetLastError();

        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            fprintf(stderr, "Unable to register Tkgl Test Window class\n");
            return NULL;
        }
    }
    wnd = CreateWindow(ClassName, "create WGL device context",
            WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            0, 0, 1, 1, NULL, NULL, instance, NULL);
    if (wnd == NULL) {
        fprintf(stderr, "Unable to create temporary OpenGL window\n");
        return NULL;
    }
    dc = GetDC(wnd);
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 3;
    pfd.iLayerType = PFD_MAIN_PLANE;
    pixelFormat = ChoosePixelFormat(dc, &pfd);
    if (pixelFormat == 0) {
        fprintf(stderr, "Unable to choose simple pixel format\n");
        ReleaseDC(wnd, dc);
        return NULL;
    }
    if (!SetPixelFormat(dc, pixelFormat, NULL)) {
        fprintf(stderr, "Unable to set simple pixel format\n");
        ReleaseDC(wnd, dc);
        return NULL;
    }
    ShowWindow(wnd, SW_HIDE);   // make sure it's hidden
    ReleaseDC(wnd, dc);
    return wnd;
}

static HPBUFFERARB
tkgl_createPbuffer(Tkgl *tkglPtr)
{
    int     attribs[32];
    int     na = 0;
    HPBUFFERARB pbuf;

    if (tkglPtr->largestPbufferFlag) {
        attribs[na++] = WGL_PBUFFER_LARGEST_ARB;
        attribs[na++] = 1;
    }
    attribs[na] = 0;
    pbuf = createPbuffer(tkglPtr->deviceContext, (int) tkglPtr->pixelFormat,
			 tkglPtr->width, tkglPtr->height, attribs);
    if (pbuf && tkglPtr->largestPbufferFlag) {
        queryPbuffer(pbuf, WGL_PBUFFER_WIDTH_ARB, &tkglPtr->width);
        queryPbuffer(pbuf, WGL_PBUFFER_HEIGHT_ARB, &tkglPtr->height);
    }
    return pbuf;
}

static void
tkgl_destroyPbuffer(Tkgl *tkglPtr)
{
    destroyPbuffer(tkglPtr->pbuf);
}

static int
tkgl_describePixelFormat(Tkgl *tkglPtr)
{
    if (getPixelFormatAttribiv == NULL) {
        PIXELFORMATDESCRIPTOR pfd;

	/* Fetch the attributes of the pixelformat we are using. */
        DescribePixelFormat(tkglPtr->deviceContext,
	    (int) tkglPtr->pixelFormat, sizeof (pfd), &pfd);
        /* Use them to fill in flags in the widget record. */
        tkglPtr->rgbaFlag = pfd.iPixelType == PFD_TYPE_RGBA;
        tkglPtr->doubleFlag = (pfd.dwFlags & PFD_DOUBLEBUFFER) != 0;
        tkglPtr->depthFlag = (pfd.cDepthBits != 0);
        tkglPtr->accumFlag = (pfd.cAccumBits != 0);
        tkglPtr->alphaFlag = (pfd.cAlphaBits != 0);
        tkglPtr->stencilFlag = (pfd.cStencilBits != 0);
        if ((pfd.dwFlags & PFD_STEREO) != 0)
            tkglPtr->stereo = TKGL_STEREO_NATIVE;
        else
            tkglPtr->stereo = TKGL_STEREO_NONE;
    } else {
        static int attribs[] = {
            WGL_PIXEL_TYPE_ARB,
            WGL_DOUBLE_BUFFER_ARB,
            WGL_DEPTH_BITS_ARB,
            WGL_ACCUM_RED_BITS_ARB,
            WGL_ALPHA_BITS_ARB,
            WGL_STENCIL_BITS_ARB,
            WGL_STEREO_ARB,
            WGL_SAMPLES_ARB
        };
#define NUM_ATTRIBS (sizeof attribs / sizeof attribs[0])
        int     info[NUM_ATTRIBS];

        getPixelFormatAttribiv(tkglPtr->deviceContext,
	    (int) tkglPtr->pixelFormat, 0, NUM_ATTRIBS, attribs, info);
#undef NUM_ATTRIBS
        tkglPtr->rgbaFlag = info[0];
        tkglPtr->doubleFlag = info[1];
        tkglPtr->depthFlag = (info[2] != 0);
        tkglPtr->accumFlag = (info[3] != 0);
        tkglPtr->alphaFlag = (info[4] != 0);
        tkglPtr->stencilFlag = (info[5] != 0);
        tkglPtr->stereo = info[6] ? TKGL_STEREO_NATIVE : TKGL_STEREO_NONE;
        tkglPtr->multisampleFlag = (info[7] != 0);
    }
    return True;
}

/*
 * Tkgl_CreateGLContext
 *
 * Creates an OpenGL rendering context for the widget.  It is called when the
 * widget is created, before it is mapped. For Windows and macOS, creating a
 * rendering context also requires creating the rendering surface, which is
 * a child window on Windows.  The child occupies  the rectangle
 * in the toplevel window belonging to the Tkgl widget.
 *
 * On Windows it is necessary to create a dummy rendering context, associate
 * with a hidden window, in order to query the OpenGL server to find out what
 * pixel formats are available and to obtain pointers to functions needed to
 * create a rendering context.  These functions are provided of the graphics
 * card driver rather than by the openGL library.
 *  
 * The OpenGL documentation acknowledges that this is weird, but proclaims
 * that it is just how WGL works.  So there.
 */

static const int attributes_2_1[] = {    
    WGL_CONTEXT_MAJOR_VERSION_ARB, 2,
    WGL_CONTEXT_MINOR_VERSION_ARB, 1,
    0                                       
};                                          

static const int attributes_3_0[] = {    
    WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
    WGL_CONTEXT_MINOR_VERSION_ARB, 0,
    0                                       
};                                          

static const int attributes_3_2[] = {       
    WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
    WGL_CONTEXT_MINOR_VERSION_ARB, 2,
    0                                       
};                                          
                                            
static const int attributes_4_1[] = {       
    WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
    WGL_CONTEXT_MINOR_VERSION_ARB, 1,
    0                                       
};                                          

static int
tkglCreateChildWindow(
    Tkgl *tkglPtr)
{
    HWND    parentWin;
    DWORD   style;
    int     width, height;
    HINSTANCE hInstance= Tk_GetHINSTANCE();
    Bool    createdPbufferDC = False;
    // We assume this is called with the dummy context current
    // and with a pixelformat stored in the widget record.
    
    if (!TkglClassInitialized) {
        WNDCLASS TkglClass;

        TkglClassInitialized = True;
        TkglClass.style = CS_HREDRAW | CS_VREDRAW;
        TkglClass.cbClsExtra = 0;
        TkglClass.cbWndExtra = sizeof (LONG_PTR);       /* to save Tkgl* */
        TkglClass.hInstance = hInstance;
        TkglClass.hbrBackground = NULL;
        TkglClass.lpszMenuName = NULL;
        TkglClass.lpszClassName = TKGL_CLASS_NAME;
        TkglClass.lpfnWndProc = Win32WinProc;
        TkglClass.hIcon = NULL;
        TkglClass.hCursor = NULL;
        if (!RegisterClass(&TkglClass)) {
            Tcl_SetResult(tkglPtr->interp,
                    "unable register Tkgl window class", TCL_STATIC);
            goto error;
        }
    }

    if (!tkglPtr->pBufferFlag) {
	//parentWin = Tk_GetHWND(parent);
	parentWin = Tk_GetHWND(Tk_WindowId(Tk_Parent(tkglPtr->tkwin)));
        style = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    } else {
        parentWin = NULL;
        style = WS_POPUP | WS_CLIPCHILDREN;
    }
    if (tkglPtr->pBufferFlag) {
        width = height = 1;     /* TODO: demo code mishaves when set to 1000 */
    } else {
        width = tkglPtr->width;
        height = tkglPtr->height;
    }
    tkglPtr->child = CreateWindowEx(WS_EX_NOPARENTNOTIFY, TKGL_CLASS_NAME,
	 NULL, style, 0, 0, width, height, parentWin, NULL, hInstance, NULL);
    if (!tkglPtr->child) {
      char *msg;
      DWORD errorcode = GetLastError();
      FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		    NULL, errorcode, 0, (LPSTR)&msg, 0, NULL);
      fprintf(stderr, "%s\n", msg);
      goto error;
    }
    SetWindowLongPtr(tkglPtr->child, 0, (LONG_PTR) tkglPtr);
    SetWindowPos(tkglPtr->child, HWND_TOP, 0, 0, 0, 0,
		 SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    if (tkglPtr->pBufferFlag) {
        ShowWindow(tkglPtr->child, SW_HIDE); /* make sure it's hidden */
    }

    /* Fill out the widget record from the pixel format attribures. */
    if (!tkgl_describePixelFormat(tkglPtr)) {
	Tcl_SetResult(tkglPtr->interp,
	    "Pixel format is not consistent with widget configurait.",
	    TCL_STATIC);
	goto error;
    }
    if (tkglPtr->pBufferFlag) {
        tkglPtr->pbuf = tkgl_createPbuffer(tkglPtr);
        if (tkglPtr->pbuf == NULL) {
            Tcl_SetResult(tkglPtr->interp,
                    "couldn't create pbuffer", TCL_STATIC);
            goto error;
        }
        ReleaseDC(tkglPtr->child, tkglPtr->deviceContext);
        tkglPtr->deviceContext = getPbufferDC(tkglPtr->pbuf);
        createdPbufferDC = True;
    } else {
	/* Install the pixelFormat in the child's device context. */
	tkglPtr->deviceContext = GetDC(tkglPtr->child);
	bool result = SetPixelFormat(tkglPtr->deviceContext,
				     (int) tkglPtr->pixelFormat, NULL);
	if (result == FALSE) {
	    Tcl_SetResult(tkglPtr->interp,
		"Couldn't set child's pixel format", TCL_STATIC);
	    goto error;
	}
    }
    
    /* 
     * Create an OpenGL rendering context for the child, or get
     a shared context.
     */
    
    if (tkglPtr->shareContext &&
	FindTkgl(tkglPtr, tkglPtr->shareContext)) {
        /* share OpenGL context with existing Tkgl widget */
        Tkgl   *shareWith = FindTkgl(tkglPtr, tkglPtr->shareContext);

        if (tkglPtr->pixelFormat != shareWith->pixelFormat) {
            Tcl_SetResult(tkglPtr->interp,
                    "Unable to share OpenGL context.", TCL_STATIC);
            goto error;
        }
        tkglPtr->context = shareWith->context;
    } else {
	int *attributes = NULL;
	switch(tkglPtr->profile) {
	case PROFILE_LEGACY:
	    attributes = attributes_2_1;
	    break;
	case PROFILE_3_2:
	    attributes = attributes_3_2;
	    break;
	case PROFILE_4_1:
	    attributes = attributes_4_1;
	    break;
	case PROFILE_SYSTEM:
	    break;
	}
	if (createContextAttribs && attributes) {
	    tkglPtr->context = createContextAttribs(
	        tkglPtr->deviceContext, 0, attributes);
	    Tkgl_MakeCurrent(tkglPtr);
	} else {
	    fprintf(stderr,
	    "WARNING: wglCreateContextAttribsARB is not being used.\n"
	    "Your GL version will depend on your graphics driver.\n");
	}
    }
    if (tkglPtr->shareList) {
        /* share display lists with existing tkgl widget */
        Tkgl *shareWith = FindTkgl(tkglPtr, tkglPtr->shareList);

        if (shareWith) {
            if (!wglShareLists(shareWith->context, tkglPtr->context)) {
                Tcl_SetResult(tkglPtr->interp,
                        "unable to share display lists", TCL_STATIC);
                goto error;
            }
            tkglPtr->contextTag = shareWith->contextTag;
        }
    }
    if (tkglPtr->context == NULL) {
        Tcl_SetResult(tkglPtr->interp,
                "Could not create rendering context", TCL_STATIC);
        goto error;
    }    
    return TCL_OK;

 error:
    tkglPtr->badWindow = True;
    if (tkglPtr->deviceContext) {
        if (createdPbufferDC) {
            releasePbufferDC(tkglPtr->pbuf, tkglPtr->deviceContext);
        }
	if (tkglPtr->child) {
	    if (tkglPtr->deviceContext) {
                ReleaseDC(tkglPtr->child, tkglPtr->deviceContext);
		tkglPtr->deviceContext = NULL;
	    }
	    DestroyWindow(tkglPtr->child);
	    tkglPtr->child = NULL;
	}
    }
    return TCL_ERROR;    
}

/* Attributes to pass to wglChoosePixelFormatARB/EXT. */
static const int attribList[] = {
    WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
    WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
    WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
    WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
    WGL_COLOR_BITS_ARB,     24,
    WGL_ALPHA_BITS_ARB,     8,	
    WGL_DEPTH_BITS_ARB,     24,
    WGL_STENCIL_BITS_ARB,   8,
    0,
};

int
Tkgl_CreateGLContext(
    Tkgl *tkglPtr)
{
    HDC   dc;  /* Device context handle */
    HGLRC rc;  /* Rendering context handle */
    HWND  dummy = NULL;
    int   pixelFormat;
    UINT  numFormats;

    dummy = tkglCreateDummyWindow();
    if (dummy == NULL) {
	Tcl_SetResult(tkglPtr->interp,
		      "can't create dummy OpenGL window",
		      TCL_STATIC);
	return 0;
    }
    dc = GetDC(dummy);
    rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    ReleaseDC(dummy, dc);

    /*
     * Now that we have a current context, initialize our
     * our function pointers, and request a pixel format.
     */
    initializeDeviceProcs();

    /* Cache the extension string pointer in the widget record. */
    tkglPtr->extensions = (const char *) getExtensionsString(dc);

    /* Check for multisampling in the extensions string */
    if (strstr(tkglPtr->extensions, "WGL_ARB_multisample") != NULL
	|| strstr(tkglPtr->extensions, "WGL_EXT_multisample") != NULL) {
	hasMultisampling = TRUE;
    }

    /* Choose the pixel format that best matches our requirements. */
    if (choosePixelFormat == NULL) {
	Tcl_SetResult(tkglPtr->interp,
	    "Neither wglChoosePixelFormatARB nor wglChoosePixelFormatEXT "
	    "are available in this openGL.\n"
	    "We cannot create an OpenGL rendering context.",
	    TCL_STATIC);
	return TCL_ERROR;
    }
    choosePixelFormat(dc, attribList, NULL, 1, &pixelFormat,
			  &numFormats);

    /* Save the pixel format in the widget record. */
    tkglPtr->pixelFormat = pixelFormat;

    /* Create a child window to use as the rendering surface. */
    if (tkglCreateChildWindow(tkglPtr) != TCL_OK) {
	fprintf(stderr, "Failed to create child window.\n");
	return TCL_ERROR;
    }
    
    /* Destroy the dummy window. */
    if (dummy != NULL) {
	DestroyWindow(dummy);
    } else {
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 * Tkgl_MakeWindow
 *
 * This is a callback function registered to be called by Tk_MakeWindowExist
 * when the tkgl widget is mapped. It fills out the widget record and does
 * other Tk-related initialization.  This function is not allowed to fail.  It
 * must return a valid X window identifier.  If something goes wrong, it sets
 * the badWindow flag in the widget record, which is passed to it as the
 * instanceData.
 */

Window
Tkgl_MakeWindow(Tk_Window tkwin, Window parent, ClientData instanceData)
{
    Tkgl   *tkglPtr = (Tkgl *) instanceData;
    Display *dpy = Tk_Display(tkwin);
    int     scrnum = Tk_ScreenNumber(tkwin);
    Colormap cmap;
    Window  window = None;
    Bool    createdPbufferDC = False;
    PIXELFORMATDESCRIPTOR pfd;

    if (tkglPtr->badWindow) {
	/*
	 * This function has been called before and it failed.  This test
	 * exists because this callback function is not allowed to fail.  It
	 * must return a valid X Window Id.
	 */
        return Tk_MakeWindow(tkwin, parent);
    }
    /* We require that TkglCreateGLContext has been called. */
    if (tkglPtr->child) {
	window = Tk_AttachHWND(tkwin, tkglPtr->child);
    } else {
	goto error;
    }
    
    /* For indexed color mode. */
    if (tkglPtr->redMap)
        free(tkglPtr->redMap);
    if (tkglPtr->greenMap)
        free(tkglPtr->greenMap);
    if (tkglPtr->blueMap)
        free(tkglPtr->blueMap);
    tkglPtr->redMap = tkglPtr->greenMap = tkglPtr->blueMap = NULL;
    tkglPtr->mapSize = 0;

    if ( tkglPtr->pBufferFlag) {
	/* We don't need a colormap, nor overlay, since the
	   rendering surface won't be displayed */
	goto done;
    }

    /* 
     * Find a colormap.
     */

    /* Fetch the attributes of the pixelformat we are using. */
    DescribePixelFormat(tkglPtr->deviceContext,
	(int) tkglPtr->pixelFormat, sizeof (pfd), &pfd);
    if (tkglPtr->rgbaFlag) {
        /* Colormap for RGB mode */
        if (pfd.dwFlags & PFD_NEED_PALETTE) {
            cmap = Win32CreateRgbColormap(pfd);
        } else {
            cmap = DefaultColormap(dpy, scrnum);
        }
    } else {
        /* Colormap for CI mode */
        /* this logic is to overcome a combination driver/compiler bug: (1)
         * cColorBits may be unusally large (e.g., 32 instead of 8 or 12) and
         * (2) 1 << 32 might be 1 instead of zero (gcc for ia32) */
        if (pfd.cColorBits >= MAX_CI_COLORMAP_BITS) {
            tkglPtr->ciColormapSize = MAX_CI_COLORMAP_SIZE;
        } else {
            tkglPtr->ciColormapSize = 1 << pfd.cColorBits;
            if (tkglPtr->ciColormapSize >= MAX_CI_COLORMAP_SIZE)
                tkglPtr->ciColormapSize = MAX_CI_COLORMAP_SIZE;
        }
        if (tkglPtr->privateCmapFlag) {
            /* need read/write colormap so user can store own color entries */
            cmap = Win32CreateCiColormap(tkglPtr);
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

    /* Install the colormap */
    SelectPalette(tkglPtr->deviceContext,
	((TkWinColormap *) cmap)->palette, TRUE);
    RealizePalette(tkglPtr->deviceContext);

    if (!tkglPtr->doubleFlag) {
        /* See if we requested single buffering but had to accept a double
         * buffered visual.  If so, set the GL draw buffer to be the front
         * buffer to simulate single buffering. */
        if (getPixelFormatAttribiv == NULL) {
            /* pfd is already set */
            if ((pfd.dwFlags & PFD_DOUBLEBUFFER) != 0) {
                wglMakeCurrent(tkglPtr->deviceContext, tkglPtr->context);
                glDrawBuffer(GL_FRONT);
                glReadBuffer(GL_FRONT);
            }
        } else {
            static int attribs[] = {
                WGL_DOUBLE_BUFFER_ARB,
            };
#define NUM_ATTRIBS (sizeof attribs / sizeof attribs[0])
            int     info[NUM_ATTRIBS];

            getPixelFormatAttribiv(tkglPtr->deviceContext,
		(int) tkglPtr->pixelFormat, 0, NUM_ATTRIBS, attribs, info);
#undef NUM_ATTRIBS
            if (info[0]) {
                wglMakeCurrent(tkglPtr->deviceContext, tkglPtr->context);
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

        index_size = tkglPtr->ciColormapSize;
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

    /* Create visual info */
    if (tkglPtr->visInfo == NULL) {
        /* 
         * Create a new OpenGL rendering context. And check whether
	 * to share lists.
         */
        Visual *visual;
        /* Just for portability, define the simplest visinfo */
        visual = DefaultVisual(dpy, scrnum);
        tkglPtr->visInfo = (XVisualInfo *) calloc(1, sizeof (XVisualInfo));
        tkglPtr->visInfo->screen = scrnum;
        tkglPtr->visInfo->visual = visual;
        tkglPtr->visInfo->visualid = visual->visualid;
#if defined(__cplusplus) || defined(c_plusplus)
        tkglPtr->visInfo->c_class = visual->c_class;
        tkglPtr->visInfo->depth = visual->bits_per_rgb;
#endif
    }
    /*
     * Make sure Tk knows to switch to the new colormap when the cursor is
     * over this window when running in color index mode.
     */
    (void) Tk_SetWindowVisual(tkwin, tkglPtr->visInfo->visual,
            tkglPtr->visInfo->depth, cmap);

 done:
    return window;

 error:
    tkglPtr->badWindow = True;
    if (tkglPtr->deviceContext) {
        if (createdPbufferDC) {
            releasePbufferDC(tkglPtr->pbuf, tkglPtr->deviceContext);
        }
        tkglPtr->deviceContext = NULL;
    }
    return window;
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
    bool result = wglMakeCurrent(tkglPtr->deviceContext,
				 tkglPtr->context);
    if (!result) {
	fprintf(stderr, "wglMakeCurrent failed\n");
    }
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
    const Tkgl *tkglPtr)
{
    if (tkglPtr->doubleFlag) {
        int result = SwapBuffers(tkglPtr->deviceContext);
	if (!result) {
	    fprintf(stderr, "SwapBuffers failed\n");
	}
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
    /*
     * We already requested, and cached the extensions string in
     * Tkgl_CreateGLContext, so we can just return the cached string.
     */

    return tkglPtr->extensions;
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


void
Tkgl_FreeResources(
    Tkgl *tkglPtr)
{
    wglMakeCurrent(NULL, NULL);
    if (tkglPtr->deviceContext) {
        ReleaseDC(tkglPtr->child, tkglPtr->deviceContext);
	tkglPtr->deviceContext = NULL;
	if (tkglPtr->pBufferFlag) {
	    releasePbufferDC(tkglPtr->pbuf, tkglPtr->deviceContext);
	}
    }
    if (tkglPtr->context) {
	if (FindTkglWithSameContext(tkglPtr) == NULL) {
	    wglDeleteContext(tkglPtr->context);
	    tkglPtr->context = NULL;
	    if (tkglPtr->visInfo) {
		free(tkglPtr->visInfo);
		tkglPtr->visInfo = NULL;
	    }
	}
    }
    if (tkglPtr->child) {
	DestroyWindow(tkglPtr->child);
    }
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
