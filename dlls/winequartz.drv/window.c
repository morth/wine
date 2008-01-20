/*
 * Window related functions
 *
 * Copyright 1993, 1994, 1995, 1996, 2001 Alexandre Julliard
 * Copyright 1993 David Metcalfe
 * Copyright 1995, 1996 Alex Korobka
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

#include <stdarg.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winreg.h"
#include "winuser.h"
#include "wine/unicode.h"

#include "quartzdrv.h"
#include "wine/debug.h"
#include "wine/server.h"
#include "win.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartzdrv);

static const char whole_window_prop[] = "__wine_quartz_whole_window";
static const char icon_window_prop[]  = "__wine_quartz_icon_window";

static struct list win_data_list = LIST_INIT(win_data_list);

void destroy_window_data()
{
    struct list *cursor, *cursor2;
    struct quartzdrv_win_data *data;
    
    wine_quartzdrv_lock();
    LIST_FOR_EACH_SAFE(cursor, cursor2, &win_data_list)
    {
        data = LIST_ENTRY(cursor, struct quartzdrv_win_data, entry);
        HeapFree( GetProcessHeap(), 0, data );
    }
    wine_quartzdrv_unlock();
}

struct quartzdrv_win_data * get_win_data( HWND hwnd )
{
    struct quartzdrv_win_data *data;
    wine_quartzdrv_lock();
    LIST_FOR_EACH_ENTRY( data, &win_data_list, struct quartzdrv_win_data, entry )
    {
        if ( data->hwnd == hwnd )
        {
            wine_quartzdrv_unlock();
            return data;
        }
    }
    
    wine_quartzdrv_unlock();
    return NULL;
}

struct quartzdrv_win_data *get_win_data_carbon(WindowRef win)
{
    struct quartzdrv_win_data *data;
    wine_quartzdrv_lock();
    LIST_FOR_EACH_ENTRY( data, &win_data_list, struct quartzdrv_win_data, entry )
    {
        if ( data->whole_window == win )
        {
            wine_quartzdrv_unlock();
            return data;
        }
    }
    wine_quartzdrv_unlock();
    return NULL;
}

struct quartzdrv_win_data *new_win_data(void)
{
    struct quartzdrv_win_data * data;
    if (!(data = HeapAlloc( GetProcessHeap(), 0, sizeof(*data) )))
    {
        ERR("Not enough memory\n");
        return NULL;
    }
    
    wine_quartzdrv_lock();
    list_add_head( &win_data_list, &data->entry );
    wine_quartzdrv_unlock();
    return data;
}

void delete_win_data( HWND hwnd )
{
    struct quartzdrv_win_data *data;

    wine_quartzdrv_lock();
    LIST_FOR_EACH_ENTRY( data, &win_data_list, struct quartzdrv_win_data, entry )
    {
        if ( data->hwnd == hwnd )
        {
            list_remove( &data->entry );
            break;
        }
    }
    wine_quartzdrv_unlock();
}

struct quartzdrv_win_data *alloc_win_data(HWND hwnd )
{
    struct quartzdrv_win_data *data = NULL;
    if ( (data = new_win_data()) )
    {
        data->hwnd          = hwnd;
        data->whole_window  = 0;
        data->icon_window   = 0;
        data->managed       = FALSE;
        data->lock_changes  = 0;
        data->hWMIconBitmap = 0;
        data->hWMIconMask   = 0;
        data->dce = 0;
    }
    return data;
}


/***********************************************************************
 *              get_process_name
 *
 * get the name of the current process for setting class hints
 */
char *QDRV_get_process_name(void)
{
    static char *name;

    if (!name)
    {
        WCHAR module[MAX_PATH];
        DWORD len = GetModuleFileNameW( 0, module, MAX_PATH );
        if (len && len < MAX_PATH)
        {
            char *ptr;
            WCHAR *p, *appname = module;

            if ((p = strrchrW( appname, '/' ))) appname = p + 1;
            if ((p = strrchrW( appname, '\\' ))) appname = p + 1;
            len = WideCharToMultiByte( CP_UNIXCP, 0, appname, -1, NULL, 0, NULL, NULL );
            if ((ptr = HeapAlloc( GetProcessHeap(), 0, len )))
            {
                WideCharToMultiByte( CP_UNIXCP, 0, appname, -1, ptr, len, NULL, NULL );
                name = ptr;
            }
        }
    }
    return name;
}

