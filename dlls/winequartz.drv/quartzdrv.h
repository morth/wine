/*
 * Quartz driver definitions
 *
 * Copyright 1996 Alexandre Julliard
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

#ifndef __WINE_QUARTZDRV_H
#define __WINE_QUARTZDRV_H

#ifndef __WINE_CONFIG_H
# error You must include config.h to use this header
#endif

#define WINE_SIDE
#include "wine_carbon.h"

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "ddrawi.h"
#include "wine/list.h"

struct dce;

  /* Quartz physical pen */
typedef struct
{
    int          style;
    int          endcap;
    int          linejoin;
    int          pixel;
    int          width;
    char *       dashes;
    int          dash_len;
    int          type;          /* GEOMETRIC || COSMETIC */
} QUARTZ_PHYSPEN;

  /* Quartz physical brush */
typedef struct
{
    int          style;
    int          fillStyle;
    int          pixel;
    CGContextRef context;
} QUARTZ_PHYSBRUSH;


typedef struct 
{
    HBITMAP hbitmap;
    
    CGContextRef    context;
    CGContextRef    image;             /* context for DIB section */

    int depth;
    int         status, p_status;  /* mapping status */
    int         *colorMap;          /* color map info */
    int          nColorMap;
    RGBQUAD     *colorTable;        /* original dib color table converted to rgb values if usage was DIB_PAL_COLORS */
    CRITICAL_SECTION lock;          /* GDI access lock */
    struct list entry;
    BYTE         *base;             /* Base address */
    SIZE_T        size;             /* Size in bytes */
} QUARTZ_PHYSBITMAP;

typedef UINT QUARTZ_PHYSFONT;

typedef struct 
{
    HDC hdc;

    AKObjectRef     drawable;
    POINT           org;          /* DC origin relative to drawable */
    POINT           drawable_org; /* Origin of drawable relative to screen */
    HRGN            region;       /* Device region (visible region & clip region) */

    QUARTZ_PHYSPEN      pen;
    QUARTZ_PHYSBRUSH    brush;
    QUARTZ_PHYSBITMAP   *bitmap;
    QUARTZ_PHYSFONT     font;
    
    BOOL          has_gdi_font; /* is current font a GDI font? */
    
    int           backgroundPixel;
    int           textPixel;
    int           depth;       /* bit depth of the DC */
    int           exposures;   /* count of graphics exposures operations */
    struct dce   *dce;         /* opaque pointer to DCE */
    
} QDRV_PDEVICE;


  /* GCs used for B&W and color bitmap operations */
extern CGContextRef BITMAP_monoGC, BITMAP_colorGC;
extern QUARTZ_PHYSBITMAP BITMAP_stock_phys_bitmap;  /* phys bitmap for the default stock bitmap */

#define BITMAP_GC(physBitmap) (((physBitmap)->depth == 1) ? BITMAP_monoGC : BITMAP_colorGC)

extern LONG QDRV_SetBitmapBits( HBITMAP hbitmap, const void *bits, LONG count );
extern LONG QDRV_GetBitmapBits( HBITMAP hbitmap, void *bits, LONG count );

extern AKObjectRef root_window;
extern unsigned int screen_width;
extern unsigned int screen_height;
extern unsigned int screen_depth;
extern int managed_mode;

/*extern unsigned int text_caps;

extern int private_color_map;*/

extern BYTE key_state_table[256];
extern POINT cursor_pos;

extern void wine_quartzdrv_lock(void);
extern void wine_quartzdrv_unlock(void);

extern void QDRV_BITMAP_Init(void);
extern char *QDRV_get_process_name(void);
extern QUARTZ_PHYSBITMAP *QDRV_init_phys_bitmap( HBITMAP hbitmap );

struct quartzdrv_thread_data
{
    HANDLE   display_fd;
    int      process_event_count;  /* recursion count for event processing */
  
    AKObjectRef cursor;               /* current cursor */
    AKObjectRef cursor_window;        /* current window that contains the cursor */
 
    AKObjectRef grab_window;          /* window that currently grabs the mouse */
    HWND     last_focus;           /* last window that had focus */
 
 //   Window   selection_wnd;        /* window used for selection interactions */
};

extern struct quartzdrv_thread_data *quartzdrv_init_thread_data(void);
extern DWORD thread_data_tls_index;

