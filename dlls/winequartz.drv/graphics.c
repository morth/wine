/*
 * QUARTZ graphics driver graphics functions
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

#include <math.h>
#ifdef HAVE_FLOAT_H
# include <float.h>
#endif
#include <stdlib.h>
#ifndef PI
#define PI M_PI
#endif
#include <string.h>
#include <stdarg.h>

#include "wine/debug.h"
#include "quartzdrv.h"

WINE_DEFAULT_DEBUG_CHANNEL(graphics);

#define ABS(x)    ((x)<0?(-(x)):(x))

static float scaleColor = 1.0f / 255.0f;

static char *rop2_color_str(UINT rop2)
{
    switch (rop2)
    {
    case R2_BLACK : return "R2_BLACK";
    case R2_WHITE : return "R2_WHITE";
    case R2_XORPEN : return "R2_XORPEN";
    default: return "unknowColor";
    }
}

/***********************************************************************
 *           QDRV_XWStoDS
 *
 * Performs a world-to-viewport transformation on the specified width.
 */
INT QDRV_XWStoDS(QDRV_PDEVICE *physDev, INT width)
{
    POINT pt[2];

    pt[0].x = 0;
    pt[0].y = 0;
    pt[1].x = width;
    pt[1].y = 0;
    LPtoDP( physDev->hdc, pt, 2 );
    return pt[1].x - pt[0].x;
}

const char *X11DRV_XROPfunction[16] =
{
    "GXclear",        /* R2_BLACK */
    "GXnor",          /* R2_NOTMERGEPEN */
    "GXandInverted",  /* R2_MASKNOTPEN */
    "GXcopyInverted", /* R2_NOTCOPYPEN */
    "GXandReverse",   /* R2_MASKPENNOT */
    "GXinvert",       /* R2_NOT */
    "GXxor",          /* R2_XORPEN */
    "GXnand",         /* R2_NOTMASKPEN */
    "GXand",          /* R2_MASKPEN */
    "GXequiv",        /* R2_NOTXORPEN */
    "GXnoop",         /* R2_NOP */
    "GXorInverted",   /* R2_MERGENOTPEN */
    "GXcopy",         /* R2_COPYPEN */
    "GXorReverse",    /* R2_MERGEPENNOT */
    "GXor",           /* R2_MERGEPEN */
    "GXset"           /* R2_WHITE */
};

/***********************************************************************
 *           X11DRV_SetupGCForPatBlt
 *
 * Setup the GC for a PatBlt operation using current brush.
 * If fMapColors is TRUE, X pixels are mapped to Windows colors.
 * Return FALSE if brush is BS_NULL, TRUE otherwise.
 */
BOOL QDRV_SetupGCForPatBlt( QDRV_PDEVICE *physDev, CGContextRef ctx, BOOL fMapColors )
{
    POINT pt;
    FIXME("semi-stub (physDev=%p ctx=%p fMapColors=%d)\n", physDev, ctx, fMapColors);

    if (physDev->brush.style == BS_NULL) return FALSE;
    if (physDev->brush.pixel == -1)
    {
	/* Special case used for monochrome pattern brushes.
	 * We need to swap foreground and background because
	 * Windows does it the wrong way...
	 */         
         TRACE ("color r %f g %f b %f\n", GetRValue(physDev->backgroundPixel) * scaleColor, 
                                        GetGValue(physDev->backgroundPixel) * scaleColor, 
                                        GetBValue(physDev->backgroundPixel) * scaleColor);
         
        CGContextSetRGBFillColor(ctx, GetRValue(physDev->backgroundPixel) * scaleColor, 
                                        GetGValue(physDev->backgroundPixel) * scaleColor, 
                                        GetBValue(physDev->backgroundPixel) * scaleColor, 1.0);
    }
    else
    {
        TRACE ("color r %f g %f b %f\n", GetRValue(physDev->brush.pixel) * scaleColor, 
                                        GetGValue(physDev->brush.pixel) * scaleColor, 
                                        GetBValue(physDev->brush.pixel) * scaleColor);
                                        
        CGContextSetRGBFillColor(ctx, GetRValue(physDev->brush.pixel) * scaleColor, 
                                        GetGValue(physDev->brush.pixel) * scaleColor, 
                                        GetBValue(physDev->brush.pixel) * scaleColor, 1.0);
    }
    
    TRACE("GetROP2(physDev->hdc)=%d %s\n", GetROP2(physDev->hdc)-1, X11DRV_XROPfunction[GetROP2(physDev->hdc)-1]);
    
    switch(physDev->brush.fillStyle)
    {
    /*case FillStippled:
    case FillOpaqueStippled:
	if (GetBkMode(physDev->hdc)==OPAQUE) FIXME("fillStyle : FillOpaqueStippled\n");
            else FIXME("fillStyle : FillStippled\n");
        break;

    case FillTiled:
        FIXME("fillStyle : FillTiled\n");
        break;*/

    default:
        break;
    }
    
    GetBrushOrgEx( physDev->hdc, &pt );
    
    return TRUE;
}


