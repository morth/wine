/*
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

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import "WineView.h"
#import "WineWindow.h"
#import "WineDesktop.h"

#include <dlfcn.h>
//#include "wine_carbon.h"
typedef void * AKObjectRef;

#define TRACE

extern unsigned int screen_width;
extern unsigned int screen_height;
extern AKObjectRef root_window;
extern char *QDRV_get_process_name(void);

@interface WineAppDelegate: NSObject
{
}
@end

static WineAppDelegate *wineDelegate = nil;

static NSAutoreleasePool *pool = nil;


void QDRV_SetWindowFrame(WindowRef win, int x, int y, int width, int height)
{
#ifdef TRACE
    fprintf (stderr, "%s win=%p [%d %d] [%d %d]\n", __FUNCTION__, win, x, y, width, height);
#endif
    y = (screen_height - y) - height;
    
    [(WineWindow *) win setFrame: NSMakeRect(x, y, width, height) display: YES];
}

static void QDRV_SetMenu(void)
{
    NSMenu *menu;
    NSMenuItem *item;
    NSString *appName = [[NSString alloc] initWithFormat: @"%s", QDRV_get_process_name()];
    
    [[NSApp mainMenu] removeItemAtIndex: 0];
    
    /* Recreate our menu */
    menu = [[NSMenu alloc] initWithTitle: appName];
    [menu addItemWithTitle: [@"Quit " stringByAppendingString: appName] action: @selector(terminate:) keyEquivalent: @"q"];

    item = [[NSMenuItem alloc] initWithTitle:@"" action: nil keyEquivalent: @""];
    [item setSubmenu: menu];
    [[NSApp mainMenu] insertItem: item atIndex: 0];

    [menu release];
    [item release];
}

WindowRef QDRV_CreateNewWindow(int x, int y, int width, int height)
{
#ifdef TRACE
    fprintf (stderr, "QDRV_CreateNewWindow stub: x %d y %d w %d h %d\n", x, y, width, height);
#endif
    y = (screen_height - y) - height;
    
    /* TODO [WineWindow initWithContentRect: HWND: hwnd]
    */
    WineWindow *win = [[WineWindow alloc] initWithContentRect:NSMakeRect(x, y, width, height) styleMask: NSBorderlessWindowMask 
													 backing: NSBackingStoreBuffered
													   defer: NO];
#ifdef TRACE
    fprintf (stderr, "%s win=%p\n", __FUNCTION__, win);
#endif
    return (WindowRef) win;
}

WindowRef Cocoa_CreateDesktopWindow(int x, int y, int width, int height)
{
    /* FIXME */
#ifdef TRACE
    fprintf (stderr, "%s stub: x %d y %d w %d h %d\n", __FUNCTION__, x, y, width, height);
#endif

    return (WindowRef) [[WineDesktop desktop] window];
}

WindowRef Cocoa_GetDesktopWin(void)
{
    return (WindowRef) [[WineDesktop desktop] window];
}

void Cocoa_DestroyWindow(WindowRef win)
{
    WineWindow *_win = (WineWindow *) win;
    [_win orderOut: nil];
    [_win release];
}

void QDRV_SetWindowTitle(AKObjectRef win, char *text)
{
#pragma unused(win, text)
}

int QDRV_AppKitGetEvent(void)
{
    NSEvent *event;
    int y;
    NSPoint location;
    int ret = 0;

    event = [NSApp nextEventMatchingMask: NSAnyEventMask untilDate: [NSDate distantPast] inMode: NSDefaultRunLoopMode dequeue:YES];
    if (event) switch ([event type]) {
        case NSMouseMoved: /* received a mouse moved event so not handle by any view */
#ifdef TRACE
            fprintf (stderr, "%s get event %s\n", __FUNCTION__, [[event description] UTF8String]);
#endif
            location = [NSEvent mouseLocation];

            y = screen_height - ((int) location.y);
            QDRV_MotionNotify(root_window, (int) location.x, y);            
            break;

        case NSOtherMouseDown:
        case NSRightMouseDown:
#ifdef TRACE
            fprintf (stderr, "%s get event %s\n", __FUNCTION__, [[event description] UTF8String]);
#endif      
            location = [NSEvent mouseLocation];
            y = screen_height - ((int) location.y);
            
            QDRV_ButtonPress((WindowRef) [event window], (int) location.x, y, [event buttonNumber]);
            break;

        case NSOtherMouseUp:
        case NSRightMouseUp:
#ifdef TRACE
            fprintf (stderr, "%s get event %s\n", __FUNCTION__, [[event description] UTF8String]);
#endif      
            location = [NSEvent mouseLocation];
            y = screen_height - ((int) location.y);
            
            QDRV_ButtonRelease((WindowRef) [event window], (int) location.x, y, [event buttonNumber]);
            break;

        case NSLeftMouseDown:
        case NSLeftMouseUp:

        case NSKeyDown:
        case NSKeyUp:
        case NSScrollWheel:

        case NSOtherMouseDragged:
        case NSRightMouseDragged:
        case NSLeftMouseDragged:

        case NSSystemDefined:

        default: 
#ifdef TRACE
            fprintf (stderr, "%s get event %s\n", __FUNCTION__, [[event description] UTF8String]);
#endif
            [NSApp sendEvent:event];
            break;
    }
    ret = (event)?1:0;

    /* flush our pool */
    [pool release];
    pool = [[NSAutoreleasePool alloc] init];
    
    return ret;
}

