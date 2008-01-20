/*
 * QUARTZ palette driver
 *
 * Copyright 1999 Patrik Stridvall
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

#include "wtypes.h"
#include "wingdi.h"
#include "winbase.h"
#include "wine/debug.h"
#include "quartzdrv.h"

WINE_DEFAULT_DEBUG_CHANNEL(palette);

/**********************************************************************/

#define NB_RESERVED_COLORS     20   /* number of fixed colors in system palette */

CGDirectPaletteRef QDRV_PALETTE_QuartzPaletteColormap = 0;
UINT16 QDRV_PALETTE_PaletteFlags = 0;

static PALETTEENTRY *COLOR_sysPal;

static int palette_size;

/*********************************************************************
 *           COLOR_Init
 *
 * Initialize color management.
 */
int QDRV_PALETTE_Init(void)
{
    PALETTEENTRY sys_pal_template[NB_RESERVED_COLORS];
    FIXME("stub:\n");

    QDRV_PALETTE_PaletteFlags |= QDRV_PALETTE_VIRTUAL;
    QDRV_PALETTE_PaletteFlags |= QDRV_PALETTE_FIXED;
    palette_size = 0;
    
   /* wine_quartzdrv_lock();
    QDRV_PALETTE_QuartzPaletteColormap = CGPaletteCreateDefaultColorPalette();
    wine_quartzdrv_unlock();*/
    GetPaletteEntries( GetStockObject(DEFAULT_PALETTE), 0, NB_RESERVED_COLORS, sys_pal_template );

    return palette_size;
}

/***********************************************************************
 *           QDRV_PALETTE_ToLogical
 *
 * Return RGB color for given X pixel.
 */
COLORREF QDRV_PALETTE_ToLogical(int pixel)
{
    FIXME("stub\n");
    /* truecolor visual */
    if (screen_depth >= 24) return pixel;

#if 0
    /* check for hicolor visuals first */

    if ( (X11DRV_PALETTE_PaletteFlags & X11DRV_PALETTE_FIXED) && !X11DRV_PALETTE_Graymax )
    {
         color.red = (pixel >> X11DRV_PALETTE_LRed.shift) & X11DRV_PALETTE_LRed.max;
         if (X11DRV_PALETTE_LRed.scale<8)
             color.red=  color.red   << (8-X11DRV_PALETTE_LRed.scale) |
                         color.red   >> (2*X11DRV_PALETTE_LRed.scale-8);
         color.green = (pixel >> X11DRV_PALETTE_LGreen.shift) & X11DRV_PALETTE_LGreen.max;
         if (X11DRV_PALETTE_LGreen.scale<8)
             color.green=color.green << (8-X11DRV_PALETTE_LGreen.scale) |
                         color.green >> (2*X11DRV_PALETTE_LGreen.scale-8);
         color.blue = (pixel >> X11DRV_PALETTE_LBlue.shift) & X11DRV_PALETTE_LBlue.max;
         if (X11DRV_PALETTE_LBlue.scale<8)
             color.blue= color.blue  << (8-X11DRV_PALETTE_LBlue.scale)  |
                         color.blue  >> (2*X11DRV_PALETTE_LBlue.scale-8);
                 return RGB(color.red,color.green,color.blue);
    }

    /* check if we can bypass X */

    if ((screen_depth <= 8) && (pixel < 256) &&
        !(X11DRV_PALETTE_PaletteFlags & (X11DRV_PALETTE_VIRTUAL | X11DRV_PALETTE_FIXED)) ) {
         return  ( *(COLORREF*)(COLOR_sysPal +
		   ((X11DRV_PALETTE_XPixelToPalette)?X11DRV_PALETTE_XPixelToPalette[pixel]:pixel)) ) & 0x00ffffff;
    }

    wine_tsx11_lock();
    color.pixel = pixel;
    XQueryColor(gdi_display, X11DRV_PALETTE_PaletteXColormap, &color);
    wine_tsx11_unlock();
#endif
    return 0;
}

static int*
palette_get_mapping (HPALETTE hPal)
{
	/* XXX TODO */
	return NULL;
}


/***********************************************************************
 *           X11DRV_PALETTE_ToPhysical
 *
 * Return the physical color closest to 'color'.
 */
