/*
 *  WineView.m
 *  
 *
 *  Created by Emmanuel Maillard on 10/05/06.
 *  Copyright 2006 Emmanuel Maillard.
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

#import "WineView.h"

#ifndef WINE_COCOA
#define WINE_COCOA
#endif
#include "wine_carbon.h"

#define TRACE_METHODE() fprintf(stderr, "-[%s %s]\n", [[self className] UTF8String], _cmd)
#define TRACE_NSRECT(r) fprintf(stderr, "\t[%f %f] [%f %f]\n", r.origin.x, r.origin.y, r.size.width, r.size.height)
extern unsigned int screen_height;

@implementation WineView
- (id) initWithFrame: (NSRect) rect
{
    if ( (self = [super initWithFrame: rect]) )
    {    
        _layer = NULL;
        [self setAutoresizingMask: (NSViewHeightSizable | NSViewWidthSizable)];
        [self setAutoresizesSubviews: NO];
        [self setPostsBoundsChangedNotifications: NO];
        [self setPostsFrameChangedNotifications: NO];
    }
    return self;
}


- (void) setFrameSize: (NSSize) size
{      
    if (_layer)
    {
        CGContextRef ctx = (CGContextRef) [[NSGraphicsContext currentContext] graphicsPort];

#ifdef HAVE_CGLAYER
        CGLayerRelease(_layer);
        _layer = CGLayerCreateWithContext(ctx, *(CGSize *)&size, NULL);
#else
        QDRV_DeleteCGBitmapContext(_layer);
        _layer = QDRV_CreateCGBitmapContext(size.width, size.height, 24);
#endif
    }
}

- (void) drawRect: (NSRect) rect
{
    CGContextRef ctx = (CGContextRef) [[NSGraphicsContext currentContext] graphicsPort];
    CGSize size = CGSizeMake(rect.size.width, rect.size.height);
        
    if (!_layer)
    {
#ifdef HAVE_CGLAYER
        _layer = CGLayerCreateWithContext(ctx, size, NULL);
#else
        _layer = QDRV_CreateCGBitmapContext(size.width, size.height, 24);
#endif
    }
    else
    {
#ifdef HAVE_CGLAYER
        CGContextDrawLayerAtPoint(ctx, CGPointMake(0,0), _layer);
#else
        QDRV_GCDrawBitmap(ctx, _layer, rect.origin.x, rect.origin.y, rect.origin.x, rect.origin.y, rect.size.width, rect.size.height, 1);
#endif
    }
}

#ifdef HAVE_CGLAYER
- (CGLayerRef) layer
#else
- (CGContextRef) layer
#endif
{
    return _layer;
}

- (CGContextRef) layer_ctx
{
#ifdef HAVE_CGLAYER
    return CGLayerGetContext(_layer);
#else
    return _layer;
#endif
}

- (BOOL) acceptsFirstMouse: (NSEvent *)theEvent
{
    return YES;
}

- (BOOL) acceptsFirstResponder
{
    return YES;
}

- (void) keyDown: (NSEvent *)  event
{
    QDRV_KeyEvent(kKeyPress, [event keyCode], [[event charactersIgnoringModifiers] characterAtIndex: 0], [event modifierFlags]);
}

- (void) keyUp: (NSEvent *) event
{
    QDRV_KeyEvent(kKeyRelease, [event keyCode], [[event charactersIgnoringModifiers] characterAtIndex: 0], [event modifierFlags]);
}

- (void) mouseDown: (NSEvent *)  event
{
    NSPoint location = [NSEvent mouseLocation];
    int y = screen_height - ((int) location.y);

    QDRV_ButtonPress((WindowRef) [self window], (int) location.x, y, [event buttonNumber]);
}

- (void) mouseUp: (NSEvent *) event
{
    NSPoint location = [NSEvent mouseLocation];
    int y = screen_height - ((int) location.y);
    
    QDRV_ButtonRelease((WindowRef) [self window], (int) location.x, y, [event buttonNumber]);
}

- (void) mouseDragged: (NSEvent *) event
{
    NSPoint location = [NSEvent mouseLocation];
    int y = screen_height - ((int) location.y);

    QDRV_MotionNotify((WindowRef) [self window], (int) location.x, (int) y);
}

- (void) mouseMoved: (NSEvent *) event
{
    NSPoint location = [NSEvent mouseLocation];
    int y = screen_height - ((int) location.y);

    QDRV_MotionNotify((WindowRef) [self window], (int) location.x, (int) y);
}

@end
