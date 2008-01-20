/*
 * GDI bit-blit operations
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

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winreg.h"
#include "winuser.h"
#include "quartzdrv.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(bitblt);


#define DST 0   /* Destination drawable */
#define SRC 1   /* Source drawable */
#define TMP 2   /* Temporary drawable */
#define PAT 3   /* Pattern (brush) in destination DC */

#define OP(src,dst,rop)   (OP_ARGS(src,dst) << 4 | (rop))
#define OP_ARGS(src,dst)  (((src) << 2) | (dst))

#define OP_SRC(opcode)    ((opcode) >> 6)
#define OP_DST(opcode)    (((opcode) >> 4) & 3)
#define OP_SRCDST(opcode) ((opcode) >> 4)
#define OP_ROP(opcode)    ((opcode) & 0x0f)

#define MAX_OP_LEN  6  /* Longest opcode + 1 for the terminating 0 */

#define SWAP_INT32(i1,i2) \
    do { INT __t = *(i1); *(i1) = *(i2); *(i2) = __t; } while(0)



/***********************************************************************
 *           BITBLT_GetVisRectangles
 *
 * Get the source and destination visible rectangles for StretchBlt().
 * Return FALSE if one of the rectangles is empty.
 */
static BOOL BITBLT_GetVisRectangles( QDRV_PDEVICE *physDevDst, INT xDst, INT yDst,
                                     INT widthDst, INT heightDst,
                                     QDRV_PDEVICE *physDevSrc, INT xSrc, INT ySrc,
                                     INT widthSrc, INT heightSrc,
                                     RECT *visRectSrc, RECT *visRectDst )
{
    RECT rect, clipRect;
    TRACE("(physDevDst=%p xDst=%d yDst=%d widthDst=%d heightDst=%d physDevSrc=%p xSrc=%d ySrc=%d widthSrc=%d heightSrc=%d\n",
                physDevDst, xDst, yDst, widthDst, heightDst, physDevSrc, xSrc, ySrc, widthSrc, heightSrc);
   
      /* Get the destination visible rectangle */

    rect.left   = xDst;
    rect.top    = yDst;
    rect.right  = xDst + widthDst;
    rect.bottom = yDst + heightDst;
    if (widthDst < 0) SWAP_INT32( &rect.left, &rect.right );
    if (heightDst < 0) SWAP_INT32( &rect.top, &rect.bottom );
    
    GetRgnBox( physDevDst->region, &clipRect );

    if (!IntersectRect( visRectDst, &rect, &clipRect )) 
        return FALSE;

      /* Get the source visible rectangle */

    if (!physDevSrc) return TRUE;
    rect.left   = xSrc;
    rect.top    = ySrc;
    rect.right  = xSrc + widthSrc;
    rect.bottom = ySrc + heightSrc;
    if (widthSrc < 0) SWAP_INT32( &rect.left, &rect.right );
    if (heightSrc < 0) SWAP_INT32( &rect.top, &rect.bottom );
    /* Apparently the clipping and visible regions are only for output,
       so just check against dc extent here to avoid BadMatch errors */
    if (physDevSrc->bitmap)
    {
        BITMAP bm;
        GetObjectW( physDevSrc->bitmap->hbitmap, sizeof(bm), &bm );
        SetRect( &clipRect, 0, 0, bm.bmWidth, bm.bmHeight );
    }
    else SetRect( &clipRect, 0, 0, screen_width, screen_height );
    if (!IntersectRect( visRectSrc, &rect, &clipRect ))
        return FALSE;

      /* Intersect the rectangles */

    if ((widthSrc == widthDst) && (heightSrc == heightDst)) /* no stretching */
    {
        visRectSrc->left   += xDst - xSrc;
        visRectSrc->right  += xDst - xSrc;
        visRectSrc->top    += yDst - ySrc;
        visRectSrc->bottom += yDst - ySrc;
        if (!IntersectRect( &rect, visRectSrc, visRectDst )) return FALSE;
        *visRectSrc = *visRectDst = rect;
        visRectSrc->left   += xSrc - xDst;
        visRectSrc->right  += xSrc - xDst;
        visRectSrc->top    += ySrc - yDst;
        visRectSrc->bottom += ySrc - yDst;
    }
    else  /* stretching */
    {
        /* Map source rectangle into destination coordinates */
        rect.left = xDst + (visRectSrc->left - xSrc)*widthDst/widthSrc;
        rect.top = yDst + (visRectSrc->top - ySrc)*heightDst/heightSrc;
        rect.right = xDst + ((visRectSrc->right - xSrc)*widthDst)/widthSrc;
        rect.bottom = yDst + ((visRectSrc->bottom - ySrc)*heightDst)/heightSrc;
        if (rect.left > rect.right) SWAP_INT32( &rect.left, &rect.right );
        if (rect.top > rect.bottom) SWAP_INT32( &rect.top, &rect.bottom );

        /* Avoid rounding errors */
        rect.left--;
        rect.top--;
        rect.right++;
        rect.bottom++;
        if (!IntersectRect( visRectDst, &rect, visRectDst )) return FALSE;

        /* Map destination rectangle back to source coordinates */
        rect = *visRectDst;
        rect.left = xSrc + (visRectDst->left - xDst)*widthSrc/widthDst;
        rect.top = ySrc + (visRectDst->top - yDst)*heightSrc/heightDst;
        rect.right = xSrc + ((visRectDst->right - xDst)*widthSrc)/widthDst;
        rect.bottom = ySrc + ((visRectDst->bottom - yDst)*heightSrc)/heightDst;
        if (rect.left > rect.right) SWAP_INT32( &rect.left, &rect.right );
        if (rect.top > rect.bottom) SWAP_INT32( &rect.top, &rect.bottom );

        /* Avoid rounding errors */
        rect.left--;
        rect.top--;
        rect.right++;
        rect.bottom++;
        if (!IntersectRect( visRectSrc, &rect, visRectSrc )) return FALSE;
    }
    return TRUE;
}


