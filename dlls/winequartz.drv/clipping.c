/*
 * X11DRV clipping functions
 *
 * Copyright 1998 Huw Davies
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

#include <stdio.h>
#include "wine/debug.h"
#include "quartzdrv.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartzdrv);


/***********************************************************************
 *           QDRV_GetRegionData
 *
 * Calls GetRegionData on the given region and converts the rectangle
 * array to CGRect format. The returned buffer must be freed by
 * caller using HeapFree(GetProcessHeap(),...).
 * If hdc_lptodp is not 0, the rectangles are converted through LPtoDP.
 */
 
RGNDATA *QDRV_GetRegionData( HRGN hrgn, HDC hdc_lptodp )
{
    RGNDATA *data;
    DWORD size;
    unsigned int i;
    RECT *rect, tmp;
    CGRect *cgrect;

    if (!(size = GetRegionData( hrgn, 0, NULL ))) return NULL;
    if (sizeof(CGRect) > sizeof(RECT))
    {
        /* add extra size for CGRect array */
        int count = (size - sizeof(RGNDATAHEADER)) / sizeof(RECT);
        size += count * (sizeof(CGRect) - sizeof(RECT));
    }
    if (!(data = HeapAlloc( GetProcessHeap(), 0, size ))) return NULL;
    if (!GetRegionData( hrgn, size, data ))
    {
        HeapFree( GetProcessHeap(), 0, data );
        return NULL;
    }

    rect = (RECT *)data->Buffer;
    cgrect = (CGRect *)data->Buffer;
    if (hdc_lptodp)  /* map to device coordinates */
    {
        LPtoDP( hdc_lptodp, (POINT *)rect, data->rdh.nCount * 2 );
        for (i = 0; i < data->rdh.nCount; i++)
        {
            if (rect[i].right < rect[i].left)
            {
                INT tmp = rect[i].right;
                rect[i].right = rect[i].left;
                rect[i].left = tmp;
            }
            if (rect[i].bottom < rect[i].top)
            {
                INT tmp = rect[i].bottom;
                rect[i].bottom = rect[i].top;
                rect[i].top = tmp;
            }
        }
    }

    if (sizeof(CGRect) > sizeof(RECT))
    {
        int j;
        /* need to start from the end */
        for (j = data->rdh.nCount-1; j >= 0; j--)
        {
            tmp = rect[j];
            cgrect[j].origin.x      = tmp.left;
            cgrect[j].origin.y      = tmp.top;
            cgrect[j].size.width    = tmp.right - tmp.left;
            cgrect[j].size.height   = tmp.bottom - tmp.top;
        }
    }
    else
    {
        for (i = 0; i < data->rdh.nCount; i++)
        {
            tmp = rect[i];
            cgrect[i].origin.x      = tmp.left;
            cgrect[i].origin.y      = tmp.top;
            cgrect[i].size.width    = tmp.right - tmp.left;
            cgrect[i].size.height   = tmp.bottom - tmp.top;
        }
    }
    return data;
}


/***********************************************************************
 *           QDRV_SetDeviceClipping
 */
void QDRV_SetDeviceClipping( QDRV_PDEVICE *physDev, HRGN vis_rgn, HRGN clip_rgn )
{
    RGNDATA *data;
    FIXME("stub: physDev=%p\n", physDev);

    CombineRgn( physDev->region, vis_rgn, clip_rgn, clip_rgn ? RGN_AND : RGN_COPY );
    
    /*if (!(data = QDRV_GetRegionData( physDev->region, 0 ))) return;

    HeapFree( GetProcessHeap(), 0, data );*/
}