inline static struct quartzdrv_thread_data *quartzdrv_thread_data(void)
{
    struct quartzdrv_thread_data *data = TlsGetValue( thread_data_tls_index );
    if (!data) data = quartzdrv_init_thread_data();
    return data;
}


/* GDI escapes */

#define QDRV_ESCAPE 6789
enum quartzdrv_escape_codes
{
    QDRV_GET_DISPLAY,      /* get X11 display for a DC */
    QDRV_GET_DRAWABLE,     /* get current drawable for a DC */
    QDRV_GET_FONT,         /* get current X font for a DC */
    QDRV_SET_DRAWABLE,     /* set current drawable for a DC */
    QDRV_START_EXPOSURES,  /* start graphics exposures */
    QDRV_END_EXPOSURES,    /* end graphics exposures */
    QDRV_GET_DCE,          /* get the DCE pointer */
    QDRV_SET_DCE,          /* set the DCE pointer */
    QDRV_GET_GLX_DRAWABLE  /* get current glx drawable for a DC */
};

struct quartzdrv_escape_set_drawable
{
    enum quartzdrv_escape_codes code;         /* escape code (QDRV_SET_DRAWABLE) */
    AKObjectRef             drawable;     /* X drawable */
    int                      mode;         /* ClipByChildren or IncludeInferiors */
    POINT                    org;          /* origin of DC relative to drawable */
    POINT                    drawable_org; /* origin of drawable relative to screen */
};

struct quartzdrv_escape_set_dce
{
    enum quartzdrv_escape_codes code;            /* escape code (QDRV_SET_DRAWABLE) */
    struct dce              *dce;             /* pointer to DCE (opaque ptr for GDI) */
};

//extern void alloc_window_dce( struct quartzdrv_win_data *data );
//extern void free_window_dce( struct quartzdrv_win_data *data );
extern void invalidate_dce( HWND hwnd, const RECT *rect );


/* quartzdrv private window data */
struct quartzdrv_win_data
{
    HWND        hwnd;           /* hwnd that this private data belongs to */
    AKObjectRef whole_window;   /* NSWindow for the complete window */
    AKObjectRef icon_window;    /* NSWindow for the icon */
    RECT        window_rect;    /* USER window rectangle relative to parent */
    RECT        whole_rect;     /* X window rectangle for the whole window relative to parent */
    RECT        client_rect;    /* client area relative to whole window */
    BOOL        managed;        /* is window managed? */

    struct dce *dce;            /* DCE for CS_OWNDC or CS_CLASSDC windows */
    unsigned int lock_changes;   /* lock count for change requests */
    HBITMAP     hWMIconBitmap;
    HBITMAP     hWMIconMask;

    struct list entry;
};

extern void destroy_window_data(void);
extern struct quartzdrv_win_data *get_win_data(HWND hwnd);
extern struct quartzdrv_win_data *get_win_data_carbon(WindowRef win);
extern struct quartzdrv_win_data *new_win_data(void);
extern void delete_win_data(HWND hwnd);

extern BOOL QDRV_is_window_rect_mapped( const RECT *rect );
extern void QDRV_X_to_window_rect( struct quartzdrv_win_data *data, RECT *rect );
extern void QDRV_sync_window_position( struct quartzdrv_win_data *data,
                                  UINT swp_flags, const RECT *new_client_rect,
                                  const RECT *new_whole_rect );
                                  
extern BOOL QDRV_set_window_pos( HWND hwnd, HWND insert_after, const RECT *rectWindow,
                            const RECT *rectClient, UINT swp_flags, const RECT *valid_rects );
extern void QDRV_sync_window_style( struct quartzdrv_win_data *data );

extern AKObjectRef QDRV_get_whole_window( HWND hwnd );
extern UINT WINPOS_MinMaximize( HWND hwnd, UINT cmd, LPRECT rect );

extern INT QDRV_XWStoDS(QDRV_PDEVICE *physDev, INT width);

extern BOOL QDRV_SetupGCForText(QDRV_PDEVICE *physDev, CGContextRef ctx);
extern BOOL QDRV_SetupGCForBrush( QDRV_PDEVICE *physDev, CGContextRef ctx );
extern BOOL QDRV_SetupGCForPatBlt( QDRV_PDEVICE *physDev, CGContextRef ctx, BOOL fMapColors);


/* Quartz GDI palette driver */

