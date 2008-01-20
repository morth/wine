/*
 * QUARTZ graphics driver initialisation functions
 *
 * Copyright 1996 Alexandre Julliard
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
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "wine/debug.h"
#include "quartzdrv.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartzdrv);

/* a few dynamic device caps */
static int log_pixels_x;  /* pixels per logical inch in x direction */
static int log_pixels_y;  /* pixels per logical inch in y direction */
static int horz_size;     /* horz. size of screen in millimeters */
static int vert_size;     /* vert. size of screen in millimeters */
static int palette_size;
unsigned int text_caps = (TC_OP_CHARACTER | TC_OP_STROKE | TC_CP_STROKE |
                          TC_CR_ANY | TC_SA_DOUBLE | TC_SA_INTEGER |
                          TC_SA_CONTIN | TC_UA_ABLE | TC_SO_ABLE | TC_RA_ABLE);
                          /* QUARTZR6 adds TC_SF_X_YINDEP, Xrender adds TC_VA_ABLE */
static BOOL device_init_done = FALSE;

static const WCHAR dpi_key_name[] = {'S','o','f','t','w','a','r','e','\\','F','o','n','t','s','\0'};
static const WCHAR dpi_value_name[] = {'L','o','g','P','i','x','e','l','s','\0'};

static const WCHAR INIFontSection[] = {'S','o','f','t','w','a','r','e','\\','W','i','n','e','\\',
                                       'W','i','n','e','\\','C','o','n','f','i','g','\\',
                                       'f','o','n','t','s','\0'};
static const WCHAR INIResolution[] = {'R','e','s','o','l','u','t','i','o','n','\0'};


/******************************************************************************
 *      get_dpi
 *
 * get the dpi from the registry
 */
static DWORD get_dpi( void )
{
    DWORD dpi = 96;
    HKEY hkey;

    if (RegOpenKeyW(HKEY_CURRENT_CONFIG, dpi_key_name, &hkey) == ERROR_SUCCESS)
    {
        DWORD type, size, new_dpi;

        size = sizeof(new_dpi);
        if(RegQueryValueExW(hkey, dpi_value_name, NULL, &type, (void *)&new_dpi, &size) == ERROR_SUCCESS)
        {
            if(type == REG_DWORD && new_dpi != 0)
                dpi = new_dpi;
        }
        RegCloseKey(hkey);
    }
    return dpi;
}

/**********************************************************************
 *	     device_init
 *
 * Perform initializations needed upon creation of the first device.
 */
static void device_init(void)
{
    device_init_done = TRUE;

    palette_size = QDRV_PALETTE_Init();

    QDRV_BITMAP_Init();

    /* Initialize device caps */
    log_pixels_x = log_pixels_y = get_dpi();
    horz_size = MulDiv( screen_width, 254, log_pixels_x * 10 );
    vert_size = MulDiv( screen_height, 254, log_pixels_y * 10 );

    /* Initialize fonts and text caps */
  //  X11DRV_FONT_Init(log_pixels_x, log_pixels_y);
  //  QDRV_CGInitFont();
}

/**********************************************************************
 *	     QUARTZDRV_CreateDC
 */
BOOL QDRV_CreateDC( HDC hdc, QDRV_PDEVICE **pdev, LPCWSTR driver, LPCWSTR device,
                      LPCWSTR output, const DEVMODEW* initData )
{
    QDRV_PDEVICE *physDev;
    if (!device_init_done) device_init();
    
    physDev = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*physDev) );
    if (!physDev) return FALSE;
    
    *pdev = physDev;    
    physDev->hdc = hdc;
    
    TRACE("physDev=%p hdc=%p\n", physDev, hdc);
    
    if (GetObjectType( hdc ) == OBJ_MEMDC)
    {
        if (!BITMAP_stock_phys_bitmap.hbitmap)
            BITMAP_stock_phys_bitmap.hbitmap = GetCurrentObject( hdc, OBJ_BITMAP );
                    
        physDev->bitmap    = &BITMAP_stock_phys_bitmap;
        physDev->drawable  = BITMAP_stock_phys_bitmap.context;
        physDev->depth     = 1;
    }
    else
    {
        physDev->bitmap    = NULL;
        physDev->drawable  = root_window;
        physDev->depth     = screen_depth;
    }
    physDev->region = CreateRectRgn( 0, 0, 0, 0 );
    
    return TRUE;
}


