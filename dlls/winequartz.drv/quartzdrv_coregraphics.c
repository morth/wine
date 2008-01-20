/*
 * Quartz Driver CoreGraphics stuff
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

#import <Carbon/Carbon.h>
#include "wine_carbon.h"

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4
#define HAVE_CGIMAGECREATEWITHIMAGEINRECT 1
#endif
#ifndef kCGColorSpaceGenericRGB
#define kCGColorSpaceGenericRGB kCGColorSpaceUserRGB
#endif

#undef _CDECL
#undef DPRINTF
#include "wine/debug.h"

#define AKObjectRef WindowRef

WINE_DEFAULT_DEBUG_CHANNEL(coregraphics);

extern void wine_quartzdrv_lock(void);
extern void wine_quartzdrv_unlock(void);

#ifndef WINE_COCOA
/*
    CARBON_ hack declaration
*/

extern CGrafPtr CARBON_GetWindowPort(WindowRef);
extern Rect *CARBON_GetPortBounds(CGrafPtr, Rect *);
extern void CARBON_DisposeRgn(RgnHandle);
#endif

#define MAX_FONT 32
typedef struct _QDRVFont
{
    CFStringRef name;
    CGFontRef font;
    ATSFontMetrics metrics;
    int height;
    int weight;
} QDRVFont;
static QDRVFont QDRVFontList[MAX_FONT];
static unsigned fontCpt = 0;

#ifndef WINE_COCOA
static RgnHandle contentRgn = 0;
static CGrafPtr port = 0;
#endif
static int windowHeight = 0;

#if 0
static char *font_metrics_str(ATSFontMetrics metrics)
{
    static char dump[512] = {0};
    
    sprintf (dump,
    "ascent=%f\n"
    "descent=%f\n"
    "leading=%f\n"
    "avgAdvanceWidth=%f\n"
    "maxAdvanceWidth=%f\n"
    "minLeftSideBearing=%f\n"
    "minRightSideBearing=%f\n"
    "stemWidth=%f\n"
    "stemHeight=%f\n"
    "capHeight=%f\n"
    "xHeight=%f\n"
    "italicAngle=%f\n"
    "underlinePosition=%f\n"
    "underlineThickness=%f\n", 
        metrics.ascent, metrics.descent, metrics.leading, metrics.avgAdvanceWidth, metrics.maxAdvanceWidth,
        metrics.minLeftSideBearing, metrics.minRightSideBearing, metrics.stemWidth, metrics.stemHeight, metrics.capHeight, metrics.xHeight,
        metrics.italicAngle, metrics.underlinePosition, metrics.underlineThickness);
    
    return dump;
}
#endif

CGContextRef QDRV_CreateCGBitmapContext( int width, int height, int depth)
{
    CGContextRef ctx = NULL;
    CGColorSpaceRef colorspace;
    char *data;
    size_t bytesPerRow = 0;
    size_t byteCount = 0;
    size_t bitsPerComponent = 0;
    size_t numberOfComponents = 0;
    CGImageAlphaInfo alpha;
    
    TRACE("w %d h %d d %d\n", width, height, depth);

    if (depth == 1)
    {
        bitsPerComponent = 8;
        alpha = kCGImageAlphaNone;
        
        colorspace = CGColorSpaceCreateDeviceGray();
        
        numberOfComponents = CGColorSpaceGetNumberOfComponents(colorspace);
        bytesPerRow = (width * numberOfComponents);
        byteCount = (bytesPerRow * height);
    }
    else
    {
        if (depth == 16)
        {
            bitsPerComponent = 5;
            alpha = kCGImageAlphaNoneSkipFirst;
        }
        else
        {
            bitsPerComponent = 8;
            alpha = kCGImageAlphaNoneSkipFirst;
            numberOfComponents = 1;
        }
        colorspace = CGColorSpaceCreateDeviceRGB();
        
        numberOfComponents += CGColorSpaceGetNumberOfComponents(colorspace);
        bytesPerRow = (width * numberOfComponents);
        byteCount = (bytesPerRow * height);
    }

    data = (char *) calloc( bytesPerRow, height );
        
    if (data)
    {
        ctx = CGBitmapContextCreate( data, width, height, bitsPerComponent,
                                                bytesPerRow, colorspace, alpha);
        if (!ctx) free(data);
    }
    CGColorSpaceRelease(colorspace);
    
    TRACE("return %p\n", ctx);
    return ctx;
}

void QDRV_DeleteCGBitmapContext(CGContextRef ctx )
{ 
    free( CGBitmapContextGetData(ctx) );
    CGContextRelease(ctx);
}