/***********************************************************************
 *           X11DRV_SetupGCForBrush
 *
 * Setup physDev->gc for drawing operations using current brush.
 * Return FALSE if brush is BS_NULL, TRUE otherwise.
 */
BOOL QDRV_SetupGCForBrush( QDRV_PDEVICE *physDev, CGContextRef ctx )
{
    return QDRV_SetupGCForPatBlt( physDev, ctx, FALSE );
}

/***********************************************************************
 *           QDRV_SetupGCForPen
 *
 * Return FALSE if pen is PS_NULL, TRUE otherwise.
 */
BOOL QDRV_SetupGCForPen( QDRV_PDEVICE *physDev, CGContextRef ctx)
{
    UINT rop2 = GetROP2(physDev->hdc);
    
    if (physDev->pen.style == PS_NULL) return FALSE;
    
    TRACE("semi-stub %s pen.width=%d pen.pixel=0x%08x backgroundPixel=0x%08x\n", rop2_color_str(rop2), physDev->pen.width, physDev->pen.pixel, physDev->backgroundPixel);

    switch (rop2)
    {
    case R2_BLACK :
        CGContextSetRGBFillColor(ctx, 0.f, 0.f, 0.f, 1.f);
        CGContextSetRGBStrokeColor(ctx, 0.f, 0.f, 0.f, 1.f);
	break;
    case R2_WHITE :
        CGContextSetRGBFillColor(ctx, 1.f, 1.f, 1.f, 1.f);
        CGContextSetRGBStrokeColor(ctx, 1.f, 1.f, 1.f, 1.f);
	break;
    case R2_XORPEN :
        CGContextSetRGBFillColor(ctx, 0.f, 0.f, 0.f, 1.f);
        CGContextSetRGBStrokeColor(ctx, 0.f, 0.f, 0.f, 1.f);
	break;
    default :
        TRACE ("color r %f g %f b %f\n", GetRValue(physDev->pen.pixel) * scaleColor, 
                                        GetGValue(physDev->pen.pixel) * scaleColor, 
                                        GetBValue(physDev->pen.pixel) * scaleColor);
                                        
        CGContextSetRGBFillColor(ctx, GetRValue(physDev->pen.pixel) * scaleColor, 
                                        GetGValue(physDev->pen.pixel) * scaleColor, 
                                        GetBValue(physDev->pen.pixel) * scaleColor, 1.0);
                                        
        CGContextSetRGBStrokeColor(ctx, GetRValue(physDev->pen.pixel) * scaleColor, 
                                        GetGValue(physDev->pen.pixel) * scaleColor, 
                                        GetBValue(physDev->pen.pixel) * scaleColor, 1.0);
        break;
    }

    CGContextSetLineWidth(ctx, physDev->pen.width);
    
    switch (physDev->pen.endcap)
    {
	case PS_ENDCAP_SQUARE:
	    CGContextSetLineCap(ctx, kCGLineCapSquare);
	    break;
	case PS_ENDCAP_FLAT:
	    CGContextSetLineCap(ctx, kCGLineCapButt);
	    break;
	case PS_ENDCAP_ROUND:
	default:
	    CGContextSetLineCap(ctx, kCGLineCapRound);
    }
    
    switch (physDev->pen.linejoin)
    {
    case PS_JOIN_BEVEL:
        TRACE("PS_JOIN_BEVEL\n");
	CGContextSetLineJoin(ctx, kCGLineJoinBevel);
        break;
    case PS_JOIN_MITER:
        TRACE("PS_JOIN_MITER\n");
	CGContextSetLineJoin(ctx, kCGLineJoinMiter);
        break;
    case PS_JOIN_ROUND:
    default:
        TRACE("PS_JOIN_ROUND\n");
	CGContextSetLineJoin(ctx, kCGLineJoinRound);
    }
    
    if ((physDev->pen.width <= 1) &&
        (physDev->pen.style != PS_SOLID) &&
        (physDev->pen.style != PS_INSIDEFRAME))
    {
        FIXME("Setting physDev->pen.dashes=%p physDev->pen.dash_len=%d\n", physDev->pen.dashes, physDev->pen.dash_len);
    }
    
    return TRUE;
}


