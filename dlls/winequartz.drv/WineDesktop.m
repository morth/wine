/*
 *  WineDesktop.m
 *
 *  Created by Emmanuel Maillard on 17/06/06.
 *  Copyright 2006 Emmanuel Maillard. All rights reserved.
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

#import "WineDesktop.h"

@implementation WineDesktop
- (id) init
{
    if ( (self = [super init]) )
    {
        NSRect rect = NSMakeRect(0, 0, 
                                    CGDisplayPixelsWide(CGMainDisplayID()),
                                    CGDisplayPixelsHigh(CGMainDisplayID()) );
        _window = [[WineWindow alloc] initWithContentRect: rect styleMask:NSBorderlessWindowMask backing: NSBackingStoreBuffered defer: NO];
    
        if (!_window)
        {
            [self release];
            return nil;
        }
        
        [_window setBackgroundColor: [NSColor clearColor]];
        [_window setAlphaValue: 1.0];
        [_window setOpaque: NO];
        
        [_window orderFront: nil];
    }
    return self;
}

+ (WineDesktop *) desktop
{
    static WineDesktop *_kWineDesktop = nil;
    
    if (_kWineDesktop == nil)
        _kWineDesktop = [[WineDesktop alloc] init];
        
    return _kWineDesktop;
}

- (WineWindow *) window
{
    return _window;
}
@end