BOOL QDRV_is_window_rect_mapped( const RECT *rect )
{
    /* don't map if rect is empty */
    if (IsRectEmpty( rect )) return FALSE;

    /* don't map if rect is off-screen */
    if (rect->left >= (int)screen_width || rect->top >= (int)screen_height) return FALSE;
    if (rect->right < 0 || rect->bottom < 0) return FALSE;

    return TRUE;
}


/*****************************************************************
 *		SetWindowText   (X11DRV.@)
 */
void QDRV_SetWindowText( HWND hwnd, LPCWSTR text )
{
    UINT count;
    char *buffer;
    char *utf8_buffer;
    AKObjectRef win;

    if ((win = QDRV_get_whole_window( hwnd )))
    {
        /* allocate new buffer for window text */
        count = WideCharToMultiByte(CP_UNIXCP, 0, text, -1, NULL, 0, NULL, NULL);
        if (!(buffer = HeapAlloc( GetProcessHeap(), 0, count )))
        {
            ERR("Not enough memory for window text\n");
            return;
        }
        WideCharToMultiByte(CP_UNIXCP, 0, text, -1, buffer, count, NULL, NULL);

        count = WideCharToMultiByte(CP_UTF8, 0, text, strlenW(text), NULL, 0, NULL, NULL);
        if (!(utf8_buffer = HeapAlloc( GetProcessHeap(), 0, count )))
        {
            ERR("Not enough memory for window text in UTF-8\n");
            HeapFree( GetProcessHeap(), 0, buffer );
            return;
        }
        WideCharToMultiByte(CP_UTF8, 0, text, strlenW(text), utf8_buffer, count, NULL, NULL);

        TRACE("win=%p text=%s\n", win, buffer);
        QDRV_SetWindowTitle(win, buffer);

        HeapFree( GetProcessHeap(), 0, utf8_buffer );
        HeapFree( GetProcessHeap(), 0, buffer );
    }
}

/**********************************************************************
 *		create_whole_window
 *
 * Create the whole X window for a given window
 */
static void *create_whole_window(struct quartzdrv_win_data *data, DWORD style )
{
    int cx, cy;
    RECT rect;

    TRACE("data=%p style=0x%08x\n", data, style);
    
    rect = data->window_rect;

    if (!(cx = rect.right - rect.left)) cx = 1;
    if (!(cy = rect.bottom - rect.top)) cy = 1;
    
    wine_quartzdrv_lock();

    data->whole_rect = rect;
    data->whole_window = QDRV_CreateNewWindow(rect.left, rect.top, cx, cy);

    TRACE("return %p\n", data->whole_window);

    if (!data->whole_window)
    {
        wine_quartzdrv_unlock();
        return 0;
    }     
    /* non-maximized child must be at bottom of Z order */
    if ((style & (WS_CHILD|WS_MAXIMIZE)) == WS_CHILD)
    {
        FIXME("non-maximized child must be at bottom of Z order\n");
    }

    wine_quartzdrv_unlock();
    SetPropA( data->hwnd, whole_window_prop, (HANDLE)data->whole_window );
    
    return data->whole_window;
}

static void destroy_whole_window(struct quartzdrv_win_data *data)
{
    struct quartzdrv_thread_data *thread_data = quartzdrv_thread_data();

    if (!data->whole_window) return;

    TRACE( "win %p nswin %p\n", data->hwnd, data->whole_window );
   
    if (thread_data->cursor_window == data->whole_window) thread_data->cursor_window = 0;

    wine_quartzdrv_lock();
#ifdef WINE_COCOA
    Cocoa_DestroyWindow(data->whole_window);
#else
    FIXME("destroy Carbon window\n");
#endif
    data->whole_window = 0;
    wine_quartzdrv_unlock();

    RemovePropA( data->hwnd, whole_window_prop );
}