/***********************************************************************
 *           X11DRV_SetupGCForText
 *
 * Setup physDev->gc for text drawing operations.
 * Return FALSE if the font is null, TRUE otherwise.
 */
BOOL QDRV_SetupGCForText(QDRV_PDEVICE *physDev, CGContextRef ctx)
{
    TRACE ("color r %f g %f b %f\n", GetRValue(physDev->textPixel) * scaleColor, 
                                        GetGValue(physDev->textPixel) * scaleColor, 
                                        GetBValue(physDev->textPixel) * scaleColor);
                                        
    CGContextSetRGBFillColor(ctx, GetRValue(physDev->textPixel) * scaleColor, 
                                        GetGValue(physDev->textPixel) * scaleColor, 
                                        GetBValue(physDev->textPixel) * scaleColor, 1.0);
    return TRUE;
}


/***********************************************************************
*           QDRV_LineTo
*/
BOOL QDRV_LineTo( QDRV_PDEVICE *physDev, INT x, INT y )
{
    POINT pt[2];

    TRACE( "physDev=%p hdc=%p x=%d y=%d org.x=%ld org.y=%ld\n", physDev, physDev->hdc, x, y, physDev->org.x, physDev->org.y);

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(physDev, DIB_Status_GdiMod, FALSE);

    GetCurrentPositionEx( physDev->hdc, &pt[0] );
    pt[1].x = x;
    pt[1].y = y;
    LPtoDP( physDev->hdc, pt, 2 );

    if (GetObjectType( physDev->hdc ) == OBJ_MEMDC)
    {
        wine_quartzdrv_lock();
        if ( QDRV_SetupGCForPen(physDev, physDev->drawable) )
                QDRV_GCDrawLine(physDev->drawable,
                    physDev->org.x + pt[0].x,
                    physDev->org.y + pt[0].y,
                    physDev->org.x + pt[1].x, 
                    physDev->org.y + pt[1].y, 0);
        wine_quartzdrv_unlock();        
    }
    else
    {
        CGContextRef context = NULL;
        
        if ( (context = QDRV_BeginDrawing(physDev->drawable)) )
        {
            if ( QDRV_SetupGCForPen(physDev, context) )
                QDRV_GCDrawLine(context,
                                physDev->org.x + pt[0].x,
                                physDev->org.y + pt[0].y,
                                physDev->org.x + pt[1].x, 
                                physDev->org.y + pt[1].y, 1);
        }
        QDRV_EndDrawing(physDev->drawable, context); 
    }
    
    /* Update the pixmap from the DIB section */
    X11DRV_UnlockDIBSection(physDev, TRUE);
    
    return TRUE;
}



/***********************************************************************
*           QDRV_DrawArc
*
* Helper functions for Arc(), Chord() and Pie().
* 'lines' is the number of lines to draw: 0 for Arc, 1 for Chord, 2 for Pie.
*
*/
static BOOL
QDRV_DrawArc( QDRV_PDEVICE *physDev, INT left, INT top, INT right,
              INT bottom, INT xstart, INT ystart,
              INT xend, INT yend, INT lines )
{
    FIXME( ":stub\n" );
    return TRUE;
}


/***********************************************************************
*           QDRV_Arc
*/
BOOL
QDRV_Arc( QDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom,
          INT xstart, INT ystart, INT xend, INT yend )
{
    FIXME( ":stub\n" );
    return TRUE;
}


/***********************************************************************
*           QDRV_Pie
*/
BOOL
QDRV_Pie( QDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom,
          INT xstart, INT ystart, INT xend, INT yend )
{
    FIXME( ":stub\n" );
    return TRUE;
}

/***********************************************************************
*           QDRV_Chord
*/
BOOL
QDRV_Chord( QDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom,
            INT xstart, INT ystart, INT xend, INT yend )
{
    FIXME( ":stub\n" );
    return TRUE;
}


