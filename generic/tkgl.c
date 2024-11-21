/*
 * tkgl.c --
 *
 * Copyright (C) 2024, Marc Culler, Nathan Dunfield, Matthias Goerner
 *
 * This file is part of the TkGL project.  TkGL is derived from Togl, which
 * was written by Brian Paul, Ben Bederson and Greg Couch.  TkGL is licensed
 * under the Tcl license.  The terms of the license are described in the file
 * "license.terms" which should be included with this distribution.
 */

#include "tcl.h"
#include "tk.h"
#include "tkgl.h"
#include "tkglOptions.h" 
#include <string.h>

/*
 * Declarations of static functions defined in this file:
 */

static void TkglDeletedProc(void *clientData);
static int  TkglConfigure(Tcl_Interp *interp, Tkgl *tkglPtr);
static void TkglDisplay(void *clientData);
static void TkglObjEventProc(void *clientData, XEvent *eventPtr);
static int  TkglWidgetObjCmd(void *clientData, Tcl_Interp *interp, int objc,
			     Tcl_Obj * const objv[]);
static int  ObjectIsEmpty(Tcl_Obj *objPtr);
static void TkglPostRedisplay(Tkgl *tkglPtr);
static void TkglFrustum(const Tkgl *tkgl, GLdouble left, GLdouble right,
			GLdouble bottom, GLdouble top, GLdouble zNear,
			GLdouble zFar);
static void TkglOrtho(const Tkgl *tkgl, GLdouble left, GLdouble right,
		       GLdouble bottom, GLdouble top, GLdouble zNear,
		      GLdouble zFar);
static int GetTkglFromObj(Tcl_Interp *interp, Tcl_Obj *obj, Tkgl **target);

/*
 * The Tkgl package maintains a per-thread list of all Tkgl widgets.
 */

typedef struct {
    Tkgl *tkglHead;        /* Head of linked list of all Tkgl widgets. */
    int nextContextTag;    /* Used to assign similar context tags. */
    int initialized;       /* Set to 1 when the struct is initialized. */ 
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;
static void addToList(Tkgl *t);
static void removeFromList(Tkgl *t);


/*
 *--------------------------------------------------------------
 *
 * TkglObjCmd --
 *
 *	This procedure is invoked to process the "tkgl" Tcl command. It
 *	creates a new "tkgl" widget.  After allocating and initializing
 *      the widget record it first calls TkglConfigure to set up the
 *      widget with the specified option values.  Then it calls
 *      Tkgl_CreateGLContext to create a GL rendering context.  This
 *      allows the client to start drawing before the widget gets
 *      mapped.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	A new widget is created and configured.
 *
 *--------------------------------------------------------------
 */

int
TkglObjCmd(
    void *clientData,
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tkgl *tkglPtr;
    Tk_Window tkwin = NULL;
    Tk_OptionTable optionTable;

    /* 
     * Setup the Tk_ClassProcs callbacks.
     */

    static Tk_ClassProcs procs = {0};
    if (procs.size == 0) {
	procs.size = sizeof(Tk_ClassProcs);
	procs.worldChangedProc = Tkgl_WorldChanged;
	procs.createProc = Tkgl_MakeWindow;
	procs.modalProc = NULL;
    } 
   
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    if (!tsdPtr->initialized) {
	tsdPtr->initialized = 1;
    }
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "pathName ?-option value ...?");
	return TCL_ERROR;
    }
    tkwin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp),
    	    Tcl_GetString(objv[1]), NULL);
    if (tkwin == NULL) {
	return TCL_ERROR;
    }
    Tk_SetClass(tkwin, "Tkgl");

    /*
     * Create the option table for this widget class. If it has already been
     * created, the refcount will get bumped and just the pointer will be
     * returned. The refcount getting bumped does not concern us, because Tk
     * will ensure the table is deleted when the interpreter is destroyed.
     */

    optionTable = Tk_CreateOptionTable(interp, tkglOptionSpecs);

    /*
     * Allocate and initialize the widget record.
     */

    tkglPtr = (Tkgl *)ckalloc(sizeof(Tkgl));
    memset(tkglPtr, 0, sizeof(Tkgl));
    tkglPtr->tkwin = tkwin;
    tkglPtr->display = Tk_Display(tkwin);
    tkglPtr->interp = interp;
    tkglPtr->widgetCmd = Tcl_CreateObjCommand(interp,
	    Tk_PathName(tkglPtr->tkwin), TkglWidgetObjCmd, tkglPtr,
	    TkglDeletedProc);
    tkglPtr->optionTable = optionTable;
    if (Tk_InitOptions(interp, (void *) tkglPtr, optionTable, tkwin)
	    != TCL_OK) {
	Tk_DestroyWindow(tkglPtr->tkwin);
	ckfree(tkglPtr);
	return TCL_ERROR;
    }
    Tk_SetClassProcs(tkglPtr->tkwin, &procs, tkglPtr);
    Tk_CreateEventHandler(tkglPtr->tkwin, ExposureMask|StructureNotifyMask,
	 TkglObjEventProc, (void *) tkglPtr);
    if (Tk_SetOptions(interp, (void *) tkglPtr, optionTable, objc - 2,
	    objv + 2, tkwin, NULL, NULL) != TCL_OK) {
	goto error;
    }
    /* Create a rendering context for drawing to the widget. */
    if (Tkgl_CreateGLContext(tkglPtr) != TCL_OK) {
         goto error;
    }
    /* Configure the widget to match the specified options. */
    if (TkglConfigure(interp, tkglPtr) != TCL_OK) {
	goto error;
    }
    /* Add this widget to the global list. */
    addToList(tkglPtr);
    Tcl_SetObjResult(interp,
	Tcl_NewStringObj(Tk_PathName(tkglPtr->tkwin), TCL_INDEX_NONE));
    /* Make the widget's context current. */
    Tkgl_MakeCurrent(tkglPtr);
    return TCL_OK;

  error:
    Tk_DestroyWindow(tkglPtr->tkwin);
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * TkglWidgetObjCmd --
 *
 *      When a Tkgl widget is created it registers its pathname as a new Tcl
 *      command. This procedure is invoked to process the options for that
 *      command.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

