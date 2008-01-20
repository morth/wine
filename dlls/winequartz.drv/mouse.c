/*
 * X11 mouse driver
 *
 * Copyright 1998 Ulrich Weigand
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

#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#include "windef.h"
#include "winbase.h"
#include "wine/winuser16.h"

#include "win.h"
#include "quartzdrv.h"
#include "wine/server.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(cursor);

/**********************************************************************/

#define NB_BUTTONS   7     /* Windows can handle 5 buttons and the wheel too */

static const UINT button_down_flags[NB_BUTTONS] =
{
    MOUSEEVENTF_LEFTDOWN,
    MOUSEEVENTF_RIGHTDOWN,
    MOUSEEVENTF_MIDDLEDOWN,
    MOUSEEVENTF_WHEEL,
    MOUSEEVENTF_WHEEL,
    MOUSEEVENTF_XDOWN,
    MOUSEEVENTF_XDOWN
};

static const UINT button_up_flags[NB_BUTTONS] =
{
    MOUSEEVENTF_LEFTUP,
    MOUSEEVENTF_RIGHTUP,
    MOUSEEVENTF_MIDDLEUP,
    0,
    0,
    MOUSEEVENTF_XUP,
    MOUSEEVENTF_XUP
};

POINT cursor_pos;

/***********************************************************************
 *		update_button_state
 *
 * Update the button state with what X provides us
 */
static inline void update_button_state( unsigned int state )
{
    key_state_table[VK_LBUTTON] = (state == kLeftButton) ? 0x80 : 0;
    key_state_table[VK_MBUTTON] = (state == kMidButton) ? 0x80 : 0;
    key_state_table[VK_RBUTTON] = (state == kRightButton) ? 0x80 : 0;
    key_state_table[VK_XBUTTON1]= 0; //(state & Button6Mask ? 0x80 : 0);
    key_state_table[VK_XBUTTON2]= 0; //(state & Button7Mask ? 0x80 : 0);
}

/***********************************************************************
 *		update_key_state
 *
 * Update the key state with what X provides us
 */
static inline void update_key_state( unsigned int state )
{
    key_state_table[VK_SHIFT]   = 0; //(state & ShiftMask   ? 0x80 : 0);
    key_state_table[VK_CONTROL] = 0; //(state & ControlMask ? 0x80 : 0);
}

/***********************************************************************
 *           get_key_state
 */
static WORD get_key_state(void)
{
    WORD ret = 0;

    if (GetSystemMetrics( SM_SWAPBUTTON ))
    {
        if (key_state_table[VK_RBUTTON] & 0x80) ret |= MK_LBUTTON;
        if (key_state_table[VK_LBUTTON] & 0x80) ret |= MK_RBUTTON;
    }
    else
    {
        if (key_state_table[VK_LBUTTON] & 0x80) ret |= MK_LBUTTON;
        if (key_state_table[VK_RBUTTON] & 0x80) ret |= MK_RBUTTON;
    }
    if (key_state_table[VK_MBUTTON] & 0x80)  ret |= MK_MBUTTON;
    if (key_state_table[VK_SHIFT] & 0x80)    ret |= MK_SHIFT;
    if (key_state_table[VK_CONTROL] & 0x80)  ret |= MK_CONTROL;
    if (key_state_table[VK_XBUTTON1] & 0x80) ret |= MK_XBUTTON1;
    if (key_state_table[VK_XBUTTON2] & 0x80) ret |= MK_XBUTTON2;
    return ret;
}

/***********************************************************************
 *           queue_raw_mouse_message
 */
static void queue_raw_mouse_message( UINT message, HWND hwnd, DWORD x, DWORD y,
                                     DWORD data, DWORD time, DWORD extra_info, UINT injected_flags )
{
    MSLLHOOKSTRUCT hook;

    hook.pt.x        = x;
    hook.pt.y        = y;
    hook.mouseData   = MAKELONG( 0, data );
    hook.flags       = injected_flags;
    hook.time        = time;
    hook.dwExtraInfo = extra_info;

    if (HOOK_CallHooks( WH_MOUSE_LL, HC_ACTION, message, (LPARAM)&hook, TRUE )) return;

    SERVER_START_REQ( send_hardware_message )
    {
        req->id       = (injected_flags & LLMHF_INJECTED) ? 0 : GetCurrentThreadId();
        req->win      = hwnd;
        req->msg      = message;
        req->wparam   = MAKEWPARAM( get_key_state(), data );
        req->lparam   = 0;
        req->x        = x;
        req->y        = y;
        req->time     = time;
        req->info     = extra_info;
        wine_server_call( req );
    }
    SERVER_END_REQ;

}


/***********************************************************************
 *		QDRV_send_mouse_input
 */
