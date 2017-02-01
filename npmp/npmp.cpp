/*
 *
 * File name   :  npmp.cpp
 *
 * Copyright (c) 2008 by KO Myung-Hun (komh@chollian.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Changes :
 *     KO Myung-Hun <komh@chollian.net> 2008/02/20
 *         - Support playlist such as asx, Winamp pls and m3u
 *         - Support simple status message
 *         - Support simple interface
 *
 *     KO Myung-Hun <komh@chollian.net> 2008/10/21
 *         - Start MPlayer with '-nofs' option
 *         - Support popup menu
 *         - Now maximize plugin window instead of letting MPlayer do it
 */

#define INCL_WIN
#define INCL_DOS
#define INCL_GPI
#include <os2.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <process.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifndef _NPAPI_H_
#include "npapi.h"
#endif

#include "npmp.h"

#include "osdep/keycodes.h"

#ifdef DEBUG
#define dprintf( ... ) \
{\
    FILE *fp;\
    fp = fopen("npmp.log", "at");\
    fprintf( fp, __VA_ARGS__ );\
    fclose( fp );\
}
#else
#define dprintf( ... )
#endif

#define SetMsg( This, message ) ( This )->msg = message, WinInvalidateRect(( This )->hWnd, 0, TRUE )

#define TID_MPLAYER ( TID_USERMAX - 1 )

#define MSG_PLUGIN      "MPlayer plugin"
#define MSG_CLICKTOPLAY "Click here to play"
#define MSG_START       "Starting MPlayer..."
#define MSG_RUN         "MPlayer running..."
#define MSG_FAIL        "Failed to start MPlayer!!!"
#define MSG_NOMEDIA     "Media not found!!!"

//
// Instance state information about the plugin.
//
// *Developers*: Use this struct to hold per-instance
//               information that you'll need in the
//               various functions in this file.
//

typedef struct _PluginInstance PluginInstance;
typedef struct _PluginInstance
{
    NPWindow*       fWindow;
    HWND            hWnd;
    uint16          fMode;
    PFNWP           lpfnOldWndProc;
    NPSavedData*    pSavedInstanceData;
    PluginInstance* pNext;
    char*           srcname;
#if 0
    int32           width;
    int32           height;
#endif
    int32           pidMP;
    HPIPE           hpipe;
    char*           url;
    BOOL            fPlaylist;
    const char*     msg;
    void*           buffer;
    int32           buflen;
    HWND            hwndParent;
    SWP             swp;
    BOOL            fFullScreen;
    BOOL            fPaused;
    HWND            hwndPopup;
} PluginInstance;

typedef struct _PluginStream PluginStream;
typedef struct _PluginStream
{
    BOOL    fPlaylist;
    void*   buffer;
    int32   buflen;
} PluginStream;

static MRESULT APIENTRY
SubClassFunc(HWND hWnd,ULONG Message,MPARAM wParam, MPARAM lParam);

static void Draw(PluginInstance *This, HPS hps, POINTL *endPoint, BOOL fPrinting);

static char *StrDup( const char *s, int len = -1 );
static void StrFree( const char *s );

static void PipeOpen( PluginInstance *This );
static void PipeClose( PluginInstance *This );
static void PipeWrite( PluginInstance *This, const char *s );

static void ExecMP( PluginInstance *This );
static void KillMP( PluginInstance *This );

static void SwitchWindowMode( PluginInstance *This, BOOL fToFullScreen );

static inline BOOL IsVideoOpened( PluginInstance *This );
static inline void PutKeyToMP( PluginInstance *This, int key );

static HMODULE  DLLInstance;

static HMTX     hmtxMP;

#if defined ( __cplusplus )
extern "C" {
#endif

/* _CRT_init is the C run-time environment initialization function.         */
/* It will return 0 to indicate success and -1 to indicate failure.         */
   int _CRT_init(void);

/* __ctordtorInit calls the C++ run-time constructors for static objects.   */
   void __ctordtorInit(void);

/* __ctordtorTerm calls the C++ run-time destructors for static objects.    */
   void __ctordtorTerm(void);

#ifdef   STATIC_LINK

/* _CRT_term is the C run-time environment termination function.            */
/* It only needs to be called when the C run-time functions are statically  */
/* linked.                                                                  */
   void _CRT_term(void);

#else

/* A clean up routine registered with DosExitList must be used if runtime   */
/* calls are required at exit AND the runtime is dynamically linked.  This  */
/* will guarantee that this clean up routine is run before the library DLL  */
/* is terminated.  Do any application housekeeping in cleanup()             */
    static void _System cleanup(ULONG ulReason);

#endif

#if defined ( __cplusplus )
}
#endif

