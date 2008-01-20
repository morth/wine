/*
 * QUARTZDRV brush objects
 *
 * Copyright 1993, 1994  Alexandre Julliard
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

#include <stdlib.h>

#include "wine/winbase16.h"
#include "quartzdrv.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(gdi);

static const char HatchBrushes[][8] =
{
    { 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00 }, /* HS_HORIZONTAL */
    { 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08 }, /* HS_VERTICAL   */
    { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 }, /* HS_FDIAGONAL  */
    { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 }, /* HS_BDIAGONAL  */
    { 0x08, 0x08, 0x08, 0xff, 0x08, 0x08, 0x08, 0x08 }, /* HS_CROSS      */
    { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 }, /* HS_DIAGCROSS  */
};

  /* Levels of each primary for dithering */
#define PRIMARY_LEVELS  3
#define TOTAL_LEVELS    (PRIMARY_LEVELS*PRIMARY_LEVELS*PRIMARY_LEVELS)

 /* Dithering matrix size  */
#define MATRIX_SIZE     8
#define MATRIX_SIZE_2   (MATRIX_SIZE*MATRIX_SIZE)

  /* Total number of possible levels for a dithered primary color */
#define DITHER_LEVELS   (MATRIX_SIZE_2 * (PRIMARY_LEVELS-1) + 1)

  /* Dithering matrix */
static const int dither_matrix[MATRIX_SIZE_2] =
{
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21
};

  /* Mapping between (R,G,B) triples and EGA colors */
static const int EGAmapping[TOTAL_LEVELS] =
{
    0,  /* 000000 -> 000000 */
    4,  /* 00007f -> 000080 */
    12, /* 0000ff -> 0000ff */
    2,  /* 007f00 -> 008000 */
    6,  /* 007f7f -> 008080 */
    6,  /* 007fff -> 008080 */
    10, /* 00ff00 -> 00ff00 */
    6,  /* 00ff7f -> 008080 */
    14, /* 00ffff -> 00ffff */
    1,  /* 7f0000 -> 800000 */
    5,  /* 7f007f -> 800080 */
    5,  /* 7f00ff -> 800080 */
    3,  /* 7f7f00 -> 808000 */
    8,  /* 7f7f7f -> 808080 */
    7,  /* 7f7fff -> c0c0c0 */
    3,  /* 7fff00 -> 808000 */
    7,  /* 7fff7f -> c0c0c0 */
    7,  /* 7fffff -> c0c0c0 */
    9,  /* ff0000 -> ff0000 */
    5,  /* ff007f -> 800080 */
    13, /* ff00ff -> ff00ff */
    3,  /* ff7f00 -> 808000 */
    7,  /* ff7f7f -> c0c0c0 */
    7,  /* ff7fff -> c0c0c0 */
    11, /* ffff00 -> ffff00 */
    7,  /* ffff7f -> c0c0c0 */
    15  /* ffffff -> ffffff */
};

/*
#define PIXEL_VALUE(r,g,b) \
    X11DRV_PALETTE_mapEGAPixel[EGAmapping[((r)*PRIMARY_LEVELS+(g))*PRIMARY_LEVELS+(b)]]
*/

static const COLORREF BLACK = RGB(0, 0, 0);
static const COLORREF WHITE = RGB(0xff, 0xff, 0xff);

/***********************************************************************
 *           BRUSH_DitherColor
 */
static AKObjectRef BRUSH_DitherColor( COLORREF color )
{
    FIXME("stub : color=0x%08x\n", (unsigned) color);

    return 0;
}


/***********************************************************************
 *           BRUSH_DitherMono
 */
static AKObjectRef BRUSH_DitherMono( COLORREF color )
{
    FIXME("stub : color=0x%08x\n", (unsigned) color);
    
    return 0;
}

/***********************************************************************
 *           BRUSH_SelectSolidBrush
 */