/***********************************************************************
 *		QDRV_DestroyWindow   (X11DRV.@)
 */
void QDRV_DestroyWindow( HWND hwnd )
{
    struct quartzdrv_thread_data *thread_data = quartzdrv_thread_data();
    struct quartzdrv_win_data *data;
    TRACE("hwnd=%p\n", hwnd);
    if (!(data = get_win_data( hwnd ))) return;

    free_window_dce(data);

    destroy_whole_window(data);
    if (data->hWMIconBitmap) DeleteObject( data->hWMIconBitmap );
    if (data->hWMIconMask) DeleteObject( data->hWMIconMask);
/*    destroy_icon_window( display, data );
*/
    if (thread_data->last_focus == hwnd) thread_data->last_focus = 0;
    
    HeapFree( GetProcessHeap(), 0, data );
}

/* fill in the desktop window id in the quartzdrv_win_data structure */
static void get_desktop_win(struct quartzdrv_win_data *data )
{
    RECT rect;
    WindowRef win = (WindowRef)GetPropA( data->hwnd, whole_window_prop );

    if (win)
    {
        unsigned int width, height;

        /* retrieve the real size of the desktop */
        SERVER_START_REQ( get_window_rectangles )
        {
            req->handle = data->hwnd;
            wine_server_call( req );
            width  = reply->window.right - reply->window.left;
            height = reply->window.bottom - reply->window.top;
        }
        SERVER_END_REQ;
        data->whole_window = win;
        if (win != root_window) QDRV_init_desktop( win, width, height );
    }
    else
    {
        SetPropA( data->hwnd, whole_window_prop, (HANDLE)root_window );
        //SetPropA( data->hwnd, visual_id_prop, (HANDLE)visualid );
        data->whole_window = root_window;
        SetRect( &rect, 0, 0, screen_width, screen_height );
        QDRV_set_window_pos( data->hwnd, 0, &rect, &rect, SWP_NOZORDER, NULL );
    }
}

BOOL QDRV_CreateDesktopWindow(HWND hwnd)
{
    struct quartzdrv_win_data *data;

    if (!(data = alloc_win_data( hwnd ))) return FALSE;
    get_desktop_win(data);
    
    return TRUE;
}