#if defined ( __cplusplus )
extern "C"
#endif
unsigned long _System _DLL_InitTerm(unsigned long hModule, unsigned long
                                    ulFlag)
{
    DLLInstance = (HMODULE) hModule;
    switch (ulFlag)
    {
        case 0:
            if ( _CRT_init() == -1 )
            {
                return(0UL);
            }
#if defined ( __cplusplus )
            __ctordtorInit();
#endif

#ifndef  STATIC_LINK

         /*******************************************************************/
         /* A DosExitList routine must be used to clean up if runtime calls */
         /* are required at exit and the runtime is dynamically linked.     */
         /*******************************************************************/

            DosExitList(0x0000FF00|EXLST_ADD, cleanup);
#endif
            dprintf("DLL initialized\n");

            hmtxMP = 0;
            DosCreateMutexSem( 0, &hmtxMP, 0, FALSE );
            break;
        case 1:
            dprintf("DLL terminated\n");

            DosCloseMutexSem( hmtxMP );

#if defined ( __cplusplus )
            __ctordtorTerm();
#endif

#ifdef  STATIC_LINK
            _CRT_term();
#endif
            break;
    }

    return 1;
}

#ifndef  STATIC_LINK
static void cleanup(ULONG ulReason)
{
   /* do any DLL cleanup here if needed AND if dynamically linked to the */
   /* C Runtime libraries                                                */
   DosExitList(EXLST_EXIT, cleanup);   /* remember to unlink ourselves   */
   return ;
}
#endif

// A plugin instance typically will subclass the plugin's client window, so
// it can get Windows messages, (such as paint, palettechanged, keybd, etc).
// To do work associated with a specific plugin instance the WndProc which
// receives the Windows messages, (named "SubClassFunc" herein), needs access
// to the "This" (PluginInstance*) ptr.
// If the plugin wants all the keyboard messages, it will have to
// create a frame window with it's own accelerator table (even it it is empty)
// and insure that that frame tracks the plugin window that Navigator creates,
// otherwise the Navigator Accelerators will interfere with the WM_CHARs
// that the plugin windows receive...

// When Navigator registers the plugin client's window class, (the class for
// the window passed in NPP_SetWindow()), Navigator reserves 4
// "extra" window bytes for the plugins use... at QWL_USER
// Associate the hWnd with pInstance by setting the hWnd member of the
// PluginInstance struct.
static void AssociateInstance(HWND hWnd, PluginInstance* pInstance)
{
    pInstance->hWnd = hWnd;     // redundant, but usefull to get hwnd from
                                // pinstance later.
    BOOL rc = WinSetWindowULong(hWnd, QWL_USER, (ULONG)pInstance);

    WinQueryWindowPos( hWnd, &pInstance->swp );

    pInstance->hwndParent = WinQueryWindow( hWnd, QW_PARENT );

    if( pInstance->srcname && *pInstance->srcname )
        SetMsg( pInstance, MSG_CLICKTOPLAY );
}

// Find the PluginInstance associated with this hWnd and return it
static PluginInstance* GetInstance(HWND hWnd)
{
    return (PluginInstance*)WinQueryWindowULong(hWnd, QWL_USER);
}

//----------------------------------------------------------------------------
// NPP_Initialize:
//----------------------------------------------------------------------------
NPError NPP_Initialize(void)
{
    // do your one time initialization here, such as dynamically loading
    // dependant DLLs

    dprintf("NPP_Initialize\n");

    return NPERR_NO_ERROR;
}


//----------------------------------------------------------------------------
// NPP_Shutdown:
//----------------------------------------------------------------------------
void NPP_Shutdown(void)
{
    // do your one time uninitialization here, such as unloading dynamically
    // loaded DLLs

    dprintf("NPP_Shutdown\n");
}


//----------------------------------------------------------------------------
// NPP_New:
//----------------------------------------------------------------------------
NPError NP_LOADDS
NPP_New(NPMIMEType pluginType,
                NPP instance,
                uint16 mode,
                int16 argc,
                char* argn[],
                char* argv[],
                NPSavedData* saved)
{
    dprintf("NPP_New\n");

    if (instance == NULL)
        return NPERR_INVALID_INSTANCE_ERROR;

    instance->pdata = NPN_MemAlloc(sizeof(PluginInstance));
    PluginInstance* This = (PluginInstance*) instance->pdata;

    if (This == NULL)
        return NPERR_OUT_OF_MEMORY_ERROR;
    //
    // *Developers*: Initialize fields of your plugin
    // instance data here.  If the NPSavedData is non-
    // NULL, you can use that data (returned by you from
    // NPP_Destroy to set up the new plugin instance.
    //

    This->fWindow = 0;
    // mode is NP_EMBED, NP_FULL, or NP_BACKGROUND (see npapi.h)
    This->fMode = mode;
    This->hWnd = 0;
    This->pSavedInstanceData = saved;
    This->pNext = 0   ;
    This->srcname = 0;
#if 0
    This->width = 0;
    This->height = 0;
#endif
    This->pidMP = -1;
    This->hpipe = 0;
    This->url = 0;
    This->fPlaylist = FALSE;
    This->buffer = 0;
    This->buflen = 0;
    This->hwndParent = 0;
    This->fFullScreen = FALSE;
    This->fPaused = FALSE;
    This->hwndPopup = WinLoadMenu( HWND_DESKTOP, DLLInstance, ID_POPUP );

    SetMsg( This, MSG_PLUGIN );

    // Should check src attribute here,
    // because some url does not create stream
    for( int i = 0; i < argc; i++ )
    {
        if( strcmp( argn[ i ], "src" ) == 0 )
            This->srcname = StrDup( argv[ i ]);
#if 0
        else if( strcmp( argn[ i ], "width" ) == 0 )
            This->width = atoi( argv[ i ]);
        else if( strcmp( argn[ i ], "height" ) == 0 )
            This->height = atoi( argv[ i ]);
#endif
    }

    return NPERR_NO_ERROR;
}