void QDRV_CGInitFont(void)
{
    OSStatus err;
    ATSFontIterator iterator;
    ATSFontRef font;
    //fontCpt = 0;
    
    err = ATSFontIteratorCreate(kATSFontContextGlobal, NULL, NULL, kATSOptionFlagsUnRestrictedScope, &iterator); 
    if (err == noErr)
    {
        while (ATSFontIteratorNext(iterator, &font) == noErr)
        {
            if (font)
            {
                CFStringRef name;
                ATSFontGetName(font, kATSOptionFlagsDefault, &name);
#if 0
                CFShow(name);
#endif
            }
        }
        ATSFontIteratorRelease(iterator);
    }
}

unsigned int QRDV_SelectATSFont(char *facename, int height, int weight)
{
    unsigned int i;
    
    ATSFontMetrics metrics;
    ATSFontRef atsFont;
    CFStringRef name;
    TRACE("facename=%s height=%d weight=%d\n", facename, height, weight);

    for (i = 0; i < fontCpt; i++)
        if (QDRVFontList[i].height == height)
            return i;

    name = CFStringCreateWithCString(kCFAllocatorDefault, "Arial", kCFStringEncodingMacRoman);
    if (!name)
    {
        ERR("CFStringCreateWithCString failed\n");
        return 0;
    }

    atsFont = ATSFontFindFromName(name, kATSOptionFlagsDefault);
    if (!atsFont)
    {
        ERR("failed for Arial\n");
        if (name) CFRelease(name);
        return 0;
    }
    
    ATSFontGetHorizontalMetrics(atsFont, kATSOptionFlagsDefault, &metrics);
#ifdef CGTRACE
 //   fprintf(stderr, "ATSFontGetHorizontalMetrics %s\n", font_metrics_str(metrics));
#endif
    QDRVFontList[fontCpt].name = name;
    QDRVFontList[fontCpt].metrics = metrics;
    QDRVFontList[fontCpt].font = CGFontCreateWithPlatformFont( (void *) &atsFont );
    QDRVFontList[fontCpt].height = height;
    QDRVFontList[fontCpt].weight = weight;
    fontCpt++;
    
    return fontCpt - 1;
}

ATSFontMetrics QDRV_CGGetFontMetrics(unsigned int fontRef)
{
    return QDRVFontList[fontRef].metrics;
}

int QDRV_CGGetFontHeight(unsigned int fontRef)
{
    return QDRVFontList[fontRef].height;
}

int QDRV_CGGetFontWeight(unsigned int fontRef)
{
    return QDRVFontList[fontRef].weight;
}

static inline void CoordToCGPoint(int x, int y, CGPoint *dest, int height)
{
    dest->y = height - y;
    dest->x = x;
}

static CGColorSpaceRef QuartzGetRGBColorSpace(void)
{
    static CGColorSpaceRef qzRGBColorSpace = NULL;
    if (qzRGBColorSpace == NULL)
    {
        qzRGBColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
    }
    return qzRGBColorSpace;
}

CGContextRef QDRV_BeginDrawing(WindowRef win)
{
    CGContextRef ctx = NULL;

    TRACE("win=%p\n", win);

    wine_quartzdrv_lock();
#ifdef WINE_COCOA

    ctx = Cocoa_GetWindowContextAndHeight(win, &windowHeight);
    CGContextSetFillColorSpace(ctx, QuartzGetRGBColorSpace());
    CGContextSetStrokeColorSpace(ctx, QuartzGetRGBColorSpace());
#else    
    if (win)
    {
        Rect bounds;

        port = CARBON_GetWindowPort(win);
        CARBON_GetPortBounds(port, &bounds);

        windowHeight = bounds.bottom - bounds.top;
        
        contentRgn = CARBON_NewRgn();
        
        QDBeginCGContext(port, &ctx);
                                
        CARBON_GetWindowRegion(win, kWindowContentRgn, contentRgn);
        QDGlobalToLocalRegion(port, contentRgn);
        
        ClipCGContextToRegion(ctx, &bounds, contentRgn);
    }
#endif
    return ctx;
}

void QDRV_EndDrawing(WindowRef win, CGContextRef ctx)
{
#ifdef WINE_COCOA
    TRACE("win=%p ctx=%p\n", win, ctx);
#else
    if (ctx)
    {       
        CGContextSynchronize(ctx);
        QDEndCGContext(port, &ctx);        
    }
    if (contentRgn) CARBON_DisposeRgn(contentRgn);
#endif
    wine_quartzdrv_unlock();
}

