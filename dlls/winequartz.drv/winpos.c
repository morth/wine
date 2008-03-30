/*
 * Window position related functions.
 *
 * Copyright 2006 Emmanuel Maillard
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

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winerror.h"
#include "wownt32.h"
#include "wine/wingdi16.h"

#include "quartzdrv.h"
#include "win.h"

#include "wine/server.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartzdrv);

#define SWP_AGG_NOPOSCHANGE \
(SWP_NOSIZE | SWP_NOMOVE | SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE | SWP_NOZORDER)
#define SWP_AGG_STATUSFLAGS \
(SWP_AGG_NOPOSCHANGE | SWP_FRAMECHANGED | SWP_HIDEWINDOW | SWP_SHOWWINDOW)

#define SWP_EX_NOCOPY       0x0001
#define SWP_EX_PAINTSELF    0x0002
#define SWP_EX_NONCLIENT    0x0004

#define HAS_THICKFRAME(style) \
(((style) & WS_THICKFRAME) && \
 !(((style) & (WS_DLGFRAME|WS_BORDER)) == WS_DLGFRAME))

#define ON_LEFT_BORDER(hit) \
(((hit) == HTLEFT) || ((hit) == HTTOPLEFT) || ((hit) == HTBOTTOMLEFT))
#define ON_RIGHT_BORDER(hit) \
(((hit) == HTRIGHT) || ((hit) == HTTOPRIGHT) || ((hit) == HTBOTTOMRIGHT))
#define ON_TOP_BORDER(hit) \
(((hit) == HTTOP) || ((hit) == HTTOPLEFT) || ((hit) == HTTOPRIGHT))
#define ON_BOTTOM_BORDER(hit) \
(((hit) == HTBOTTOM) || ((hit) == HTBOTTOMLEFT) || ((hit) == HTBOTTOMRIGHT))



/***********************************************************************
*           SWP_DoWinPosChanging
*/
static BOOL SWP_DoWinPosChanging( WINDOWPOS* pWinpos, RECT* pNewWindowRect, RECT* pNewClientRect )
{
    WND *wndPtr;
    
    /* Send WM_WINDOWPOSCHANGING message */
    
    if (!(pWinpos->flags & SWP_NOSENDCHANGING))
        SendMessageW( pWinpos->hwnd, WM_WINDOWPOSCHANGING, 0, (LPARAM)pWinpos );
    
    if (!(wndPtr = WIN_GetPtr( pWinpos->hwnd )) || wndPtr == WND_OTHER_PROCESS) return FALSE;
    
    /* Calculate new position and size */
    
    *pNewWindowRect = wndPtr->rectWindow;
    *pNewClientRect = (wndPtr->dwStyle & WS_MINIMIZE) ? wndPtr->rectWindow
                                                      : wndPtr->rectClient;
    
    if (!(pWinpos->flags & SWP_NOSIZE))
    {
        pNewWindowRect->right  = pNewWindowRect->left + pWinpos->cx;
        pNewWindowRect->bottom = pNewWindowRect->top + pWinpos->cy;
    }
    if (!(pWinpos->flags & SWP_NOMOVE))
    {
        pNewWindowRect->left    = pWinpos->x;
        pNewWindowRect->top     = pWinpos->y;
        pNewWindowRect->right  += pWinpos->x - wndPtr->rectWindow.left;
        pNewWindowRect->bottom += pWinpos->y - wndPtr->rectWindow.top;
        
        OffsetRect( pNewClientRect, pWinpos->x - wndPtr->rectWindow.left,
                    pWinpos->y - wndPtr->rectWindow.top );
    }
    pWinpos->flags |= SWP_NOCLIENTMOVE | SWP_NOCLIENTSIZE;
    
    TRACE( "hwnd %p, after %p, swp %d,%d %dx%d flags %08x\n",
           pWinpos->hwnd, pWinpos->hwndInsertAfter, pWinpos->x, pWinpos->y,
           pWinpos->cx, pWinpos->cy, pWinpos->flags );
    TRACE( "current %s style %08lx new %s\n",
           wine_dbgstr_rect( &wndPtr->rectWindow ), wndPtr->dwStyle,
           wine_dbgstr_rect( pNewWindowRect ));
    
    WIN_ReleasePtr( wndPtr );
    return TRUE;
}


/***********************************************************************
*           get_valid_rects
*
* Compute the valid rects from the old and new client rect and WVR_* flags.
* Helper for WM_NCCALCSIZE handling.
*/
static inline void get_valid_rects( const RECT *old_client, const RECT *new_client, UINT flags,
                                    RECT *valid )
{
    int cx, cy;
    
    if (flags & WVR_REDRAW)
    {
        SetRectEmpty( &valid[0] );
        SetRectEmpty( &valid[1] );
        return;
    }
    
    if (flags & WVR_VALIDRECTS)
    {
        if (!IntersectRect( &valid[0], &valid[0], new_client ) ||
            !IntersectRect( &valid[1], &valid[1], old_client ))
        {
            SetRectEmpty( &valid[0] );
            SetRectEmpty( &valid[1] );
            return;
        }
        flags = WVR_ALIGNLEFT | WVR_ALIGNTOP;
    }
    else
    {
        valid[0] = *new_client;
        valid[1] = *old_client;
    }
    
    /* make sure the rectangles have the same size */
    cx = min( valid[0].right - valid[0].left, valid[1].right - valid[1].left );
    cy = min( valid[0].bottom - valid[0].top, valid[1].bottom - valid[1].top );
    
    if (flags & WVR_ALIGNBOTTOM)
    {
        valid[0].top = valid[0].bottom - cy;
        valid[1].top = valid[1].bottom - cy;
    }
    else
    {
        valid[0].bottom = valid[0].top + cy;
        valid[1].bottom = valid[1].top + cy;
    }
    if (flags & WVR_ALIGNRIGHT)
    {
        valid[0].left = valid[0].right - cx;
        valid[1].left = valid[1].right - cx;
    }
    else
    {
        valid[0].right = valid[0].left + cx;
        valid[1].right = valid[1].left + cx;
    }
}