//-----------------------------------------------------------------------------
// NPP_Destroy:
//-----------------------------------------------------------------------------
NPError NP_LOADDS
NPP_Destroy(NPP instance, NPSavedData** save)
{
    dprintf("NPP_Destroy\n");

    if (instance == 0   )
        return NPERR_INVALID_INSTANCE_ERROR;

    PluginInstance* This = (PluginInstance*) instance->pdata;

    //
    // *Developers*: If desired, call NP_MemAlloc to create a
    // NPSavedDate structure containing any state information
    // that you want restored if this plugin instance is later
    // recreated.
    //

    if (This != 0   )
    {
        // Remove the subclass for the client window
        if(This->hWnd)
        {
            WinSubclassWindow(This->hWnd, This->lpfnOldWndProc);
        }

        // make some saved instance data if necessary
        if(This->pSavedInstanceData == 0   ) {
            // make a struct header for the data
            This->pSavedInstanceData =
                (NPSavedData*)NPN_MemAlloc(sizeof (struct _NPSavedData));

            // fill in the struct
            if(This->pSavedInstanceData != 0   ) {
                This->pSavedInstanceData->len = 0;
                This->pSavedInstanceData->buf = 0   ;

                // replace the def below and references to it with your data
                #define SIDATA "aSavedInstanceDataBlock"

                // the data
                This->pSavedInstanceData->buf = NPN_MemAlloc(sizeof SIDATA);

                if(This->pSavedInstanceData->buf != 0   ) {
                    strcpy((char*)This->pSavedInstanceData->buf, SIDATA);
                    This->pSavedInstanceData->len = sizeof SIDATA;
                }
            }
        }

        // save some instance data
        *save = This->pSavedInstanceData;

        KillMP( This );

        StrFree( This->srcname );
        StrFree( This->url );

        NPN_MemFree( This->buffer );

        NPN_MemFree(instance->pdata);
        instance->pdata = 0   ;
    }

    return NPERR_NO_ERROR;
}


//----------------------------------------------------------------------------
// NPP_SetWindow:
//----------------------------------------------------------------------------
NPError NP_LOADDS
NPP_SetWindow(NPP instance, NPWindow* window)
{
    dprintf("NPP_SetWindow\n");

    if (instance == 0   )
        return NPERR_INVALID_INSTANCE_ERROR;

    PluginInstance* This = (PluginInstance*) instance->pdata;

    //
    // *Developers*: Before setting fWindow to point to the
    // new window, you may wish to compare the new window
    // info to the previous window (if any) to note window
    // size changes, etc.
    //
    if((window->window != 0   ) && (This->hWnd == 0   ))
    {
        This->fWindow = window;
        This->hWnd    = (HWND)This->fWindow->window;

        // subclass the window
        This->lpfnOldWndProc = WinSubclassWindow(This->hWnd, SubClassFunc);
        AssociateInstance(This->hWnd, This);
    }
    else {
        // if window handle changed
        if(This->hWnd != (HWND)window->window) {
            // remember the new window
            This->fWindow = window;

            // Remove the subclass for the old client window
            WinSubclassWindow(This->hWnd, This->lpfnOldWndProc);

            KillMP( This );

            // remember the new window handle
            This->hWnd = (HWND)This->fWindow->window;

            if(This->hWnd != 0   ) {
                // subclass the new one
                This->lpfnOldWndProc = WinSubclassWindow(This->hWnd,
                                                         SubClassFunc);
                AssociateInstance(This->hWnd, This);
            }
        }
    }

    return NPERR_NO_ERROR;
}


//----------------------------------------------------------------------------
// NPP_NewStream:
//----------------------------------------------------------------------------
NPError NP_LOADDS
NPP_NewStream(NPP instance,
              NPMIMEType type,
              NPStream *stream,
              NPBool seekable,
              uint16 *stype)
{
    dprintf("NPP_NewStream\n");

    if (instance == 0   )
        return NPERR_INVALID_INSTANCE_ERROR;

    PluginInstance* This = (PluginInstance*) instance->pdata;

    dprintf("URL = %s\n", stream->url );

    stream->pdata = NPN_MemAlloc( sizeof( PluginStream ));
    PluginStream* Stream = ( PluginStream * )stream->pdata;

    if( Stream == 0 )
        return NPERR_OUT_OF_MEMORY_ERROR;

    Stream->fPlaylist = FALSE;
    Stream->buffer = 0;
    Stream->buflen = 0;

    // if your plugin must operate file based, you may wish to do this:
    //    *stype = NP_ASFILE;
    // remember, though, that use of NP_ASFILE is strongly discouraged;
    // your plugin should attempt to work with data as it comes in on
    // the stream if at all possible

    return NPERR_NO_ERROR;
}