#define QDRV_PALETTE_FIXED    0x0001 /* read-only colormap - have to use XAllocColor (if not virtual) */
#define QDRV_PALETTE_VIRTUAL  0x0002 /* no mapping needed - pixel == pixel color */

#define QDRV_PALETTE_PRIVATE  0x1000 /* private colormap, identity mapping */
#define QDRV_PALETTE_WHITESET 0x2000

extern CGDirectPaletteRef QDRV_PALETTE_QuartzPaletteColormap;
extern UINT16 QDRV_PALETTE_PaletteFlags;

extern int *QDRV_PALETTE_PaletteToQuartzPixel;
extern int *QDRV_PALETTE_QuartzPixelToPalette;

extern int X11DRV_PALETTE_mapEGAPixel[16];

extern int QDRV_PALETTE_Init(void);
extern void QDRV_PALETTE_Cleanup(void);
extern BOOL QDRV_IsSolidColor(COLORREF color);

extern COLORREF QDRV_PALETTE_ToLogical(int pixel);
extern int QDRV_PALETTE_ToPhysical(QDRV_PDEVICE *physDev, COLORREF color);


/* DIB Section sync state */
enum { DIB_Status_None, DIB_Status_InSync, DIB_Status_GdiMod, DIB_Status_AppMod };


