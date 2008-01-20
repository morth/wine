/*
 * QUARTZ DC objects
 *
 * Copyright 1999 Patrik Stridvall
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

#include "config.h"

#include "wtypes.h"
#include "wingdi.h"
#include "winnls.h"
#include "quartzdrv.h"
#include "wine/debug.h"

#define WINE_SIDE
#include "wine_carbon.h"

WINE_DEFAULT_DEBUG_CHANNEL(font);

/***********************************************************************
 *		SelectFont   (QUARTZDRV.@)
 */
HFONT QDRV_SelectFont(QDRV_PDEVICE *physDev, HFONT hfont, HANDLE gdiFont )
{
    LOGFONTW logfont;
    char facename[LF_FACESIZE+1];
    FIXME(":stub physDev %p hfont %p gdiFont %p\n", physDev, hfont, gdiFont);

    if (!GetObjectW( hfont, sizeof(logfont), &logfont )) return HGDI_ERROR;
    
    WideCharToMultiByte( CP_ACP, 0, logfont.lfFaceName, -1, facename, sizeof(facename), NULL, NULL );
    /*
    TRACE("lfHeight=%d \n"
            "lfWidth=%d \n"
            "lfEscapement=%d \n"
            "lfOrientation=%d \n"
            "lfWeight=%d \n"
            "lfItalic=%d \n"
            "lfUnderline=%d \n"
            "lfStrikeOut=%d \n"
            "lfCharSet=%d \n"
            "lfOutPrecision=%d \n"
            "lfClipPrecision=%d \n"
            "lfQuality=%d \n"
            "lfPitchAndFamily=%d \n"
            "lfFaceName=%s\n", logfont.lfHeight, logfont.lfWidth, logfont.lfEscapement,
            logfont.lfOrientation, logfont.lfWeight, logfont.lfItalic, logfont.lfUnderline,
            logfont.lfStrikeOut, logfont.lfCharSet, logfont.lfOutPrecision,
            logfont.lfClipPrecision, logfont.lfQuality, logfont.lfPitchAndFamily, facename);*/

    physDev->font = QRDV_SelectATSFont(facename, abs(logfont.lfHeight), logfont.lfWeight);

    return (HFONT)1; /* Use device font */
}

#define IROUND(x) (int)((x)>0? (x)+0.5 : (x) - 0.5)

/***********************************************************************
 *           X11DRV_GetTextMetrics
 */
BOOL QDRV_GetTextMetrics(QDRV_PDEVICE *physDev, TEXTMETRICW *metrics)
{    
    ATSFontMetrics ats_metrics = QDRV_CGGetFontMetrics(physDev->font);
    FIXME("stub: (physDev %p metrics %p)\n", physDev, metrics);
   
    metrics->tmHeight = QDRV_CGGetFontHeight(physDev->font);
    metrics->tmAscent = IROUND(metrics->tmHeight * ats_metrics.ascent);
    metrics->tmDescent = IROUND(metrics->tmHeight * -ats_metrics.descent);

    metrics->tmAveCharWidth = IROUND(metrics->tmHeight * ats_metrics.avgAdvanceWidth);
    metrics->tmMaxCharWidth = IROUND(metrics->tmHeight * ats_metrics.maxAdvanceWidth);

    metrics->tmInternalLeading = IROUND(metrics->tmHeight * ats_metrics.leading);
    metrics->tmExternalLeading = IROUND(metrics->tmHeight * ats_metrics.leading);

    metrics->tmStruckOut = 1;
    metrics->tmUnderlined = IROUND( metrics->tmHeight * ats_metrics.underlinePosition);

    metrics->tmOverhang = 0;
    metrics->tmItalic = IROUND(ats_metrics.italicAngle);

    metrics->tmWeight = QDRV_CGGetFontWeight(physDev->font);
    metrics->tmFirstChar = 0; 
    metrics->tmLastChar = 65535; 
    metrics->tmDefaultChar = 0; 
    metrics->tmBreakChar = 32; 
    metrics->tmCharSet = 0;
    metrics->tmPitchAndFamily = 0;

    metrics->tmDigitizedAspectX = 0;
    metrics->tmDigitizedAspectY = 0;
    
  /*  TRACE("tmHeight=%d\n"
                "tmAscent=%d\n"
                "tmDescent=%d\n"
                "tmInternalLeading=%d\n"
                "tmExternalLeading=%d\n"
                "tmAveCharWidth=%d\n"
                "tmMaxCharWidth=%d\n"
                "tmWeight=%d\n"
                "tmOverhang=%d\n"
                "tmDigitizedAspectX=%d\n"
                "tmDigitizedAspectY=%d\n"
                "tmFirstChar=%d\n"
                "tmLastChar=%d\n"
                "tmDefaultChar=%d\n"
                "tmBreakChar=%d\n"
                "tmItalic=%d\n"
                "tmUnderlined=%d\n"
                "tmStruckOut=%d\n"
                "tmPitchAndFamily=%d\n"
              "tmCharSet=%d\n", metrics->tmHeight,
              metrics->tmAscent,
              metrics->tmDescent,
              metrics->tmInternalLeading,
              metrics->tmExternalLeading,
              metrics->tmAveCharWidth,
              metrics->tmMaxCharWidth,
              metrics->tmWeight,
              metrics->tmOverhang,
              metrics->tmDigitizedAspectX,
              metrics->tmDigitizedAspectY,
              metrics->tmFirstChar,
              metrics->tmLastChar,
              metrics->tmDefaultChar,
              metrics->tmBreakChar,
              metrics->tmItalic,
              metrics->tmUnderlined,
              metrics->tmStruckOut,
              metrics->tmPitchAndFamily,
              metrics->tmCharSet); */
    
    return TRUE;
}


/***********************************************************************
 *           QDRV_GetCharWidth
 */
BOOL QDRV_GetCharWidth( QDRV_PDEVICE *physDev, UINT firstChar, UINT lastChar,
                            LPINT buffer )
{
    FIXME(":stub (firstChar=%d lastChar=%d buffer=%p)\n", firstChar, lastChar, buffer);
    int height = QDRV_CGGetFontHeight(physDev->font);
    ATSFontMetrics ats_metrics = QDRV_CGGetFontMetrics(physDev->font);

    unsigned int i;

    for (i = firstChar; i <= lastChar; i++)
        *buffer++ = height * IROUND(ats_metrics.maxAdvanceWidth);
	
    return TRUE;
}


/***********************************************************************
 *
 *           QDRV_EnumDeviceFonts
 */
BOOL QDRV_EnumDeviceFonts( QDRV_PDEVICE *physDev, LPLOGFONTW plf, FONTENUMPROCW proc, LPARAM lp )
{
    FIXME("stub: physDev=%p has_gdi_font=%d\n", physDev, physDev->has_gdi_font);
    
    ENUMLOGFONTEXW	lf;
    NEWTEXTMETRICEXW	tm;
   // fontResource*	pfr = fontList;
    BOOL	  	b, bRet = FALSE;

    /* don't enumerate x11 fonts if we're using client side fonts */
   // if (physDev->has_gdi_font) return FALSE;

    if( plf->lfFaceName[0] )
    {
        char facename[LF_FACESIZE+1];
        WideCharToMultiByte( CP_ACP, 0, plf->lfFaceName, -1,
                             facename, sizeof(facename), NULL, NULL );
                             
        FIXME("facename = %s\n", facename);
    }

    return bRet;
}