//
// *Developers*:
// These next 2 functions are directly relevant in a plug-in which handles the
// data in a streaming manner.  If you want zero bytes because no buffer space
// is YET available, return 0.  As long as the stream has not been written
// to the plugin, Navigator will continue trying to send bytes.  If the plugin
// doesn't want them, just return some large number from NPP_WriteReady(), and
// ignore them in NPP_Write().  For a NP_ASFILE stream, they are still called
// but can safely be ignored using this strategy.
//

int32 STREAMBUFSIZE = 0X0FFFFFFF;   // If we are reading from a file in
                                    // NP_ASFILE mode, we can take any size
                                    // stream in our write call (since we
                                    // ignore it)

//----------------------------------------------------------------------------
// NPP_WriteReady:
//----------------------------------------------------------------------------
int32 NP_LOADDS
NPP_WriteReady(NPP instance, NPStream *stream)
{
    dprintf("NPP_WriteReady\n");

    if (instance != 0   )
        PluginInstance* This = (PluginInstance*) instance->pdata;

    return STREAMBUFSIZE;   // Number of bytes ready to accept in NPP_Write()
}


//----------------------------------------------------------------------------
// NPP_Write:
//----------------------------------------------------------------------------
int32 NP_LOADDS
NPP_Write(NPP instance, NPStream *stream,
          int32 offset, int32 len, void *buffer)
{
    dprintf("NPP_Write, offset = %d, len = %d\n", offset, len );

    if (instance == 0   )
        return -1;

    PluginInstance* This = (PluginInstance*) instance->pdata;

    PluginStream* Stream = ( PluginStream * )stream->pdata;

    void *newbuf;

    newbuf = NPN_MemAlloc( Stream->buflen + len );
    if( newbuf == 0 )
        return -1;

    memcpy( newbuf, Stream->buffer, Stream->buflen );
    NPN_MemFree( Stream->buffer );

    memcpy(( void * )(( char * )newbuf + Stream->buflen ), buffer, len );

    Stream->buffer  = newbuf;
    Stream->buflen += len;

    if( !Stream->fPlaylist )
    {
        char *p;
        int len;

        // asx
        p = ( char * )memchr( Stream->buffer, '<', Stream->buflen );
        if( p )
        {
            len = Stream->buflen - ( p - ( char * )Stream->buffer );

            if( len >= 4 && strnicmp( p, "<asx", 4 ) == 0 )
                Stream->fPlaylist = TRUE;
        }

        // Winamp pls
        p = ( char * )memchr( Stream->buffer, '[', Stream->buflen );
        if( p )
        {
            len = Stream->buflen - ( p - ( char * )Stream->buffer );

            if( len >= 10 && strncmp( p, "[playlist]", 10 ) == 0 )
                Stream->fPlaylist = TRUE;
        }

        // extended m3u
        p = ( char * )memchr( Stream->buffer, '#', Stream->buflen );
        if( p )
        {
            len = Stream->buflen - ( p - ( char * )Stream->buffer );

            if( len >= 7 && strncmp( p, "#EXTM3U", 7 ) == 0 )
                Stream->fPlaylist = TRUE;
        }

        // others
        if( strnstr(( const char *)Stream->buffer, "://", Stream->buflen ) != 0 )
            Stream->fPlaylist = TRUE;

        if( !Stream->fPlaylist && Stream->buflen > 100 )
            return -1;
    }

    return len;     // The number of bytes accepted.  Return a
                    // negative number here if, e.g., there was an error
                    // during plugin operation and you want to abort the
                    // stream
}


