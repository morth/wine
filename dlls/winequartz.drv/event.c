/*
 * X11 event driver
 *
 * Copyright 1993 Alexandre Julliard
 *	     1999 Noel Borthwick
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

#define COM_NO_WINDOWS_H
#include "config.h"

#include <assert.h>
#include <stdarg.h>
#include <string.h>

#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "wingdi.h"
#include "shlobj.h"  /* DROPFILES */

#include "win.h"
#include "winreg.h"
#include "quartzdrv.h"
#include "shellapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(event);

extern int QDRV_AppKitGetEvent(void);

/***********************************************************************
 *           MsgWaitForMultipleObjectsEx   (X11DRV.@)
 */
DWORD QDRV_MsgWaitForMultipleObjectsEx( DWORD count, const HANDLE *handles,
                                          DWORD timeout, DWORD mask, DWORD flags )
{
    DWORD i, ret;
    /* FIXME quartzdrv_thread_data not initialize so can't call TlsGetValue */
    struct quartzdrv_thread_data *data = quartzdrv_thread_data();  // TlsGetValue( thread_data_tls_index );
    
    if (!data || data->process_event_count)
    {
        if (!count && !timeout) return WAIT_TIMEOUT;
        return WaitForMultipleObjectsEx( count, handles, flags & MWMO_WAITALL,
                                         timeout, flags & MWMO_ALERTABLE );
    }

    data->process_event_count++;
    if (QDRV_AppKitGetEvent()) ret = count;
    else if (count || timeout)
    {   
        /* FIXME can't set INFINITE timeout without a "quartz handle", it's toooo slooowww */
        ret = WaitForMultipleObjectsEx( count, handles, flags & MWMO_WAITALL,
                                        50 /*timeout*/, flags & MWMO_ALERTABLE );
        
        if (ret == count) QDRV_AppKitGetEvent();
        else ret = WAIT_TIMEOUT;
    }
    else ret = WAIT_TIMEOUT;
    data->process_event_count--;

    return ret;
}


/***********************************************************************
 *           EVENT_x11_time_to_win32_time
 *
 * Make our timer and the X timer line up as best we can
 *  Pass 0 to retrieve the current adjustment value (times -1)
 */
DWORD EVENT_x11_time_to_win32_time(unsigned int time)
{
  static DWORD adjust = 0;
  DWORD now = GetTickCount();
  DWORD ret;

  if (! adjust && time != 0)
  {
    ret = now;
    adjust = time - now;
  }
  else
  {
      /* If we got an event in the 'future', then our clock is clearly wrong. 
         If we got it more than 10000 ms in the future, then it's most likely
         that the clock has wrapped.  */

      ret = time - adjust;
      if (ret > now && ((ret - now) < 10000) && time != 0)
      {
        adjust += ret - now;
        ret    -= ret - now;
      }
  }

  return ret;
}