/**********************************************************************
*		CreateWindow   (X11DRV.@)
*/
BOOL QDRV_CreateWindow( HWND hwnd, CREATESTRUCTA *cs, BOOL unicode )
{
    struct quartzdrv_win_data *data;
    WND *wndPtr;
    HWND insert_after;
    RECT rect;
    DWORD style;
    CBT_CREATEWNDA cbtc;
    CREATESTRUCTA cbcs;
    BOOL ret = FALSE;
        
    if (!(data = alloc_win_data(hwnd))) return FALSE;
    
    TRACE( "hwnd %p data %p cs %d,%d %dx%d\n", hwnd, data, cs->x, cs->y, cs->cx, cs->cy );

    if (cs->cx > 65535)
    {
        ERR( "invalid window width %d\n", cs->cx );
        cs->cx = 65535;
    }
    if (cs->cy > 65535)
    {
        ERR( "invalid window height %d\n", cs->cy );
        cs->cy = 65535;
    }
    if (cs->cx < 0)
    {
        ERR( "invalid window width %d\n", cs->cx );
        cs->cx = 0;
    }
    if (cs->cy < 0)
    {
        ERR( "invalid window height %d\n", cs->cy );
        cs->cy = 0;
    }

    /* initialize the dimensions before sending WM_GETMINMAXINFO */
    SetRect( &rect, cs->x, cs->y, cs->x + cs->cx, cs->y + cs->cy );
    QDRV_set_window_pos( hwnd, 0, &rect, &rect, SWP_NOZORDER, NULL );

    /* create an X window if it's a top level window */
    if (GetAncestor( hwnd, GA_PARENT ) == GetDesktopWindow())
    {
        if (!create_whole_window(data, cs->style )) goto failed;
    }
    else if (hwnd == GetDesktopWindow())
    {
        get_desktop_win(data);
    }
    alloc_window_dce( data );
    
    /* Call the WH_CBT hook */

    /* the window style passed to the hook must be the real window style,
     * rather than just the window style that the caller to CreateWindowEx
     * passed in, so we have to copy the original CREATESTRUCT and get the
     * the real style. */
    cbcs = *cs;
    cbcs.style = GetWindowLongW(hwnd, GWL_STYLE);

    cbtc.lpcs = &cbcs;
    cbtc.hwndInsertAfter = HWND_TOP;
    if (HOOK_CallHooks( WH_CBT, HCBT_CREATEWND, (WPARAM)hwnd, (LPARAM)&cbtc, unicode ))
    {
        TRACE("CBT-hook returned !0\n");
        goto failed;
    }

    /* Send the WM_GETMINMAXINFO message and fix the size if needed */
    if ((cs->style & WS_THICKFRAME) || !(cs->style & (WS_POPUP | WS_CHILD)))
    {
        POINT maxSize, maxPos, minTrack, maxTrack;

        WINPOS_GetMinMaxInfo( hwnd, &maxSize, &maxPos, &minTrack, &maxTrack);
        if (maxSize.x < cs->cx) cs->cx = maxSize.x;
        if (maxSize.y < cs->cy) cs->cy = maxSize.y;
        if (cs->cx < 0) cs->cx = 0;
        if (cs->cy < 0) cs->cy = 0;

        SetRect( &rect, cs->x, cs->y, cs->x + cs->cx, cs->y + cs->cy );
        if (!QDRV_set_window_pos( hwnd, 0, &rect, &rect, SWP_NOZORDER, NULL )) return FALSE;
    }

    /* send WM_NCCREATE */
    TRACE( "hwnd %p cs %d,%d %dx%d\n", hwnd, cs->x, cs->y, cs->cx, cs->cy );
    if (unicode)
        ret = SendMessageW( hwnd, WM_NCCREATE, 0, (LPARAM)cs );
    else
        ret = SendMessageA( hwnd, WM_NCCREATE, 0, (LPARAM)cs );
    if (!ret)
    {
        WARN("aborted by WM_xxCREATE!\n");
        return FALSE;
    }

    /* make sure the window is still valid */
    if (!(data = get_win_data( hwnd ))) return FALSE;

    /* send WM_NCCALCSIZE */
    rect = data->window_rect;
    SendMessageW( hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&rect );

    if (!(wndPtr = WIN_GetPtr(hwnd))) return FALSE;

    /* yes, even if the CBT hook was called with HWND_TOP */
    insert_after = ((wndPtr->dwStyle & (WS_CHILD|WS_MAXIMIZE)) == WS_CHILD) ? HWND_BOTTOM : HWND_TOP;

    QDRV_set_window_pos( hwnd, insert_after, &wndPtr->rectWindow, &rect, 0, NULL );

    TRACE( "win %p window %ld,%ld,%ld,%ld client %ld,%ld,%ld,%ld whole %ld,%ld,%ld,%ld X client %ld,%ld,%ld,%ld xwin %x\n",
           hwnd, wndPtr->rectWindow.left, wndPtr->rectWindow.top,
           wndPtr->rectWindow.right, wndPtr->rectWindow.bottom,
           wndPtr->rectClient.left, wndPtr->rectClient.top,
           wndPtr->rectClient.right, wndPtr->rectClient.bottom,
           data->whole_rect.left, data->whole_rect.top,
           data->whole_rect.right, data->whole_rect.bottom,
           data->client_rect.left, data->client_rect.top,
           data->client_rect.right, data->client_rect.bottom,
           (unsigned int)data->whole_window );

    WIN_ReleasePtr( wndPtr );

    if (unicode)
        ret = (SendMessageW( hwnd, WM_CREATE, 0, (LPARAM)cs ) != -1);
    else
        ret = (SendMessageA( hwnd, WM_CREATE, 0, (LPARAM)cs ) != -1);

    if (!ret) return FALSE;

    NotifyWinEvent(EVENT_OBJECT_CREATE, hwnd, OBJID_WINDOW, 0);

    /* Send the size messages */

    if (!(wndPtr = WIN_GetPtr(hwnd)) || wndPtr == WND_OTHER_PROCESS) return FALSE;
    if (!(wndPtr->flags & WIN_NEED_SIZE))
    {
        RECT rect = wndPtr->rectClient;
        WIN_ReleasePtr( wndPtr );
        /* send it anyway */
        if (((rect.right-rect.left) <0) ||((rect.bottom-rect.top)<0))
            WARN("sending bogus WM_SIZE message 0x%08lx\n",
                 MAKELONG(rect.right-rect.left, rect.bottom-rect.top));
        SendMessageW( hwnd, WM_SIZE, SIZE_RESTORED,
                      MAKELONG(rect.right-rect.left, rect.bottom-rect.top));
        SendMessageW( hwnd, WM_MOVE, 0, MAKELONG( rect.left, rect.top ) );
    }
    else WIN_ReleasePtr( wndPtr );

    /* Show the window, maximizing or minimizing if needed */

    style = GetWindowLongW( hwnd, GWL_STYLE );
    if (style & (WS_MINIMIZE | WS_MAXIMIZE))
    {
        extern UINT WINPOS_MinMaximize( HWND hwnd, UINT cmd, LPRECT rect ); /*FIXME*/

        RECT newPos;
        UINT swFlag = (style & WS_MINIMIZE) ? SW_MINIMIZE : SW_MAXIMIZE;
        WIN_SetStyle( hwnd, 0, WS_MAXIMIZE | WS_MINIMIZE );
        WINPOS_MinMaximize( hwnd, swFlag, &newPos );

        swFlag = SWP_FRAMECHANGED | SWP_NOZORDER; /* Frame always gets changed */
        if (!(style & WS_VISIBLE) || (style & WS_CHILD) || GetActiveWindow()) swFlag |= SWP_NOACTIVATE;

        SetWindowPos( hwnd, 0, newPos.left, newPos.top,
                      newPos.right, newPos.bottom, swFlag );
    }


    return TRUE;
    
failed:
    QDRV_DestroyWindow( hwnd );
    return FALSE;
}