int QDRV_PALETTE_ToPhysical( QDRV_PDEVICE *physDev, COLORREF color )
{
    WORD 		 index = 0;
    HPALETTE hPal = physDev ? GetCurrentObject(physDev->hdc, OBJ_PAL ) : GetStockObject(DEFAULT_PALETTE);
    unsigned char	 spec_type = color >> 24;
    int *mapping = palette_get_mapping( hPal );
    PALETTEENTRY entry;

    TRACE("semi-stub physDev=%p color=0x%08x\n", physDev, (unsigned) color);

    if ( QDRV_PALETTE_PaletteFlags & QDRV_PALETTE_FIXED )
    {
        /* there is no colormap limitation; we are going to have to compute
         * the pixel value from the visual information stored earlier
	 */

	// unsigned 	long red, green, blue;
	unsigned	idx = color & 0xffff;
        RGBQUAD         quad;

	switch(spec_type)
        {
          case 0x10: /* DIBINDEX */
            TRACE("DIBINDEX\n");
            
            if( QDRV_GetDIBColorTable( physDev, idx, 1, &quad ) != 1 ) {
                WARN("DIBINDEX(%lx) : idx %d is out of bounds, assuming black\n", color , idx);
                return 0;
            }
            color = RGB( quad.rgbRed, quad.rgbGreen, quad.rgbBlue );
            return 0;
            break;
                
          case 1: /* PALETTEINDEX */
            TRACE("PALETTEINDEX\n");
            
            if( !GetPaletteEntries( hPal, idx, 1, &entry ))
            {
                WARN("PALETTEINDEX(%lx) : idx %d is out of bounds, assuming black\n", color, idx);
                return 0;
            }

	    if (mapping)
	    {
                int ret = mapping[idx];
		return ret;
	    }
	    color = RGB( entry.peRed, entry.peGreen, entry.peBlue );
	    break;

	  default:
	    color &= 0xffffff;
	    /* fall through to RGB */

	  case 0: /* RGB */
	    if (physDev && (physDev->depth == 1) )
	    {
		return (((color >> 16) & 0xff) +
			((color >> 8) & 0xff) + (color & 0xff) > 255*3/2) ? 1 : 0;
	    }

	}

        TRACE("return color\n");
            return color;
    }
    return index;
}

/***********************************************************************
 *               GetSystemPaletteEntries   (QUARTZDRV.@)
 */
UINT QDRV_GetSystemPaletteEntries( QDRV_PDEVICE *dev, UINT start, UINT count, LPPALETTEENTRY entries )
{
    UINT i;

    if (!entries) return palette_size;
    if (start >= palette_size) return 0;
    if (start + count >= palette_size) count = palette_size - start;

    for (i = 0; i < count; i++)
    {
        entries[i].peRed   = COLOR_sysPal[start + i].peRed;
        entries[i].peGreen = COLOR_sysPal[start + i].peGreen;
        entries[i].peBlue  = COLOR_sysPal[start + i].peBlue;
        entries[i].peFlags = 0;
        TRACE("\tidx(%02x) -> RGB(%08lx)\n", start + i, *(COLORREF*)(entries + i) );
    }
    return count;
}


/***********************************************************************
 *              RealizePalette    (X11DRV.@)
 */
UINT QDRV_RealizePalette( QDRV_PDEVICE *physDev, HPALETTE hpal, BOOL primary )
{
    UINT ret = 0;
    TRACE("semi-stub only virtual\n");
    //PALETTEOBJ *palPtr;

    if (QDRV_PALETTE_PaletteFlags & QDRV_PALETTE_VIRTUAL) return 0;

    /*if (!(palPtr = GDI_GetObjPtr( hpal, PALETTE_MAGIC ))) return 0;
    ret = X11DRV_PALETTE_SetMapping( palPtr, 0, palPtr->logpalette.palNumEntries, !primary );
    */
    return ret;
}


/***********************************************************************
 *           X11DRV_PALETTE_LookupSystemXPixel
 */
static int X11DRV_PALETTE_LookupSystemXPixel(COLORREF col)
{
    int            i, best = 0, diff = 0x7fffffff;
    int            size = palette_size;
    int            r,g,b;
    
    for( i = 0; i < size && diff ; i++ )
    {
        if( i == NB_RESERVED_COLORS/2 )
        {
            int newi = size - NB_RESERVED_COLORS/2;
            if (newi>i) i=newi;
        }
        
        r = COLOR_sysPal[i].peRed - GetRValue(col);
        g = COLOR_sysPal[i].peGreen - GetGValue(col);
        b = COLOR_sysPal[i].peBlue - GetBValue(col);
        
        r = r*r + g*g + b*b;
        
        if( r < diff ) { best = i; diff = r; }
    }
    
    return best;
}


/***********************************************************************
 *              RealizeDefaultPalette    (X11DRV.@)
 */
UINT QDRV_RealizeDefaultPalette( QDRV_PDEVICE *physDev )
{
    UINT ret = 0;
    TRACE("semi-stub (physDev=%p)\n", physDev);
    
    if (palette_size && GetObjectType(physDev->hdc) != OBJ_MEMDC)
    {
        /* lookup is needed to account for SetSystemPaletteUse() stuff */
        int i, index, *mapping = palette_get_mapping( GetStockObject(DEFAULT_PALETTE) );
        PALETTEENTRY entries[NB_RESERVED_COLORS];

        GetPaletteEntries( GetStockObject(DEFAULT_PALETTE), 0, NB_RESERVED_COLORS, entries );
        for( i = 0; i < NB_RESERVED_COLORS; i++ )
        {
            index = X11DRV_PALETTE_LookupSystemXPixel( RGB(entries[i].peRed,
                                                           entries[i].peGreen,
                                                           entries[i].peBlue) );
            /* mapping is allocated in COLOR_InitPalette() */
            if( index != mapping[i] )
            {
                mapping[i]=index;
                ret++;
            }
        }
    }
    return ret;
}