void QDRV_InitializeCarbon(void)
{
    ProcessSerialNumber psn;

    pool = [[NSAutoreleasePool alloc] init];
    
    if (NSApplicationLoad())
    {
        GetProcessForPID(getpid(), &psn);
        TransformProcessType(&psn, kProcessTransformToForegroundApplication);
        SetFrontProcess(&psn);
        
        wineDelegate = [[WineAppDelegate alloc] init];
        [NSApp setDelegate: wineDelegate];
        [NSApp finishLaunching];
        /* FIXME */
        /* Loading a fake nib menu make the "magics" to deal with menu bar, heuu sometimes */
        [NSBundle loadNibNamed: @"MainMenu" owner: NSApp];
        QDRV_SetMenu(); 
        root_window = [[WineDesktop desktop] window];
    }
}

void QDRV_FinalizeCarbon(void)
{
   [pool release];
}

void CARBON_ShowWindow(WindowRef w)
{
    WineWindow *win = (WineWindow *) w;
    [win makeKeyAndOrderFront:nil];
}

void CARBON_HideWindow(WindowRef w)
{
    WineWindow *win = (WineWindow *) w;
    [win orderOut: win];
}

CGContextRef Cocoa_GetWindowContextAndHeight(WindowRef win, int *height)
{
    WineWindow *_win = (WineWindow *) win;
    
    *height = (int) [_win frame].size.height;
    return [(WineView *) [_win contentView] layer_ctx];
}

void Cocoa_NeedDisplay(WindowRef win)
{
    [(WineView *) [(WineWindow *) win contentView] setNeedsDisplay: YES];
}

void Cocoa_SetCursor(AKObjectRef oldCursor, CGImageRef image, int hot_x, int hot_y)
{
    NSImage *nsImage = nil;
    NSRect imageRect = NSMakeRect(0.0, 0.0, 32.0, 32.0); /* FIXME */
    CGContextRef imageContext = NULL;
    NSCursor *cursor;
    
#ifdef TRACE
    fprintf (stderr, "fixme:winequartzdrv:Cocoa_SetCursor oldCursor=%p image=%p\n", oldCursor, image);
#endif
    if (image)
    {        
        nsImage = [[NSImage alloc] initWithSize: imageRect.size]; 
        [nsImage lockFocus];
            imageContext = (CGContextRef) [[NSGraphicsContext currentContext] graphicsPort];
        
            [[NSColor clearColor] set];
            NSRectFill(imageRect);
            CGContextDrawImage(imageContext, *(CGRect*)&imageRect, image);
        [nsImage unlockFocus];
        
        cursor = [[NSCursor alloc] initWithImage: nsImage hotSpot: NSMakePoint(hot_x, hot_y)];
        if (cursor)
        {
            if (oldCursor) [(NSCursor *) oldCursor release];
            [cursor set];
            oldCursor = (AKObjectRef) cursor;
        }
        [nsImage release];
    }
    else
    {
        [[NSCursor arrowCursor] set];
        if (oldCursor)
        {
            [(NSCursor *) oldCursor release];
            oldCursor = 0;
        }
    }
}

void Cocoa_GetMousePos(int *x, int *y)
{
    NSPoint location = [NSEvent mouseLocation];
    *x = (int) location.x;
    *y = screen_height - ((int) location.y);
}

void QDRV_SetDockAppIcon(CGImageRef icon)
{
    if (icon)
    {
        NSImage *image = nil;
        NSRect imageRect = NSMakeRect(0.0, 0.0, CGImageGetWidth(icon), CGImageGetHeight(icon));
        CGContextRef imageContext = NULL;
    
        image = [[NSImage alloc] initWithSize: imageRect.size]; 
        [image lockFocus];
            imageContext = (CGContextRef) [[NSGraphicsContext currentContext] graphicsPort];
            CGContextDrawImage(imageContext, *(CGRect*)&imageRect, icon);        
        [image unlockFocus];
        
        [NSApp setApplicationIconImage: image];
        
        [image release];
    }
}

#define TRACE_METHODE() fprintf(stderr, "-[%s %s]\n", [[self className] UTF8String], _cmd)

@implementation WineAppDelegate
- (id) init
{
    return (self = [super init]);
}

- (void) applicationWillBecomeActive: (NSNotification *) notif
{
    TRACE_METHODE();
}

- (void) applicationWillHide: (NSNotification *) notif
{
    TRACE_METHODE();
}
@end
