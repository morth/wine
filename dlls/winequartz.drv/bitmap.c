/*
 * X11DRV bitmap objects
 *
 * Copyright 1993 Alexandre Julliard
 *           1999 Noel Borthwick
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
#include <stdlib.h>
#include "windef.h"
#include "wingdi.h"
#include "wine/debug.h"
#include "quartzdrv.h"

#define WINE_SIDE
#include "wine_carbon.h"

WINE_DEFAULT_DEBUG_CHANNEL(bitmap);

/* GCs used for B&W and color bitmap operations */
CGContextRef BITMAP_monoGC = 0, BITMAP_colorGC = 0;
QUARTZ_PHYSBITMAP BITMAP_stock_phys_bitmap = { 0 };  /* phys bitmap for the default stock bitmap */

static struct list quartzdrv_bitmap_data_list;

static void init_bitmap_data(void)
{
    wine_quartzdrv_lock();
    list_init( &quartzdrv_bitmap_data_list );
    wine_quartzdrv_unlock();
}

static QUARTZ_PHYSBITMAP* get_bitmap_data(HBITMAP hbitmap)
{
    QUARTZ_PHYSBITMAP *data;
    
    wine_quartzdrv_lock();
    LIST_FOR_EACH_ENTRY( data, &quartzdrv_bitmap_data_list, QUARTZ_PHYSBITMAP, entry )
    {
        if ( data->hbitmap == hbitmap )
        {
            wine_quartzdrv_unlock();
            return data;
        }
    }
    wine_quartzdrv_unlock();
    return NULL;
}

static QUARTZ_PHYSBITMAP * new_bitmap_data(HBITMAP hbitmap)
{
    QUARTZ_PHYSBITMAP *data;
    if (!(data = HeapAlloc( GetProcessHeap(), 0, sizeof(QUARTZ_PHYSBITMAP) )))
    {
        return NULL;
    }
    memset(data, 0, sizeof(QUARTZ_PHYSBITMAP)); // ?? once more and elsewhere !!
    data->hbitmap = hbitmap;
    
    wine_quartzdrv_lock();
    list_add_head( &quartzdrv_bitmap_data_list, &data->entry );
    wine_quartzdrv_unlock();
    return data;
}

static void delete_bitmap_data( HBITMAP hbitmap )
{
    QUARTZ_PHYSBITMAP *data;
    wine_quartzdrv_lock();
    LIST_FOR_EACH_ENTRY( data, &quartzdrv_bitmap_data_list, QUARTZ_PHYSBITMAP, entry )
    {
        if ( data->hbitmap == hbitmap )
        {
            list_remove( &data->entry );
            HeapFree( GetProcessHeap(), 0, data );
            break;
        }
    }
    wine_quartzdrv_unlock();
}

/***********************************************************************
*           QDRV_BITMAP_Init
*/

void QDRV_BITMAP_Init(void)
{
    init_bitmap_data();
    
    wine_quartzdrv_lock();
    BITMAP_stock_phys_bitmap.depth = 1;
    BITMAP_stock_phys_bitmap.context = QDRV_CreateCGBitmapContext(1, 1, 1); /* FIXME */
    
    BITMAP_monoGC = QDRV_CreateCGBitmapContext(1, 1, 1);
    
    if (screen_depth != 1)
        BITMAP_colorGC = QDRV_CreateCGBitmapContext(1, 1, screen_depth);
    
    wine_quartzdrv_unlock();
}

/***********************************************************************
 *           SelectBitmap   (QUARTZDRV.@)
 */
HBITMAP QDRV_SelectBitmap( QDRV_PDEVICE *physDev, HBITMAP hbitmap )
{
    QUARTZ_PHYSBITMAP *physBitmap;

    if (hbitmap == BITMAP_stock_phys_bitmap.hbitmap) physBitmap = &BITMAP_stock_phys_bitmap;
    else if (!(physBitmap = get_bitmap_data( hbitmap ))) return 0;

    physDev->bitmap = physBitmap;
    physDev->drawable = physBitmap->context;
    
    TRACE("physDev=%p hbitmap=%p drawable=%p\n", physDev, hbitmap, physDev->drawable);
    
    if (physDev->depth != physBitmap->depth)
    {
        FIXME("changing depth : physDev %d physBitmap %d\n", physDev->depth, physBitmap->depth);
       // physDev->depth = physBitmap->depth;
    }
    return hbitmap;
}

