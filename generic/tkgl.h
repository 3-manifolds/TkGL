/*
 * tkgl.h --
 *
 * Copyright (C) 2024, Marc Culler, Nathan Dunfield, Matthias Goerner
 *
 * This file is part of the TkGL project.  TkGL is derived from Togl, which
 * was written by Brian Paul, Ben Bederson and Greg Couch.  TkGL is licensed
 * under the Tcl license.  The terms of the license are described in the file
 * "license.terms" which should be included with this distribution.
 */

#ifndef USE_TCL_STUBS
#   define USE_TCL_STUBS
#endif
#ifndef USE_TK_STUBS
#   define USE_TK_STUBS
#endif
#include "tk.h"

#if TCL_MAJOR_VERION == 9
/* Allow this package to load after Tcl 9.0 is released */
#   define Tcl_InitStubs(interp, version, exact) \
	(Tcl_InitStubs)(interp, ((exact) ? (version) : "9b0"), \
	    (exact)|(TCL_MAJOR_VERSION<<8)|(TCL_MINOR_VERSION<<16), \
	    TCL_STUB_MAGIC)
#endif
#if TCL_MAJOR_VERSION == 8
#define TCL_INDEX_NONE (-1)
typedef int Tcl_Size;
#define Tk_MakeWindow(tkwin, parent) TkpMakeWindow((TkWindow *)tkwin, parent)
#endif
#include "tkglPlatform.h"

/*
 * Forward declarations
 */

typedef struct Tkgl Tkgl;

/*
 * Enum used for the -profile option to specify an OpenGL profile.
 */

enum profile {
    PROFILE_LEGACY, PROFILE_3_2, PROFILE_4_1, PROFILE_SYSTEM
};


/*
 * The Tkgl widget record.  Each Tkgl widget maintains one of these.
 */

typedef struct Tkgl {
    struct Tkgl *next;          /* Next in a linked list of all tkgl widgets.*/
    Tk_Window tkwin;		/* Window identifier for the tkgl widget.*/
    Display *display;		/* X token for the window's display. */
    Tcl_Interp *interp;		/* Interpreter associated with widget. */
    Tcl_Command widgetCmd;	/* Token for tkgl's widget command. */
    Tk_OptionTable optionTable; /* Token representing the option specs. */
    int updatePending;		/* A call to TkglDisplay has been scheduled. */
    int x, y;                   /* Upper left corner of Tkgl widget */
    int width;	                /* Width of tkgl widget in pixels. */
    int height;	                /* Height of tkgl widget in pixels. */
    int setGrid;                /* positive is grid size for window manager */
    int contextTag;             /* contexts with same tag share display lists */
    XVisualInfo *visInfo;       /* Visual info of the widget */
    Tk_Cursor cursor;           /* The widget's cursor */
    int     timerInterval;      /* Time interval for timer in milliseconds */
    Tcl_TimerToken timerHandler;  /* Token for tkgl's timer handler */
    Bool    rgbaFlag;           /* configuration flags (ala GLX parameters) */
    int     rgbaRed;
    int     rgbaGreen;
    int     rgbaBlue;
    Bool    doubleFlag;
    Bool    depthFlag;
    int     depthSize;
    Bool    accumFlag;
    int     accumRed;
    int     accumGreen;
    int     accumBlue;
    int     accumAlpha;
    Bool    alphaFlag;
    int     alphaSize;
    Bool    stencilFlag;
    int     stencilSize;
    Bool    privateCmapFlag;
    Bool    overlayFlag;
    int     stereo;
    double  eyeSeparation;
    double  convergence;
    GLuint  riStencilBit;       /* row interleaved stencil bit */
    int     auxNumber;
    enum    profile profile;
    int     swapInterval;
    Bool    multisampleFlag;
    Bool    fullscreenFlag;
    Bool    pBufferFlag;
    Bool    largestPbufferFlag;
    const char *shareList;      /* name (ident) of Tkgl to share dlists with */
    const char *shareContext;   /* name (ident) to share OpenGL context with */
    const char *ident;          /* User's identification string */
    void    *clientData;        /* Pointer to user data */
    Bool    UpdatePending;      /* Should normal planes be redrawn? */
    Tcl_Obj *createProc;        /* Callback when widget is realized */
    Tcl_Obj *displayProc;       /* Callback when widget is redrawn */
    Tcl_Obj *reshapeProc;       /* Callback when window size changes */
    Tcl_Obj *destroyProc;       /* Callback when widget is destroyed */
    Tcl_Obj *timerProc;         /* Callback when widget is idle */
    Window  overlayWindow;      /* The overlay window, or 0 */
    Tcl_Obj *overlayDisplayProc;     /* Overlay redraw proc */
    Bool    overlayUpdatePending;    /* Should overlay be redrawn? */
    Colormap overlayCmap;            /* colormap for overlay is created */
    int     overlayTransparentPixel; /* transparent pixel */
    Bool    overlayIsMapped;
    GLfloat *redMap;            /* Index2RGB Maps for Color index modes */
    GLfloat *greenMap;
    GLfloat *blueMap;
    GLint   mapSize;            /* Number of indices in our color map */
    int     currentStereoBuffer;
    int     badWindow;          /* true when Tkgl_MakeWindow fails or should
                                 * create a dummy window */
#if defined(TKGL_WGL)
    HGLRC   context;            /* OpenGL rendering context */
    HDC     deviceContext;      /* Device context */
    HWND    child;              /* rendering surface for the context */
    HPBUFFERARB pbuf;
    int     ciColormapSize;     /* (Maximum) size of indexed colormap */
    int     pBufferLost;
    int     pixelFormat;
    const char *extensions;
#elif defined(TKGL_X11)
    GLXContext context;         /* OpenGL context for normal planes */
    GLXContext overlayContext;  /* OpenGL context for overlay planes */
    unsigned long pixelFormat;  /* visualID */
    GLXPbuffer pbuf;
    Window surface;             /* rendering surface for the context */
    GLXFBConfig fbcfg;          /* cached FBConfig */
    Tcl_TimerToken timerToken;

#elif defined(TKGL_NSOPENGL)
    NSOpenGLContext *context;
    NSOpenGLPixelFormat *pixelFormat;
    NSOpenGLPixelBuffer *pbuf;
    NSView *nsview;             /* rendering surface for the context */
    const char *extensions;
#endif
} Tkgl;