typedef struct {
    void (*Convert_5x5_asis)(int width, int height,
                             const void* srcbits, int srclinebytes,
                             void* dstbits, int dstlinebytes);
    void (*Convert_555_reverse)(int width, int height,
                                const void* srcbits, int srclinebytes,
                                void* dstbits, int dstlinebytes);
    void (*Convert_555_to_565_asis)(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes);
    void (*Convert_555_to_565_reverse)(int width, int height,
                                       const void* srcbits, int srclinebytes,
                                       void* dstbits, int dstlinebytes);
    void (*Convert_555_to_888_asis)(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes);
    void (*Convert_555_to_888_reverse)(int width, int height,
                                       const void* srcbits, int srclinebytes,
                                       void* dstbits, int dstlinebytes);
    void (*Convert_555_to_0888_asis)(int width, int height,
                                     const void* srcbits, int srclinebytes,
                                     void* dstbits, int dstlinebytes);
    void (*Convert_555_to_0888_reverse)(int width, int height,
                                        const void* srcbits, int srclinebytes,
                                        void* dstbits, int dstlinebytes);
    void (*Convert_5x5_to_any0888)(int width, int height,
                                   const void* srcbits, int srclinebytes,
                                   WORD rsrc, WORD gsrc, WORD bsrc,
                                   void* dstbits, int dstlinebytes,
                                   DWORD rdst, DWORD gdst, DWORD bdst);
    void (*Convert_565_reverse)(int width, int height,
                                const void* srcbits, int srclinebytes,
                                void* dstbits, int dstlinebytes);
    void (*Convert_565_to_555_asis)(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes);
    void (*Convert_565_to_555_reverse)(int width, int height,
                                       const void* srcbits, int srclinebytes,
                                       void* dstbits, int dstlinebytes);
    void (*Convert_565_to_888_asis)(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes);
    void (*Convert_565_to_888_reverse)(int width, int height,
                                       const void* srcbits, int srclinebytes,
                                       void* dstbits, int dstlinebytes);
    void (*Convert_565_to_0888_asis)(int width, int height,
                                     const void* srcbits, int srclinebytes,
                                     void* dstbits, int dstlinebytes);
    void (*Convert_565_to_0888_reverse)(int width, int height,
                                        const void* srcbits, int srclinebytes,
                                        void* dstbits, int dstlinebytes);
    void (*Convert_888_asis)(int width, int height,
                             const void* srcbits, int srclinebytes,
                             void* dstbits, int dstlinebytes);
    void (*Convert_888_reverse)(int width, int height,
                                const void* srcbits, int srclinebytes,
                                void* dstbits, int dstlinebytes);
    void (*Convert_888_to_555_asis)(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes);
    void (*Convert_888_to_555_reverse)(int width, int height,
                                       const void* srcbits, int srclinebytes,
                                       void* dstbits, int dstlinebytes);
    void (*Convert_888_to_565_asis)(int width, int height,
                                    const void* srcbits, int srclinebytes,
                                    void* dstbits, int dstlinebytes);
    void (*Convert_888_to_565_reverse)(int width, int height,
                                       const void* srcbits, int srclinebytes,
                                       void* dstbits, int dstlinebytes);
    void (*Convert_888_to_0888_asis)(int width, int height,
                                     const void* srcbits, int srclinebytes,
                                     void* dstbits, int dstlinebytes);
    void (*Convert_888_to_0888_reverse)(int width, int height,
                                        const void* srcbits, int srclinebytes,
                                        void* dstbits, int dstlinebytes);
    void (*Convert_rgb888_to_any0888)(int width, int height,
                                      const void* srcbits, int srclinebytes,
                                      void* dstbits, int dstlinebytes,
                                      DWORD rdst, DWORD gdst, DWORD bdst);
    void (*Convert_bgr888_to_any0888)(int width, int height,
                                      const void* srcbits, int srclinebytes,
                                      void* dstbits, int dstlinebytes,
                                      DWORD rdst, DWORD gdst, DWORD bdst);
    void (*Convert_0888_asis)(int width, int height,
                              const void* srcbits, int srclinebytes,
                              void* dstbits, int dstlinebytes);
    void (*Convert_0888_reverse)(int width, int height,
                                 const void* srcbits, int srclinebytes,
                                 void* dstbits, int dstlinebytes);
    void (*Convert_0888_any)(int width, int height,
                             const void* srcbits, int srclinebytes,
                             DWORD rsrc, DWORD gsrc, DWORD bsrc,
                             void* dstbits, int dstlinebytes,
                             DWORD rdst, DWORD gdst, DWORD bdst);
    void (*Convert_0888_to_555_asis)(int width, int height,
                                     const void* srcbits, int srclinebytes,
                                     void* dstbits, int dstlinebytes);
    void (*Convert_0888_to_555_reverse)(int width, int height,
                                        const void* srcbits, int srclinebytes,
                                        void* dstbits, int dstlinebytes);
    void (*Convert_0888_to_565_asis)(int width, int height,
                                     const void* srcbits, int srclinebytes,
                                     void* dstbits, int dstlinebytes);
    void (*Convert_0888_to_565_reverse)(int width, int height,
                                        const void* srcbits, int srclinebytes,
                                        void* dstbits, int dstlinebytes);
    void (*Convert_any0888_to_5x5)(int width, int height,
                                   const void* srcbits, int srclinebytes,
                                   DWORD rsrc, DWORD gsrc, DWORD bsrc,
                                   void* dstbits, int dstlinebytes,
                                   WORD rdst, WORD gdst, WORD bdst);
    void (*Convert_0888_to_888_asis)(int width, int height,
                                     const void* srcbits, int srclinebytes,
                                     void* dstbits, int dstlinebytes);
    void (*Convert_0888_to_888_reverse)(int width, int height,
                                        const void* srcbits, int srclinebytes,
                                        void* dstbits, int dstlinebytes);
    void (*Convert_any0888_to_rgb888)(int width, int height,
                                      const void* srcbits, int srclinebytes,
                                      DWORD rsrc, DWORD gsrc, DWORD bsrc,
                                      void* dstbits, int dstlinebytes);
    void (*Convert_any0888_to_bgr888)(int width, int height,
                                      const void* srcbits, int srclinebytes,
                                      DWORD rsrc, DWORD gsrc, DWORD bsrc,
                                      void* dstbits, int dstlinebytes);
} dib_conversions;

extern const dib_conversions dib_normal, dib_src_byteswap, dib_dst_byteswap;

extern INT X11DRV_DIB_MaskToShift(DWORD mask);
extern int *X11DRV_DIB_BuildColorMap( QDRV_PDEVICE *physDev, WORD coloruse,
				      WORD depth, const BITMAPINFO *info,
				      int *nColors );
extern INT X11DRV_CoerceDIBSection(QDRV_PDEVICE *physDev, INT req, BOOL lossy);
extern INT X11DRV_LockDIBSection(QDRV_PDEVICE *physDev, INT req, BOOL lossy);
extern void X11DRV_UnlockDIBSection(QDRV_PDEVICE *physDev, BOOL commit);

extern QUARTZ_PHYSBITMAP *QDRV_get_phys_bitmap(HBITMAP hbitmap);
extern QUARTZ_PHYSBITMAP *QDRV_init_phys_bitmap(HBITMAP hbitmap);


extern DWORD EVENT_x11_time_to_win32_time(unsigned int time);

#endif  /* __WINE_QUARTZDRV_H */