//----------------------------------------------------------------------------
// NPP_DestroyStream:
//----------------------------------------------------------------------------
NPError NP_LOADDS
NPP_DestroyStream(NPP instance, NPStream *stream, NPError reason)
{
    dprintf("NPP_DestroyStream\n");

    if (instance == 0   )
        return NPERR_INVALID_INSTANCE_ERROR;
    PluginInstance* This = (PluginInstance*) instance->pdata;

    PluginStream* Stream = ( PluginStream * )stream->pdata;

    // The same url can be passed many times
    if( This->url == 0 || strcmp( This->url, stream->url ) != 0 )
    {
        StrFree( This->url );
        This->url = StrDup( stream->url );

        if( Stream->fPlaylist )
        {
            NPN_MemFree( This->buffer );
            This->buffer = NPN_MemAlloc( Stream->buflen );
            if( This->buffer == 0 )
            {
                This->buflen = 0;

                return NPERR_OUT_OF_MEMORY_ERROR;
            }

            memcpy( This->buffer, Stream->buffer, Stream->buflen );
            This->buflen = Stream->buflen;

            StrFree( This->srcname );
            This->srcname = StrDup("-");
        }

        This->fPlaylist = Stream->fPlaylist;

        if( This->srcname && *This->srcname )
        {
            if( strcmp( This->srcname, "-") != 0 )
            {
                StrFree( This->srcname );
                This->srcname = StrDup( stream->url );
            }

            SetMsg( This, MSG_CLICKTOPLAY );
        }
        else
            SetMsg( This, MSG_NOMEDIA );
    }
    else
        dprintf("The same url was passed, ignore it.\n");

    NPN_MemFree( Stream->buffer );

    NPN_MemFree( stream->pdata );

    return NPERR_NO_ERROR;
}


//----------------------------------------------------------------------------
// NPP_StreamAsFile:
//----------------------------------------------------------------------------
void NP_LOADDS
NPP_StreamAsFile(NPP instance, NPStream *stream, const char* fname)
{
    dprintf("NPP_StreamAsFile\n");

    if (instance == 0   )
       return;

    PluginInstance* This = (PluginInstance*) instance->pdata;

    // invalidate window to ensure a redraw
    WinInvalidateRect(This->hWnd, 0, TRUE);
}


//----------------------------------------------------------------------------
// NPP_Print:
//----------------------------------------------------------------------------
void NP_LOADDS
NPP_Print(NPP instance, NPPrint* printInfo)
{
    dprintf("NPP_Print\n");

    if(printInfo == 0   )   // trap invalid parm
        return;

    if (instance != 0   )
    {
        PluginInstance* This = (PluginInstance*) instance->pdata;

        if (printInfo->mode == NP_FULL)
        {
            //
            // *Developers*: If your plugin would like to take over
            // printing completely when it is in full-screen mode,
            // set printInfo->pluginPrinted to TRUE and print your
            // plugin as you see fit.  If your plugin wants Netscape
            // to handle printing in this case, set printInfo->pluginPrinted
            // to FALSE (the default) and do nothing.  If you do want
            // to handle printing yourself, printOne is true if the
            // print button (as opposed to the print menu) was clicked.
            // On the Macintosh, platformPrint is a THPrint; on Windows,
            // platformPrint is a structure (defined in npapi.h) containing
            // the printer name, port, etc.
            //
            void* platformPrint = printInfo->print.fullPrint.platformPrint;
            NPBool printOne = printInfo->print.fullPrint.printOne;

            printInfo->print.fullPrint.pluginPrinted = FALSE; // Do the default

        }
        else    // If not fullscreen, we must be embedded
        {
            //
            // *Developers*: If your plugin is embedded, or is full-screen
            // but you returned false in pluginPrinted above, NPP_Print
            // will be called with mode == NP_EMBED.  The NPWindow
            // in the printInfo gives the location and dimensions of
            // the embedded plugin on the printed page.  On the Macintosh,
            // platformPrint is the printer port; on Windows, platformPrint
            // is the handle to the printing device context. On OS/2,
            // platformPrint is the printing presentation space (HPS).
            //
            NPWindow* printWindow = &(printInfo->print.embedPrint.window);

            /* get Presentation Space */
            void* platformPrint = printInfo->print.embedPrint.platformPrint;
            HPS hps = (HPS)platformPrint;

            /* create GPI various data structures about the drawing area */
            POINTL offWindow = { (int)printWindow->x, (int)printWindow->y };
            POINTL endPoint = { (int)printWindow->width, (int)printWindow->height };
            RECTL rect = { (int)printWindow->x,
                           (int)printWindow->y,
                           (int)printWindow->x + (int)printWindow->width,
                           (int)printWindow->y + (int)printWindow->height };

            /* set model transform so origin is 0,0 */
            MATRIXLF matModel;
            GpiQueryModelTransformMatrix(hps, 9L, &matModel);
            GpiTranslate(hps, &matModel, TRANSFORM_ADD, &offWindow);
            GpiSetModelTransformMatrix(hps, 9L, &matModel, TRANSFORM_REPLACE);

            /* draw using common drawing routine */
            Draw(This, hps, &endPoint, TRUE);
        }
    }
}


//----------------------------------------------------------------------------
// NPP_HandleEvent:
// Mac-only.
//----------------------------------------------------------------------------
int16 NP_LOADDS NPP_HandleEvent(NPP instance, void* event)
{
    NPBool eventHandled = FALSE;

    dprintf("NPP_HandleEvent\n");

    if (instance == 0   )
        return eventHandled;

    PluginInstance* This = (PluginInstance*) instance->pdata;

    //
    // *Developers*: The "event" passed in is a Macintosh
    // EventRecord*.  The event.what field can be any of the
    // normal Mac event types, or one of the following additional
    // types defined in npapi.h: getFocusEvent, loseFocusEvent,
    // adjustCursorEvent.  The focus events inform your plugin
    // that it will become, or is no longer, the recepient of
    // key events.  If your plugin doesn't want to receive key
    // events, return false when passed at getFocusEvent.  The
    // adjustCursorEvent is passed repeatedly when the mouse is
    // over your plugin; if your plugin doesn't want to set the
    // cursor, return false.  Handle the standard Mac events as
    // normal.  The return value for all standard events is currently
    // ignored except for the key event: for key events, only return
    // true if your plugin has handled that particular key event.
    //

    return eventHandled;
}