void QDRV_GCDrawLine(CGContextRef ctx, int sx, int sy, int ex, int ey, int inWindow)
{
    CGPoint cgCoord;
    int _height = (inWindow) ? windowHeight : CGBitmapContextGetHeight(ctx);

    CGContextSaveGState(ctx);
        CGContextBeginPath(ctx);
        
        CoordToCGPoint(sx, sy, &cgCoord, _height);
        CGContextMoveToPoint(ctx, cgCoord.x, cgCoord.y);
        
        CoordToCGPoint(ex, ey, &cgCoord, _height);
        CGContextAddLineToPoint(ctx, cgCoord.x, cgCoord.y);

        CGContextClosePath(ctx);
        CGContextStrokePath(ctx);
    CGContextRestoreGState(ctx);
}

void QDRV_CGFillPath(CGContextRef ctx, CGPoint *points, int count, int inWindow)
{
    CGPoint cgCoord;
    int i;
    int _height = (inWindow) ? windowHeight : CGBitmapContextGetHeight(ctx);
    TRACE("ctx=%p points=%p count=%d inWindows=%d\n", ctx, points, count, inWindow);
    
    CGContextSaveGState(ctx);

    CGContextBeginPath(ctx);
    
    CoordToCGPoint(points[0].x, points[0].y, &cgCoord, _height);
    CGContextMoveToPoint(ctx, cgCoord.x, cgCoord.y);

    for (i = 1; i < count; i++)
    {
        CoordToCGPoint(points[i].x, points[i].y, &cgCoord, _height);
        CGContextAddLineToPoint(ctx, cgCoord.x, cgCoord.y);
    }
    
    CGContextClosePath(ctx);
    CGContextFillPath(ctx);
    CGContextRestoreGState(ctx);
}

void QDRV_CGStrokePath(CGContextRef ctx, CGPoint *points, int count, int inWindow)
{
    CGPoint cgCoord;
    int i;
    int _height = (inWindow) ? windowHeight : CGBitmapContextGetHeight(ctx);
    TRACE("ctx=%p points=%p count=%d inWindows=%d\n", ctx, points, count, inWindow);
    
    CGContextSaveGState(ctx);
   
    CGContextBeginPath(ctx);
    
    CoordToCGPoint(points[0].x, points[0].y, &cgCoord, _height);
    CGContextMoveToPoint(ctx, cgCoord.x, cgCoord.y);

    for (i = 1; i < count; i++)
    {
        CoordToCGPoint(points[i].x, points[i].y, &cgCoord, _height);
        CGContextAddLineToPoint(ctx, cgCoord.x, cgCoord.y);
    }
    
    CGContextClosePath(ctx);
    CGContextStrokePath(ctx);
    CGContextRestoreGState(ctx);
}

void QDRV_CGDrawRectangle(CGContextRef ctx, int x, int y, int width, int height, int inWindow)
{
    int _height = (inWindow) ? windowHeight : CGBitmapContextGetHeight(ctx);
    CGPoint cgCoord;
    CGRect fillRect;

    CoordToCGPoint(x, y, &cgCoord, _height);
    
    fillRect = CGRectMake(cgCoord.x, cgCoord.y, width, height);

    CGContextSaveGState(ctx);
        CGContextStrokeRect(ctx, fillRect);
    CGContextRestoreGState(ctx);
}

void QDRV_CGFillRectangle(CGContextRef ctx, int x, int y, int width, int height, int inWindow)
{
    int _height = (inWindow) ? windowHeight : CGBitmapContextGetHeight(ctx);
    CGPoint cgCoord;
    CGRect fillRect;

    CoordToCGPoint(x, y, &cgCoord, _height);

    fillRect = CGRectMake(cgCoord.x, cgCoord.y, width, height);

    CGContextSaveGState(ctx);
        CGContextFillRect(ctx, fillRect);
    CGContextRestoreGState(ctx);
}

void QDRV_CGFillRectangles(CGContextRef ctx, CGRect *rects, int count, int inWindow)
{
    int _height = (inWindow) ? windowHeight : CGBitmapContextGetHeight(ctx);
    CGPoint cgCoord;
    CGRect fillRect;
    int i;
    TRACE("ctx=%p rects=%p count=%d inWindow=%d\n", ctx, rects, count, inWindow);

    CGContextSaveGState(ctx);

    for (i = 0; i < count; i++)
    {
        CoordToCGPoint(rects[i].origin.x, rects[i].origin.y, &cgCoord, _height);

        fillRect.origin = cgCoord;
        fillRect.size = rects[i].size;
        CGContextFillRect(ctx, fillRect);
    }
    CGContextRestoreGState(ctx);
}