void QDRV_send_mouse_input( HWND hwnd, DWORD flags, DWORD x, DWORD y,
                              DWORD data, DWORD time, DWORD extra_info, UINT injected_flags )
{
    POINT pt;

    if (flags & MOUSEEVENTF_ABSOLUTE)
    {
        if (injected_flags & LLMHF_INJECTED)
        {
            pt.x = (x * screen_width) >> 16;
            pt.y = (y * screen_height) >> 16;
        }
        else
        {
            pt.x = x;
            pt.y = y;
        }
        wine_quartzdrv_lock();
        cursor_pos = pt;
        wine_quartzdrv_unlock();
    }
    else if (flags & MOUSEEVENTF_MOVE)
    {
        int accel[3], xMult = 1, yMult = 1;

        /* dx and dy can be negative numbers for relative movements */
        SystemParametersInfoW(SPI_GETMOUSE, 0, accel, 0);

        if (x > accel[0] && accel[2] != 0)
        {
            xMult = 2;
            if ((x > accel[1]) && (accel[2] == 2)) xMult = 4;
        }
        if (y > accel[0] && accel[2] != 0)
        {
            yMult = 2;
            if ((y > accel[1]) && (accel[2] == 2)) yMult = 4;
        }

        wine_quartzdrv_lock();
        pt.x = cursor_pos.x + (long)x * xMult;
        pt.y = cursor_pos.y + (long)y * yMult;

        /* Clip to the current screen size */
        if (pt.x < 0) pt.x = 0;
        else if (pt.x >= screen_width) pt.x = screen_width - 1;
        if (pt.y < 0) pt.y = 0;
        else if (pt.y >= screen_height) pt.y = screen_height - 1;
        cursor_pos = pt;
        wine_quartzdrv_unlock();
    }
    else
    {
        wine_quartzdrv_lock();
        pt = cursor_pos;
        wine_quartzdrv_unlock();
    }

    if (flags & MOUSEEVENTF_MOVE)
    {
        queue_raw_mouse_message( WM_MOUSEMOVE, hwnd, pt.x, pt.y, data, time,
                                 extra_info, injected_flags );
        if ((injected_flags & LLMHF_INJECTED) &&
            ((flags & MOUSEEVENTF_ABSOLUTE) || x || y))  /* we have to actually move the cursor */
        {
            FIXME( "warping to (%ld,%ld)\n", pt.x, pt.y );
            /*wine_tsx11_lock();
            XWarpPointer( thread_display(), root_window, root_window, 0, 0, 0, 0, pt.x, pt.y );
            wine_tsx11_unlock();*/
        }
    }
    if (flags & MOUSEEVENTF_LEFTDOWN)
    {
        key_state_table[VK_LBUTTON] |= 0xc0;
        queue_raw_mouse_message( GetSystemMetrics(SM_SWAPBUTTON) ? WM_RBUTTONDOWN : WM_LBUTTONDOWN,
                                 hwnd, pt.x, pt.y, data, time, extra_info, injected_flags );
    }
    if (flags & MOUSEEVENTF_LEFTUP)
    {
        key_state_table[VK_LBUTTON] &= ~0x80;
        queue_raw_mouse_message( GetSystemMetrics(SM_SWAPBUTTON) ? WM_RBUTTONUP : WM_LBUTTONUP,
                                 hwnd, pt.x, pt.y, data, time, extra_info, injected_flags );
    }
    if (flags & MOUSEEVENTF_RIGHTDOWN)
    {
        key_state_table[VK_RBUTTON] |= 0xc0;
        queue_raw_mouse_message( GetSystemMetrics(SM_SWAPBUTTON) ? WM_LBUTTONDOWN : WM_RBUTTONDOWN,
                                 hwnd, pt.x, pt.y, data, time, extra_info, injected_flags );
    }
    if (flags & MOUSEEVENTF_RIGHTUP)
    {
        key_state_table[VK_RBUTTON] &= ~0x80;
        queue_raw_mouse_message( GetSystemMetrics(SM_SWAPBUTTON) ? WM_LBUTTONUP : WM_RBUTTONUP,
                                 hwnd, pt.x, pt.y, data, time, extra_info, injected_flags );
    }
    if (flags & MOUSEEVENTF_MIDDLEDOWN)
    {
        key_state_table[VK_MBUTTON] |= 0xc0;
        queue_raw_mouse_message( WM_MBUTTONDOWN, hwnd, pt.x, pt.y, data, time,
                                 extra_info, injected_flags );
    }
    if (flags & MOUSEEVENTF_MIDDLEUP)
    {
        key_state_table[VK_MBUTTON] &= ~0x80;
        queue_raw_mouse_message( WM_MBUTTONUP, hwnd, pt.x, pt.y, data, time,
                                 extra_info, injected_flags );
    }
    if (flags & MOUSEEVENTF_WHEEL)
    {
        queue_raw_mouse_message( WM_MOUSEWHEEL, hwnd, pt.x, pt.y, data, time,
                                 extra_info, injected_flags );
    }
    if (flags & MOUSEEVENTF_XDOWN)
    {
        key_state_table[VK_XBUTTON1 + data - 1] |= 0xc0;
        queue_raw_mouse_message( WM_XBUTTONDOWN, hwnd, pt.x, pt.y, data, time,
                                 extra_info, injected_flags );
    }
    if (flags & MOUSEEVENTF_XUP)
    {
        key_state_table[VK_XBUTTON1 + data - 1] &= ~0x80;
        queue_raw_mouse_message( WM_XBUTTONUP, hwnd, pt.x, pt.y, data, time,
                                 extra_info, injected_flags );
    }
}