/***********************************************************************
 *           BITBLT_InternalStretchBlt
 *
 * Implementation of PatBlt(), BitBlt() and StretchBlt().
 */
static BOOL BITBLT_InternalStretchBlt( QDRV_PDEVICE *physDevDst, INT xDst, INT yDst,
                                       INT widthDst, INT heightDst,
                                       QDRV_PDEVICE *physDevSrc, INT xSrc, INT ySrc,
                                       INT widthSrc, INT heightSrc,
                                       DWORD rop )
{
    BOOL usePat, useSrc, useDst, fStretch;
    RECT visRectDst, visRectSrc;
    INT width, height;
     
    POINT pts[2];

    /* compensate for off-by-one shifting for negative widths and heights */
    if (widthDst < 0)
        ++xDst;
    if (heightDst < 0)
        ++yDst;
    if (widthSrc < 0)
        ++xSrc;
    if (heightSrc < 0)
        ++ySrc;

    usePat = (((rop >> 4) & 0x0f0000) != (rop & 0x0f0000));
    useSrc = (((rop >> 2) & 0x330000) != (rop & 0x330000));
    useDst = (((rop >> 1) & 0x550000) != (rop & 0x550000));
    if (!physDevSrc && useSrc) return FALSE;

      /* Map the coordinates to device coords */

    pts[0].x = xDst;
    pts[0].y = yDst;
    pts[1].x = xDst + widthDst;
    pts[1].y = yDst + heightDst;
    LPtoDP(physDevDst->hdc, pts, 2);
    xDst      = pts[0].x;
    yDst      = pts[0].y;
    widthDst  = pts[1].x - pts[0].x;
    heightDst = pts[1].y - pts[0].y;

    TRACE("    rectdst=%d,%d-%d,%d orgdst=%ld,%ld\n",
                    xDst, yDst, widthDst, heightDst,
                    physDevDst->org.x, physDevDst->org.y );

    if (useSrc)
    {
        pts[0].x = xSrc;
        pts[0].y = ySrc;
        pts[1].x = xSrc + widthSrc;
        pts[1].y = ySrc + heightSrc;
        LPtoDP(physDevSrc->hdc, pts, 2);
        xSrc      = pts[0].x;
        ySrc      = pts[0].y;
        widthSrc  = pts[1].x - pts[0].x;
        heightSrc = pts[1].y - pts[0].y;

        fStretch  = (widthSrc != widthDst) || (heightSrc != heightDst);
        TRACE("  rectsrc=%d,%d-%d,%d orgsrc=%ld,%ld\n",
                        xSrc, ySrc, widthSrc, heightSrc,
                        physDevSrc->org.x, physDevSrc->org.y );
        if (!BITBLT_GetVisRectangles( physDevDst, xDst, yDst, widthDst, heightDst,
                                      physDevSrc, xSrc, ySrc, widthSrc, heightSrc,
                                      &visRectSrc, &visRectDst ))
            return TRUE;
        TRACE("  BITBLT_GetVisRectangles   vissrc=%ld,%ld-%ld,%ld visdst=%ld,%ld-%ld,%ld\n",
                        visRectSrc.left, visRectSrc.top,
                        visRectSrc.right, visRectSrc.bottom,
                        visRectDst.left, visRectDst.top,
                        visRectDst.right, visRectDst.bottom );
    }
    else
    {
        fStretch = FALSE;
        if (!BITBLT_GetVisRectangles( physDevDst, xDst, yDst, widthDst, heightDst,
                                      NULL, 0, 0, 0, 0, NULL, &visRectDst ))
            return TRUE;
        TRACE("    vissrc=none visdst=%ld,%ld-%ld,%ld\n",
                        visRectDst.left, visRectDst.top,
                        visRectDst.right, visRectDst.bottom );
    }

    width  = visRectDst.right - visRectDst.left;
    height = visRectDst.bottom - visRectDst.top;

    if (!fStretch) switch(rop)  /* A few optimisations */
    {
    case BLACKNESS:  /* 0x00 */
       /* wine_tsx11_lock();
        if ((physDevDst->depth == 1) || !X11DRV_PALETTE_PaletteToXPixel)
            XSetFunction( gdi_display, physDevDst->gc, GXclear );
        else
        {
            XSetFunction( gdi_display, physDevDst->gc, GXcopy );
            XSetForeground( gdi_display, physDevDst->gc, X11DRV_PALETTE_PaletteToXPixel[0] );
            XSetFillStyle( gdi_display, physDevDst->gc, FillSolid );
        }
        XFillRectangle( gdi_display, physDevDst->drawable, physDevDst->gc,
                        physDevDst->org.x + visRectDst.left,
                        physDevDst->org.y + visRectDst.top,
                        width, height );
        wine_tsx11_unlock();*/
        FIXME("BLACKNESS\n");
        
        return TRUE;

    case DSTINVERT:  /* 0x55 */
        FIXME("DSTINVERT\n");
    #if 0
        wine_tsx11_lock();
        XSetFunction( gdi_display, physDevDst->gc, GXinvert );

        if( X11DRV_PALETTE_PaletteFlags & (X11DRV_PALETTE_PRIVATE | X11DRV_PALETTE_VIRTUAL) )
            XSetFunction( gdi_display, physDevDst->gc, GXinvert);
        else
        {
            /* Xor is much better when we do not have full colormap.   */
            /* Using white^black ensures that we invert at least black */
            /* and white. */
            unsigned long xor_pix = (WhitePixel( gdi_display, DefaultScreen(gdi_display) ) ^
                                     BlackPixel( gdi_display, DefaultScreen(gdi_display) ));
            XSetFunction( gdi_display, physDevDst->gc, GXxor );
            XSetForeground( gdi_display, physDevDst->gc, xor_pix);
            XSetFillStyle( gdi_display, physDevDst->gc, FillSolid );
        }
        XFillRectangle( gdi_display, physDevDst->drawable, physDevDst->gc,
                        physDevDst->org.x + visRectDst.left,
                        physDevDst->org.y + visRectDst.top,
                        width, height );
        wine_tsx11_unlock();
    #endif
        return TRUE;

    case PATINVERT:  /* 0x5a */
        FIXME("PATINVERT\n");
    #if 0
        if (X11DRV_SetupGCForBrush( physDevDst ))
        {
            wine_tsx11_lock();
            XSetFunction( gdi_display, physDevDst->gc, GXxor );
            XFillRectangle( gdi_display, physDevDst->drawable, physDevDst->gc,
                            physDevDst->org.x + visRectDst.left,
                            physDevDst->org.y + visRectDst.top,
                            width, height );
            wine_tsx11_unlock();
        }
    #endif
        return TRUE;

    case 0xa50065:
        FIXME("0xa50065\n");
    #if 0
	if (X11DRV_SetupGCForBrush( physDevDst ))
	{
            wine_tsx11_lock();
	    XSetFunction( gdi_display, physDevDst->gc, GXequiv );
	    XFillRectangle( gdi_display, physDevDst->drawable, physDevDst->gc,
                            physDevDst->org.x + visRectDst.left,
                            physDevDst->org.y + visRectDst.top,
                            width, height );
            wine_tsx11_unlock();
	}
    #endif
	return TRUE;

    case SRCCOPY:  /* 0xcc */    
        if (GetObjectType( physDevDst->hdc ) == OBJ_MEMDC)
        {           
            QDRV_GCDrawBitmap(physDevDst->drawable, physDevSrc->drawable, 
                              visRectSrc.left, visRectSrc.top, 
                              physDevDst->org.x + visRectDst.left, physDevDst->org.y + visRectDst.top,
                              width, height, 0);
        }
        else
        {
            CGContextRef context = NULL;
            
            if ( (context = QDRV_BeginDrawing(physDevDst->drawable)) )
            {
                QDRV_GCDrawBitmap(context, physDevSrc->drawable, 
                                  visRectSrc.left, visRectSrc.top, 
                                  physDevDst->org.x + visRectDst.left, physDevDst->org.y + visRectDst.top + height,
                                  width, height, 1);
            }
            QDRV_EndDrawing(physDevDst->drawable, context); 
        }
        return TRUE;
        break;

    case PATCOPY:  /* 0xf0 */
                       
        if (GetObjectType( physDevDst->hdc ) == OBJ_MEMDC)
        {
            wine_quartzdrv_lock();
            if ( QDRV_SetupGCForBrush( physDevDst, physDevDst->drawable) )
                QDRV_CGFillRectangle(physDevDst->drawable,
                            physDevDst->org.x + visRectDst.left,
                            physDevDst->org.y + visRectDst.top + height,
                            width, height, 0);
            wine_quartzdrv_unlock();        
        }
        else
        {
            CGContextRef context = NULL;
                
            if ( (context = QDRV_BeginDrawing(physDevDst->drawable)) )
            {
                if ( QDRV_SetupGCForBrush( physDevDst, context) )
                    QDRV_CGFillRectangle(context, 
                                             physDevDst->org.x + visRectDst.left,
                                             physDevDst->org.y + visRectDst.top + height,
                                             width, height, 1);
            }
            QDRV_EndDrawing(physDevDst->drawable, context); 
        }
        return TRUE;

    case WHITENESS:  /* 0xff */
        FIXME("WHITENESS\n");
    #if 0
        wine_tsx11_lock();
        if ((physDevDst->depth == 1) || !X11DRV_PALETTE_PaletteToXPixel)
            XSetFunction( gdi_display, physDevDst->gc, GXset );
        else
        {
            XSetFunction( gdi_display, physDevDst->gc, GXcopy );
            XSetForeground( gdi_display, physDevDst->gc,
                            WhitePixel( gdi_display, DefaultScreen(gdi_display) ));
            XSetFillStyle( gdi_display, physDevDst->gc, FillSolid );
        }
        XFillRectangle( gdi_display, physDevDst->drawable, physDevDst->gc,
                        physDevDst->org.x + visRectDst.left,
                        physDevDst->org.y + visRectDst.top,
                        width, height );
        wine_tsx11_unlock();
    #endif
        return TRUE;
    }

    return TRUE;
}


