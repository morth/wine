/*
 * X11DRV initialization code
 *
 * Copyright 1998 Patrik Stridvall
 * Copyright 2000 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "config.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "windef.h"
#include "winbase.h"
#include "wine/winbase16.h"
#include "winreg.h"
#include "wine/server.h"
#include "wine/debug.h"

#include "quartzdrv.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartzdrv);

AKObjectRef root_window;
unsigned int screen_width;
unsigned int screen_height;
unsigned int screen_depth;
int managed_mode = 0;


DWORD thread_data_tls_index = TLS_OUT_OF_INDEXES;

static CRITICAL_SECTION QDRV_CritSection;
static CRITICAL_SECTION_DEBUG critsect_debug =
{
    0, 0, &QDRV_CritSection,
    { &critsect_debug.ProcessLocksList, &critsect_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": QDRV_CritSection") }
};
static CRITICAL_SECTION QDRV_CritSection = { &critsect_debug, -1, 0, 0, 0, 0 };


/***********************************************************************
 *		wine_tsx11_lock   (X11DRV.@)
 */
void wine_quartzdrv_lock(void)
{
    EnterCriticalSection( &QDRV_CritSection );
}

/***********************************************************************
 *		wine_tsx11_unlock   (X11DRV.@)
 */
void wine_quartzdrv_unlock(void)
{
    LeaveCriticalSection( &QDRV_CritSection );
}

/***********************************************************************
 *           X11DRV thread initialisation routine
 */
struct quartzdrv_thread_data *quartzdrv_init_thread_data(void)
{
    struct quartzdrv_thread_data *data;
    FIXME("\n");
    
    if (!(data = HeapAlloc( GetProcessHeap(), 0, sizeof(*data) )))
    {
        ERR( "could not create data\n" );
        ExitProcess(1);
    }
    
    data->process_event_count = 0;

    data->cursor = 0;
    data->cursor_window = 0;
    data->grab_window = 0;
    data->last_focus = 0;

    TlsSetValue( thread_data_tls_index, data );
   // if (desktop_tid) AttachThreadInput( GetCurrentThreadId(), desktop_tid, TRUE );
    return data;
}

/***********************************************************************
 *           X11DRV process initialisation routine
 */
static BOOL process_attach(void)
{
    TRACE("\n");
    if ((thread_data_tls_index = TlsAlloc()) == TLS_OUT_OF_INDEXES) return FALSE;

    root_window = CGMainDisplayID();

    screen_width  = CGDisplayPixelsWide(CGMainDisplayID());
    screen_height = CGDisplayPixelsHigh(CGMainDisplayID());
    screen_depth = CGDisplayBitsPerPixel(CGMainDisplayID());
    
    QDRV_InitializeCarbon();
    QDRV_displays_init();
        
    TRACE("root_window=%p , screen_width=%u, screen_height=%u, screen_depth=%u \n", root_window, screen_width, screen_height, screen_depth);
    
    return TRUE;
}


/***********************************************************************
 *           thread termination routine
 */
static void thread_detach(void)
{
    TRACE("\n");
}


/***********************************************************************
 *           process termination routine
 */
static void process_detach(void)
{
    FIXME("\n");
    QDRV_FinalizeCarbon();
}

BOOL WINAPI DllMain( HINSTANCE hinst, DWORD reason, LPVOID reserved )
{
    BOOL ret = TRUE;

    switch(reason)
    {
    case DLL_PROCESS_ATTACH:
        ret = process_attach();
        break;
    case DLL_THREAD_DETACH:
        thread_detach();
        break;
    case DLL_PROCESS_DETACH:
        process_detach();
        break;
    }
    return ret;
}