static int get_window_changes(xp_window_changes *changes, const RECT *old, const RECT *new )
{
    int mask = 0;
    
    if (old->right - old->left != new->right - new->left )
    {
        if (!(changes->width = new->right - new->left)) changes->width = 1;
        mask |= XP_SIZE;
    }
    if (old->bottom - old->top != new->bottom - new->top)
    {
        if (!(changes->height = new->bottom - new->top)) changes->height = 1;
        mask |= XP_SIZE;
    }
    if (old->left != new->left)
    {
        changes->x = new->left;
        mask |= XP_ORIGIN;
    }
    if (old->top != new->top)
    {
        changes->y = new->top;
        mask |= XP_ORIGIN;
    }

    return mask;
}

/***********************************************************************
 *		QDRV_sync_window_position
 *
 * Synchronize the X window position with the Windows one
 */
void QDRV_sync_window_position( struct quartzdrv_win_data *data,
                                  UINT swp_flags, const RECT *new_client_rect,
                                  const RECT *new_whole_rect )
{
    TRACE("semi-stub data=%p data->whole_window=%p swp_flags=0x%08x, new_client_rect=%s new_whole_rect=%s\n", data, data->whole_window, swp_flags, wine_dbgstr_rect(new_client_rect), wine_dbgstr_rect(new_whole_rect));

    int mask;
    RECT old_whole_rect;
    xp_window_changes changes;
    
    old_whole_rect = data->whole_rect;
    data->whole_rect = *new_whole_rect;

    data->client_rect = *new_client_rect;
    OffsetRect( &data->client_rect, -data->whole_rect.left, -data->whole_rect.top );

    if (!data->whole_window || data->lock_changes) return;

    mask = get_window_changes(&changes, &old_whole_rect, &data->whole_rect );

    if (!(swp_flags & SWP_NOZORDER))
    {
        /* find window that this one must be after */
        HWND prev = GetWindow( data->hwnd, GW_HWNDPREV );
        while (prev && !(GetWindowLongW( prev, GWL_STYLE ) & WS_VISIBLE))
            prev = GetWindow( prev, GW_HWNDPREV );
        if (!prev)  /* top child */
        {
            changes.stack_mode = XP_MAPPED_ABOVE;
            changes.sibling = 0;
            mask |= XP_STACKING;
        }
        else
        {
            /* should use stack_mode Below but most window managers don't get it right */
            /* so move it above the next one in Z order */
            HWND next = GetWindow( data->hwnd, GW_HWNDNEXT );
            while (next && !(GetWindowLongW( next, GWL_STYLE ) & WS_VISIBLE))
                next = GetWindow( next, GW_HWNDNEXT );
            if (next)
            {
                changes.stack_mode = XP_MAPPED_ABOVE;
                changes.sibling = QDRV_get_whole_window(next);
                mask |= XP_STACKING;
            }
        }
    }

    if (mask)
    {
        DWORD style = GetWindowLongW( data->hwnd, GWL_STYLE );

        TRACE( "setting win %lx pos %ld,%ld,%ldx%ld after %lx changes=%x\n",
               data->whole_window, data->whole_rect.left, data->whole_rect.top,
               data->whole_rect.right - data->whole_rect.left,
               data->whole_rect.bottom - data->whole_rect.top, /*changes.sibling*/0, mask );

        wine_quartzdrv_lock();       
        QDRV_SetWindowFrame(data->whole_window, data->whole_rect.left, data->whole_rect.top, data->whole_rect.right - data->whole_rect.left,
                                data->whole_rect.bottom - data->whole_rect.top);
        
        if (mask & XP_STACKING)
            FIXME("stacking windows\n");
        wine_quartzdrv_unlock();
    }
}

