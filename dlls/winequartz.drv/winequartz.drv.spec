@ cdecl Arc(ptr long long long long long long long long) QDRV_Arc
@ cdecl BitBlt(ptr long long long long ptr long long long) QDRV_BitBlt
@ cdecl Chord(ptr long long long long long long long long) QDRV_Chord
@ cdecl CreateBitmap(ptr long ptr) QDRV_CreateBitmap
@ cdecl CreateDC(long ptr wstr wstr wstr ptr) QDRV_CreateDC
@ cdecl CreateDIBSection(ptr long ptr long) QDRV_CreateDIBSection
@ cdecl DeleteBitmap(long) QDRV_DeleteBitmap
@ cdecl DeleteDC(ptr) QDRV_DeleteDC
@ cdecl Ellipse(ptr long long long long) QDRV_Ellipse
@ cdecl EnumDeviceFonts(ptr ptr ptr long) QDRV_EnumDeviceFonts
@ cdecl ExtEscape(ptr long long ptr long ptr) QDRV_ExtEscape
@ cdecl ExtTextOut(ptr long long long ptr ptr long ptr) QDRV_ExtTextOut
@ cdecl GetBitmapBits(long ptr long) QDRV_GetBitmapBits
@ cdecl GetDCEx(long long long) QDRV_GetDCEx
@ cdecl GetDCOrgEx(ptr ptr) QDRV_GetDCOrgEx
@ cdecl GetDIBColorTable(ptr long long ptr) QDRV_GetDIBColorTable
@ cdecl GetDIBits(ptr long long long ptr ptr long) QDRV_GetDIBits
@ cdecl GetDeviceCaps(ptr long) QDRV_GetDeviceCaps
@ cdecl CreateDesktopWindow(long) QDRV_CreateDesktopWindow
@ cdecl CreateWindow(long ptr long) QDRV_CreateWindow
@ cdecl GetCharWidth(ptr long long ptr) QDRV_GetCharWidth
@ cdecl GetPixel(ptr long long) QDRV_GetPixel
@ cdecl GetSystemPaletteEntries(ptr long long ptr) QDRV_GetSystemPaletteEntries
@ cdecl GetTextExtentExPoint(ptr ptr long long ptr ptr ptr) QDRV_GetTextExtentExPoint
@ cdecl GetTextMetrics(ptr ptr) QDRV_GetTextMetrics
@ cdecl LineTo(ptr long long) QDRV_LineTo
@ cdecl MsgWaitForMultipleObjectsEx(long ptr long long long) QDRV_MsgWaitForMultipleObjectsEx
@ cdecl PaintRgn(ptr long) QDRV_PaintRgn
@ cdecl PatBlt(ptr long long long long long) QDRV_PatBlt
@ cdecl Pie(ptr long long long long long long long long) QDRV_Pie
@ cdecl PolyPolygon(ptr ptr ptr long) QDRV_PolyPolygon
@ cdecl PolyPolyline(ptr ptr ptr long) QDRV_PolyPolyline
@ cdecl Polygon(ptr ptr long) QDRV_Polygon
@ cdecl Polyline(ptr ptr long) QDRV_Polyline
@ cdecl RealizePalette(ptr long long) QDRV_RealizePalette
@ cdecl Rectangle(ptr long long long long) QDRV_Rectangle
@ cdecl RealizeDefaultPalette(ptr) QDRV_RealizeDefaultPalette
@ cdecl ReleaseDC(long long long) QDRV_ReleaseDC
@ cdecl RoundRect(ptr long long long long long long) QDRV_RoundRect
@ cdecl SelectBitmap(ptr long) QDRV_SelectBitmap
@ cdecl SelectBrush(ptr long) QDRV_SelectBrush
@ cdecl SetBitmapBits(long ptr long) QDRV_SetBitmapBits
@ cdecl SetBkColor(ptr long) QDRV_SetBkColor
@ cdecl SetDCBrushColor(ptr long) QDRV_SetDCBrushColor
@ cdecl SetDCOrg(ptr long long) QDRV_SetDCOrg
@ cdecl SetDCPenColor(ptr long) QDRV_SetDCPenColor
@ cdecl SetDIBColorTable(ptr long long ptr) QDRV_SetDIBColorTable
@ cdecl SetDIBits(ptr long long long ptr ptr long) QDRV_SetDIBits
@ cdecl SetDIBitsToDevice(ptr long long long long long long long long ptr ptr long) QDRV_SetDIBitsToDevice
@ cdecl SetDeviceClipping(ptr long long) QDRV_SetDeviceClipping
@ cdecl SelectFont(ptr long long) QDRV_SelectFont
@ cdecl SelectPen(ptr long) QDRV_SelectPen
@ cdecl SetFocus(long) QDRV_SetFocus
@ cdecl SetPixel(ptr long long long) QDRV_SetPixel
@ cdecl SetTextColor(ptr long) QDRV_SetTextColor
@ cdecl SetParent(long long) QDRV_SetParent
@ cdecl SetWindowIcon(long long long) QDRV_SetWindowIcon
@ cdecl SetWindowPos(ptr) QDRV_SetWindowPos
@ cdecl SetWindowRgn(long long long) QDRV_SetWindowRgn
@ cdecl SetWindowStyle(ptr long) QDRV_SetWindowStyle
@ cdecl SetWindowText(long wstr) QDRV_SetWindowText
@ cdecl ShowWindow(long long) QDRV_ShowWindow
@ cdecl SysCommandSizeMove(long long) QDRV_SysCommandSizeMove
@ cdecl WindowFromDC(long) QDRV_WindowFromDC
@ cdecl SetCursor(ptr) QDRV_SetCursor
@ cdecl GetCursorPos(ptr) QDRV_GetCursorPos
@ cdecl SetCursorPos(long long) QDRV_SetCursorPos
@ cdecl StretchBlt(ptr long long long long ptr long long long long long) QDRV_StretchBlt

# User
@ cdecl ActivateKeyboardLayout(long long) QDRV_ActivateKeyboardLayout
@ cdecl Beep() QDRV_Beep
@ cdecl EnumDisplayMonitors(long ptr ptr long) QDRV_EnumDisplayMonitors
@ cdecl GetKeyboardLayout(long) QDRV_GetKeyboardLayout
@ cdecl GetKeyboardLayoutList(long ptr) QDRV_GetKeyboardLayoutList
@ cdecl GetKeyboardLayoutName(ptr) QDRV_GetKeyboardLayoutName
@ cdecl GetMonitorInfo(long ptr) QDRV_GetMonitorInfo
@ cdecl LoadKeyboardLayout(wstr long) QDRV_LoadKeyboardLayout
# @ cdecl SendInput(long ptr long) QDRV_SendInput
@ cdecl ToUnicodeEx(long long ptr ptr long long long) QDRV_ToUnicodeEx
@ cdecl UnloadKeyboardLayout(long) QDRV_UnloadKeyboardLayout

# QUARTZ locks
@ cdecl -norelay wine_quartzdrv_lock()
@ cdecl -norelay wine_quartzdrv_unlock()
