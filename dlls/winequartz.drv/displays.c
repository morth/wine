/*
 * Quartz driver displays support
 *
 * Copyright 2006 Alexandre Julliard
 * Copyright 2006 Phil Krylov
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "windef.h"

#include "quartzdrv.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartzdrv);

static MONITORINFOEXW default_monitor =
{
    sizeof(default_monitor),    /* cbSize */
    { 0, 0, 0, 0 },             /* rcMonitor */
    { 0, 0, 0, 0 },             /* rcWork */
    MONITORINFOF_PRIMARY,       /* dwFlags */
    { '\\','\\','.','\\','D','I','S','P','L','A','Y','1',0 }   /* szDevice */
};

static MONITORINFOEXW *monitors = NULL;
static int nb_monitors;

static int primary_monitor = 0;

static inline MONITORINFOEXW *get_primary(void)
{
    /* default to 0 if specified primary is invalid */
    int idx = primary_monitor;
    if (idx >= nb_monitors) idx = 0;
    return &monitors[idx];
}

static inline HMONITOR index_to_monitor( int index )
{
    return (HMONITOR)(UINT_PTR)(index + 1);
}

static inline int monitor_to_index( HMONITOR handle )
{
    UINT_PTR index = (UINT_PTR)handle;
    if (index < 1 || index > nb_monitors) return -1;
    return index - 1;
}

static int query_displays(void)
{
    INT count;
    CGDirectDisplayID *displays;
    
    if (CGGetActiveDisplayList(65536, NULL, &count))
        return 0;
    if ((displays = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*displays))) == NULL)
        return 0;
    if (CGGetActiveDisplayList(count, displays, &count))
    {
        HeapFree(GetProcessHeap(), 0, displays);
        return 0;
    }
    if (monitors && monitors != &default_monitor) HeapFree(GetProcessHeap(), 0, monitors);
    if ((monitors = HeapAlloc( GetProcessHeap(), 0, count * sizeof(*monitors) )))
    {
        int i;

        nb_monitors = count;
        for (i = 0; i < nb_monitors; i++)
        {
            CGRect bounds = CGDisplayBounds(i);
            
            monitors[i].cbSize = sizeof( monitors[i] );
            monitors[i].rcMonitor.left   = bounds.origin.x;
            monitors[i].rcMonitor.top    = bounds.origin.y;
            monitors[i].rcMonitor.right  = bounds.origin.x + bounds.size.width;
            monitors[i].rcMonitor.bottom = bounds.origin.y + bounds.size.height;
            monitors[i].rcWork           = monitors[i].rcMonitor;
            monitors[i].dwFlags          = 0;
            /* FIXME: using the same device name for all monitors for now */
            lstrcpyW( monitors[i].szDevice, default_monitor.szDevice );
        }

        get_primary()->dwFlags |= MONITORINFOF_PRIMARY;
    }
    else count = 0;
    HeapFree(GetProcessHeap(), 0, displays);
    return count;
}


void QDRV_displays_init(void)
{
    query_displays();
}


/***********************************************************************
 *		QDRV_GetMonitorInfo  (qDRV.@)
 */
BOOL QDRV_GetMonitorInfo( HMONITOR handle, LPMONITORINFO info )
{
    int i = monitor_to_index( handle );

    if (i == -1)
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    info->rcMonitor = monitors[i].rcMonitor;
    info->rcWork = monitors[i].rcWork;
    info->dwFlags = monitors[i].dwFlags;
    if (info->cbSize >= sizeof(MONITORINFOEXW))
        lstrcpyW( ((MONITORINFOEXW *)info)->szDevice, monitors[i].szDevice );
    return TRUE;
}


/***********************************************************************
 *		QDRV_EnumDisplayMonitors  (QDRV.@)
 */
BOOL QDRV_EnumDisplayMonitors( HDC hdc, LPRECT rect, MONITORENUMPROC proc, LPARAM lp )
{
    int i;

    if (hdc)
    {
        POINT origin;
        RECT limit;

        if (!GetDCOrgEx( hdc, &origin )) return FALSE;
        if (GetClipBox( hdc, &limit ) == ERROR) return FALSE;

        if (rect && !IntersectRect( &limit, &limit, rect )) return TRUE;

        for (i = 0; i < nb_monitors; i++)
        {
            RECT monrect = monitors[i].rcMonitor;
            OffsetRect( &monrect, -origin.x, -origin.y );
            if (IntersectRect( &monrect, &monrect, &limit ))
                if (!proc( index_to_monitor(i), hdc, &monrect, lp )) break;
        }
    }
    else
    {
        for (i = 0; i < nb_monitors; i++)
        {
            RECT unused;
            if (!rect || IntersectRect( &unused, &monitors[i].rcMonitor, rect ))
                if (!proc( index_to_monitor(i), 0, &monitors[i].rcMonitor, lp )) break;
        }
    }
    return TRUE;
}