/***********************************************************************
*           SWP_DoNCCalcSize
*/
static UINT SWP_DoNCCalcSize( WINDOWPOS* pWinpos, const RECT* pNewWindowRect, RECT* pNewClientRect,
                              RECT *validRects )
{
    UINT wvrFlags = 0;
    WND *wndPtr;
    
    if (!(wndPtr = WIN_GetPtr( pWinpos->hwnd )) || wndPtr == WND_OTHER_PROCESS) return 0;
    
    /* Send WM_NCCALCSIZE message to get new client area */
    if( (pWinpos->flags & (SWP_FRAMECHANGED | SWP_NOSIZE)) != SWP_NOSIZE )
    {
        NCCALCSIZE_PARAMS params;
        WINDOWPOS winposCopy;
        
        params.rgrc[0] = *pNewWindowRect;
        params.rgrc[1] = wndPtr->rectWindow;
        params.rgrc[2] = wndPtr->rectClient;
        params.lppos = &winposCopy;
        winposCopy = *pWinpos;
        WIN_ReleasePtr( wndPtr );
        
        wvrFlags = SendMessageW( pWinpos->hwnd, WM_NCCALCSIZE, TRUE, (LPARAM)&params );
        
        *pNewClientRect = params.rgrc[0];
        
        if (!(wndPtr = WIN_GetPtr( pWinpos->hwnd )) || wndPtr == WND_OTHER_PROCESS) return 0;
        
        TRACE( "hwnd %p old win %s old client %s new win %s new client %s\n", pWinpos->hwnd,
               wine_dbgstr_rect(&wndPtr->rectWindow), wine_dbgstr_rect(&wndPtr->rectClient),
               wine_dbgstr_rect(pNewWindowRect), wine_dbgstr_rect(pNewClientRect) );
        
        if( pNewClientRect->left != wndPtr->rectClient.left ||
            pNewClientRect->top != wndPtr->rectClient.top )
            pWinpos->flags &= ~SWP_NOCLIENTMOVE;
        
        if( (pNewClientRect->right - pNewClientRect->left !=
             wndPtr->rectClient.right - wndPtr->rectClient.left))
            pWinpos->flags &= ~SWP_NOCLIENTSIZE;
        else
            wvrFlags &= ~WVR_HREDRAW;
        
        if (pNewClientRect->bottom - pNewClientRect->top !=
            wndPtr->rectClient.bottom - wndPtr->rectClient.top)
            pWinpos->flags &= ~SWP_NOCLIENTSIZE;
        else
            wvrFlags &= ~WVR_VREDRAW;
        
        validRects[0] = params.rgrc[1];
        validRects[1] = params.rgrc[2];
    }
    else
    {
        if (!(pWinpos->flags & SWP_NOMOVE) &&
            (pNewClientRect->left != wndPtr->rectClient.left ||
             pNewClientRect->top != wndPtr->rectClient.top))
            pWinpos->flags &= ~SWP_NOCLIENTMOVE;
    }
    
    if (pWinpos->flags & (SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_SHOWWINDOW | SWP_HIDEWINDOW))
    {
        SetRectEmpty( &validRects[0] );
        SetRectEmpty( &validRects[1] );
    }
    else get_valid_rects( &wndPtr->rectClient, pNewClientRect, wvrFlags, validRects );
    
    WIN_ReleasePtr( wndPtr );
    return wvrFlags;
}


struct move_owned_info
{
    HWND owner;
    HWND insert_after;
};

static BOOL CALLBACK move_owned_popups( HWND hwnd, LPARAM lparam )
{
    struct move_owned_info *info = (struct move_owned_info *)lparam;
    
    if (hwnd == info->owner) return FALSE;
    if ((GetWindowLongW( hwnd, GWL_STYLE ) & WS_POPUP) &&
        GetWindow( hwnd, GW_OWNER ) == info->owner)
    {
        SetWindowPos( hwnd, info->insert_after, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                      SWP_NOSENDCHANGING | SWP_DEFERERASE );
        info->insert_after = hwnd;
    }
    return TRUE;
}

/***********************************************************************
*           SWP_DoOwnedPopups
*
* fix Z order taking into account owned popups -
* basically we need to maintain them above the window that owns them
*
* FIXME: hide/show owned popups when owner visibility changes.
*/
static HWND SWP_DoOwnedPopups(HWND hwnd, HWND hwndInsertAfter)
{
    HWND owner = GetWindow( hwnd, GW_OWNER );
    LONG style = GetWindowLongW( hwnd, GWL_STYLE );
    struct move_owned_info info;
    
    TRACE("(%p) hInsertAfter = %p\n", hwnd, hwndInsertAfter );
    
    if ((style & WS_POPUP) && owner)
    {
        /* make sure this popup stays above the owner */
        
        if( hwndInsertAfter != HWND_TOP )
        {
            HWND hwndLocalPrev = HWND_TOP;
            HWND prev = GetWindow( owner, GW_HWNDPREV );
            
            while (prev && prev != hwndInsertAfter)
            {
                if (hwndLocalPrev == HWND_TOP && GetWindowLongW( prev, GWL_STYLE ) & WS_VISIBLE)
                    hwndLocalPrev = prev;
                prev = GetWindow( prev, GW_HWNDPREV );
            }
            if (!prev) hwndInsertAfter = hwndLocalPrev;
        }
    }
    else if (style & WS_CHILD) return hwndInsertAfter;
    
    info.owner = hwnd;
    info.insert_after = hwndInsertAfter;
    EnumWindows( move_owned_popups, (LPARAM)&info );
    return info.insert_after;
}


/* fix redundant flags and values in the WINDOWPOS structure */
static BOOL fixup_flags( WINDOWPOS *winpos )
{
    HWND parent;
    WND *wndPtr = WIN_GetPtr( winpos->hwnd );
    BOOL ret = TRUE;
    
    if (!wndPtr || wndPtr == WND_OTHER_PROCESS)
    {
        SetLastError( ERROR_INVALID_WINDOW_HANDLE );
        return FALSE;
    }
    winpos->hwnd = wndPtr->hwndSelf;  /* make it a full handle */
    
    /* Finally make sure that all coordinates are valid */
    if (winpos->x < -32768) winpos->x = -32768;
    else if (winpos->x > 32767) winpos->x = 32767;
    if (winpos->y < -32768) winpos->y = -32768;
    else if (winpos->y > 32767) winpos->y = 32767;
    
    if (winpos->cx < 0) winpos->cx = 0;
    else if (winpos->cx > 32767) winpos->cx = 32767;
    if (winpos->cy < 0) winpos->cy = 0;
    else if (winpos->cy > 32767) winpos->cy = 32767;
    
    parent = GetAncestor( winpos->hwnd, GA_PARENT );
    if (!IsWindowVisible( parent )) winpos->flags |= SWP_NOREDRAW;
    
    if (wndPtr->dwStyle & WS_VISIBLE) winpos->flags &= ~SWP_SHOWWINDOW;
    else
    {
        winpos->flags &= ~SWP_HIDEWINDOW;
        if (!(winpos->flags & SWP_SHOWWINDOW)) winpos->flags |= SWP_NOREDRAW;
    }
    
    if ((wndPtr->rectWindow.right - wndPtr->rectWindow.left == winpos->cx) &&
        (wndPtr->rectWindow.bottom - wndPtr->rectWindow.top == winpos->cy))
        winpos->flags |= SWP_NOSIZE;    /* Already the right size */
    
    if ((wndPtr->rectWindow.left == winpos->x) && (wndPtr->rectWindow.top == winpos->y))
        winpos->flags |= SWP_NOMOVE;    /* Already the right position */
    
    if ((wndPtr->dwStyle & (WS_POPUP | WS_CHILD)) != WS_CHILD)
    {
        if (!(winpos->flags & (SWP_NOACTIVATE|SWP_HIDEWINDOW))) /* Bring to the top when activating */
        {
            winpos->flags &= ~SWP_NOZORDER;
            winpos->hwndInsertAfter = HWND_TOP;
        }
    }
    
    /* Check hwndInsertAfter */
    if (winpos->flags & SWP_NOZORDER) goto done;
    
    /* fix sign extension */
    if (winpos->hwndInsertAfter == (HWND)0xffff) winpos->hwndInsertAfter = HWND_TOPMOST;
    else if (winpos->hwndInsertAfter == (HWND)0xfffe) winpos->hwndInsertAfter = HWND_NOTOPMOST;
    
    /* FIXME: TOPMOST not supported yet */
    if ((winpos->hwndInsertAfter == HWND_TOPMOST) ||
        (winpos->hwndInsertAfter == HWND_NOTOPMOST)) winpos->hwndInsertAfter = HWND_TOP;
    
    /* hwndInsertAfter must be a sibling of the window */
    if (winpos->hwndInsertAfter == HWND_TOP)
    {
        if (GetWindow(winpos->hwnd, GW_HWNDFIRST) == winpos->hwnd)
            winpos->flags |= SWP_NOZORDER;
    }
    else if (winpos->hwndInsertAfter == HWND_BOTTOM)
    {
        if (GetWindow(winpos->hwnd, GW_HWNDLAST) == winpos->hwnd)
            winpos->flags |= SWP_NOZORDER;
    }
    else
    {
        if (GetAncestor( winpos->hwndInsertAfter, GA_PARENT ) != parent) ret = FALSE;
        else
        {
            /* don't need to change the Zorder of hwnd if it's already inserted
            * after hwndInsertAfter or when inserting hwnd after itself.
            */
            if ((winpos->hwnd == winpos->hwndInsertAfter) ||
                (winpos->hwnd == GetWindow( winpos->hwndInsertAfter, GW_HWNDNEXT )))
                winpos->flags |= SWP_NOZORDER;
        }
    }
done:
        WIN_ReleasePtr( wndPtr );
    return ret;
}

