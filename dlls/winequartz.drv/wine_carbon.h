/*
 * Quartz driver Wine/Carbon bridge header
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

#ifndef __WINE_CARBON_H
#define __WINE_CARBON_H

#define XP_NO_X_HEADERS
#include <Xplugin.h>

#ifdef WINE_SIDE
typedef void * CGContextRef;
typedef void * CGDirectDisplayID;
typedef void * CGDirectPaletteRef;
typedef void * CGImageRef;

typedef void * WindowRef;
typedef void * EventRef;

typedef unsigned short UniChar;

typedef struct 
{
    short v;
    short h;
} CARBONPOINT;

/*
    Redefine CoreGraphics enums
*/

enum CGLineJoin {
    kCGLineJoinMiter,
    kCGLineJoinRound,
    kCGLineJoinBevel
};
typedef enum CGLineJoin CGLineJoin;

/* Line cap styles. */

enum CGLineCap {
    kCGLineCapButt,
    kCGLineCapRound,
    kCGLineCapSquare
};
typedef enum CGLineCap CGLineCap;

/* Drawing modes for paths. */

enum CGPathDrawingMode {
    kCGPathFill,
    kCGPathEOFill,
    kCGPathStroke,
    kCGPathFillStroke,
    kCGPathEOFillStroke
};
typedef enum CGPathDrawingMode CGPathDrawingMode;

/* Drawing modes for text. */

enum CGTextDrawingMode {
    kCGTextFill,
    kCGTextStroke,
    kCGTextFillStroke,
    kCGTextInvisible,
    kCGTextFillClip,
    kCGTextStrokeClip,
    kCGTextFillStrokeClip,
    kCGTextClip
};
typedef enum CGTextDrawingMode CGTextDrawingMode;

struct ATSFontMetrics {
  DWORD version;
  FLOAT ascent;                 /* Maximum height above baseline reached by the glyphs in the font */
                                              /* or maximum distance to the right of the centerline reached by the glyphs in the font */
  FLOAT descent;                /* Maximum depth below baseline reached by the glyphs in the font */
                                              /* or maximum distance to the left of the centerline reached by the glyphs in the font */
  FLOAT leading;                /* Desired spacing between lines of text */
  FLOAT avgAdvanceWidth;
  FLOAT maxAdvanceWidth;        /* Maximum advance width or height of the glyphs in the font */
  FLOAT minLeftSideBearing;     /* Minimum left or top side bearing */
  FLOAT minRightSideBearing;    /* Minimum right or bottom side bearing */
  FLOAT stemWidth;              /* Width of the dominant vertical stems of the glyphs in the font */
  FLOAT stemHeight;             /* Vertical width of the dominant horizontal stems of glyphs in the font */
  FLOAT capHeight;              /* Height of a capital letter from the baseline to the top of the letter */
  FLOAT xHeight;                /* Height of lowercase characters in a font, specifically the letter x, excluding ascenders and descenders */
  FLOAT italicAngle;            /* Angle in degrees counterclockwise from the vertical of the dominant vertical strokes of the glyphs in the font */
  FLOAT underlinePosition;      /* Distance from the baseline for positioning underlining strokes */
  FLOAT underlineThickness;     /* Stroke width for underlining */
};
typedef struct ATSFontMetrics ATSFontMetrics;


struct CGPoint {
    float x;
    float y;
};
typedef struct CGPoint CGPoint;

/* Sizes. */

struct CGSize {
    float width;
    float height;
};
typedef struct CGSize CGSize;

/* Rectangles. */

struct CGRect {
    CGPoint origin;
    CGSize size;
};
typedef struct CGRect CGRect;

extern void *CGBitmapContextGetData(CGContextRef );
extern size_t CGBitmapContextGetBytesPerRow(CGContextRef );
extern CGImageRef CGBitmapContextCreateImage(CGContextRef );
extern void CGContextClearRect(CGContextRef, CGRect );
extern void CGContextSaveGState(CGContextRef );
extern void CGContextRestoreGState(CGContextRef );

extern void CGImageRelease(CGImageRef );
extern CGImageRef CGImageCreateWithMask(CGImageRef , CGImageRef );

extern void CGContextSetRGBFillColor(CGContextRef c, float r, float g, float b, float a);
extern void CGContextSetRGBStrokeColor(CGContextRef c, float r, float g, float b, float a);

extern CGRect CGDisplayBounds(INT);
#else