/***********************************************************************
 *           QDRV_PatBlt
 */
BOOL QDRV_PatBlt( QDRV_PDEVICE *physDev, INT left, INT top, INT width, INT height, DWORD rop )
{
    BOOL result = TRUE;
    TRACE("(physDev=%p left=%d top=%d width=%d height=%d rop=%d)\n", 
                physDev, left, top, width, height, (int) rop);
    X11DRV_LockDIBSection( physDev, DIB_Status_GdiMod, FALSE );
    result = BITBLT_InternalStretchBlt( physDev, left, top, width, height, NULL, 0, 0, 0, 0, rop );
    X11DRV_UnlockDIBSection( physDev, TRUE );
    return result;
}

/***********************************************************************
 *           X11DRV_BitBlt
 */
BOOL QDRV_BitBlt( QDRV_PDEVICE *physDevDst, INT xDst, INT yDst,
                    INT width, INT height, QDRV_PDEVICE *physDevSrc,
                    INT xSrc, INT ySrc, DWORD rop )
{
    BOOL result = FALSE;
    INT sSrc, sDst;
    RECT visRectDst, visRectSrc;

    TRACE("(physDevDst=%p xDst=%d yDst=%d width=%d height=%d physDevSrc=%p xSrc=%d ySrc=%d rop=%d\n",
                    physDevDst, xDst, yDst, width, height, physDevSrc, xSrc, ySrc, (int) rop);
        
    if (((rop >> 16) & 0x55) == ((rop >> 17) & 0x55)) {
      /* FIXME: seems the ROP doesn't include destination;
       * now if the destination area include the entire dcDst,
       * we can pass TRUE instead of FALSE to CoerceDIBSection(dcDst...),
       * which may avoid a copy in some situations */
    }
    sDst = X11DRV_LockDIBSection( physDevDst, DIB_Status_None, FALSE );

    if (physDevDst != physDevSrc)
        sSrc = X11DRV_LockDIBSection( physDevSrc, DIB_Status_None, FALSE );
    else
        sSrc = sDst;

    if ((sSrc == DIB_Status_AppMod) && (rop == SRCCOPY) &&
        (physDevSrc->depth == physDevDst->depth))
    {
        POINT pts[2];
        /* do everything ourselves; map coordinates */        
        pts[0].x = xSrc;
        pts[0].y = ySrc;
        pts[1].x = xSrc + width;
        pts[1].y = ySrc + height;
        
        LPtoDP(physDevSrc->hdc, pts, 2);
        width = pts[1].x - pts[0].x;
        height = pts[1].y - pts[0].y;
        xSrc = pts[0].x;
        ySrc = pts[0].y;
        
        pts[0].x = xDst;
        pts[0].y = yDst;
        LPtoDP(physDevDst->hdc, pts, 1);
        
        xDst = pts[0].x;
        yDst = pts[0].y;
        
        /* Perform basic clipping */
        if (!BITBLT_GetVisRectangles( physDevDst, xDst, yDst, width, height,
                                      physDevSrc, xSrc, ySrc, width, height,
                                      &visRectSrc, &visRectDst ))
            goto END;
            
        xSrc = visRectSrc.left;
        ySrc = visRectSrc.top;
        xDst = visRectDst.left;
        yDst = visRectDst.top;
        width = visRectDst.right - visRectDst.left;
        height = visRectDst.bottom - visRectDst.top;
                                    
        X11DRV_CoerceDIBSection( physDevDst, DIB_Status_GdiMod, FALSE );
        QDRV_DIB_CopyDIBSection( physDevSrc, physDevDst, xSrc, ySrc, xDst, yDst, width, height );
      
        result = TRUE;
        goto END;
    }
    X11DRV_CoerceDIBSection( physDevDst, DIB_Status_GdiMod, FALSE );
    if (physDevDst != physDevSrc)
      X11DRV_CoerceDIBSection( physDevSrc, DIB_Status_GdiMod, FALSE );
      
    result = BITBLT_InternalStretchBlt( physDevDst, xDst, yDst, width, height,
                                        physDevSrc, xSrc, ySrc, width, height, rop );
END:
    if (physDevDst != physDevSrc)
      X11DRV_UnlockDIBSection( physDevSrc, FALSE );
    X11DRV_UnlockDIBSection( physDevDst, TRUE );

    return result;
}