/***********************************************************************
 *		SetWindowPos   (X11DRV.@)
 */
BOOL QDRV_SetWindowPos( WINDOWPOS *winpos )
{
    RECT newWindowRect, newClientRect, valid_rects[2];
    UINT orig_flags;

    TRACE( "hwnd %p, after %p, swp %d,%d %dx%d flags %08x\n",
           winpos->hwnd, winpos->hwndInsertAfter, winpos->x, winpos->y,
           winpos->cx, winpos->cy, winpos->flags);

    orig_flags = winpos->flags;

    /* Check window handle */
   // if (winpos->hwnd == GetDesktopWindow()) return FALSE;

    /* First make sure that coordinates are valid for WM_WINDOWPOSCHANGING */
    if (!(winpos->flags & SWP_NOMOVE))
    {
        if (winpos->x < -32768) winpos->x = -32768;
        else if (winpos->x > 32767) winpos->x = 32767;
        if (winpos->y < -32768) winpos->y = -32768;
        else if (winpos->y > 32767) winpos->y = 32767;
    }
    if (!(winpos->flags & SWP_NOSIZE))
    {
        if (winpos->cx < 0) winpos->cx = 0;
        else if (winpos->cx > 32767) winpos->cx = 32767;
        if (winpos->cy < 0) winpos->cy = 0;
        else if (winpos->cy > 32767) winpos->cy = 32767;
    }

    if (!SWP_DoWinPosChanging( winpos, &newWindowRect, &newClientRect )) return FALSE;

    /* Fix redundant flags */
    if (!fixup_flags( winpos )) return FALSE;

    if((winpos->flags & (SWP_NOZORDER | SWP_HIDEWINDOW | SWP_SHOWWINDOW)) != SWP_NOZORDER)
    {
        if (GetAncestor( winpos->hwnd, GA_PARENT ) == GetDesktopWindow())
            winpos->hwndInsertAfter = SWP_DoOwnedPopups( winpos->hwnd, winpos->hwndInsertAfter );
    }

    /* Common operations */

    SWP_DoNCCalcSize( winpos, &newWindowRect, &newClientRect, valid_rects );

    if (!QDRV_set_window_pos( winpos->hwnd, winpos->hwndInsertAfter,
                                &newWindowRect, &newClientRect, orig_flags, valid_rects ))
        return FALSE;

    if (!(orig_flags & SWP_SHOWWINDOW))
    {
        UINT rdw_flags = RDW_FRAME | RDW_ERASE;
        if ( !(orig_flags & SWP_DEFERERASE) ) rdw_flags |= RDW_ERASENOW;
        RedrawWindow( winpos->hwnd, NULL, NULL, rdw_flags );
    }

    if( winpos->flags & SWP_HIDEWINDOW )
        HideCaret(winpos->hwnd);
    else if (winpos->flags & SWP_SHOWWINDOW)
        ShowCaret(winpos->hwnd);

    if (!(winpos->flags & (SWP_NOACTIVATE|SWP_HIDEWINDOW)))
    {
        /* child windows get WM_CHILDACTIVATE message */
        if ((GetWindowLongW( winpos->hwnd, GWL_STYLE ) & (WS_CHILD | WS_POPUP)) == WS_CHILD)
            SendMessageA( winpos->hwnd, WM_CHILDACTIVATE, 0, 0 );
        else
            SetForegroundWindow( winpos->hwnd );
    }

      /* And last, send the WM_WINDOWPOSCHANGED message */

    TRACE("\tstatus flags = %04x\n", winpos->flags & SWP_AGG_STATUSFLAGS);

    if (((winpos->flags & SWP_AGG_STATUSFLAGS) != SWP_AGG_NOPOSCHANGE))
    {
        /* WM_WINDOWPOSCHANGED is sent even if SWP_NOSENDCHANGING is set
           and always contains final window position.
         */
        winpos->x = newWindowRect.left;
        winpos->y = newWindowRect.top;
        winpos->cx = newWindowRect.right - newWindowRect.left;
        winpos->cy = newWindowRect.bottom - newWindowRect.top;
        SendMessageW( winpos->hwnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)winpos );
    }

    return TRUE;
}

/***********************************************************************
 *		SetWindowStyle   (X11DRV.@)
 *
 * Update the X state of a window to reflect a style change
 */
void QDRV_SetWindowStyle( HWND hwnd, DWORD old_style )
{
    struct quartzdrv_win_data *data;
    DWORD new_style, changed;

    if (hwnd == GetDesktopWindow()) return;
    if (!(data = get_win_data( hwnd ))) return;

    TRACE("hwnd=%p old_style=%d\n", hwnd, old_style);

    new_style = GetWindowLongW( hwnd, GWL_STYLE );
    changed = new_style ^ old_style;

    if (changed & WS_VISIBLE)
    {
        if (data->whole_window && QDRV_is_window_rect_mapped( &data->window_rect ))
        {
            if (new_style & WS_VISIBLE)
            {
                TRACE( "mapping win %p\n", hwnd );
               /* X11DRV_sync_window_style( display, data );
                X11DRV_set_wm_hints( display, data );*/
                                
                CARBON_ShowWindow( data->whole_window );
            }
            else
            {
                TRACE( "unmapping win %p\n", hwnd );
                CARBON_HideWindow( data->whole_window );
            }
        }
        invalidate_dce( hwnd, &data->window_rect );
    }

    if (changed & WS_DISABLED)
    {
        if (data->whole_window && data->managed)
        {
            FIXME("WS_DISABLED\n");
        }
    }
}