/***********************************************************************
*           QDRV_Ellipse
*/
BOOL
QDRV_Ellipse( QDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom )
{
    FIXME( ":stub\n" );
    return TRUE;
}


/***********************************************************************
*           QDRV_Rectangle
*/
BOOL
QDRV_Rectangle(QDRV_PDEVICE *physDev, INT left, INT top, INT right, INT bottom)
{
    INT width, oldwidth, oldjoinstyle;
    BOOL update = FALSE;
    RECT rc;

    TRACE("physDev=%p %d %d %d %d\n", physDev, left, top, right, bottom);

    SetRect(&rc, left, top, right, bottom);
    LPtoDP(physDev->hdc, (POINT*)&rc, 2);

    if ((rc.left == rc.right) || (rc.top == rc.bottom)) return TRUE;

    if (rc.right < rc.left) { INT tmp = rc.right; rc.right = rc.left; rc.left = tmp; }
    if (rc.bottom < rc.top) { INT tmp = rc.bottom; rc.bottom = rc.top; rc.top = tmp; }

    oldwidth = width = physDev->pen.width;
    if (!width) width = 1;
    if(physDev->pen.style == PS_NULL) width = 0;

    if ((physDev->pen.style == PS_INSIDEFRAME))
    {
        if (2*width > (rc.right-rc.left)) width=(rc.right-rc.left + 1)/2;
        if (2*width > (rc.bottom-rc.top)) width=(rc.bottom-rc.top + 1)/2;
        rc.left   += width / 2;
        rc.right  -= (width - 1) / 2;
        rc.top    += width / 2;
        rc.bottom -= (width - 1) / 2;
    }
    if(width == 1) width = 0;
    physDev->pen.width = width;
    oldjoinstyle = physDev->pen.linejoin;
    if(physDev->pen.type != PS_GEOMETRIC)
        physDev->pen.linejoin = PS_JOIN_MITER;

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(physDev, DIB_Status_GdiMod, FALSE);

    if ((rc.right > rc.left + width) && (rc.bottom > rc.top + width))
    {
        if (GetObjectType( physDev->hdc ) == OBJ_MEMDC)
        {
            wine_quartzdrv_lock();
            if ( QDRV_SetupGCForBrush(physDev, physDev->drawable) )
                QDRV_CGFillRectangle(physDev->drawable,
                                physDev->org.x + rc.left + (width + 1) / 2,
                                physDev->org.y + rc.top + (width + 1) / 2,
                                rc.right-rc.left-width-1, rc.bottom-rc.top-width-1, 0);
            wine_quartzdrv_unlock();        
        }
        else
        {
            CGContextRef context = NULL;
            
            if ( (context = QDRV_BeginDrawing(physDev->drawable)) )
            {
                if ( QDRV_SetupGCForBrush(physDev, context) )
                    QDRV_CGFillRectangle(context,
                                    physDev->org.x + rc.left + (width + 1) / 2,
                                    physDev->org.y + rc.top + (width + 1) / 2,
                                    rc.right-rc.left-width-1, rc.bottom-rc.top-width-1, 1);
            }
            QDRV_EndDrawing(physDev->drawable, context); 
        }
        
        update = TRUE;
    }
    if (GetObjectType( physDev->hdc ) == OBJ_MEMDC)
    {
        wine_quartzdrv_lock();
        if ( QDRV_SetupGCForPen(physDev, physDev->drawable) )
        {
            QDRV_CGDrawRectangle(physDev->drawable,
                                 physDev->org.x + rc.left, physDev->org.y + rc.top,
                                 rc.right-rc.left-1, rc.bottom-rc.top-1, 0);
            update = TRUE;
        }
        wine_quartzdrv_unlock();        
    }
    else
    {
        CGContextRef context = NULL;
        
        if ( (context = QDRV_BeginDrawing(physDev->drawable)) )
        {
            if ( QDRV_SetupGCForPen(physDev, context) )
            {
                QDRV_CGDrawRectangle(context,
                                     physDev->org.x + rc.left, physDev->org.y + rc.top,
                                     rc.right-rc.left-1, rc.bottom-rc.top-1, 1);
                update = TRUE;
            }
        }
        QDRV_EndDrawing(physDev->drawable, context); 
    }
    
    /* Update the DIBSection from the pixmap */
    X11DRV_UnlockDIBSection(physDev, update);

    physDev->pen.width = oldwidth;
    physDev->pen.linejoin = oldjoinstyle;
    return TRUE;
}