AKObjectRef QDRV_get_whole_window( HWND hwnd )
{
    struct quartzdrv_win_data *data = get_win_data( hwnd );

    if (!data) return (AKObjectRef) GetPropA( hwnd, whole_window_prop );
    return data->whole_window;
}

/*****************************************************************
 *		SetParent   (QDRV.@)
 */
HWND QDRV_SetParent( HWND hwnd, HWND parent )
{
 //   Display *display = thread_display();
    WND *wndPtr;
    BOOL ret;
    HWND old_parent = 0;

    /* Windows hides the window first, then shows it again
     * including the WM_SHOWWINDOW messages and all */
    BOOL was_visible = ShowWindow( hwnd, SW_HIDE );

    FIXME("hwnd=%p parent=%p\n", hwnd, parent);

    wndPtr = WIN_GetPtr( hwnd );
    if (!wndPtr || wndPtr == WND_OTHER_PROCESS || wndPtr == WND_DESKTOP) return 0;

    SERVER_START_REQ( set_parent )
    {
        req->handle = hwnd;
        req->parent = parent;
        if ((ret = !wine_server_call( req )))
        {
            old_parent = reply->old_parent;
            wndPtr->parent = parent = reply->full_parent;
        }

    }
    SERVER_END_REQ;
    WIN_ReleasePtr( wndPtr );
    if (!ret) return 0;

    if (parent != old_parent)
    {
        struct quartzdrv_win_data *data = get_win_data( hwnd );

        if (!data) return 0;

        if (parent != GetDesktopWindow()) /* a child window */
        {
            if (old_parent == GetDesktopWindow())
            {
                /* destroy the old X windows */
                TRACE("destroy the old windows\n");
                    
                destroy_whole_window(data);
              //  destroy_icon_window( display, data );
            }
        }
        else  /* new top level window */
        {
            /* FIXME: we ignore errors since we can't really recover anyway */
            create_whole_window(data, GetWindowLongW( hwnd, GWL_STYLE ) );
        }
    }

    /* SetParent additionally needs to make hwnd the topmost window
       in the x-order and send the expected WM_WINDOWPOSCHANGING and
       WM_WINDOWPOSCHANGED notification messages.
    */
    SetWindowPos( hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                  SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | (was_visible ? SWP_SHOWWINDOW : 0) );
    /* FIXME: a WM_MOVE is also generated (in the DefWindowProc handler
     * for WM_WINDOWPOSCHANGED) in Windows, should probably remove SWP_NOMOVE */

    return old_parent;
}