/***********************************************************************
*           QDRV_set_window_pos
*
* Set a window position and Z order.
*/
BOOL QDRV_set_window_pos( HWND hwnd, HWND insert_after, const RECT *rectWindow,
                          const RECT *rectClient, UINT swp_flags, const RECT *valid_rects )
{
    struct quartzdrv_win_data *data;
    RECT new_whole_rect;
    WND *win;
    DWORD old_style, new_style;
    BOOL ret;
    
    TRACE( "in win %p window %s client %s style %08lx\n",
           hwnd, wine_dbgstr_rect(rectWindow), wine_dbgstr_rect(rectClient), new_style );

    if (!(data = get_win_data( hwnd ))) return FALSE;
    
    new_whole_rect = *rectWindow;
    
    if (!IsRectEmpty( &valid_rects[0] ))
    {
        int x_offset = 0, y_offset = 0;
        
        if (data->whole_window)
        {
            /* the X server will move the bits for us */
            x_offset = data->whole_rect.left - new_whole_rect.left;
            y_offset = data->whole_rect.top - new_whole_rect.top;
        }
        
        if (x_offset != valid_rects[1].left - valid_rects[0].left ||
            y_offset != valid_rects[1].top - valid_rects[0].top)
        {
            /* FIXME: should copy the window bits here */
            valid_rects = NULL;
        }
    }
    
    if (!(win = WIN_GetPtr( hwnd ))) return FALSE;
    if (win == WND_OTHER_PROCESS)
    {
        if (IsWindow( hwnd )) ERR( "cannot set rectangles of other process window %p\n", hwnd );
        return FALSE;
    }
    SERVER_START_REQ( set_window_pos )
    {
        req->handle        = hwnd;
        req->previous      = insert_after;
        req->flags         = swp_flags;
        req->window.left   = rectWindow->left;
        req->window.top    = rectWindow->top;
        req->window.right  = rectWindow->right;
        req->window.bottom = rectWindow->bottom;
        req->client.left   = rectClient->left;
        req->client.top    = rectClient->top;
        req->client.right  = rectClient->right;
        req->client.bottom = rectClient->bottom;
        if (memcmp( rectWindow, &new_whole_rect, sizeof(RECT) ) || !IsRectEmpty( &valid_rects[0] ))
        {
            wine_server_add_data( req, &new_whole_rect, sizeof(new_whole_rect) );
            if (!IsRectEmpty( &valid_rects[0] ))
                wine_server_add_data( req, valid_rects, 2 * sizeof(*valid_rects) );
        }
        ret = !wine_server_call( req );
        new_style = reply->new_style;
    }
    SERVER_END_REQ;
    
    if (win == WND_DESKTOP || data->whole_window == Cocoa_GetDesktopWin())
    {
        data->whole_rect = data->client_rect = data->window_rect = *rectWindow;
        if (win != WND_DESKTOP)
        {
            win->rectWindow   = *rectWindow;
            win->rectClient   = *rectClient;
            win->dwStyle      = new_style;
            WIN_ReleasePtr( win );
        }
        return ret;
    }
    
    if (ret)
    {
        /* invalidate DCEs */
        
        if ((((swp_flags & SWP_AGG_NOPOSCHANGE) != SWP_AGG_NOPOSCHANGE) && (new_style & WS_VISIBLE)) ||
            (swp_flags & (SWP_HIDEWINDOW | SWP_SHOWWINDOW)))
        {
            RECT rect;
            UnionRect( &rect, rectWindow, &win->rectWindow );
            invalidate_dce( hwnd, &rect );
        }
        
        win->rectWindow   = *rectWindow;
        win->rectClient   = *rectClient;
        old_style         = win->dwStyle;
        win->dwStyle      = new_style;
        data->window_rect = *rectWindow;
        
        TRACE( "win %p window %s client %s style %08lx\n",
               hwnd, wine_dbgstr_rect(rectWindow), wine_dbgstr_rect(rectClient), new_style );
        
        /* FIXME: copy the valid bits */
        
        if (data->whole_window && !data->lock_changes)
        {
            if ((old_style & WS_VISIBLE) && !(new_style & WS_VISIBLE))
            {
                /* window got hidden, unmap it */
                TRACE( "unmapping win %p\n", hwnd );
                CARBON_HideWindow( data->whole_window );
            }
            else if ((new_style & WS_VISIBLE) && !QDRV_is_window_rect_mapped( rectWindow ))
            {
                /* resizing to zero size or off screen -> unmap */
                TRACE( "unmapping zero size or off-screen win %p\n", hwnd );
                CARBON_HideWindow( data->whole_window );
            }
        }
        
        QDRV_sync_window_position(data, swp_flags, rectClient, &new_whole_rect );
        
        if (data->whole_window && !data->lock_changes)
        {
            if ((new_style & WS_VISIBLE) && !(new_style & WS_MINIMIZE) &&
                QDRV_is_window_rect_mapped( rectWindow ) )
            {
                if (!(old_style & WS_VISIBLE))
                {
                    /* window got shown, map it */
                    TRACE( "mapping win %p\n", hwnd );
                    CARBON_ShowWindow( data->whole_window );
                }
                else if ((swp_flags & (SWP_NOSIZE | SWP_NOMOVE)) != (SWP_NOSIZE | SWP_NOMOVE))
                {
                    /* resizing from zero size to non-zero -> map */
                    TRACE( "mapping non zero size or off-screen win %p\n", hwnd );
                    CARBON_ShowWindow( data->whole_window );
                }
            }
            
        }
    }
    WIN_ReleasePtr( win );
    return ret;
}

/***********************************************************************
 *           WINPOS_FindIconPos
 *
 * Find a suitable place for an iconic window.
 */
static POINT WINPOS_FindIconPos( HWND hwnd, POINT pt )
{
    RECT rect, rectParent;
    HWND parent, child;
    HRGN hrgn, tmp;
    int xspacing, yspacing;

    TRACE("\n");

    parent = GetAncestor( hwnd, GA_PARENT );
    GetClientRect( parent, &rectParent );
    if ((pt.x >= rectParent.left) && (pt.x + GetSystemMetrics(SM_CXICON) < rectParent.right) &&
        (pt.y >= rectParent.top) && (pt.y + GetSystemMetrics(SM_CYICON) < rectParent.bottom))
        return pt;  /* The icon already has a suitable position */

    xspacing = GetSystemMetrics(SM_CXICONSPACING);
    yspacing = GetSystemMetrics(SM_CYICONSPACING);

    /* Check if another icon already occupies this spot */
    /* FIXME: this is completely inefficient */

    hrgn = CreateRectRgn( 0, 0, 0, 0 );
    tmp = CreateRectRgn( 0, 0, 0, 0 );
    for (child = GetWindow( parent, GW_HWNDFIRST ); child; child = GetWindow( child, GW_HWNDNEXT ))
    {
        WND *childPtr;
        if (child == hwnd) continue;
        if ((GetWindowLongW( child, GWL_STYLE ) & (WS_VISIBLE|WS_MINIMIZE)) != (WS_VISIBLE|WS_MINIMIZE))
            continue;
        if (!(childPtr = WIN_GetPtr( child )) || childPtr == WND_OTHER_PROCESS)
            continue;
        SetRectRgn( tmp, childPtr->rectWindow.left, childPtr->rectWindow.top,
                    childPtr->rectWindow.right, childPtr->rectWindow.bottom );
        CombineRgn( hrgn, hrgn, tmp, RGN_OR );
        WIN_ReleasePtr( childPtr );
    }
    DeleteObject( tmp );

    for (rect.bottom = rectParent.bottom; rect.bottom >= yspacing; rect.bottom -= yspacing)
    {
        for (rect.left = rectParent.left; rect.left <= rectParent.right - xspacing; rect.left += xspacing)
        {
            rect.right = rect.left + xspacing;
            rect.top = rect.bottom - yspacing;
            if (!RectInRegion( hrgn, &rect ))
            {
                /* No window was found, so it's OK for us */
                pt.x = rect.left + (xspacing - GetSystemMetrics(SM_CXICON)) / 2;
                pt.y = rect.top + (yspacing - GetSystemMetrics(SM_CYICON)) / 2;
                DeleteObject( hrgn );
                return pt;
            }
        }
    }
    DeleteObject( hrgn );
    pt.x = pt.y = 0;
    return pt;
}