/* The typeMasks used in option specs. */

#define GEOMETRY_MASK 0x1       /* widget geometry */
#define FORMAT_MASK 0x2         /* pixel format */
#define CURSOR_MASK 0x4
#define TIMER_MASK 0x8
#define OVERLAY_MASK 0x10
#define SWAP_MASK 0x20
#define STEREO_MASK 0x40
#define STEREO_FORMAT_MASK 0x80

/* Default values for options. */

#define DEFAULT_WIDTH		"400"
#define DEFAULT_HEIGHT		"400"
#define DEFAULT_IDENT		""
#define DEFAULT_FONTNAME	"Courier"
#define DEFAULT_TIME		"1"

/* 
 * Stereo techniques:
 *      Only the native method uses OpenGL quad-buffered stereo.
 *      All need the eye offset and eye distance set properly.
 */
/* These versions need one eye drawn */
#  define TKGL_STEREO_NONE		0
#  define TKGL_STEREO_LEFT_EYE		1       /* just the left eye */
#  define TKGL_STEREO_RIGHT_EYE		2       /* just the right eye */
#  define TKGL_STEREO_ONE_EYE_MAX	127
/* These versions need both eyes drawn */
#  define TKGL_STEREO_NATIVE		128
#  define TKGL_STEREO_SGIOLDSTYLE	129     /* interlaced, SGI API */
#  define TKGL_STEREO_ANAGLYPH		130
#  define TKGL_STEREO_CROSS_EYE		131
#  define TKGL_STEREO_WALL_EYE		132
#  define TKGL_STEREO_DTI		133     /* dti3d.com */
#  define TKGL_STEREO_ROW_INTERLEAVED	134     /* www.vrex.com/developer/interleave.htm */

#ifndef STEREO_BUFFER_NONE
/* From <X11/extensions/SGIStereo.h>, but we use this constants elsewhere */
#  define STEREO_BUFFER_NONE 0
#  define STEREO_BUFFER_LEFT 1
#  define STEREO_BUFFER_RIGHT 2
#endif

/*
 * Declarations of utility functions defined in tkgl.c.
 */

Tkgl* FindTkgl(Tkgl *tkgl, const char *ident);
Tkgl* FindTkglWithSameContext(const Tkgl *tkgl);
int   Tkgl_CallCallback(Tkgl *tkgl, Tcl_Obj *cmd);

/*
 * The functions declared below constitute the interface
 * provided by the platform code for each platform.
 */

/*
 * Tkgl_CreateGLContext
 *
 * Creates an OpenGL rendering context for the widget.  It is called when the
 * widget is created, before it is mapped. Creating a rendering context also
 * requires creating the rendering surface.  The surface is an NSView on
 * macOS, a child window on Windows and an X Window (i.e. an X widget) on
 * systems using the X window manager.  In each case the surface fills the
 * rectangle in the toplevel window which is occupied by the Tkgl widget.
 */

int Tkgl_CreateGLContext(Tkgl *tkglPtr);

/*
 * Tkgl_MakeWindow
 *
 * This is a callback function which is called by Tk_MakeWindowExist
 * when the tkgl widget is mapped.  It sets up the widget record and
 * does other Tk-related initialization.  This function is not allowed
 * to fail.  I must return a valid X window identifier.  If something
 * goes wrong, it sets the badWindow flag in the widget record,
 * which is passed as the instanceData.
 */

Window Tkgl_MakeWindow(Tk_Window tkwin, Window parent, void* instanceData);

/*
 * Tkgl_MakeCurrent
 *
 * This is the key function of the Tkgl widget in its role as the
 * manager of an NSOpenGL rendering context.  Must be called by
 * a GL client before drawing into the widget.
 */

void Tkgl_MakeCurrent(const Tkgl *tkglPtr);

/*
 * Tkgl_SwapBuffers
 *
 * Called by the GL Client after updating the image.  If the Tkgl
 * is double-buffered it interchanges the front and back framebuffers.
 * otherwise it calls GLFlush.
 */

void Tkgl_SwapBuffers(const Tkgl *tkglPtr);

/*
 * TkglUpdate
 *
 * Called by TkglDisplay whenever the size of the Tkgl widget may
 * have changed.  On macOS it adjusts the frame of the NSView that
 * is being used as the rendering surface.  The other platforms
 * handle the size changes automatically.
 */

void Tkgl_Update(const Tkgl *tkglPtr);

/*
 * Tkgl_GetExtensions
 *
 * Queries the rendering context for its extension string, a
 * space-separated list of the names of all supported GL extensions.
 * The string is cached in the widget record and the cached
 * string is returned in subsequent calls.
 */

const char* Tkgl_GetExtensions(Tkgl *tkglPtr);

void Tkgl_FreeResources(Tkgl *tkglPtr);
int Tkgl_TakePhoto(Tkgl *tkglPtr, Tk_PhotoHandle photo);
int Tkgl_CopyContext(const Tkgl *from, const Tkgl *to, unsigned mask);
void Tkgl_MapWidget(ClientData instanceData);
void Tkgl_UnmapWidget(ClientData instanceData);
void Tkgl_WorldChanged(void* instanceData);

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