#ifndef WINE_COCOA
extern OSStatus CARBON_MoveWindowStructure(WindowRef w, short x, short y);
extern void CARBON_SetRect(Rect *r, short x, short y, short width, short height);
extern void CARBON_SizeWindow(WindowRef window, short w, short h, Boolean fUpdate);
extern void CARBON_DisposeRgn(RgnHandle rgn);
extern Rect *CARBON_GetPortBounds(CGrafPtr port, Rect *rect);
extern CGrafPtr CARBON_GetWindowPort(WindowRef w);
extern OSStatus CARBON_GetWindowRegion(WindowRef window, WindowRegionCode inRegionCode, RgnHandle ioWinRgn);
extern RgnHandle CARBON_NewRgn(void);
#endif

#endif /* WINE_SIDE */

typedef void * AKObjectRef; /* fake NSObject */

extern void CARBON_ShowWindow(WindowRef w);
extern void CARBON_HideWindow(WindowRef r);

#ifdef WINE_COCOA
//extern WindowRef QDRV_CreateNewWindow(HWND hwnd, int x, int y, int width, int height);

extern void Cocoa_GetMousePos(int *x, int *y);
extern void Cocoa_SetCursor(AKObjectRef oldCursor, CGContextRef ctx, int hot_x, int hot_y);
extern CGContextRef Cocoa_GetWindowContextAndHeight(WindowRef win, int *height);
extern void Cocoa_NeedDisplay(WindowRef win);
extern void Cocoa_DestroyWindow(WindowRef win);
#endif

extern void QDRV_InitializeCarbon(void);
extern void QDRV_FinalizeCarbon(void);

extern CGContextRef QDRV_BeginDrawing(WindowRef win);
extern void QDRV_EndDrawing(WindowRef win, CGContextRef ctx);
extern void QDRV_GCDrawLine(CGContextRef ctx, int sx, int sy, int ex, int ey, int inWindow);

extern void QDRV_GCDrawText(CGContextRef ctx, unsigned fontRef, int x, int y, char *text, int count, int inWindow);
extern void QDRV_GCDrawTextUniChar(CGContextRef ctx, unsigned fontRef, int x, int y, UniChar *text, int count, int inWindow);

extern void QDRV_CGFillRectangle(CGContextRef ctx, int x, int y, int width, int height, int inWindow);
extern void QDRV_GCDrawBitmap(CGContextRef ctx, CGContextRef bitmap, int srcx, int srcy, int destx, int desty, int width, int height, int inWindow);
extern void QDRV_CGFillRectangles(CGContextRef ctx, CGRect *rects, int count, int inWindow);

/*
 *  QDRV_CGPutPixel_1 and QDRV_CGPutPixel_RGB always flip coordinates
 */
extern void QDRV_CGPutPixel_1(CGContextRef ctx, int x, int y, int color);
extern void QDRV_CGPutPixel_RGB(CGContextRef ctx, int x, int y, float r, float g, float b);

extern CGImageRef QDRV_CreateCGImageMask(void *data, int width, int height);

extern CGContextRef QDRV_CreateCGBitmapContext(int width, int height, int depth);
extern void QDRV_DeleteCGBitmapContext(CGContextRef ctx);

extern ATSFontMetrics QDRV_CGGetFontMetrics(unsigned int fontRef);
extern int QDRV_CGGetTextExtentPoint(unsigned fontRef, char *buffer, int count, int *cx, int *cy);

extern WindowRef QDRV_CreateNewWindow(int x, int y, int width, int height);
extern void QDRV_SetWindowFrame(WindowRef win, int x, int y, int width, int height);
extern void QDRV_Map(WindowRef window);

#define kLeftButton     0
#define kMidButton      1
#define kRightButton    2
extern void QDRV_ButtonPress(WindowRef window, int x, int y, int button);
extern void QDRV_ButtonRelease(WindowRef window, int x, int y, int button);
extern void QDRV_MotionNotify(WindowRef window, int x, int y);

#define kKeyPress   1
#define kKeyRelease 0
extern void QDRV_KeyEvent(int pressed, int keyCode, UniChar c, unsigned int modifier);

extern unsigned int QRDV_SelectATSFont(char *facename, int height, int weigth);
extern int QDRV_CGGetFontWeight(unsigned int fontRef);
extern int QDRV_CGGetFontHeight(unsigned int fontRef);

extern void QDRV_SetDockAppIcon(CGImageRef icon);
#endif  /* __WINE_CARBON_H */
