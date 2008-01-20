/*
 * X11DRV pen objects
 *
 * Copyright 1993 Alexandre Julliard
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
#include <stdarg.h>

#include "wine/debug.h"
#include "quartzdrv.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartzdrv);

static const char PEN_dash[]       = { 16,8 };
static const char PEN_dot[]        = { 4,4 };
static const char PEN_dashdot[]    = { 12,8,4,8 };
static const char PEN_dashdotdot[] = { 12,4,4,4,4,4 };
static const char PEN_alternate[]  = { 1,1 };

/***********************************************************************
*           SelectPen   (X11DRV.@)
*/
HPEN QDRV_SelectPen( QDRV_PDEVICE *physDev, HPEN hpen )
{
    LOGPEN logpen;
    
    if (!GetObjectA( hpen, sizeof(logpen), &logpen )) return 0;
    
    TRACE("\n");
    
    physDev->pen.style = logpen.lopnStyle & PS_STYLE_MASK;
    physDev->pen.type = logpen.lopnStyle & PS_TYPE_MASK;
    physDev->pen.endcap = logpen.lopnStyle & PS_ENDCAP_MASK;
    physDev->pen.linejoin = logpen.lopnStyle & PS_JOIN_MASK;
    
    physDev->pen.width = logpen.lopnWidth.x;
    if ((logpen.lopnStyle & PS_GEOMETRIC) || (physDev->pen.width > 1))
    {
        physDev->pen.width = QDRV_XWStoDS( physDev, physDev->pen.width );
        if (physDev->pen.width < 0) physDev->pen.width = -physDev->pen.width;
    }

    if (hpen == GetStockObject( DC_PEN ))
        logpen.lopnColor = GetDCPenColor( physDev->hdc );
    physDev->pen.pixel = QDRV_PALETTE_ToPhysical( physDev, logpen.lopnColor );
    
    switch(logpen.lopnStyle & PS_STYLE_MASK)
    {
        case PS_DASH:
            physDev->pen.dashes = (char *)PEN_dash;
            physDev->pen.dash_len = sizeof(PEN_dash)/sizeof(*PEN_dash);
            break;
        case PS_DOT:
            physDev->pen.dashes = (char *)PEN_dot;
            physDev->pen.dash_len = sizeof(PEN_dot)/sizeof(*PEN_dot);
            break;
        case PS_DASHDOT:
            physDev->pen.dashes = (char *)PEN_dashdot;
            physDev->pen.dash_len = sizeof(PEN_dashdot)/sizeof(*PEN_dashdot);
            break;
        case PS_DASHDOTDOT:
            physDev->pen.dashes = (char *)PEN_dashdotdot;
            physDev->pen.dash_len = sizeof(PEN_dashdotdot)/sizeof(*PEN_dashdotdot);
            break;
        case PS_ALTERNATE:
            physDev->pen.dashes = (char *)PEN_alternate;
            physDev->pen.dash_len = sizeof(PEN_alternate)/sizeof(*PEN_alternate);
            break;
        case PS_USERSTYLE:
            FIXME("PS_USERSTYLE is not supported\n");
            break;
    }
    return hpen;
}


/***********************************************************************
*           SetDCPenColor (X11DRV.@)
*/
COLORREF QDRV_SetDCPenColor( QDRV_PDEVICE *physDev, COLORREF crColor )
{
    TRACE("physDev=%p crColor=0x%08x\n", physDev, (unsigned) crColor);
    if (GetCurrentObject(physDev->hdc, OBJ_PEN) == GetStockObject( DC_PEN ))
        physDev->pen.pixel = QDRV_PALETTE_ToPhysical( physDev, crColor );
    
    return crColor;
}