/***********************************************************************
*           QDRV_RoundRect
*/
BOOL
QDRV_RoundRect( QDRV_PDEVICE *physDev, INT left, INT top, INT right,
                INT bottom, INT ell_width, INT ell_height )
{
    FIXME( ":stub\n" );
    return TRUE;
}


/***********************************************************************
*           QDRV_SetPixel
*/
COLORREF
QDRV_SetPixel( QDRV_PDEVICE *physDev, INT x, INT y, COLORREF color )
{
    unsigned long pixel;
    POINT pt;
    BOOL memdc = (GetObjectType(physDev->hdc) == OBJ_MEMDC);

    pt.x = x;
    pt.y = y;
    LPtoDP( physDev->hdc, &pt, 1 );
    pixel = QDRV_PALETTE_ToPhysical( physDev, color );
    FIXME("stub physDev=%p color=%08x pixel=%08x MEMDC=%s\n", physDev, (unsigned) color, (unsigned) pixel, (memdc)?"YES":"NO");

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(physDev, DIB_Status_GdiMod, FALSE);

    if (memdc)
    {
        wine_quartzdrv_lock();
        QDRV_CGPutPixel_RGB(physDev->drawable, physDev->org.x + pt.x, physDev->org.y + pt.y +1, GetRValue(pixel) * scaleColor, GetGValue(pixel) * scaleColor, GetBValue(pixel) * scaleColor);
        wine_quartzdrv_unlock();        
    }
    else
    {
        FIXME("!OBJ_MEMDC\n");
    }

    /* Update the DIBSection from the pixmap */
    X11DRV_UnlockDIBSection(physDev, TRUE);

    return QDRV_PALETTE_ToLogical(pixel);
}


/***********************************************************************
*           QDRV_GetPixel
*/
COLORREF
QDRV_GetPixel( QDRV_PDEVICE *physDev, INT x, INT y )
{
    unsigned int pixel;
    POINT pt;
    BOOL memdc = (GetObjectType(physDev->hdc) == OBJ_MEMDC);

    pt.x = x;
    pt.y = y;
    LPtoDP( physDev->hdc, &pt, 1 );
    
    FIXME( ":stub physDev=%p MEMDC=%s\n", physDev, (memdc)?"YES":"NO");
    
    return pixel;
}


/***********************************************************************
*           QDRV_PaintRgn
*/
BOOL
QDRV_PaintRgn( QDRV_PDEVICE *physDev, HRGN hrgn )
{
    unsigned int i;
    CGRect *rect;
    RGNDATA *data = QDRV_GetRegionData( hrgn, physDev->hdc );
    if (!data) return FALSE;
    
    TRACE("(physDev=%p hrgn=%p\n", physDev, hrgn);
    
    rect = (CGRect *)data->Buffer;
    for (i = 0; i < data->rdh.nCount; i++)
    {
        rect[i].origin.x += physDev->org.x;
        rect[i].origin.y += (physDev->org.y + rect[i].size.height);
    }
    
    X11DRV_LockDIBSection(physDev, DIB_Status_GdiMod, FALSE);

    if (GetObjectType( physDev->hdc ) == OBJ_MEMDC)
    {
        wine_quartzdrv_lock();
        if ( QDRV_SetupGCForBrush(physDev, physDev->drawable) )
            QDRV_CGFillRectangles(physDev->drawable, rect, data->rdh.nCount, 0);
        wine_quartzdrv_unlock();        
    }
    else
    {
        CGContextRef context = NULL;
        
        if ( (context = QDRV_BeginDrawing(physDev->drawable)) )
        {
            if ( QDRV_SetupGCForBrush(physDev, context) )
                QDRV_CGFillRectangles(context, rect, data->rdh.nCount, 1);
        }
        QDRV_EndDrawing(physDev->drawable, context); 
    }
    
    X11DRV_UnlockDIBSection(physDev, TRUE);
    
    HeapFree( GetProcessHeap(), 0, data );

    return TRUE;    
}

/**********************************************************************
*          QDRV_Polyline
*/
BOOL
QDRV_Polyline( QDRV_PDEVICE *physDev, const POINT* pt, INT count )
{
    FIXME( ":stub\n" );
    return TRUE;
}