static int
TkglWidgetObjCmd(
    void *clientData,	        /* Information about tkgl widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj * const objv[])	/* Argument objects. */
{
    Tkgl *tkglPtr = (Tkgl *)clientData;
    int result = TCL_OK;
    static const char *const tkglOptions[] = {
        "cget", "configure", "extensions", "glversion", "postredisplay",
	"render", "swapbuffers", "makecurrent", "takephoto", "loadbitmapfont",
	"unloadbitmapfont", "write", "uselayer", "showoverlay",
	"hideoverlay", "postredisplayoverlay", "renderoverlay",
        "existsoverlay", "ismappedoverlay", "getoverlaytransparentvalue",
        "drawbuffer", "clear", "frustum", "ortho", "numeyes",
	"contexttag", "copycontextto",
        NULL
    };
    enum
    {
        TKGL_CGET, TKGL_CONFIGURE, TKGL_EXTENSIONS,
        TKGL_GLVERSION, TKGL_POSTREDISPLAY, TKGL_RENDER,
        TKGL_SWAPBUFFERS, TKGL_MAKECURRENT, TKGL_TAKEPHOTO,
        TKGL_LOADBITMAPFONT, TKGL_UNLOADBITMAPFONT, TKGL_WRITE,
        TKGL_USELAYER, TKGL_SHOWOVERLAY, TKGL_HIDEOVERLAY,
        TKGL_POSTREDISPLAYOVERLAY, TKGL_RENDEROVERLAY,
        TKGL_EXISTSOVERLAY, TKGL_ISMAPPEDOVERLAY,
        TKGL_GETOVERLAYTRANSPARENTVALUE,
        TKGL_DRAWBUFFER, TKGL_CLEAR, TKGL_FRUSTUM, TKGL_ORTHO,
        TKGL_NUMEYES, TKGL_CONTEXTTAG, TKGL_COPYCONTEXTTO
    };
    Tcl_Obj *resultObjPtr;
    int index;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], tkglOptions,
	    sizeof(char *), "command", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }

    Tcl_Preserve(tkglPtr);

    switch (index) {
    case TKGL_CGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    goto error;
	}
	resultObjPtr = Tk_GetOptionValue(interp, (void *)tkglPtr,
		tkglPtr->optionTable, objv[2], tkglPtr->tkwin);
	if (resultObjPtr == NULL) {
	    result = TCL_ERROR;
	} else {
	    Tcl_SetObjResult(interp, resultObjPtr);
	}
	break;
    case TKGL_CONFIGURE:
	resultObjPtr = NULL;
	if (objc == 2) {
	    resultObjPtr = Tk_GetOptionInfo(interp, (void *)tkglPtr,
		    tkglPtr->optionTable, NULL, tkglPtr->tkwin);
	    if (resultObjPtr == NULL) {
		result = TCL_ERROR;
	    }
	} else if (objc == 3) {
	    resultObjPtr = Tk_GetOptionInfo(interp, (void *)tkglPtr,
		    tkglPtr->optionTable, objv[2], tkglPtr->tkwin);
	    if (resultObjPtr == NULL) {
		result = TCL_ERROR;
	    }
	} else {
	    result = Tk_SetOptions(interp, (void *)tkglPtr,
		    tkglPtr->optionTable, objc - 2, objv + 2,
		    tkglPtr->tkwin, NULL, NULL);
	    if (result == TCL_OK) {
		result = TkglConfigure(interp, tkglPtr);
	    }
	    if (!tkglPtr->updatePending) {
		Tcl_DoWhenIdle(TkglDisplay, (void *)tkglPtr);
		tkglPtr->updatePending = 1;
	    }
	}
	if (resultObjPtr != NULL) {
	    Tcl_SetObjResult(interp, resultObjPtr);
	}
	break;
    case TKGL_EXTENSIONS:
	/* Return a list of available OpenGL extensions
	 * TODO: -glu for glu extensions,
	 * -platform for glx/wgl extensions
	 */
	if (objc == 2) {
	    const char *extensions = Tkgl_GetExtensions(tkglPtr);
	    Tcl_Obj *objPtr;
	    Tcl_Size length = -1;
	    if (extensions) {
		objPtr = Tcl_NewStringObj(extensions, -1);
		/* This will convert the object to a list. */
		(void) Tcl_ListObjLength(interp, objPtr, &length);
		Tcl_SetObjResult(interp, objPtr);
	    } else {
		Tcl_SetResult(tkglPtr->interp,
		    "The extensions string is not available now.",
		    TCL_STATIC);
		result = TCL_ERROR;
	    }
	} else {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    result = TCL_ERROR;
	}
	break;
    case TKGL_GLVERSION:
	/* Return the GL version string of the current context. */
	if (objc == 2) {
	    const char *version = (const char *)glGetString(GL_VERSION);
	    Tcl_Obj *objPtr;
	    if (version) {
		objPtr = Tcl_NewStringObj(version, -1);
		Tcl_SetObjResult(interp, objPtr);
	    } else {
		Tcl_SetResult(tkglPtr->interp,
		    "The version string is not available until "
                     "the widget is mapped.",
		     TCL_STATIC);
		result = TCL_ERROR;
	    }
	} else {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    result = TCL_ERROR;
	}
	break;
    case TKGL_POSTREDISPLAY:
	/* schedule the widget to be redrawn */
	if (objc == 2) {
	    TkglPostRedisplay(tkglPtr);
	} else {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    result = TCL_ERROR;
	}
	break;
    case TKGL_RENDER:
	/* force the widget to be redrawn */
	if (objc == 2) {
	    TkglDisplay((void *) tkglPtr);
	} else {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    result = TCL_ERROR;
	}
	break;
    case TKGL_SWAPBUFFERS:
	if (objc == 2) {
	    Tkgl_SwapBuffers(tkglPtr);
	} else {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    result = TCL_ERROR;
	}
	break;
    case TKGL_MAKECURRENT:
	if (objc == 2) {
	    Tkgl_MakeCurrent(tkglPtr);
	} else {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    result = TCL_ERROR;
	}
	break;
    case TKGL_TAKEPHOTO:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "name");
	    result = TCL_ERROR;
	} else {
	    const char *name;
	    Tk_PhotoHandle photo;

	    name = Tcl_GetStringFromObj(objv[2], NULL);
	    photo = Tk_FindPhoto(interp, name);
	    if (photo == NULL) {
		Tcl_AppendResult(interp, "image \"", name,
		    "\" doesn't exist or is not a photo image", NULL);
		result = TCL_ERROR;
		break;
	    }
	    glPushAttrib(GL_PIXEL_MODE_BIT);
	    if (tkglPtr->doubleFlag) {
		glReadBuffer(GL_FRONT);
	    }
	    Tkgl_TakePhoto(tkglPtr, photo);
	    glPopAttrib();    /* restore glReadBuffer */
          }
          break;
    case TKGL_LOADBITMAPFONT:
    case TKGL_UNLOADBITMAPFONT:
    case TKGL_WRITE:
#if TKGL_USE_FONTS != 1
	Tcl_AppendResult(interp, "unsupported", NULL);
	result = TCL_ERROR;
	break;
#else	
ERROR
#endif
    case TKGL_USELAYER:
    case TKGL_SHOWOVERLAY:
    case TKGL_HIDEOVERLAY:
    case TKGL_POSTREDISPLAYOVERLAY:
    case TKGL_RENDEROVERLAY:
    case TKGL_EXISTSOVERLAY:
    case TKGL_ISMAPPEDOVERLAY:
    case TKGL_GETOVERLAYTRANSPARENTVALUE:
#if TKGL_USE_OVERLAY != 1
	Tcl_AppendResult(interp, "unsupported", NULL);
	result = TCL_ERROR;
	break;
#else
ERROR
#endif
    case TKGL_DRAWBUFFER:
    case TKGL_CLEAR:
#if 1
	Tcl_AppendResult(interp, "unsupported", NULL);
	result = TCL_ERROR;
	break;
#endif
    case TKGL_FRUSTUM:
	if (objc != 8) {
	    Tcl_WrongNumArgs(interp, 2, objv,
			     "left right bottom top near far");
	    result = TCL_ERROR;
	} else {
	    double  left, right, bottom, top, zNear, zFar;

	    if (Tcl_GetDoubleFromObj(interp, objv[2], &left) == TCL_ERROR
		|| Tcl_GetDoubleFromObj(interp, objv[3],
					&right) == TCL_ERROR
		|| Tcl_GetDoubleFromObj(interp, objv[4],
					&bottom) == TCL_ERROR
		|| Tcl_GetDoubleFromObj(interp, objv[5],
					&top) == TCL_ERROR
		|| Tcl_GetDoubleFromObj(interp, objv[6],
					&zNear) == TCL_ERROR
		|| Tcl_GetDoubleFromObj(interp, objv[7],
					&zFar) == TCL_ERROR) {
		result = TCL_ERROR;
		break;
	    }
	    TkglFrustum(tkglPtr, left, right, bottom, top, zNear, zFar);
	}
	break;
    case TKGL_ORTHO:
	if (objc != 8) {
	    Tcl_WrongNumArgs(interp, 2, objv,
			     "left right bottom top near far");
	    result = TCL_ERROR;
	} else {
	    double  left, right, bottom, top, zNear, zFar;

	    if (Tcl_GetDoubleFromObj(interp, objv[2], &left) == TCL_ERROR
		|| Tcl_GetDoubleFromObj(interp, objv[3],
					&right) == TCL_ERROR
		|| Tcl_GetDoubleFromObj(interp, objv[4],
					&bottom) == TCL_ERROR
		|| Tcl_GetDoubleFromObj(interp, objv[5],
					&top) == TCL_ERROR
		|| Tcl_GetDoubleFromObj(interp, objv[6],
					&zNear) == TCL_ERROR
		|| Tcl_GetDoubleFromObj(interp, objv[7],
					&zFar) == TCL_ERROR) {
		result = TCL_ERROR;
		break;
	    }
	    TkglOrtho(tkglPtr, left, right, bottom, top, zNear, zFar);
	}
	break;
    case TKGL_NUMEYES:
	if (objc == 2) {
	    Tcl_SetObjResult(interp, Tcl_NewIntObj(
		tkglPtr->stereo > TKGL_STEREO_ONE_EYE_MAX ? 2 : 1));
	} else {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    result = TCL_ERROR;
	}
	break;
    case TKGL_CONTEXTTAG:
	if (objc == 2) {
	    Tcl_SetObjResult(interp, Tcl_NewIntObj(tkglPtr->contextTag));
	} else {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    result = TCL_ERROR;
	}
	break;
    case TKGL_COPYCONTEXTTO:
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    result = TCL_ERROR;
	} else {
	    Tkgl *to;
	    unsigned int mask;

	    if (GetTkglFromObj(tkglPtr->interp, objv[2], &to) == TCL_ERROR
		|| Tcl_GetIntFromObj(tkglPtr->interp, objv[3],
				     (int *) &mask) == TCL_ERROR) {
		result = TCL_ERROR;
		break;
	    }
	    result = Tkgl_CopyContext(tkglPtr, to, mask);
	}
	break;
    default:
	break;
    }
    Tcl_Release(tkglPtr);
    return result;

  error:
    Tcl_Release(tkglPtr);
    return TCL_ERROR;
}