/***********************************************************************
 *		update_mouse_state
 *
 * Update the various window states on a mouse event.
 */
static void update_mouse_state( HWND hwnd, WindowRef window, int x, int y)
{
    struct quartzdrv_thread_data *data = quartzdrv_thread_data();

    TRACE("\n");

    /* update the cursor */
    if (data->cursor_window != window)
    {
        FIXME("update the cursor, data->cursor_window %p window %p\n", data->cursor_window, window);
        data->cursor_window = window;
    }

    /* update the wine server Z-order */

    if (window != data->grab_window)
    {
        SERVER_START_REQ( update_window_zorder )
        {
            req->window      = hwnd;
            req->rect.left   = x;
            req->rect.top    = y;
            req->rect.right  = x + 1;
            req->rect.bottom = y + 1;
            wine_server_call( req );
        }
        SERVER_END_REQ;
    }
}

/***********************************************************************
 *           QDRV_ButtonPress
 */
void QDRV_ButtonPress(WindowRef window, int x, int y, int button)
{
    struct quartzdrv_win_data *data;

    TRACE("win=%p x=%d y=%d button=%d\n", window, x, y, button);

    if (!(data = get_win_data_carbon(window))) return;

    update_mouse_state(data->hwnd, window, x, y);

    QDRV_send_mouse_input( data->hwnd, button_down_flags[button] | MOUSEEVENTF_ABSOLUTE,
                             x, y, button, EVENT_x11_time_to_win32_time(GetTickCount()), 0, 0 );
}


/***********************************************************************
 *           QDRV_ButtonRelease
 */
void QDRV_ButtonRelease(WindowRef window, int x, int y, int button)
{
    struct quartzdrv_win_data *data;
    
    TRACE("win=%p x=%d y=%d button=%d\n", window, x, y, button);

    if (!(data = get_win_data_carbon(window))) return;

    update_mouse_state( data->hwnd, window, x, y);

    QDRV_send_mouse_input( data->hwnd, button_up_flags[button] | MOUSEEVENTF_ABSOLUTE,
                             x, y, button, EVENT_x11_time_to_win32_time(GetTickCount()), 0, 0 );
}

/***********************************************************************
 *           QDRV_MotionNotify
 */
void QDRV_MotionNotify(WindowRef window, int x, int y)
{
    struct quartzdrv_win_data *data;
    
    TRACE("win=%p x=%d y=%d\n", window, x, y);

    if (!(data = get_win_data_carbon(window))) return;

    update_mouse_state(data->hwnd, window, x, y);

    QDRV_send_mouse_input( data->hwnd, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE,
                             x, y, 0, EVENT_x11_time_to_win32_time(GetTickCount()), 0, 0 );
}

/***********************************************************************
 *		create_cursor
 *
 * Create a CGImageRef of cursor from a Windows one.
 */