/***********************************************************************
 *           QDRV_StretchBlt
 */
BOOL QDRV_StretchBlt( QDRV_PDEVICE *physDevDst, INT xDst, INT yDst,
                        INT widthDst, INT heightDst,
                        QDRV_PDEVICE *physDevSrc, INT xSrc, INT ySrc,
                        INT widthSrc, INT heightSrc, DWORD rop )
{
    BOOL result = TRUE;
    
    TRACE("(physDevDst=%p xDst=%d yDst=%d widthDst=%d heightDst=%d physDevSrc=%p xSrc=%d ySrc=%d widthSrc=%d heightSrc=%d rop=%d\n",
          physDevDst, xDst, yDst, widthDst, heightDst, physDevSrc, xSrc, ySrc, widthSrc, heightSrc, (int) rop);
    
    X11DRV_LockDIBSection( physDevDst, DIB_Status_GdiMod, FALSE );
    if (physDevDst != physDevSrc)
        X11DRV_LockDIBSection( physDevSrc, DIB_Status_GdiMod, FALSE );
    
    result = BITBLT_InternalStretchBlt( physDevDst, xDst, yDst, widthDst, heightDst,
                                        physDevSrc, xSrc, ySrc, widthSrc, heightSrc, rop );
    
    if (physDevDst != physDevSrc)
        X11DRV_UnlockDIBSection( physDevSrc, FALSE );
    X11DRV_UnlockDIBSection( physDevDst, TRUE );
    
    return result;
}