static void
TkglPostRedisplay(Tkgl *tkglPtr)
{
    if (!tkglPtr->UpdatePending) {
        tkglPtr->UpdatePending = True;
        Tcl_DoWhenIdle(TkglDisplay, (void *) tkglPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkglConfigure --
 *
 *	This procedure is called to process an argv/argc list in conjunction
 *	with the Tk option database to configure (or reconfigure) a tkgl
 *	widget.
 *
 * Results:
 *	The return value is a standard Tcl result. If TCL_ERROR is returned,
 *	then the interp's result contains an error message.
 *
 * Side effects:
 *	Configuration information gets set for tkglPtr.
 *
 *----------------------------------------------------------------------
 */

static int
TkglConfigure(
    Tcl_Interp *interp,		/* Used for error reporting. */
    Tkgl *tkglPtr)		/* Information about widget. */
{
    /*
     * Register the desired geometry for the window. Then arrange for the
     * window to be redisplayed.
     */

    Tk_GeometryRequest(tkglPtr->tkwin, tkglPtr->width, tkglPtr->height);
    if (!tkglPtr->updatePending) {
	Tcl_DoWhenIdle(TkglDisplay, tkglPtr);
	tkglPtr->updatePending = 1;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TkglObjEventProc --
 *
 *	This procedure is invoked by the Tk dispatcher to process events
 *	sent to a TkGL widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When the window gets deleted, internal structures get cleaned up. When
 *	it gets exposed or the size changes, it is redisplayed.
 *
 *--------------------------------------------------------------
 */

static void
TkglObjEventProc(
    void *clientData,	/* Information about window. */
    XEvent *eventPtr)		/* Information about event. */
{
    Tkgl *tkglPtr = (Tkgl *)clientData;
    int new_width, new_height;

    switch(eventPtr->type) {
    case Expose:
	if (!tkglPtr->updatePending) {
	    Tcl_DoWhenIdle(TkglDisplay, tkglPtr);
	    tkglPtr->updatePending = 1;
	}
	break;
    case ConfigureNotify:
	new_width = Tk_Width(tkglPtr->tkwin);
	new_height = Tk_Height(tkglPtr->tkwin);
#if 0
	fprintf(stderr, "ConfigureNotify: %d x %d -> %d x %d\n",
		tkglPtr->width, tkglPtr->height, new_width, new_height);
#endif
	if (new_width != tkglPtr->width || new_height != tkglPtr->height) {
	    tkglPtr->width = new_width;
	    tkglPtr->height = new_height;
	    if (!tkglPtr->updatePending) {
		Tcl_DoWhenIdle(TkglDisplay, tkglPtr);
		tkglPtr->updatePending = 1;
	    }
	}
	break;
    case DestroyNotify:
	if (tkglPtr->tkwin != NULL) {
	    Tk_FreeConfigOptions((char *) tkglPtr, tkglPtr->optionTable,
		    tkglPtr->tkwin);
	    tkglPtr->tkwin = NULL;
	    Tcl_DeleteCommandFromToken(tkglPtr->interp,
		    tkglPtr->widgetCmd);
	}
	if (tkglPtr->updatePending) {
	    Tcl_CancelIdleCall(TkglDisplay, tkglPtr);
	}
	Tcl_EventuallyFree(tkglPtr, TCL_DYNAMIC);
	break;
    case MapNotify:
	Tkgl_MapWidget(tkglPtr);
	break;
    case UnmapNotify:
	Tkgl_UnmapWidget(tkglPtr);
	break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkglDeletedProc --
 *
 *	This procedure is invoked when a widget command is deleted. If the
 *	widget isn't already in the process of being destroyed, this command
 *	destroys it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The widget is destroyed.
 *
 *----------------------------------------------------------------------
 */

/// need to port Tkgl_EnterStereo and Tkgl_LeaveStereo

static void
TkglDeletedProc(
    void *clientData)	/* Pointer to widget record for widget. */
{
    Tkgl *tkglPtr = (Tkgl *)clientData;
    Tk_Window tkwin = tkglPtr->tkwin;

    /*
     * This procedure could be invoked either because the window was destroyed
     * and the command was then deleted (in which case tkwin is NULL) or
     * because the command was deleted, and then this procedure destroys the
     * widget.
     */
    // Tkgl_LeaveStereo(tkgl, tkgl->Stereo);

    Tcl_Preserve((void *) tkglPtr);
    if (tkglPtr->destroyProc) {
        /* call user's cleanup code */
        Tkgl_CallCallback(tkglPtr, tkglPtr->destroyProc);
    }
    if (tkglPtr->timerProc != NULL) {
        Tcl_DeleteTimerHandler(tkglPtr->timerHandler);
        tkglPtr->timerHandler = NULL;
    }
    if (tkglPtr->updatePending) {
        Tcl_CancelIdleCall(TkglDisplay, (void *) tkglPtr);
        tkglPtr->updatePending = 0;
    }
#ifndef NO_TK_CURSOR
    if (tkglPtr->cursor != NULL) {
        Tk_FreeCursor(tkglPtr->display, tkglPtr->cursor);
        tkglPtr->cursor = NULL;
    }
#endif
    removeFromList(tkglPtr);
    Tkgl_FreeResources(tkglPtr);
    if (tkwin != NULL) {
        Tk_DeleteEventHandler(tkwin, ExposureMask | StructureNotifyMask,
                TkglObjEventProc, (void *) tkglPtr);
	if (tkglPtr->setGrid > 0) {
	    Tk_UnsetGrid(tkwin);
	}
	Tk_DestroyWindow(tkwin);
    }
    tkglPtr->tkwin = NULL;
    Tcl_Release((void *) tkglPtr);
}

/*
 *--------------------------------------------------------------
 *
 * TkglDisplay --
 *
 *	This procedure redraws the contents of a tkgl window. It is invoked
 *	as a do-when-idle handler, so it only runs when there's nothing else
 *	for the application to do.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information appears on the screen.
 *
 *--------------------------------------------------------------
 */

static void
TkglDisplay(
    void *clientData)	/* Information about window. */
{
    Tkgl *tkglPtr = (Tkgl *)clientData;
    Tk_Window tkwin = tkglPtr->tkwin;

    tkglPtr->updatePending = 0;
    if (!Tk_IsMapped(tkwin)) {
	return;
    }
    Tkgl_Update(tkglPtr);
    Tkgl_MakeCurrent(tkglPtr);
    if (tkglPtr->displayProc) {
        Tkgl_CallCallback(tkglPtr, tkglPtr->displayProc);
    }
#if 0
    /* Very simple tests */
    static int toggle = 0;
    printf("Running simple test\n");
    if (toggle) {
	glClearColor(0, 0, 1, 1);
    } else {
	glClearColor(1, 0, 1, 1);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    toggle = (toggle + 1) % 2;
    Tkgl_SwapBuffers(tkglPtr);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * Tkgl_Init --
 *
 *	Initialize the new package.  The string "Sample" in the
 *	function name must match the PACKAGE declaration at the top of
 *	configure.ac.
 *
 * Results:
 *	A standard Tcl result
 *
 * Side effects:
 *	The Tkgl package is created.
 *
 *----------------------------------------------------------------------
 */

#ifndef STRINGIFY
#  define STRINGIFY(x) STRINGIFY1(x)
#  define STRINGIFY1(x) #x
#endif

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

DLLEXPORT int
Tkgl_Init(
    Tcl_Interp* interp)		/* Tcl interpreter */
{
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
	return TCL_ERROR;
    }

    if (Tk_InitStubs(interp, TK_VERSION, 0) == NULL) {
        return TCL_ERROR;
    }
    if (Tcl_PkgProvideEx(interp, PACKAGE_NAME, PACKAGE_VERSION, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    if (!Tcl_CreateObjCommand(interp, "tkgl", (Tcl_ObjCmdProc *)TkglObjCmd,
			      NULL, NULL)) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

/* Support for the -stereo custom option. */

static Tk_CustomOptionSetProc SetStereo;
static Tk_CustomOptionGetProc GetStereo;
static Tk_CustomOptionRestoreProc RestoreStereo;

static Tk_ObjCustomOption stereoOption = {
    "stereo",                   /* name */
    SetStereo,                  /* setProc */
    GetStereo,                  /* getProc */
    RestoreStereo,              /* restoreProc */
    NULL,                       /* freeProc */
    0
};

static Tcl_Obj *GetStereo(
    void *clientData,
    Tk_Window tkwin,
    char *recordPtr,
    Tcl_Size internalOffset)
{
    int     stereo = *(int *) (recordPtr + internalOffset);
    const char *name = "unknown";

    switch (stereo) {
      case TKGL_STEREO_NONE:
          name = "";
          break;
      case TKGL_STEREO_LEFT_EYE:
          name = "left eye";
          break;
      case TKGL_STEREO_RIGHT_EYE:
          name = "right eye";
          break;
      case TKGL_STEREO_NATIVE:
          name = "native";
          break;
      case TKGL_STEREO_SGIOLDSTYLE:
          name = "sgioldstyle";
          break;
      case TKGL_STEREO_ANAGLYPH:
          name = "anaglyph";
          break;
      case TKGL_STEREO_CROSS_EYE:
          name = "cross-eye";
          break;
      case TKGL_STEREO_WALL_EYE:
          name = "wall-eye";
          break;
      case TKGL_STEREO_DTI:
          name = "dti";
          break;
      case TKGL_STEREO_ROW_INTERLEAVED:
          name = "row interleaved";
          break;
    }
    return Tcl_NewStringObj(name, -1);
}

/* 
 *----------------------------------------------------------------------
 *
 * SetStereo --
 *
 *      Converts a Tcl_Obj representing a widgets stereo into an
 *      integer value.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      May store the integer value into the internal representation
 *      pointer.  May change the pointer to the Tcl_Obj to NULL to indicate
 *      that the specified string was empty and that is acceptable.
 *
 *----------------------------------------------------------------------
 */

static int SetStereo(
    void *clientData,
    Tcl_Interp *interp,
    Tk_Window tkwin,
    Tcl_Obj **value,
    char *recordPtr,
    Tcl_Size internalOffset,
    char *saveInternalPtr,
    int flags)
    /* interp is the current interp; may be used for errors. */
    /* tkwin is the Window for which option is being set. */
    /* value is a pointer to the pointer to the value object. We use a pointer
     * to the pointer because we may need to return a value (NULL). */
    /* recordPtr is a pointer to storage for the widget record. */
    /* internalOffset is the offset within *recordPtr at which the internal
     * value is to be stored. */
    /* saveInternalPtr is a pointer to storage for the old value. */
    /* flags are the flags for the option, set Tk_SetOptions. */
{
    int     stereo = 0;
    char   *string, *internalPtr;

    internalPtr = (internalOffset > 0) ? recordPtr + internalOffset : NULL;

    if ((flags & TK_OPTION_NULL_OK) && ObjectIsEmpty(*value)) {
        *value = NULL;
    } else {
        /* 
         * Convert the stereo specifier into an integer value.
         */

        if (Tcl_GetBooleanFromObj(NULL, *value, &stereo) == TCL_OK) {
            stereo = stereo ? TKGL_STEREO_NATIVE : TKGL_STEREO_NONE;
        } else {
            string = Tcl_GetString(*value);

            if (strcmp(string, "") == 0 || strcasecmp(string, "none") == 0) {
                stereo = TKGL_STEREO_NONE;
            } else if (strcasecmp(string, "native") == 0) {
                stereo = TKGL_STEREO_NATIVE;
                /* check if available when creating visual */
            } else if (strcasecmp(string, "left eye") == 0) {
                stereo = TKGL_STEREO_LEFT_EYE;
            } else if (strcasecmp(string, "right eye") == 0) {
                stereo = TKGL_STEREO_RIGHT_EYE;
            } else if (strcasecmp(string, "sgioldstyle") == 0) {
                stereo = TKGL_STEREO_SGIOLDSTYLE;
            } else if (strcasecmp(string, "anaglyph") == 0) {
                stereo = TKGL_STEREO_ANAGLYPH;
            } else if (strcasecmp(string, "cross-eye") == 0) {
                stereo = TKGL_STEREO_CROSS_EYE;
            } else if (strcasecmp(string, "wall-eye") == 0) {
                stereo = TKGL_STEREO_WALL_EYE;
            } else if (strcasecmp(string, "dti") == 0) {
                stereo = TKGL_STEREO_DTI;
            } else if (strcasecmp(string, "row interleaved") == 0) {
                stereo = TKGL_STEREO_ROW_INTERLEAVED;
            } else {
                Tcl_ResetResult(interp);
                Tcl_AppendResult(interp, "bad stereo value \"",
                        Tcl_GetString(*value), "\"", NULL);
                return TCL_ERROR;
            }
        }
    }

    if (internalPtr != NULL) {
        *((int *) saveInternalPtr) = *((int *) internalPtr);
        *((int *) internalPtr) = stereo;
    }
    return TCL_OK;
}

/* 
 *----------------------------------------------------------------------
 * RestoreStereo --
 *
 *      Restore a stereo option value from a saved value.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Restores the old value.
 *
 *----------------------------------------------------------------------
 */

static void
RestoreStereo(
    void *clientData,
    Tk_Window tkwin,
    char *internalPtr,
    char *saveInternalPtr)
{
    *(int *) internalPtr = *(int *) saveInternalPtr;
}
/* 
 * The following structure contains pointers to functions used for
 * processing the custom "-pixelformat" option.  Copied from tkPanedWindow.c.
 */
/* 
 * Support for the custom "-pixelformat" option.
 */
static Tk_CustomOptionSetProc SetWideInt;
static Tk_CustomOptionGetProc GetWideInt;
static Tk_CustomOptionRestoreProc RestoreWideInt;

static Tk_ObjCustomOption wideIntOption = {
    "wide int",                 /* name */
    SetWideInt,                 /* setProc */
    GetWideInt,                 /* getProc */
    RestoreWideInt,             /* restoreProc */
    NULL,                       /* freeProc */
    0
};
/* 
 *----------------------------------------------------------------------
 *
 * GetWideInt -
 *
 *      Converts an internal wide integer into a a Tcl WideInt obj.
 *
 * Results:
 *      Tcl_Obj containing the wide int value.
 *
 * Side effects:
 *      Creates a new Tcl_Obj.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *GetWideInt(
    void *clientData,
    Tk_Window tkwin,
    char *recordPtr,
    Tcl_Size internalOffset)
{
    Tcl_WideInt wi = *(Tcl_WideInt *) (recordPtr + internalOffset);
    return Tcl_NewWideIntObj(wi);
}

/* 
 *----------------------------------------------------------------------
 *
 * SetWideInt --
 *
 *      Converts a Tcl_Obj representing a Tcl_WideInt.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      May store the wide int value into the internal representation
 *      pointer.  May change the pointer to the Tcl_Obj to NULL to indicate
 *      that the specified string was empty and that is acceptable.
 *
 *----------------------------------------------------------------------
 */

static int SetWideInt(
    void *clientData,
    Tcl_Interp *interp,
    Tk_Window tkwin,
    Tcl_Obj **value,
    char *recordPtr,
    Tcl_Size internalOffset,
    char *saveInternalPtr,
    int flags)
{
    Tcl_WideInt w;
    char   *internalPtr;
    internalPtr = (internalOffset > 0) ? recordPtr + internalOffset : NULL;

    if ((flags & TK_OPTION_NULL_OK) && ObjectIsEmpty(*value)) {
        *value = NULL;
        w = 0;
    } else {
        if (Tcl_GetWideIntFromObj(interp, *value, &w) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    if (internalPtr != NULL) {
        *((Tcl_WideInt *) saveInternalPtr) = *((Tcl_WideInt *) internalPtr);
        *((Tcl_WideInt *) internalPtr) = w;
    }
    return TCL_OK;
}
/* 
 *----------------------------------------------------------------------
 * RestoreWideInt --
 *
 *      Restore a wide int option value from a saved value.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Restores the old value.
 *
 *----------------------------------------------------------------------
 */


static void
RestoreWideInt(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,
    char *saveInternalPtr)
{
    *(Tcl_WideInt *) internalPtr = *(Tcl_WideInt *) saveInternalPtr;
}

/* 
 *----------------------------------------------------------------------
 *
 * ObjectIsEmpty --
 *
 *      This procedure tests whether the string value of an object is
 *      empty.
 *
 * Results:
 *      The return value is 1 if the string value of objPtr has length
 *      zero, and 0 otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
ObjectIsEmpty(Tcl_Obj *objPtr)
/* objPtr = Object to test.  May be NULL. */
{
    Tcl_Size length;

    if (objPtr == NULL) {
        return 1;
    }
    if (objPtr->bytes != NULL) {
        return (objPtr->length == 0);
    }
    Tcl_GetStringFromObj(objPtr, &length);
    return (length == 0);
}

/* 
 * Tkgl_CallCallback
 *
 * Call command with tkgl widget as only argument
 */

int
Tkgl_CallCallback(Tkgl *tkgl, Tcl_Obj *cmd)
{
    int     result;
    Tcl_Obj *objv[3];

    if (cmd == NULL || tkgl->widgetCmd == NULL)
        return TCL_OK;

    objv[0] = cmd;
    Tcl_IncrRefCount(objv[0]);
    objv[1] =
            Tcl_NewStringObj(Tcl_GetCommandName(tkgl->interp, tkgl->widgetCmd),
            -1);
    Tcl_IncrRefCount(objv[1]);
    objv[2] = NULL;
    result = Tcl_EvalObjv(tkgl->interp, 2, objv, TCL_EVAL_GLOBAL);
    Tcl_DecrRefCount(objv[1]);
    Tcl_DecrRefCount(objv[0]);
    if (result != TCL_OK)
        Tcl_BackgroundError(tkgl->interp);
    return result;
}


/* 
 *----------------------------------------------------------------------
 *
 * Utilities for managing the list of all Tkgl widgets.
 *
 *----------------------------------------------------------------------
 */

/* 
 * Add a tkgl widget to the top of the linked list.
 */
static void
addToList(Tkgl *t)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    t->next = tsdPtr->tkglHead;
    tsdPtr->tkglHead = t;
}

/* 
 * Remove a tkgl widget from the linked list.
 */
static void
removeFromList(Tkgl *t)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    Tkgl   *prev, *cur;
    for (cur = tsdPtr->tkglHead, prev = NULL; cur; prev = cur, cur = cur->next) {
        if (t != cur)
            continue;
        if (prev) {
            prev->next = cur->next;
        } else {
            tsdPtr->tkglHead = cur->next;
        }
        break;
    }
    if (cur)
        cur->next = NULL;
}

/* 
 * Return a pointer to the widget record of the Tkgl with a given pathname.
 */
Tkgl *
FindTkgl(Tkgl *tkgl, const char *ident)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    Tkgl   *t;

    if (ident[0] != '.') {
        for (t = tsdPtr->tkglHead; t; t = t->next) {
            if (strcmp(t->ident, ident) == 0)
                break;
        }
    } else {
        for (t = tsdPtr->tkglHead; t; t = t->next) {
            const char *pathname = Tk_PathName(t->tkwin);

            if (strcmp(pathname, ident) == 0)
                break;
        }
    }
    return t;
}

/* 
 * Return pointer to another tkgl widget with same OpenGL context.
 */
Tkgl *
FindTkglWithSameContext(const Tkgl *tkgl)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    Tkgl   *t;

    for (t = tsdPtr->tkglHead; t != NULL; t = t->next) {
        if (t == tkgl)
            continue;
        if ((void *)t->context == (void *)tkgl->context) {
            return t;
        }
    }
    return NULL;
}

/*
 * TkglFrustum and TkglOrtho:
 *
 *     eyeOffset is the distance from the center line
 *     and is negative for the left eye and positive for right eye.
 *     eyeDist and eyeOffset need to be in the same units as your model space.
 *     In physical space, eyeDist might be 30 inches from the screen
 *     and eyeDist would be +/- 1.25 inch (for a total interocular distance
 *     of 2.5 inches).
 */
static void
TkglFrustum(const Tkgl *tkgl, GLdouble left, GLdouble right,
        GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    GLdouble eyeOffset = 0, eyeShift = 0;

    if (tkgl->stereo == TKGL_STEREO_LEFT_EYE
            || tkgl->currentStereoBuffer == STEREO_BUFFER_LEFT)
        eyeOffset = -tkgl->eyeSeparation / 2;   /* for left eye */
    else if (tkgl->stereo == TKGL_STEREO_RIGHT_EYE
            || tkgl->currentStereoBuffer == STEREO_BUFFER_RIGHT)
        eyeOffset = tkgl->eyeSeparation / 2;    /* for right eye */
    eyeShift = (tkgl->convergence - zNear) * (eyeOffset / tkgl->convergence);

    /* compenstate for altered viewports */
    switch (tkgl->stereo) {
      default:
          break;
      case TKGL_STEREO_SGIOLDSTYLE:
      case TKGL_STEREO_DTI:
          /* squished image is expanded, nothing needed */
          break;
      case TKGL_STEREO_CROSS_EYE:
      case TKGL_STEREO_WALL_EYE:{
          GLdouble delta = (top - bottom) / 2;

          top += delta;
          bottom -= delta;
          break;
      }
    }

    glFrustum(left + eyeShift, right + eyeShift, bottom, top, zNear, zFar);
    glTranslated(-eyeShift, 0, 0);
}

static void
TkglOrtho(const Tkgl *tkgl, GLdouble left, GLdouble right,
        GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    /* TODO: debug this */
    GLdouble eyeOffset = 0, eyeShift = 0;

    if (tkgl->currentStereoBuffer == STEREO_BUFFER_LEFT)
        eyeOffset = -tkgl->eyeSeparation / 2;   /* for left eye */
    else if (tkgl->currentStereoBuffer == STEREO_BUFFER_RIGHT)
        eyeOffset = tkgl->eyeSeparation / 2;    /* for right eye */
    eyeShift = (tkgl->convergence - zNear) * (eyeOffset / tkgl->convergence);

    /* compenstate for altered viewports */
    switch (tkgl->stereo) {
      default:
          break;
      case TKGL_STEREO_SGIOLDSTYLE:
      case TKGL_STEREO_DTI:
          /* squished image is expanded, nothing needed */
          break;
      case TKGL_STEREO_CROSS_EYE:
      case TKGL_STEREO_WALL_EYE:{
          GLdouble delta = (top - bottom) / 2;

          top += delta;
          bottom -= delta;
          break;
      }
    }

    glOrtho(left + eyeShift, right + eyeShift, bottom, top, zNear, zFar);
    glTranslated(-eyeShift, 0, 0);
}

static int
GetTkglFromObj(Tcl_Interp *interp, Tcl_Obj *obj, Tkgl **tkglPtr)
{
    Tcl_Command tkglCmd;
    Tcl_CmdInfo info;

    tkglCmd = Tcl_GetCommandFromObj(interp, obj);
    if (Tcl_GetCommandInfoFromToken(tkglCmd, &info) == 0
            || info.objProc != TkglWidgetObjCmd) {
        Tcl_AppendResult(interp, "expected tkgl command argument", NULL);
        return TCL_ERROR;
    }
    *tkglPtr = (Tkgl *) info.objClientData;
    return TCL_OK;
}

    
#ifdef __cplusplus
}
#endif  /* __cplusplus */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