void QDRV_SetFocus( HWND hwnd )
{
    struct quartzdrv_win_data *data;
    FIXME("stub: hwnd=%p\n", hwnd);
    
    /* Only mess with the X focus if there's */
    /* no desktop window and if the window is not managed by the WM. */
    if (root_window != Cocoa_GetDesktopWin() ) return;

    if (!hwnd)  /* If setting the focus to 0, uninstall the colormap */
    {
       /* wine_tsx11_lock();
        if (X11DRV_PALETTE_PaletteFlags & X11DRV_PALETTE_PRIVATE)
            XUninstallColormap( display, X11DRV_PALETTE_PaletteXColormap );
        wine_tsx11_unlock();*/
        return;
    }

    hwnd = GetAncestor( hwnd, GA_ROOT );

    if (!(data = get_win_data( hwnd ))) return;
    if (!data->whole_window) return;

#if 0
    wine_quartzdrv_lock();
    
        CARBON_ShowWindow(data->whole_window);
                        
    wine_quartzdrv_unlock();
#endif
}

/**********************************************************************
 *		SetWindowIcon (QuartzDrv.@)
 *
 * hIcon or hIconSm has changed (or is being initialised for the
 * first time).
 * 
 * Set Window icon (TODO)
 * and reset Dock icon.
 */
void QDRV_SetWindowIcon( HWND hwnd, UINT type, HICON hicon)
{
    struct quartzdrv_win_data *data;

    TRACE("semi-stub hwnd=%p type=%x icon=%p\n", hwnd, type, hicon);

    if (type != ICON_BIG) return;  /* nothing to do here */
    if (!(data = get_win_data( hwnd ))) return;
    if (!data->whole_window) return;

    if (data->hWMIconBitmap) DeleteObject( data->hWMIconBitmap );
    if (data->hWMIconMask) DeleteObject( data->hWMIconMask);
    data->hWMIconBitmap = 0;
    data->hWMIconMask = 0;

    if (hicon)
    {
        HBITMAP hbmOrig;
        RECT rcMask;
        BITMAP bmMask;
        ICONINFO ii;
        HDC hDC;
        QUARTZ_PHYSBITMAP *icon_bitmap;
        QUARTZ_PHYSBITMAP *mask_bitmap;
        CGImageRef toDraw = NULL;
        CGImageRef img = NULL;
        CGImageRef mask = NULL;
        
        GetIconInfo(hicon, &ii);

        GetObjectA(ii.hbmMask, sizeof(bmMask), &bmMask);
        rcMask.top    = 0;
        rcMask.left   = 0;
        rcMask.right  = bmMask.bmWidth;
        rcMask.bottom = bmMask.bmHeight;

        hDC = CreateCompatibleDC(0);
        hbmOrig = SelectObject(hDC, ii.hbmMask);
        InvertRect(hDC, &rcMask);
        SelectObject(hDC, ii.hbmColor);
        SelectObject(hDC, hbmOrig);
        DeleteDC(hDC);

        data->hWMIconBitmap = ii.hbmColor;
        data->hWMIconMask = ii.hbmMask;

        icon_bitmap = QDRV_get_phys_bitmap(data->hWMIconBitmap);
        mask_bitmap = QDRV_get_phys_bitmap(data->hWMIconMask);
             
        /* Dock image */
        img = CGBitmapContextCreateImage(icon_bitmap->context);
        mask = CGBitmapContextCreateImage(mask_bitmap->context);
        
        toDraw = CGImageCreateWithMask(img, mask);
        
        QDRV_SetDockAppIcon(toDraw);
                
        CGImageRelease(toDraw);
        CGImageRelease(mask);
        CGImageRelease(img);
    }
}