//
// Here is a sample subclass function.
//
MRESULT APIENTRY
SubClassFunc(  HWND hWnd,
               ULONG Message,
               MPARAM wParam,
               MPARAM lParam)
{
    PluginInstance *This = GetInstance(hWnd);

    switch(Message) {
    case WM_REALIZEPALETTE :
        WinInvalidateRect(hWnd, 0, TRUE);
        WinUpdateWindow(hWnd);
        return 0;

    case WM_PAINT :
        {
            /* invalidate the whole window */
            WinInvalidateRect(hWnd, NULL, TRUE);

            /* get PS associated with window  and clear window */
            RECTL rcl;
            HPS hps = WinBeginPaint(hWnd, NULL, &rcl);
            WinFillRect(hps, &rcl, CLR_BLACK);

            WinQueryWindowRect( hWnd, &rcl );
            WinDrawText( hps, -1, ( PCSZ )This->msg, &rcl,
                         CLR_WHITE, CLR_BLACK, DT_CENTER | DT_VCENTER );

            WinEndPaint(hps);
        }
        return 0;

    case WM_TIMER :
        // MPlayer quitted ?
        if( SHORT1FROMMP( wParam ) == TID_MPLAYER &&
            waitpid( This->pidMP, NULL, WNOHANG ) != 0 )
        {
            WinStopTimer( WinQueryAnchorBlock( hWnd ), hWnd, TID_MPLAYER );

            // cancel popup window
            WinSendMsg( This->hwndPopup, WM_CHAR,
                        MPFROM2SHORT( KC_VIRTUALKEY, 0 ),
                        MPFROM2SHORT( 0, VK_ESC ));

            if( This->fFullScreen )
                SwitchWindowMode( This, FALSE );

            SetMsg( This, MSG_CLICKTOPLAY );
        }
        break;

    case WM_FOCUSCHANGE :
        if( This->msg == MSG_RUN && This->fFullScreen )
        {
            HWND    hwndFocus = HWNDFROMMP( wParam );
            USHORT  usSetFocus = SHORT1FROMMP( lParam );

            if( !usSetFocus && hwndFocus != This->hwndPopup )
                SwitchWindowMode( This, FALSE );
        }

        return 0;

    case WM_MOUSEMOVE :
        WinSetPointer( HWND_DESKTOP, WinQuerySysPointer( HWND_DESKTOP, SPTR_ARROW, FALSE ));
        return MRFROMLONG( TRUE );

    case WM_BUTTON1DOWN :
        while( WinQueryFocus( HWND_DESKTOP ) != hWnd )
            WinSetFocus( HWND_DESKTOP, hWnd );

        return MRFROMLONG( TRUE );

    case WM_BUTTON1CLICK :
        if( This->msg == MSG_CLICKTOPLAY )
            ExecMP( This );

        return MRFROMLONG( TRUE );

    case WM_BUTTON2UP :
        if( This->msg == MSG_RUN )
        {
            POINTS  pts;
            ULONG   fs = PU_NONE | PU_KEYBOARD | PU_MOUSEBUTTON1 |
                         PU_HCONSTRAIN | PU_VCONSTRAIN;

            WinEnableMenuItem( This->hwndPopup, IDM_FULL, IsVideoOpened( This ));

            WinCheckMenuItem( This->hwndPopup, IDM_FULL, This->fFullScreen );
            WinCheckMenuItem( This->hwndPopup, IDM_PAUSE, This->fPaused );

            pts.x = SHORT1FROMMP( wParam );
            pts.y = SHORT2FROMMP( wParam );

            WinPopupMenu( hWnd, hWnd, This->hwndPopup, pts.x, pts.y, 0, fs );
        }

        return MRFROMLONG( TRUE );

    // ignore other mouse events
    case WM_CHORD :
    //case WM_BUTTON1DOWN :
    case WM_BUTTON1UP :
    //case WM_BUTTON1CLICK :
    case WM_BUTTON1DBLCLK :
    case WM_BUTTON1MOTIONEND :
    case WM_BUTTON1MOTIONSTART :
    case WM_BUTTON2DOWN :
    //case WM_BUTTON2UP :
    case WM_BUTTON2CLICK :
    case WM_BUTTON2DBLCLK :
    case WM_BUTTON2MOTIONEND :
    case WM_BUTTON2MOTIONSTART :
    case WM_BUTTON3DOWN :
    case WM_BUTTON3UP :
    case WM_BUTTON3CLICK :
    case WM_BUTTON3DBLCLK :
    case WM_BUTTON3MOTIONEND :
    case WM_BUTTON3MOTIONSTART :
        return MRFROMLONG( TRUE );

    case WM_CHAR :
        if( This->msg == MSG_RUN )
        {
            USHORT fsFlags = SHORT1FROMMP( wParam );
            USHORT usCh = SHORT1FROMMP( lParam );
            USHORT usVk = SHORT2FROMMP( lParam );

            if( fsFlags & KC_KEYUP )
                break;

            if( fsFlags & KC_VIRTUALKEY )
            {
                switch( usVk )
                {
                    case VK_LEFT :
                        PutKeyToMP( This,  KEY_LEFT );
                        break;

                    case VK_UP :
                        PutKeyToMP( This,  KEY_UP );
                        break;

                    case VK_RIGHT :
                        PutKeyToMP( This,  KEY_RIGHT );
                        break;

                    case VK_DOWN :
                        PutKeyToMP( This,  KEY_DOWN );
                        break;

                    case VK_TAB :
                        PutKeyToMP( This,  KEY_TAB );
                        break;

                    case VK_BACKSPACE :
                        PutKeyToMP( This,  KEY_BS );
                        break;

                    case VK_DELETE :
                        PutKeyToMP( This,  KEY_DELETE );
                        break;

                    case VK_INSERT :
                        PutKeyToMP( This,  KEY_INSERT );
                        break;

                    case VK_HOME :
                        PutKeyToMP( This,  KEY_HOME );
                        break;

                    case VK_END :
                        PutKeyToMP( This,  KEY_END );
                        break;

                    case VK_PAGEUP :
                        PutKeyToMP( This,  KEY_PAGE_UP );
                        break;

                    case VK_PAGEDOWN :
                        PutKeyToMP( This,  KEY_PAGE_DOWN );
                        break;

                    case VK_ESC :
                        PutKeyToMP( This,  KEY_ESC );
                        break;

                    case VK_SPACE :
                        This->fPaused = !This->fPaused;
                        PipeWrite( This, "pause\n");
                        break;

                    case VK_NEWLINE :
                        PutKeyToMP( This,  KEY_ENTER );
                        break;
                }
            }
            else if(( fsFlags & KC_CHAR ) && !HIBYTE( usCh ))
            {
                switch( usCh )
                {
                    case 'f' :
                        SwitchWindowMode( This, !This->fFullScreen );
                        break;

                    case 'p' :
                        This->fPaused = TRUE;
                        // fall through

                    default :
                        PutKeyToMP( This,  usCh );
                        break;
                }
            }
        }

        return MRFROMLONG( TRUE );

    case WM_COMMAND :
        switch( SHORT1FROMMP( wParam ))
        {
            case IDM_FULL :
                SwitchWindowMode( This, !This->fFullScreen );
                break;

            case IDM_PAUSE :
                This->fPaused = !This->fPaused;
                PipeWrite( This, "pause\n");
                break;

            case IDM_STOP :
                PipeWrite( This, "stop\n");
                break;

            case IDM_PREV :
                // Go backward in the playlist
                PipeWrite( This, "pt_step -1\n");
                break;

            case IDM_NEXT :
                // Go forward in the playlist
                PipeWrite( This, "pt_step +1\n");
                break;
        }

        return 0;
        
    default:
        break;
    }

    return ((PFNWP)This->lpfnOldWndProc)(
                          hWnd,
                          Message,
                          wParam,
                          lParam);
}

