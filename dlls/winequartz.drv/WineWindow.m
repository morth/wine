/*
 *  WineWindow.m
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

#import "WineWindow.h"
#import "WineView.h"

#define TRACE_METHODE() fprintf(stderr, "-[%s %s]\n", [[self className] UTF8String], _cmd)

@implementation WineWindow

- (id) initWithContentRect: (NSRect) rect HWND: (HWND *) hwnd
{
    if ( (self = [self initWithContentRect: rect styleMask:NSBorderlessWindowMask backing: NSBackingStoreBuffered defer: NO]) )
        _hwnd = hwnd;
    else return nil;
    
    return self;
}

- (id) initWithContentRect: (NSRect) contentRect styleMask: (unsigned int) styleMask backing: (NSBackingStoreType) bufferingType defer: (BOOL) defer
{
    if ( (self = [super initWithContentRect: contentRect styleMask: styleMask backing: bufferingType defer: defer]) )
    {
        WineView *wv = [[WineView alloc] initWithFrame: contentRect];
        
        if (wv == nil)
        {
            [self release];
            return nil;
        }
        
        [self setContentView: wv];
        [self makeFirstResponder: wv];
        [self setHasShadow: YES];
    }
    return self;
}

- (BOOL) canBecomeKeyWindow
{
    return YES;
}
@end