/**********************************************************************
 *	     QUARTZDRV_DeleteDC
 */
BOOL QDRV_DeleteDC(QDRV_PDEVICE *physDev )
{
    TRACE("physDev=%p\n", physDev);
    DeleteObject( physDev->region );

    HeapFree( GetProcessHeap(), 0, physDev );
    return TRUE;
}


/***********************************************************************
 *           GetDeviceCaps    (QUARTZDRV.@)
 */
INT QDRV_GetDeviceCaps( QDRV_PDEVICE *physDev, INT cap )
{
    switch(cap)
    {
    case DRIVERVERSION:
        return 0x300;
    case TECHNOLOGY:
        return DT_RASDISPLAY;
    case HORZSIZE:
        return horz_size;
    case VERTSIZE:
        return vert_size;
    case HORZRES:
        return screen_width;
    case VERTRES:
        return screen_height;
    case BITSPIXEL:
        return screen_depth;
    case PLANES:
        return 1;
    case NUMBRUSHES:
        return -1;
    case NUMPENS:
        return -1;
    case NUMMARKERS:
        return 0;
    case NUMFONTS:
        return 0;
    case NUMCOLORS:
        /* MSDN: Number of entries in the device's color table, if the device has
         * a color depth of no more than 8 bits per pixel.For devices with greater
         * color depths, -1 is returned. */
        return (screen_depth > 8) ? -1 : (1 << screen_depth);
    case PDEVICESIZE:
        return sizeof(QDRV_PDEVICE);
    case CURVECAPS:
        return (CC_CIRCLES | CC_PIE | CC_CHORD | CC_ELLIPSES | CC_WIDE |
                CC_STYLED | CC_WIDESTYLED | CC_INTERIORS | CC_ROUNDRECT);
    case LINECAPS:
        return (LC_POLYLINE | LC_MARKER | LC_POLYMARKER | LC_WIDE |
                LC_STYLED | LC_WIDESTYLED | LC_INTERIORS);
    case POLYGONALCAPS:
        return (PC_POLYGON | PC_RECTANGLE | PC_WINDPOLYGON | PC_SCANLINE |
                PC_WIDE | PC_STYLED | PC_WIDESTYLED | PC_INTERIORS);
    case TEXTCAPS:
        return text_caps;
    case CLIPCAPS:
        return CP_REGION;
    case RASTERCAPS:
        return (RC_BITBLT | RC_BANDING | RC_SCALING | RC_BITMAP64 | RC_DI_BITMAP |
                RC_DIBTODEV | RC_BIGFONT | RC_STRETCHBLT | RC_STRETCHDIB | RC_DEVBITS |
                (palette_size ? RC_PALETTE : 0));
    case ASPECTX:
    case ASPECTY:
        return 36;
    case ASPECTXY:
        return 51;
    case LOGPIXELSX:
        return log_pixels_x;
    case LOGPIXELSY:
        return log_pixels_y;
    case CAPS1:
        FIXME("(%p): CAPS1 is unimplemented, will return 0\n", physDev->hdc );
        /* please see wingdi.h for the possible bit-flag values that need
           to be returned. also, see
           http://msdn.microsoft.com/library/ddkdoc/win95ddk/graphcnt_1m0p.htm */
        return 0;
    case SIZEPALETTE:
        return palette_size;
    case NUMRESERVED:
    case COLORRES:
    case PHYSICALWIDTH:
    case PHYSICALHEIGHT:
    case PHYSICALOFFSETX:
    case PHYSICALOFFSETY:
    case SCALINGFACTORX:
    case SCALINGFACTORY:
    case VREFRESH:
    case DESKTOPVERTRES:
    case DESKTOPHORZRES:
    case BLTALIGNMENT:
        return 0;
    default:
        FIXME("(%p): unsupported capability %d, will return 0\n", physDev->hdc, cap );
        return 0;
    }
}