static char *cmd_str (UINT cmd)
{
    switch (cmd)
    {
        case SW_MINIMIZE: return "SW_MINIMIZE";
        case SW_MAXIMIZE: return "SW_MAXIMIZE";
        case SW_RESTORE: return "SW_RESTORE";            
        default: return "unknow";
    }
}

UINT WINPOS_MinMaximize( HWND hwnd, UINT cmd, LPRECT rect )
{
    WND *wndPtr;
    UINT swpFlags = 0;
    POINT size;
    LONG old_style;
    WINDOWPLACEMENT wpl;

    TRACE("semi-stub: %p %u (%s)\n", hwnd, cmd , cmd_str(cmd) );

    wpl.length = sizeof(wpl);
    GetWindowPlacement( hwnd, &wpl );

    if (HOOK_CallHooks( WH_CBT, HCBT_MINMAX, (WPARAM)hwnd, cmd, TRUE ))
        return SWP_NOSIZE | SWP_NOMOVE;

    if (IsIconic( hwnd ))
    {
        if (cmd == SW_MINIMIZE) return SWP_NOSIZE | SWP_NOMOVE;
        if (!SendMessageW( hwnd, WM_QUERYOPEN, 0, 0 )) return SWP_NOSIZE | SWP_NOMOVE;
        swpFlags |= SWP_NOCOPYBITS;
    }

    switch( cmd )
    {
    case SW_MINIMIZE:
        if (!(wndPtr = WIN_GetPtr( hwnd )) || wndPtr == WND_OTHER_PROCESS) return 0;
        if( wndPtr->dwStyle & WS_MAXIMIZE) wndPtr->flags |= WIN_RESTORE_MAX;
        else wndPtr->flags &= ~WIN_RESTORE_MAX;
        WIN_ReleasePtr( wndPtr );

        WIN_SetStyle( hwnd, WS_MINIMIZE, WS_MAXIMIZE );

      //  X11DRV_set_iconic_state( hwnd );

        wpl.ptMinPosition = WINPOS_FindIconPos( hwnd, wpl.ptMinPosition );

        SetRect( rect, wpl.ptMinPosition.x, wpl.ptMinPosition.y,
                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON) );
        swpFlags |= SWP_NOCOPYBITS;
        break;

    case SW_MAXIMIZE:
        WINPOS_GetMinMaxInfo( hwnd, &size, &wpl.ptMaxPosition, NULL, NULL );

        old_style = WIN_SetStyle( hwnd, WS_MAXIMIZE, WS_MINIMIZE );
        if (old_style & WS_MINIMIZE)
        {
            WINPOS_ShowIconTitle( hwnd, FALSE );
           // X11DRV_set_iconic_state( hwnd );
        }
        SetRect( rect, wpl.ptMaxPosition.x, wpl.ptMaxPosition.y, size.x, size.y );
        break;

    case SW_RESTORE:
        old_style = WIN_SetStyle( hwnd, 0, WS_MINIMIZE | WS_MAXIMIZE );
        if (old_style & WS_MINIMIZE)
        {
            BOOL restore_max;

            WINPOS_ShowIconTitle( hwnd, FALSE );
           
            // X11DRV_set_iconic_state( hwnd );

            if (!(wndPtr = WIN_GetPtr( hwnd )) || wndPtr == WND_OTHER_PROCESS) return 0;
            restore_max = (wndPtr->flags & WIN_RESTORE_MAX) != 0;
            WIN_ReleasePtr( wndPtr );
            if (restore_max)
            {
                /* Restore to maximized position */
                WINPOS_GetMinMaxInfo( hwnd, &size, &wpl.ptMaxPosition, NULL, NULL);
                WIN_SetStyle( hwnd, WS_MAXIMIZE, 0 );
                SetRect( rect, wpl.ptMaxPosition.x, wpl.ptMaxPosition.y, size.x, size.y );
                break;
            }
        }
        else if (!(old_style & WS_MAXIMIZE)) break;

        /* Restore to normal position */

        *rect = wpl.rcNormalPosition;
        rect->right -= rect->left;
        rect->bottom -= rect->top;

        break;
    }

    return swpFlags;
}