static void BRUSH_SelectSolidBrush(QDRV_PDEVICE *physDev, COLORREF color )
{
    FIXME("stub : physDev=%p color=0x%08x physDev->depth=%d\n", physDev, (unsigned) color, physDev->depth);

    physDev->brush.pixel = QDRV_PALETTE_ToPhysical( physDev, color );
    physDev->brush.fillStyle = 1;
    physDev->brush.context = 0;
}


/***********************************************************************
 *           BRUSH_SelectPatternBrush
 */
static BOOL BRUSH_SelectPatternBrush( QDRV_PDEVICE *physDev, HBITMAP hbitmap )
{
    FIXME("stub: physDev=%p hbitmap=%p\n", physDev, hbitmap);
    
    
    return TRUE;
}


/***********************************************************************
 *           SelectBrush   (X11DRV.@)
 */
HBRUSH QDRV_SelectBrush( QDRV_PDEVICE *physDev, HBRUSH hbrush )
{
    LOGBRUSH logbrush;
    HBITMAP hBitmap;
    BITMAPINFO * bmpInfo;
    TRACE("semi-stub (physDev=%p hbrush=%p)\n", physDev, hbrush);

    if (!GetObjectA( hbrush, sizeof(logbrush), &logbrush )) return 0;

    if (physDev->brush.context)
    {
        wine_quartzdrv_lock();
        
        QDRV_DeleteCGBitmapContext(physDev->brush.context);

        wine_quartzdrv_unlock();
        physDev->brush.context = 0;
    }
    physDev->brush.style = logbrush.lbStyle;
    if (hbrush == GetStockObject( DC_BRUSH ))
        logbrush.lbColor = GetDCBrushColor( physDev->hdc );

    switch(logbrush.lbStyle)
    {
      case BS_NULL:
	TRACE("BS_NULL\n" );
	break;

      case BS_SOLID:
        TRACE("BS_SOLID\n" );
	BRUSH_SelectSolidBrush( physDev, logbrush.lbColor );
	break;

      case BS_HATCHED:
	FIXME("BS_HATCHED\n" );
	/*physDev->brush.pixel = X11DRV_PALETTE_ToPhysical( physDev, logbrush.lbColor );
        wine_tsx11_lock();
        physDev->brush.pixmap = XCreateBitmapFromData( gdi_display, root_window,
                                                       HatchBrushes[logbrush.lbHatch], 8, 8 );
        wine_tsx11_unlock();
	physDev->brush.fillStyle = FillStippled;*/
	break;

      case BS_PATTERN:
	TRACE("BS_PATTERN\n");
	if (!BRUSH_SelectPatternBrush( physDev, (HBITMAP)logbrush.lbHatch )) return 0;
	break;

      case BS_DIBPATTERN:
	TRACE("BS_DIBPATTERN\n");
	if ((bmpInfo = (BITMAPINFO *) GlobalLock16( (HGLOBAL16)logbrush.lbHatch )))
	{
	    int size = 0; // X11DRV_DIB_BitmapInfoSize( bmpInfo, logbrush.lbColor );
	    
            hBitmap = CreateDIBitmap( physDev->hdc, &bmpInfo->bmiHeader,
                                        CBM_INIT, ((char *)bmpInfo) + size,
                                        bmpInfo,
                                        (WORD)logbrush.lbColor );
	    BRUSH_SelectPatternBrush( physDev, hBitmap );
	    DeleteObject( hBitmap );
	    GlobalUnlock16( (HGLOBAL16)logbrush.lbHatch );
	}

	break;
    }
    return hbrush;
}


/***********************************************************************
 *           SetDCBrushColor (X11DRV.@)
 */
COLORREF QDRV_SetDCBrushColor( QDRV_PDEVICE *physDev, COLORREF crColor )
{
    if (GetCurrentObject(physDev->hdc, OBJ_BRUSH) == GetStockObject( DC_BRUSH ))
        BRUSH_SelectSolidBrush( physDev, crColor );

    return crColor;
}