/****************************************************************************
*	  CreateBitmap   (X11DRV.@)
*
* Create a device dependent X11 bitmap
*
* Returns TRUE on success else FALSE
*/
BOOL QDRV_CreateBitmap(QDRV_PDEVICE *physDev, HBITMAP hbitmap, LPVOID bmBits)
{
    BITMAP bitmap;
    QUARTZ_PHYSBITMAP *data;
    
    if (!GetObjectW( hbitmap, sizeof(bitmap), &bitmap )) return FALSE;
        
    /* Check parameters */
    if (bitmap.bmPlanes != 1) return FALSE;
    
    if ((bitmap.bmBitsPixel != 1) && (bitmap.bmBitsPixel != screen_depth))
    {
        ERR("Trying to make bitmap with planes=%d, bpp=%d\n",
            bitmap.bmPlanes, bitmap.bmBitsPixel);
        return FALSE;
    }
    if (hbitmap == BITMAP_stock_phys_bitmap.hbitmap)
    {
        ERR( "called for stock bitmap, please report\n" );
        return FALSE;
    }
    TRACE("(%p) %dx%d %d bpp\n", hbitmap, bitmap.bmWidth, bitmap.bmHeight, bitmap.bmBitsPixel);
    
    if (!(data = new_bitmap_data(hbitmap))) return FALSE;
    
    wine_quartzdrv_lock();
    data->depth = bitmap.bmBitsPixel;
    data->context = QDRV_CreateCGBitmapContext(bitmap.bmWidth, bitmap.bmHeight, bitmap.bmBitsPixel);
    wine_quartzdrv_unlock();
     
     if (!data->context)
    {
        delete_bitmap_data(hbitmap);
        return FALSE;
    }
     
    if (bmBits) /* Set bitmap bits */
        QDRV_SetBitmapBits( hbitmap, bmBits, bitmap.bmHeight * bitmap.bmWidthBytes );
    else /* else clear the bitmap */
    {
        CGRect rect;

        wine_quartzdrv_lock();
        rect.origin.x = 0;
        rect.origin.y = 0;
        rect.size.width = bitmap.bmWidth;
        rect.size.height = bitmap.bmHeight;
        CGContextClearRect(data->context, rect);
        wine_quartzdrv_unlock();
    }
    
    return TRUE;
}

/***********************************************************************
*           DeleteBitmap   (X11DRV.@)
*/
BOOL QDRV_DeleteBitmap( HBITMAP hbitmap )
{
    QUARTZ_PHYSBITMAP *data;
    TRACE("bmp=%p\n", hbitmap);
    
    if ( (data = get_bitmap_data(hbitmap)))
    {
        DIBSECTION dib;
        
        if (GetObjectW(hbitmap, sizeof(dib), &dib) == sizeof(dib))
            QDRV_DIB_DeleteDIBSection(data, &dib);
            
        wine_quartzdrv_lock();      
        QDRV_DeleteCGBitmapContext(data->context);
        wine_quartzdrv_unlock();
        delete_bitmap_data( hbitmap );
    }
    return TRUE;
}

/***********************************************************************
 *           GetBitmapBits   (X11DRV.@)
 *
 * RETURNS
 *    Success: Number of bytes copied
 *    Failure: 0
 */
 