/***********************************************************************
*              ShowWindow   (X11DRV.@)
*/
BOOL QDRV_ShowWindow( HWND hwnd, INT cmd )
{   
    WND *wndPtr;
    HWND parent;
    LONG style = GetWindowLongW( hwnd, GWL_STYLE );
    BOOL wasVisible = (style & WS_VISIBLE) != 0;
    BOOL showFlag = TRUE, state_change = FALSE;
    RECT newPos = {0, 0, 0, 0};
    UINT swp = 0;

    TRACE("hwnd=%p, cmd=%d, wasVisible %d\n", hwnd, cmd, wasVisible);

    switch(cmd)
    {
        case SW_HIDE:
            if (!wasVisible) return FALSE;
            showFlag = FALSE;
            swp |= SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE;
            if (hwnd != GetActiveWindow())
                swp |= SWP_NOACTIVATE | SWP_NOZORDER;
	    break;

	case SW_SHOWMINNOACTIVE:
            swp |= SWP_NOACTIVATE | SWP_NOZORDER;
            /* fall through */
        case SW_MINIMIZE:
        case SW_FORCEMINIMIZE: /* FIXME: Does not work if thread is hung. */
            if (style & WS_CHILD) swp |= SWP_NOACTIVATE | SWP_NOZORDER;
            /* fall through */
	case SW_SHOWMINIMIZED:
            swp |= SWP_SHOWWINDOW | SWP_FRAMECHANGED;
            swp |= WINPOS_MinMaximize( hwnd, cmd, &newPos );
            if (style & WS_MINIMIZE) return wasVisible;
            state_change = TRUE;
	    break;

	case SW_SHOWMAXIMIZED: /* same as SW_MAXIMIZE */
            swp |= SWP_SHOWWINDOW | SWP_FRAMECHANGED;
            swp |= WINPOS_MinMaximize( hwnd, SW_MAXIMIZE, &newPos );
            if ((style & WS_MAXIMIZE) && (style & WS_CHILD)) return wasVisible;
            state_change = TRUE;
            break;

	case SW_SHOWNA:
            swp |= SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE;
            if (style & WS_CHILD) swp |= SWP_NOZORDER;
            break;
	case SW_SHOW:
            if (wasVisible) return TRUE;
	    swp |= SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE;
            if (style & WS_CHILD) swp |= SWP_NOACTIVATE | SWP_NOZORDER;
	    break;

	case SW_RESTORE:
	    swp |= SWP_FRAMECHANGED;
            state_change = TRUE;
            /* fall through */
	case SW_SHOWNOACTIVATE:
            swp |= SWP_NOACTIVATE | SWP_NOZORDER;
            /* fall through */
	case SW_SHOWNORMAL:  /* same as SW_NORMAL: */
	case SW_SHOWDEFAULT: /* FIXME: should have its own handler */
	    swp |= SWP_SHOWWINDOW;
            if (style & (WS_MINIMIZE | WS_MAXIMIZE))
		 swp |= WINPOS_MinMaximize( hwnd, SW_RESTORE, &newPos );
            else swp |= SWP_NOSIZE | SWP_NOMOVE;
            if (style & WS_CHILD) swp |= SWP_NOACTIVATE | SWP_NOZORDER;
	    break;
    }


    if ((showFlag != wasVisible || cmd == SW_SHOWNA) && !state_change)
    {
        SendMessageW( hwnd, WM_SHOWWINDOW, showFlag, 0 );
        if (!IsWindow( hwnd )) return wasVisible;
    }

    parent = GetAncestor( hwnd, GA_PARENT );
    if (parent && !IsWindowVisible( parent ) && !state_change)
    {
        /* if parent is not visible simply toggle WS_VISIBLE and return */
        if (showFlag) WIN_SetStyle( hwnd, WS_VISIBLE, 0 );
        else WIN_SetStyle( hwnd, 0, WS_VISIBLE );
    }
    else
    {
        if (style & WS_CHILD)
        {
            if (state_change)
            {
                /* it appears that Windows always adds an undocumented 0x8000
                 * flag if the state of a window changes.
                 */
                swp |= SWP_STATECHANGED;
            }
        }

        SetWindowPos( hwnd, HWND_TOP, newPos.left, newPos.top,
                      newPos.right, newPos.bottom, LOWORD(swp) );
    }

    if (cmd == SW_HIDE)
    {
        HWND hFocus;

        /* FIXME: This will cause the window to be activated irrespective
         * of whether it is owned by the same thread. Has to be done
         * asynchronously.
         */

        if (hwnd == GetActiveWindow())
            WINPOS_ActivateOtherWindow(hwnd);

        /* Revert focus to parent */
        hFocus = GetFocus();
        if (hwnd == hFocus || IsChild(hwnd, hFocus))
        {
            HWND parent = GetAncestor(hwnd, GA_PARENT);
            if (parent == GetDesktopWindow()) parent = 0;
            SetFocus(parent);
        }
    }

    if (IsIconic(hwnd)) WINPOS_ShowIconTitle( hwnd, TRUE );

    if (!(wndPtr = WIN_GetPtr( hwnd )) || wndPtr == WND_OTHER_PROCESS) return wasVisible;

    if (wndPtr->flags & WIN_NEED_SIZE)
    {
        /* should happen only in CreateWindowEx() */
	int wParam = SIZE_RESTORED;
        RECT client = wndPtr->rectClient;

	wndPtr->flags &= ~WIN_NEED_SIZE;
	if (wndPtr->dwStyle & WS_MAXIMIZE) wParam = SIZE_MAXIMIZED;
	else if (wndPtr->dwStyle & WS_MINIMIZE) wParam = SIZE_MINIMIZED;
        WIN_ReleasePtr( wndPtr );

        SendMessageW( hwnd, WM_SIZE, wParam,
                      MAKELONG( client.right - client.left, client.bottom - client.top ));
        SendMessageW( hwnd, WM_MOVE, 0, MAKELONG( client.left, client.top ));
    }
    else WIN_ReleasePtr( wndPtr );

    return wasVisible;
}

/***********************************************************************
 *		QDRV_handle_desktop_resize
 */
void QDRV_handle_desktop_resize( unsigned int width, unsigned int height )
{
    RECT rect;
    HWND hwnd = GetDesktopWindow();
    struct quartzdrv_win_data *data;

    if (!(data = get_win_data( hwnd ))) return;
    screen_width  = width;
    screen_height = height;
    TRACE("desktop %p change to (%dx%d)\n", hwnd, width, height);
    SetRect( &rect, 0, 0, width, height );
    data->lock_changes++;
    QDRV_set_window_pos( hwnd, 0, &rect, &rect, SWP_NOZORDER|SWP_NOMOVE, NULL );
    data->lock_changes--;
    SendMessageTimeoutW( HWND_BROADCAST, WM_DISPLAYCHANGE, screen_depth,
                         MAKELPARAM( width, height ), SMTO_ABORTIFHUNG, 2000, NULL );
}

/***********************************************************************
 *		SetWindowRgn  (X11DRV.@)
 *
 * Assign specified region to window (for non-rectangular windows)
 */
int QDRV_SetWindowRgn( HWND hwnd, HRGN hrgn, BOOL redraw )
{
    struct quartzdrv_win_data *data;

    if (!(data = get_win_data( hwnd )))
    {
        if (IsWindow( hwnd ))
            FIXME( "not supported on other thread window %p\n", hwnd );
        SetLastError( ERROR_INVALID_WINDOW_HANDLE );
        return FALSE;
    }

    invalidate_dce( hwnd, &data->window_rect );
    return TRUE;
}


/***********************************************************************
 *           draw_moving_frame
 *
 * Draw the frame used when moving or resizing window.
 *
 * FIXME:  This causes problems in Win95 mode.  (why?)
 */
static void draw_moving_frame( HDC hdc, RECT *rect, BOOL thickframe )
{
    TRACE("hdc=%p thickframe=%d\n", hdc, thickframe);
    if (thickframe)
    {
        const int width = GetSystemMetrics(SM_CXFRAME);
        const int height = GetSystemMetrics(SM_CYFRAME);

        HBRUSH hbrush = SelectObject( hdc, GetStockObject( GRAY_BRUSH ) );
        PatBlt( hdc, rect->left, rect->top,
                rect->right - rect->left - width, height, PATINVERT );
        PatBlt( hdc, rect->left, rect->top + height, width,
                rect->bottom - rect->top - height, PATINVERT );
        PatBlt( hdc, rect->left + width, rect->bottom - 1,
                rect->right - rect->left - width, -height, PATINVERT );
        PatBlt( hdc, rect->right - 1, rect->top, -width,
                rect->bottom - rect->top - height, PATINVERT );
        SelectObject( hdc, hbrush );
    }
    else DrawFocusRect( hdc, rect );

    
}


/***********************************************************************
 *           start_size_move
 *
 * Initialisation of a move or resize, when initiatied from a menu choice.
 * Return hit test code for caption or sizing border.
 */