void Draw(PluginInstance *This, HPS hps, POINTL *endPoint, BOOL fPrinting)
{
    // Notice when you resize the browser window, the plugin actually gets
    // drawn twice. You may want to draw into a bitmap and bitblt to
    // improve performance if you plugin does a lot of drawing.
}

char *StrDup( const char *s, int len )
{
    char *ret;

    if( s == 0 )
        return 0;

    if( len == -1 )
        len = strlen( s );

    ret = ( char * )NPN_MemAlloc( len + 1 );
    if( ret )
    {
        strncpy( ret, s, len );
        ret[ len ] = 0;
    }

    return ret;
}

void StrFree( const char *s )
{
    NPN_MemFree(( void * )s );
}

void PipeOpen( PluginInstance *This )
{
    char pipeName[ 100 ];

    This->hpipe = 0;

    if( This->pidMP == -1 )
        return;

    sprintf( pipeName, "\\PIPE\\MPLAYER\\%x", This->pidMP );

    DosCreateNPipe(( PSZ )pipeName, &This->hpipe, NP_ACCESS_DUPLEX, 1, 1024, 1024, 0 );
}

void PipeClose( PluginInstance *This )
{
    if( This->hpipe == 0 )
        return;

    DosClose( This->hpipe );
}

void PipeWrite( PluginInstance *This, const char *s )
{
    ULONG cbActual;

    if( This->pidMP == -1 || This->hpipe == 0 || s == 0 )
        return;

    // MPlayer quitted ?
    if( waitpid( This->pidMP, NULL, WNOHANG ) != 0 )
        return;

    DosConnectNPipe( This->hpipe );

    DosWrite( This->hpipe, s, strlen( s ), &cbActual );

    // Wait for ACK
    DosRead( This->hpipe, &cbActual, sizeof( ULONG ), &cbActual );

    DosDisConnectNPipe( This->hpipe );
}

