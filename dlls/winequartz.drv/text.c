/*
 * X11 graphics driver text functions
 *
 * Copyright 1993,1994 Alexandre Julliard
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
#include <math.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "wine/debug.h"
#include "quartzdrv.h"

WINE_DEFAULT_DEBUG_CHANNEL(text);

#define SWAP_INT(a,b)  { int t = a; a = b; b = t; }
#define IROUND(x) (int)((x)>0? (x)+0.5 : (x) - 0.5)


static UniChar * QDRV_unicode_to_UniChar_symbol( /*fontObject* pfo,*/
						 LPCWSTR lpwstr, UINT count )
{
    UniChar *str2b;
    UINT i;
    char ch = 32; // pfo->fs->default_char;
    unsigned char *c;
    
    if (!(str2b = HeapAlloc( GetProcessHeap(), 0, count * sizeof(UniChar) )))
	return NULL;

    for (i = 0; i < count; i++)
    {
        c = (unsigned char *) &str2b[i];
        
	c[0] = 0;
	if(lpwstr[i] >= 0xf000 && lpwstr[i] < 0xf100)
	    c[1] = lpwstr[i] - 0xf000;
	else if(lpwstr[i] < 0x100)
	    c[1] = lpwstr[i];
	else
	    c[1] = ch;
    }

    return str2b;
}


/***********************************************************************
 *           QDRV_ExtTextOut
 */
BOOL
QDRV_ExtTextOut( QDRV_PDEVICE *physDev, INT x, INT y, UINT flags,
                   const RECT *lprect, LPCWSTR wstr, UINT count,
                   const INT *lpDx )
{
    BOOL		dibUpdateFlag = FALSE;
    BOOL                result = TRUE;
    HRGN                saved_region = 0;
    char                *buffer;
    CGContextRef        context = NULL;
  //  UniChar		*str2b = NULL;
 
    TRACE("hdc=%p %d,%d %s, %d  flags=%d lpDx=%p\n",
	  physDev->hdc, x, y,
	  debugstr_wn (wstr, count), count, flags, lpDx);
       
      /* Draw the rectangle */

    if (flags & ETO_OPAQUE)
    {
        X11DRV_LockDIBSection( physDev, DIB_Status_GdiMod, FALSE );
        if (GetObjectType( physDev->hdc ) == OBJ_MEMDC)
        {
            wine_quartzdrv_lock();
            CGContextSetRGBFillColor(physDev->drawable, GetRValue(physDev->backgroundPixel) / 255.0f, 
                                         GetGValue(physDev->backgroundPixel) / 255.0f, 
                                         GetBValue(physDev->backgroundPixel) / 255.0f, 1.0);
             QDRV_CGFillRectangle(physDev->drawable, physDev->org.x + lprect->left, physDev->org.y + lprect->top,
                                     lprect->right - lprect->left, lprect->bottom - lprect->top, 0);
            wine_quartzdrv_unlock();  
        }
        else
        {
            if ( (context = QDRV_BeginDrawing(physDev->drawable)) )
            {
                int height = lprect->bottom - lprect->top;
                CGContextSetRGBFillColor(context, GetRValue(physDev->backgroundPixel) / 255.0f, 
                                         GetGValue(physDev->backgroundPixel) / 255.0f, 
                                         GetBValue(physDev->backgroundPixel) / 255.0f, 1.0);
                
                QDRV_CGFillRectangle(context, physDev->org.x + lprect->left, physDev->org.y + lprect->top + height,
                                     lprect->right - lprect->left, height, 1);
            }
            QDRV_EndDrawing(physDev->drawable, context);
        }
        dibUpdateFlag = TRUE;
    }
    if (!count) goto END;  /* Nothing more to do */


      /* Set the clip region */

    if (flags & ETO_CLIPPED)
    {
        HRGN clip_region;
        FIXME("(flags & ETO_CLIPPED) setting CG clipping rect\n");

        clip_region = CreateRectRgnIndirect( lprect );
        /* make a copy of the current device region */
        saved_region = CreateRectRgn( 0, 0, 0, 0 );
        CombineRgn( saved_region, physDev->region, 0, RGN_COPY );
        QDRV_SetDeviceClipping( physDev, saved_region, clip_region );
        DeleteObject( clip_region );
    }

      /* Draw the text background if necessary */

    if (!dibUpdateFlag)
    {
        X11DRV_LockDIBSection( physDev, DIB_Status_GdiMod, FALSE );
        dibUpdateFlag = TRUE;
    }

    /* Draw the text (count > 0 verified) */
    if (!(buffer = HeapAlloc( GetProcessHeap(), 0, count )))
    {
        ERR("Not enough memory for window text\n");
        goto FAIL;
    }

    WideCharToMultiByte(CP_UNIXCP, 0, wstr, -1, buffer, count, NULL, NULL);
    
    if (GetObjectType( physDev->hdc ) == OBJ_MEMDC)
    {
        wine_quartzdrv_lock();
        if ( QDRV_SetupGCForText(physDev, physDev->drawable) )
            QDRV_GCDrawText(physDev->drawable, physDev->font, physDev->org.x + x, physDev->org.y + y, buffer, count, 0);
        wine_quartzdrv_unlock();
    }
    else
    {
        if ( (context = QDRV_BeginDrawing(physDev->drawable)) )
        {
            if ( QDRV_SetupGCForText(physDev, context) )
                QDRV_GCDrawText(context, physDev->font, physDev->org.x + x, physDev->org.y + y, buffer, count, 1);
        }
        QDRV_EndDrawing(physDev->drawable, context);
    }
    HeapFree( GetProcessHeap(), 0, buffer );

    if (flags & ETO_CLIPPED)
    {
        /* restore the device region */
        QDRV_SetDeviceClipping( physDev, saved_region, 0 );
        DeleteObject( saved_region );
    }
    goto END;

FAIL:
    result = FALSE;

END:
    if (dibUpdateFlag) X11DRV_UnlockDIBSection( physDev, TRUE );
    return result;
}


/***********************************************************************
 *           QDRV_GetTextExtentPoint
 */
BOOL QDRV_GetTextExtentExPoint( QDRV_PDEVICE *physDev, LPCWSTR str, INT count,
                                  INT maxExt, LPINT lpnFit, LPINT alpDx, LPSIZE size )
{
    /*
        FIXME : 
        text width may be calculate with CG by drawing it and retrieve final
        position with : CGContextGetTextPosition.
    
        Also see ATSUI function ATSUMeasureTextImage
    */
    char *buffer;
    int cx;
    int cy;
    TRACE("semi-stub %s %d\n", debugstr_wn(str,count), count);

    size->cx = 0;
    size->cy = 0;
    
    if (!(buffer = HeapAlloc( GetProcessHeap(), 0, count )))
    {
        return FALSE;
    }

    WideCharToMultiByte(CP_UNIXCP, 0, str, -1, buffer, count, NULL, NULL);
    
    if (!QDRV_CGGetTextExtentPoint(physDev->font, buffer, count, &cx, &cy))
    {
        int i;
        for (i=0; i < count; i++)
        {   
            size->cx += 4;
            size->cy = 10;
        }
    }
    else
    {
        size->cx = cx;
        size->cy = cy;
    }
    HeapFree( GetProcessHeap(), 0, buffer );

    TRACE("return size->cx=%ld size->cy=%ld\n", size->cx, size->cy);

    return TRUE;
}