LONG QDRV_GetBitmapBits( HBITMAP hbitmap, void *buffer, LONG count )
{
    BITMAP bitmap;
    QUARTZ_PHYSBITMAP *data = get_bitmap_data(hbitmap);
    LPBYTE tbuf, startline;
    int	h, w;
    unsigned char *cgbuffer;
    unsigned int *cgbuffer32;

    if (!data || !GetObjectW( hbitmap, sizeof(bitmap), &bitmap )) return 0;

    TRACE("semi-stub bmp=%p, buffer=%p, count=0x%lx depth=%d\n", hbitmap, buffer, count, data->depth);
    
    wine_quartzdrv_lock();
    
    startline = buffer;
    switch (data->depth)
    {
        case 1:
            cgbuffer = (unsigned char *) CGBitmapContextGetData(data->context);
            for (h = 0; h < bitmap.bmHeight; h++)
            {
                tbuf = startline;
                *tbuf = 0;
                for (w = 0;w < bitmap.bmWidth; w++)
                {
                    int val;
                    if ((w%8) == 0)
                        *tbuf = 0;
                    
                    val = (*cgbuffer++) ? 1 : 0;
                    *tbuf |= val << (7 - ( w & 7));
                    if ((w&7) == 7) tbuf++;
                }
                startline += bitmap.bmWidthBytes;
            }
            break;
        case 32:
            cgbuffer32 = (unsigned int *) CGBitmapContextGetData(data->context);
            for (h = 0; h < bitmap.bmHeight; h++)
            {
                tbuf = startline;
                for (w = 0; w < bitmap.bmWidth; w++)
                {
                    cgbuffer = (unsigned char *) cgbuffer32;
                    *tbuf++ = cgbuffer[0] & 0xff;
                    *tbuf++ = cgbuffer[3] & 0xff;
                    *tbuf++ = cgbuffer[2] & 0xff;
                    *tbuf++ = cgbuffer[1] & 0xff;
                    cgbuffer32++;
                }
                startline += bitmap.bmWidthBytes;
            }
            break;
        default:
            FIXME("Unhandled bits:%d\n", data->depth);
    }
    wine_quartzdrv_unlock();
        
    return count;
}


/******************************************************************************
 *             SetBitmapBits   (X11DRV.@)
 *
 * RETURNS
 *    Success: Number of bytes used in setting the bitmap bits
 *    Failure: 0
 */
LONG QDRV_SetBitmapBits( HBITMAP hbitmap, const void *bits, LONG count )
{
    BITMAP bitmap;
    QUARTZ_PHYSBITMAP *data = get_bitmap_data( hbitmap );
    LONG height;
    const BYTE *sbuf, *startline;
    int	w, h;

    if (!data || !GetObjectW( hbitmap, sizeof(bitmap), &bitmap )) return 0;
   
    TRACE("semi-stub: (bmp=%p, bits=%p, count=%ld)\n", hbitmap, bits, count);
    height = count / bitmap.bmWidthBytes;

    wine_quartzdrv_lock();
    startline = bits;
    switch (data->depth)
    {
    case 1:
        for (h=0;h<height;h++)
        {
	    sbuf = startline;
            for (w=0;w<bitmap.bmWidth;w++)
            {
                QDRV_CGPutPixel_1(data->context, w, h+1, ((sbuf[0]>>(7-(w&7))) & 1)?0:1);
                if ((w&7) == 7)
                    sbuf++;
            }
            startline += bitmap.bmWidthBytes;
        }
        break;
    case 32:
        for (h=0;h<height;h++)
        {
	    sbuf = startline;
            for (w=0;w<bitmap.bmWidth;w++)
            {
                QDRV_CGPutPixel_RGB( data->context, w, h+1,  sbuf[1] / 255.0f, sbuf[2] / 255.0f, sbuf[3] / 255.0f );
                sbuf += 4;
            }
	    startline += bitmap.bmWidthBytes;
        }
        break;
    default:
      FIXME("Unhandled bits:%d\n", data->depth);

    }
    wine_quartzdrv_unlock();

    return count;
}

QUARTZ_PHYSBITMAP *QDRV_get_phys_bitmap( HBITMAP hbitmap )
{
    return get_bitmap_data(hbitmap);
}

QUARTZ_PHYSBITMAP *QDRV_init_phys_bitmap( HBITMAP hbitmap )
{
    return new_bitmap_data(hbitmap);
}