#define WINIDFROMHWND( hwnd ) (( hwnd ) - 0x80000000UL )

void ExecMP( PluginInstance *This )
{
    DosRequestMutexSem( hmtxMP, ( ULONG )-1 );

    This->pidMP = -1;

    This->fFullScreen = FALSE;
    This->fPaused = FALSE;

    if( This->srcname && *This->srcname )
    {
        const char* mp_path;
        char        wid[ 15 ] = { '0', 'x', };
        const char* playlist = "";
        HFILE       hfRead, hfWrite;
        HFILE       hfOrgStdIn, hfStdIn = 0;

        mp_path = getenv("MPLAYER_PATH");
        if( mp_path == 0 )
            mp_path = "mplayer.exe";

        _ltoa( WINIDFROMHWND( This->hWnd ), wid + 2, 16 );

        SetMsg( This, MSG_START );

        if( This->fPlaylist )
        {
            ULONG cbActual;

            playlist = "-playlist";

            hfOrgStdIn = ( HFILE )-1;
            DosDupHandle( 0, &hfOrgStdIn );

            DosCreatePipe( &hfRead, &hfWrite, This->buflen );

            DosDupHandle( hfRead, &hfStdIn );

            dprintf("Write to pipe, size = %ld\n", This->buflen );

            dprintf("----- Buffer start -----\n");
            dprintf("%.*s", This->buflen, This->buffer );
            dprintf("----- Buffer end -----\n");

            DosWrite( hfWrite, This->buffer, This->buflen, &cbActual );

            dprintf("Finished writing to pipe, size = %ld\n", cbActual );

            DosClose( hfRead );
            DosClose( hfWrite );
        }

        dprintf("SRC = %s\n", This->srcname );

        This->pidMP = spawnlp( P_NOWAIT, mp_path, mp_path,
                               playlist, This->srcname, "-wid", wid, "-quiet", "-nofs", "-slave", NULL );

        dprintf("PID = %x\n", This->pidMP );

        if( This->pidMP == -1 )
            SetMsg( This, MSG_FAIL );
        else
        {
            SetMsg( This, MSG_RUN );

            WinStartTimer( WinQueryAnchorBlock( This->hWnd ), This->hWnd, TID_MPLAYER, 100 );
        }

        if( This->fPlaylist )
        {
            DosDupHandle( hfOrgStdIn, &hfStdIn );
            DosClose( hfOrgStdIn );
        }

        PipeOpen( This );
    }

    DosReleaseMutexSem( hmtxMP );
}

void KillMP( PluginInstance *This )
{
    if( This->pidMP == -1 )
        return;

    PipeWrite( This, "quit\n");

    waitpid( This->pidMP, NULL, 0 );

    PipeClose( This );
}

static void SwitchWindowMode( PluginInstance *This, BOOL fToFullScreen )
{
    if( fToFullScreen )
    {
        if( !IsVideoOpened( This ))
            return;

        WinQueryWindowPos( This->hWnd, &This->swp );
        WinSetParent( This->hWnd, HWND_DESKTOP, FALSE );

        WinSetWindowPos( This->hWnd, HWND_TOP,
                         0, 0,
                         WinQuerySysValue( HWND_DESKTOP, SV_CXSCREEN ),
                         WinQuerySysValue( HWND_DESKTOP, SV_CYSCREEN ),
                         SWP_SIZE | SWP_MOVE | SWP_ZORDER | SWP_SHOW );

        This->fFullScreen = TRUE;
    }
    else
    {
        WinSetParent( This->hWnd, This->hwndParent, TRUE );

        WinSetWindowPos( This->hWnd, HWND_TOP,
                         This->swp.x, This->swp.y,
                         This->swp.cx, This->swp.cy,
                         SWP_SIZE | SWP_MOVE | SWP_ZORDER | SWP_SHOW );

        This->fFullScreen = FALSE;
    }
}

static inline BOOL IsVideoOpened( PluginInstance *This )
{
    HENUM   henum;
    BOOL    result;

    // if a video window of MPlayer was opened,
    // it must be a child of plugin window

    henum = WinBeginEnumWindows( This->hWnd );

    result = WinGetNextWindow( henum ) != NULLHANDLE;

    WinEndEnumWindows( henum );

    return result;
}

static inline void PutKeyToMP( PluginInstance *This, int key )
{
    char cmd[ 100 ];

    sprintf( cmd, "key_down_event %d\n", key );
    PipeWrite( This, cmd );
}