/**********************************************************************
*          QDRV_Polygon
*/
BOOL
QDRV_Polygon( QDRV_PDEVICE *physDev, const POINT* pt, INT count )
{    
    register int i;
    CGPoint *points;
    BOOL update = FALSE;

    TRACE("physDev=%p pt=%p count=%d\n", physDev, pt, count);

    if (!(points = HeapAlloc( GetProcessHeap(), 0, sizeof(CGPoint) * (count+1) )))
    {
        WARN("No memory to convert POINTs to CGPoints !\n");
        return FALSE;
    }
    for (i = 0; i < count; i++)
    {
        POINT tmp = pt[i];
        LPtoDP(physDev->hdc, &tmp, 1);
        points[i].x = physDev->org.x + tmp.x;
        points[i].y = physDev->org.y + tmp.y;
    }
    points[count] = points[0];

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(physDev, DIB_Status_GdiMod, FALSE);
    
    if (GetObjectType( physDev->hdc ) == OBJ_MEMDC)
    {
        if ( QDRV_SetupGCForBrush(physDev, physDev->drawable) )
        {
            QDRV_CGFillPath(physDev->drawable, points, count + 1, 0);
            update = TRUE;
        }
        if ( QDRV_SetupGCForPen(physDev, physDev->drawable) )
        {
            QDRV_CGStrokePath(physDev->drawable, points, count + 1, 0);
            update = TRUE;
        }
    }
    else
    {
        CGContextRef context = NULL;

        if ( (context = QDRV_BeginDrawing(physDev->drawable)) )
        {
            if ( QDRV_SetupGCForBrush(physDev, context) )
            {
                QDRV_CGFillPath(context, points, count + 1, 1);
                update = TRUE;
            }
            if ( QDRV_SetupGCForPen(physDev, context) )
            {
                QDRV_CGStrokePath(context, points, count + 1, 1);
                update = TRUE;
            }
        }
        QDRV_EndDrawing(physDev->drawable, context); 
    }
    
    /* Update the DIBSection from the pixmap */
    X11DRV_UnlockDIBSection(physDev, update);

    HeapFree( GetProcessHeap(), 0, points );

    return TRUE;
}


/**********************************************************************
*          QDRV_PolyPolygon
*/
BOOL
QDRV_PolyPolygon( QDRV_PDEVICE *physDev, const POINT* pt, const INT* counts, UINT polygons)
{
    FIXME( ":stub\n" );
    return TRUE;
}


/**********************************************************************
*          QDRV_PolyPolyline
*/
BOOL
QDRV_PolyPolyline( QDRV_PDEVICE *physDev, const POINT* pt, const DWORD* counts, DWORD polylines )
{
    FIXME( ":stub\n" );
    return TRUE;
}


/**********************************************************************
*          QDRV_ExtFloodFill
*/
BOOL
QDRV_ExtFloodFill( QDRV_PDEVICE *physDev, INT x, INT y, COLORREF color,
                   UINT fillType )
{
    FIXME( ":stub\n" );
    return TRUE;
}

/**********************************************************************
*          QDRV_SetBkColor
*/
COLORREF QDRV_SetBkColor( QDRV_PDEVICE *physDev, COLORREF color )
{
    physDev->backgroundPixel = QDRV_PALETTE_ToPhysical(physDev, color);
    return color;
}

/**********************************************************************
*          QDRV_SetTextColor
*/
COLORREF QDRV_SetTextColor( QDRV_PDEVICE *physDev, COLORREF color )
{
    physDev->textPixel = QDRV_PALETTE_ToPhysical(physDev, color);
    return color;
}

/***********************************************************************
*           GetDCOrgEx   (QUARTZDRV.@)
*/
BOOL QDRV_GetDCOrgEx( QDRV_PDEVICE *physDev, LPPOINT lpp )
{
    lpp->x = physDev->org.x + physDev->drawable_org.x;
    lpp->y = physDev->org.y + physDev->drawable_org.y;
    return TRUE;
}


/***********************************************************************
*           SetDCOrg   (QUARTZDRV.@)
*/
DWORD QDRV_SetDCOrg( QDRV_PDEVICE *physDev, INT x, INT y )
{
    DWORD ret = MAKELONG( physDev->org.x + physDev->drawable_org.x,
                          physDev->org.y + physDev->drawable_org.y );
    physDev->org.x = x - physDev->drawable_org.x;
    physDev->org.y = y - physDev->drawable_org.y;
    return ret;
}