static LONG start_size_move( HWND hwnd, WPARAM wParam, POINT *capturePoint, LONG style )
{
    LONG hittest = 0;
    POINT pt;
    MSG msg;
    RECT rectWindow;

    GetWindowRect( hwnd, &rectWindow );

    if ((wParam & 0xfff0) == SC_MOVE)
    {
        /* Move pointer at the center of the caption */
        RECT rect = rectWindow;
        /* Note: to be exactly centered we should take the different types
         * of border into account, but it shouldn't make more that a few pixels
         * of difference so let's not bother with that */
        rect.top += GetSystemMetrics(SM_CYBORDER);
        if (style & WS_SYSMENU)
            rect.left += GetSystemMetrics(SM_CXSIZE) + 1;
        if (style & WS_MINIMIZEBOX)
            rect.right -= GetSystemMetrics(SM_CXSIZE) + 1;
        if (style & WS_MAXIMIZEBOX)
            rect.right -= GetSystemMetrics(SM_CXSIZE) + 1;
        pt.x = (rect.right + rect.left) / 2;
        pt.y = rect.top + GetSystemMetrics(SM_CYSIZE)/2;
        hittest = HTCAPTION;
        *capturePoint = pt;
    }
    else  /* SC_SIZE */
    {
        pt.x = pt.y = 0;
        while(!hittest)
        {
            GetMessageW( &msg, 0, WM_KEYFIRST, WM_MOUSELAST );
            if (CallMsgFilterW( &msg, MSGF_SIZE )) continue;

            switch(msg.message)
            {
            case WM_MOUSEMOVE:
                pt = msg.pt;
                hittest = SendMessageW( hwnd, WM_NCHITTEST, 0, MAKELONG( pt.x, pt.y ) );
                if ((hittest < HTLEFT) || (hittest > HTBOTTOMRIGHT)) hittest = 0;
                break;

            case WM_LBUTTONUP:
                return 0;

            case WM_KEYDOWN:
                switch(msg.wParam)
                {
                case VK_UP:
                    hittest = HTTOP;
                    pt.x =(rectWindow.left+rectWindow.right)/2;
                    pt.y = rectWindow.top + GetSystemMetrics(SM_CYFRAME) / 2;
                    break;
                case VK_DOWN:
                    hittest = HTBOTTOM;
                    pt.x =(rectWindow.left+rectWindow.right)/2;
                    pt.y = rectWindow.bottom - GetSystemMetrics(SM_CYFRAME) / 2;
                    break;
                case VK_LEFT:
                    hittest = HTLEFT;
                    pt.x = rectWindow.left + GetSystemMetrics(SM_CXFRAME) / 2;
                    pt.y =(rectWindow.top+rectWindow.bottom)/2;
                    break;
                case VK_RIGHT:
                    hittest = HTRIGHT;
                    pt.x = rectWindow.right - GetSystemMetrics(SM_CXFRAME) / 2;
                    pt.y =(rectWindow.top+rectWindow.bottom)/2;
                    break;
                case VK_RETURN:
                case VK_ESCAPE: return 0;
                }
            }
        }
        *capturePoint = pt;
    }
    SetCursorPos( pt.x, pt.y );
    SendMessageW( hwnd, WM_SETCURSOR, (WPARAM)hwnd, MAKELONG( hittest, WM_MOUSEMOVE ));
    return hittest;
}

/***********************************************************************
 *           set_movesize_capture
 */
static void set_movesize_capture( HWND hwnd )
{
    HWND previous = 0;

    SERVER_START_REQ( set_capture_window )
    {
        req->handle = hwnd;
        req->flags  = CAPTURE_MOVESIZE;
        if (!wine_server_call_err( req ))
        {
            previous = reply->previous;
            hwnd = reply->full_handle;
        }
    }
    SERVER_END_REQ;
    if (previous && previous != hwnd)
        SendMessageW( previous, WM_CAPTURECHANGED, 0, (LPARAM)hwnd );
}

/***********************************************************************
 *           SysCommandSizeMove   (X11DRV.@)
 *
 * Perform SC_MOVE and SC_SIZE commands.
 */