int QDRV_CGGetTextExtentPoint(unsigned fontRef, char *buffer, int count, int *cx, int *cy)
{
    CGPoint cgPoint;
    int fontHeight = QDRVFontList[fontRef].height;
    
    /* FIXME : set a suitable text encoding (cf CFStringEncodingExt.h ) */
    CFStringRef string = CFStringCreateWithBytes(kCFAllocatorDefault, buffer, count, kCFStringEncodingWindowsLatin1, 0);
    if (string)
    {
        CGContextRef temp;
        int i;
        CGGlyph *glyphs = malloc(sizeof(CGGlyph) * count);
        if (!glyphs)
        {
            CFRelease(string);
            return 0;
        }
        for (i = 0; i < count; i++)
            glyphs[i] = CFStringGetCharacterAtIndex(string, i) - 29;
            
        /* FIXME temporay context calculate more accurate rect */
        temp = QDRV_CreateCGBitmapContext(fontHeight * count * 1.5, fontHeight * 2, 1);
        if (temp)
        {
            CGContextSetShouldAntialias(temp, 0);
            CGContextSetTextDrawingMode(temp, kCGTextInvisible);
            
            CGContextSetFont(temp, QDRVFontList[0].font);
            CGContextSetFontSize(temp, (float) fontHeight);
            
            CGContextSetTextPosition(temp, 0, 0);
            CGContextShowGlyphs(temp, glyphs, count);
            cgPoint = CGContextGetTextPosition(temp);
            *cx = (int) cgPoint.x;
            *cy = (int) fontHeight;
                        
            QDRV_DeleteCGBitmapContext(temp);
        }
        free(glyphs);
        CFRelease(string);
    }

    TRACE("return %d %d\n", *cx, *cy);

    return 1;
}

void QDRV_GCDrawText(CGContextRef ctx, unsigned fontRef, int x, int y, char *text, int count, int inWindow)
{
    int _height = (inWindow) ? windowHeight : CGBitmapContextGetHeight(ctx);
    CGPoint cgCoord;

    /* FIXME : set a suitable text encoding (cf CFStringEncodingExt.h ) */
    CFStringRef string = CFStringCreateWithBytes(kCFAllocatorDefault, text, count, kCFStringEncodingWindowsLatin1, 0);
    TRACE("ctx=%p fontRef=%u x=%d y=%d inWindow=%d\n", ctx, fontRef, x, y, inWindow);

   if (string)
    {
        int i;
        CGGlyph *glyphs = malloc(sizeof(CGGlyph) * count);
        if (!glyphs)
        {
            CFRelease(string);
            return;
        }
        for (i = 0; i < count; i++)
            glyphs[i] = CFStringGetCharacterAtIndex(string, i) - 29;

        CoordToCGPoint(x, y, &cgCoord, _height);
        CGContextSaveGState(ctx);

        /* FIXME call them in QDRV_SetupGCForText ?? 
            This need to redefine text drawing modes in wine_carbon.h */
        CGContextSetShouldAntialias(ctx, 0);
        CGContextSetTextDrawingMode(ctx, kCGTextFill);
        
        CGContextSetFont(ctx, QDRVFontList[fontRef].font);
        CGContextSetFontSize(ctx, (float) QDRVFontList[fontRef].height);
        
        CGContextSetTextPosition(ctx, cgCoord.x, cgCoord.y);
        CGContextShowGlyphs(ctx, glyphs, count);
        
        CGContextRestoreGState(ctx);
        
        free(glyphs);
        CFRelease(string);
    }
}

void QDRV_GCDrawTextUniChar(CGContextRef ctx, unsigned fontRef, int x, int y, UniChar *text, int count, int inWindow)
{
    int _height = (inWindow) ? windowHeight : CGBitmapContextGetHeight(ctx);
    CGPoint cgCoord;
    int i;
    CGGlyph *glyphs;
    
    TRACE("ctx=%p fontRef=%u x=%d y=%d text=%p count=%d inWindow=%d\n", ctx, fontRef, x, y, text, count, inWindow);

    CoordToCGPoint(x, y, &cgCoord, _height);
    
    glyphs = malloc(sizeof(CGGlyph) * count);
    if (!glyphs) return;
    
    for (i = 0; i < count; i++)
        glyphs[i] = text[i] - 29;
    
    CGContextSaveGState(ctx);
    
    CGContextSetShouldAntialias(ctx, 0);
    CGContextSetTextDrawingMode(ctx, kCGTextFill);
    
    CGContextSetFont(ctx, QDRVFontList[fontRef].font);
    CGContextSetFontSize(ctx, (float) QDRVFontList[fontRef].height);
    
    CGContextSetTextPosition(ctx, cgCoord.x, cgCoord.y);
    CGContextShowGlyphs(ctx, glyphs, count);
    
    CGContextRestoreGState(ctx);
    
    free(glyphs);
}