/**********************************************************************
 *           ExtEscape  (X11DRV.@)
 */
INT QDRV_ExtEscape( QDRV_PDEVICE *physDev, INT escape, INT in_count, LPCVOID in_data,
                      INT out_count, LPVOID out_data )
{
    FIXME("semi-stub: physDev=%p escape=0x%08x in_count=%d in_data=%p out_count=%d out_data=%p\n", physDev, escape,
                    in_count, in_data, out_count, out_data);
    switch(escape)
    {
    case QUERYESCSUPPORT:
        if (in_data)
        {
            switch (*(const INT *)in_data)
            {
            case DCICOMMAND:
                return DD_HAL_VERSION;
            case QDRV_ESCAPE:
                return TRUE;
            }
        }
        break;

    case DCICOMMAND:
        FIXME("DCICOMMAND\n");
        if (in_data)
        {
            const DCICMD *lpCmd = in_data;
            if (lpCmd->dwVersion != DD_VERSION) break;
            return 0; // return X11DRV_DCICommand(in_count, lpCmd, out_data);
        }
        break;

    case QDRV_ESCAPE:
        if (in_data && in_count >= sizeof(enum quartzdrv_escape_codes))
        {
            switch(*(const enum quartzdrv_escape_codes *)in_data)
            {
            case QDRV_GET_DISPLAY:
                *(int *)out_data = 0;
                return TRUE;
                break;
            case QDRV_GET_DRAWABLE:
                TRACE("QDRV_GET_DRAWABLE\n");
                if (out_count >= sizeof(AKObjectRef))
                {
                    *(AKObjectRef *)out_data = physDev->drawable;
                    return TRUE;
                }
                break;
            case QDRV_GET_FONT:
                FIXME("QDRV_GET_FONT\n");
                //if (out_count >= sizeof(Font))
                {
                  //  fontObject* pfo = XFONT_GetFontObject( physDev->font );
		   // if (pfo == NULL) return FALSE;
                  //  *(Font *)out_data = pfo->fs->fid;
                    *(int *)out_data = 0;
                    return TRUE;
                }
                break;
            case QDRV_SET_DRAWABLE:
                TRACE("QDRV_SET_DRAWABLE\n");
                if (in_count >= sizeof(struct quartzdrv_escape_set_drawable))
                {
                    const struct quartzdrv_escape_set_drawable *data = (const struct quartzdrv_escape_set_drawable *)in_data;
                    physDev->org = data->org;
                    physDev->drawable = data->drawable;
                    physDev->drawable_org = data->drawable_org;
                                      
                    return TRUE;
                }
                break;
            case QDRV_START_EXPOSURES:
                FIXME("QDRV_START_EXPOSURES\n");

                return TRUE;
            case QDRV_END_EXPOSURES:
                FIXME("QDRV_END_EXPOSURES\n");
                if (out_count >= sizeof(HRGN))
                {
                    HRGN hrgn = 0, tmp = 0;
                    *(HRGN *)out_data = hrgn;
                    return TRUE;
                }
                break;
            case QDRV_GET_DCE:
                if (out_count >= sizeof(struct dce *))
                {
                    *(struct dce **)out_data = physDev->dce;
                    return TRUE;
                }
                break;
            case QDRV_SET_DCE:
                if (in_count >= sizeof(struct quartzdrv_escape_set_dce))
                {
                    const struct quartzdrv_escape_set_dce *data = (const struct quartzdrv_escape_set_dce *)in_data;
                    physDev->dce = data->dce;
                    return TRUE;
                }
                break;
            case QDRV_GET_GLX_DRAWABLE:
                return TRUE;
                break;
            }
        }
        break;
    }
    return 0;
}