void QDRV_SysCommandSizeMove( HWND hwnd, WPARAM wParam )
{
    MSG msg;
    RECT sizingRect, mouseRect, origRect;
    HDC hdc;
    HWND parent;
    LONG hittest = (LONG)(wParam & 0x0f);
    WPARAM syscommand = wParam & 0xfff0;
    HCURSOR hDragCursor = 0, hOldCursor = 0;
    POINT minTrack, maxTrack;
    POINT capturePoint, pt;
    LONG style = GetWindowLongA( hwnd, GWL_STYLE );
    BOOL    thickframe = HAS_THICKFRAME( style );
    BOOL    iconic = style & WS_MINIMIZE;
    BOOL    moved = FALSE;
    DWORD     dwPoint = GetMessagePos ();
    BOOL DragFullWindows = TRUE; //  FALSE;
    BOOL grab;
    WindowRef parent_win, whole_win;
    struct quartzdrv_thread_data *thread_data = quartzdrv_thread_data();
    struct quartzdrv_win_data *data;

    FIXME("hwnd=%p\n", hwnd);

    pt.x = (short)LOWORD(dwPoint);
    pt.y = (short)HIWORD(dwPoint);
    capturePoint = pt;

    if (IsZoomed(hwnd) || !IsWindowVisible(hwnd)) return;

    if (!(data = get_win_data( hwnd ))) return;


    SystemParametersInfoA(SPI_GETDRAGFULLWINDOWS, 0, &DragFullWindows, 0);

    if (syscommand == SC_MOVE)
    {
        if (!hittest) hittest = start_size_move( hwnd, wParam, &capturePoint, style );
        if (!hittest) return;
    }
    else  /* SC_SIZE */
    {
        if ( hittest && (syscommand != SC_MOUSEMENU) )
            hittest += (HTLEFT - WMSZ_LEFT);
        else
        {
            set_movesize_capture( hwnd );
            hittest = start_size_move( hwnd, wParam, &capturePoint, style );
            if (!hittest)
            {
                set_movesize_capture(0);
                return;
            }
        }
    }

      /* Get min/max info */

    WINPOS_GetMinMaxInfo( hwnd, NULL, NULL, &minTrack, &maxTrack );
    GetWindowRect( hwnd, &sizingRect );
    if (style & WS_CHILD)
    {
        parent = GetParent(hwnd);
        /* make sizing rect relative to parent */
        MapWindowPoints( 0, parent, (POINT*)&sizingRect, 2 );
        GetClientRect( parent, &mouseRect );
    }
    else
    {
        parent = 0;
        SetRect(&mouseRect, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    }
    origRect = sizingRect;

    if (ON_LEFT_BORDER(hittest))
    {
        mouseRect.left  = max( mouseRect.left, sizingRect.right-maxTrack.x );
        mouseRect.right = min( mouseRect.right, sizingRect.right-minTrack.x );
    }
    else if (ON_RIGHT_BORDER(hittest))
    {
        mouseRect.left  = max( mouseRect.left, sizingRect.left+minTrack.x );
        mouseRect.right = min( mouseRect.right, sizingRect.left+maxTrack.x );
    }
    if (ON_TOP_BORDER(hittest))
    {
        mouseRect.top    = max( mouseRect.top, sizingRect.bottom-maxTrack.y );
        mouseRect.bottom = min( mouseRect.bottom,sizingRect.bottom-minTrack.y);
    }
    else if (ON_BOTTOM_BORDER(hittest))
    {
        mouseRect.top    = max( mouseRect.top, sizingRect.top+minTrack.y );
        mouseRect.bottom = min( mouseRect.bottom, sizingRect.top+maxTrack.y );
    }
    if (parent) MapWindowPoints( parent, 0, (LPPOINT)&mouseRect, 2 );

    /* Retrieve a default cache DC (without using the window style) */
    hdc = GetDCEx( parent, 0, DCX_CACHE );

    if( iconic ) /* create a cursor for dragging */
    {
        hDragCursor = (HCURSOR)GetClassLongPtrW( hwnd, GCLP_HICON);
        if( !hDragCursor ) hDragCursor = (HCURSOR)SendMessageW( hwnd, WM_QUERYDRAGICON, 0, 0L);
        if( !hDragCursor ) iconic = FALSE;
    }

    /* repaint the window before moving it around */
    RedrawWindow( hwnd, NULL, 0, RDW_UPDATENOW | RDW_ALLCHILDREN );

    SendMessageW( hwnd, WM_ENTERSIZEMOVE, 0, 0 );
    set_movesize_capture( hwnd );

    /* grab the server only when moving top-level windows without desktop */
    grab = (!DragFullWindows && !parent && (root_window == Cocoa_GetDesktopWin() /* DefaultRootWindow(gdi_display)*/ ));
    FIXME("grab=%d\n", grab);
    
#if 0
    if (grab)
    {
        wine_tsx11_lock();
        XSync( gdi_display, False );
        XGrabServer( thread_data->display );
        XSync( thread_data->display, False );
        /* switch gdi display to the thread display, since the server is grabbed */
        old_gdi_display = gdi_display;
        gdi_display = thread_data->display;
        wine_tsx11_unlock();
    }
#endif
    whole_win = QDRV_get_whole_window( GetAncestor(hwnd,GA_ROOT) );
    parent_win = parent ? QDRV_get_whole_window( GetAncestor(parent,GA_ROOT) ) : root_window;

#if 0
    wine_tsx11_lock();
    XGrabPointer( thread_data->display, whole_win, False,
                  PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
                  GrabModeAsync, GrabModeAsync, parent_win, None, CurrentTime );
    wine_tsx11_unlock();
#endif
    thread_data->grab_window = whole_win;

    while(1)
    {
        int dx = 0, dy = 0;

        if (!GetMessageW( &msg, 0, WM_KEYFIRST, WM_MOUSELAST )) break;
        if (CallMsgFilterW( &msg, MSGF_SIZE )) continue;

        /* Exit on button-up, Return, or Esc */
        if ((msg.message == WM_LBUTTONUP) ||
            ((msg.message == WM_KEYDOWN) &&
             ((msg.wParam == VK_RETURN) || (msg.wParam == VK_ESCAPE)))) break;

        if ((msg.message != WM_KEYDOWN) && (msg.message != WM_MOUSEMOVE))
            continue;  /* We are not interested in other messages */

        pt = msg.pt;

        if (msg.message == WM_KEYDOWN) switch(msg.wParam)
        {
        case VK_UP:    pt.y -= 8; break;
        case VK_DOWN:  pt.y += 8; break;
        case VK_LEFT:  pt.x -= 8; break;
        case VK_RIGHT: pt.x += 8; break;
        }

        pt.x = max( pt.x, mouseRect.left );
        pt.x = min( pt.x, mouseRect.right );
        pt.y = max( pt.y, mouseRect.top );
        pt.y = min( pt.y, mouseRect.bottom );

        dx = pt.x - capturePoint.x;
        dy = pt.y - capturePoint.y;

        if (dx || dy)
        {
            if( !moved )
            {
                moved = TRUE;

                if( iconic ) /* ok, no system popup tracking */
                {
                    hOldCursor = SetCursor(hDragCursor);
                    ShowCursor( TRUE );
                    WINPOS_ShowIconTitle( hwnd, FALSE );
                }
                else if(!DragFullWindows)
                    draw_moving_frame( hdc, &sizingRect, thickframe );
            }

            if (msg.message == WM_KEYDOWN) SetCursorPos( pt.x, pt.y );
            else
            {
                RECT newRect = sizingRect;
                WPARAM wpSizingHit = 0;

                if (hittest == HTCAPTION) OffsetRect( &newRect, dx, dy );
                if (ON_LEFT_BORDER(hittest)) newRect.left += dx;
                else if (ON_RIGHT_BORDER(hittest)) newRect.right += dx;
                if (ON_TOP_BORDER(hittest)) newRect.top += dy;
                else if (ON_BOTTOM_BORDER(hittest)) newRect.bottom += dy;
                if(!iconic && !DragFullWindows) draw_moving_frame( hdc, &sizingRect, thickframe );
                capturePoint = pt;

                /* determine the hit location */
                if (hittest >= HTLEFT && hittest <= HTBOTTOMRIGHT)
                    wpSizingHit = WMSZ_LEFT + (hittest - HTLEFT);
                SendMessageW( hwnd, WM_SIZING, wpSizingHit, (LPARAM)&newRect );

                if (!iconic)
                {
                    if(!DragFullWindows)
                        draw_moving_frame( hdc, &newRect, thickframe );
                    else
                        SetWindowPos( hwnd, 0, newRect.left, newRect.top,
                                      newRect.right - newRect.left,
                                      newRect.bottom - newRect.top,
                                      ( hittest == HTCAPTION ) ? SWP_NOSIZE : 0 );
                }
                sizingRect = newRect;
            }
        }
    }

    set_movesize_capture(0);
    if( iconic )
    {
        if( moved ) /* restore cursors, show icon title later on */
        {
            ShowCursor( FALSE );
            SetCursor( hOldCursor );
        }
    }
    else if (moved && !DragFullWindows)
        draw_moving_frame( hdc, &sizingRect, thickframe );

    ReleaseDC( parent, hdc );

#if 0
    wine_tsx11_lock();
    XUngrabPointer( thread_data->display, CurrentTime );
    if (grab)
    {
        XSync( thread_data->display, False );
        XUngrabServer( thread_data->display );
        XSync( thread_data->display, False );
        gdi_display = old_gdi_display;
    }
    wine_tsx11_unlock();
#endif
    thread_data->grab_window = 0;

    if (HOOK_CallHooks( WH_CBT, HCBT_MOVESIZE, (WPARAM)hwnd, (LPARAM)&sizingRect, TRUE ))
        moved = FALSE;

    SendMessageW( hwnd, WM_EXITSIZEMOVE, 0, 0 );
    SendMessageW( hwnd, WM_SETVISIBLE, !IsIconic(hwnd), 0L);

    /* window moved or resized */
    if (moved)
    {
        /* if the moving/resizing isn't canceled call SetWindowPos
         * with the new position or the new size of the window
         */
        if (!((msg.message == WM_KEYDOWN) && (msg.wParam == VK_ESCAPE)) )
        {
            /* NOTE: SWP_NOACTIVATE prevents document window activation in Word 6 */
            if(!DragFullWindows)
                SetWindowPos( hwnd, 0, sizingRect.left, sizingRect.top,
                              sizingRect.right - sizingRect.left,
                              sizingRect.bottom - sizingRect.top,
                              ( hittest == HTCAPTION ) ? SWP_NOSIZE : 0 );
        }
        else
        { /* restore previous size/position */
            if(DragFullWindows)
                SetWindowPos( hwnd, 0, origRect.left, origRect.top,
                              origRect.right - origRect.left,
                              origRect.bottom - origRect.top,
                              ( hittest == HTCAPTION ) ? SWP_NOSIZE : 0 );
        }
    }

    if (IsIconic(hwnd))
    {
        /* Single click brings up the system menu when iconized */

        if( !moved )
        {
            if(style & WS_SYSMENU )
                SendMessageW( hwnd, WM_SYSCOMMAND,
                              SC_MOUSEMENU + HTSYSMENU, MAKELONG(pt.x,pt.y));
        }
        else WINPOS_ShowIconTitle( hwnd, TRUE );
    }
}