void QDRV_GCDrawBitmap(CGContextRef ctx, CGContextRef bitmap, int srcx, int srcy, int destx, int desty, int width, int height, int inWindow)
{
    int _height = (inWindow) ? windowHeight : 0;
    CGPoint cgCoord;
    CGRect destRect;
    CGImageRef srcImg = CGBitmapContextCreateImage(bitmap);

    TRACE("ctx=%p bitmap=%p srcx=%d srcy=%d destx=%d desty=%d width=%d height=%d inWindow=%d\n", ctx, bitmap, srcx, srcy, destx, desty, width, height, inWindow);

    if (srcx == destx && srcy == desty)
    {
        destRect.origin.x = srcx;
        destRect.origin.y = srcy;
        destRect.size.width = width;
        destRect.size.height = height;
        
        CGContextSaveGState(ctx);
        CGContextDrawImage(ctx, destRect, srcImg);
        CGContextRestoreGState(ctx);        
        CGImageRelease(srcImg);
    }
    else
    {
        CGImageRef toDraw;
        destRect.origin.x = srcx;
        destRect.origin.y = srcy;
        destRect.size.width = width;
        destRect.size.height = height;
            
#ifdef HAVE_CGIMAGECREATEWITHIMAGEINRECT
        toDraw = CGImageCreateWithImageInRect(srcImg, destRect);
        CGImageRelease(srcImg);
#else
        toDraw = srcImg;
#endif
    
        CoordToCGPoint(destx, desty, &cgCoord, _height);
        
        destRect.origin = cgCoord;
        destRect.size.width = width;
        destRect.size.height = height;
        CGContextSaveGState(ctx);
#ifndef HAVE_CGIMAGECREATEWITHIMAGEINRECT
        CGContextClipToRect(ctx, destRect);
#endif
        CGContextDrawImage(ctx, destRect, toDraw);
        CGContextRestoreGState(ctx);
        
        CGImageRelease(toDraw);
    }
}

void QDRV_CGPutPixel_1(CGContextRef ctx, int x, int y, int color)
{
    CGPoint cgCoord;
    CGRect fillRect;
    
    CoordToCGPoint(x, y, &cgCoord, CGBitmapContextGetHeight(ctx));
    
    fillRect = CGRectMake(cgCoord.x, cgCoord.y, 1, 1);
    CGContextSetGrayFillColor(ctx, color, 1.0);

    // CGContextSaveGState(ctx);
        CGContextFillRect(ctx, fillRect);
    // CGContextRestoreGState(ctx);
}

void QDRV_CGPutPixel_RGB(CGContextRef ctx, int x, int y, float r, float g, float b)
{
    CGPoint cgCoord;
    CGRect fillRect;
    
    CoordToCGPoint(x, y, &cgCoord, CGBitmapContextGetHeight(ctx));
    fillRect = CGRectMake(cgCoord.x, cgCoord.y, 1, 1);

    CGContextSetRGBFillColor(ctx, r, g, b, 1.0);
   // CGContextSaveGState(ctx);
        CGContextFillRect(ctx, fillRect);
   // CGContextRestoreGState(ctx);
}

CGImageRef QDRV_CreateCGImageMask(void *data, int width, int height)
{
    CGImageRef mask = NULL;
    size_t bytesPerRow = 0;
    size_t bitsPerComponent = 1;
    size_t bitsPerPixel = 1;
    CGDataProviderRef dataProvider;
    
    TRACE("data=%p w %d h %d ", data, width, height);
    
    bytesPerRow = width / 8;

    dataProvider = CGDataProviderCreateWithData(NULL, data, bytesPerRow * height, NULL);
    if(dataProvider)
    {
        mask = CGImageMaskCreate(width, height, bitsPerComponent, bitsPerPixel, bytesPerRow, dataProvider, 0, 1);
        CGDataProviderRelease(dataProvider);
    }
    
    TRACE("return %p\n", mask);
    return mask;
}