static CGImageRef create_cursor(CURSORICONINFO *ptr, unsigned int *hot_x, unsigned int *hot_y)
{
    CGContextRef cursor = NULL;
    CGRect destRect;
    CGImageRef final = NULL;
    
    if (!ptr)  /* Create an empty cursor */
    {
        cursor = QDRV_CreateCGBitmapContext(32, 32, 32);
        if (cursor)
        {
            destRect.origin.x = destRect.origin.y = 0;
            destRect.size.width = 32;
            destRect.size.height = 32;
        
            CGContextClearRect(cursor, destRect);
            QDRV_CGFillRectangle(cursor, 0, 0, 32, 32, 0);
            final = CGBitmapContextCreateImage(cursor);
            QDRV_DeleteCGBitmapContext(cursor);
        }
    }
    else  /* Create the CGImageRef from the bits */
    {
        unsigned char *buffer;
        int cpt;
        int i;
        int h, w;
        CGImageRef mask;
        destRect.origin.x = destRect.origin.y = 0;
        destRect.size.width = ptr->nWidth;
        destRect.size.height = ptr->nHeight;

        TRACE("Bitmap %dx%d planes=%d bpp=%d bytesperline=%d\n",
            ptr->nWidth, ptr->nHeight, ptr->bPlanes, ptr->bBitsPerPixel,
            ptr->nWidthBytes);
        
        /* FIXME check correct size must be 32x32 cursor */
            
        cursor = QDRV_CreateCGBitmapContext(ptr->nWidth, ptr->nHeight, 32);        
        buffer = (unsigned char *)(ptr + 1);
         
        mask = QDRV_CreateCGImageMask(buffer, ptr->nWidth, ptr->nHeight);
        buffer = (unsigned char *)(ptr + 1);
        buffer += ptr->nWidthBytes * ptr->nHeight;

        CGContextClearRect(cursor, destRect);
        CGContextSaveGState(cursor);

        if ( ptr->bBitsPerPixel == 1)
        {
            cpt = ptr->nWidthBytes * ptr->nHeight;
            h = 0;
            
            for (i = 0; i < cpt; i += ptr->nWidthBytes)
            {
                int j, k;
                w = 0;
                
                for (k = 0; k < ptr->nWidthBytes; k++)
                    for (j = 7; j >= 0; j--)
                    {
                        int color = ((buffer[i + k] >> j) & 1);
                        QDRV_CGPutPixel_RGB(cursor, w++, h, (float) color, (float) color, (float) color);
                    }
                h++;
            }
            
            CGContextRestoreGState(cursor);
            if (mask)
            {            
                CGImageRef cursimg = CGBitmapContextCreateImage(cursor);
                final = CGImageCreateWithMask(cursimg, mask);
                CGImageRelease(cursimg);
                CGImageRelease(mask);
                QDRV_DeleteCGBitmapContext(cursor);
            }

            *hot_x = ptr->ptHotSpot.x;
            *hot_y = ptr->ptHotSpot.y;
            if (*hot_x < 0 || *hot_x >= ptr->nWidth ||
                *hot_y < 0 || *hot_y >= ptr->nHeight)
            {
                *hot_x = ptr->nWidth / 2;
                *hot_y = ptr->nHeight / 2;
            }
        }
    }
    return final;
}

/***********************************************************************
 *		SetCursor (X11DRV.@)
 */
void QDRV_SetCursor( CURSORICONINFO *lpCursor )
{
    unsigned int hot_x;
    unsigned int hot_y;
    CGImageRef cursor = create_cursor(lpCursor, &hot_x, &hot_y);
    struct quartzdrv_thread_data *data = quartzdrv_thread_data();    

    TRACE("semi-stub: lpCursor=%p cursor_pos [%d %d] cursor=%p\n", lpCursor, (int) cursor_pos.x, (int) cursor_pos.y, cursor);

    wine_quartzdrv_lock();
    if (cursor)
    {
#ifdef WINE_COCOA
        Cocoa_SetCursor(data->cursor, cursor,  hot_x, hot_y);
        CGImageRelease(cursor);
#else
        FIXME("SetCursor with Carbon/QD or else\n");
#endif
    }
    wine_quartzdrv_unlock();
}

/***********************************************************************
 *		SetCursorPos (X11DRV.@)
 */
BOOL QDRV_SetCursorPos( INT x, INT y )
{
    CGPoint point;
    TRACE("warping to (%d,%d)\n", x, y );
    wine_quartzdrv_lock();
    point.x = x;
    point.y = /*screen_height -*/ y; /* Not in CG coord ?? */
    /* FIXME x11drv use XWarpPointer which generate mouse event
        and CGDisplayMoveCursorToPoint will not generate event for mouse move */
    CGDisplayMoveCursorToPoint(CGMainDisplayID(), point); 
    
    cursor_pos.x = x;
    cursor_pos.y = y;
    wine_quartzdrv_unlock();
    return TRUE;
}

/***********************************************************************
 *		GetCursorPos (X11DRV.@)
 */
BOOL QDRV_GetCursorPos(LPPOINT pos)
{
#ifdef WINE_COCOA
    int x, y;
    wine_quartzdrv_lock();
    Cocoa_GetMousePos(&x, &y);
    TRACE("pointer at (%d,%d)\n", x, y);
    cursor_pos.x = x;
    cursor_pos.y = y;
    *pos = cursor_pos;
    wine_quartzdrv_unlock();
#else
    CARBONPOINT cp;
    
    wine_quartzdrv_lock();
    GetMouse(&cp);
    TRACE("pointer at (%d,%d)\n", cp.h, cp.v);
    cursor_pos.x = cp.h;
    cursor_pos.y = cp.v;
    *pos = cursor_pos;
    wine_quartzdrv_unlock();
#endif
    return TRUE;
}
