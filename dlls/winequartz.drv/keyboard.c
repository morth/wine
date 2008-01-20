/*
 *  keyboard.c
 *  
 * Copyright 1993 Bob Amstadt
 * Copyright 1996 Albrecht Kleine
 * Copyright 1997 David Faure
 * Copyright 1998 Morten Welinder
 * Copyright 1998 Ulrich Weigand
 * Copyright 1999 Ove Kåven
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

#include <stdarg.h>

#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "wine/winuser16.h"
#include "winnls.h"
#include "win.h"
#include "quartzdrv.h"
#include "wine/server.h"
#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(keyboard);
WINE_DECLARE_DEBUG_CHANNEL(key);

/*
    Redefine key modifier (see NSEvent.h)
*/
#define CapsLockKeyMask     1 << 16
#define ShiftKeyMask        1 << 17
#define ControlKeyMask      1 << 18
#define AlternateKeyMask    1 << 19
#define CommandKeyMask      1 << 20
#define NumericPadKeyMask   1 << 21
#define HelpKeyMask         1 << 22
#define FunctionKeyMask     1 << 23
#define DeviceIndependentModifierFlagsMask 0xffff0000U

typedef union
{
    struct
    {
#ifndef BITFIELDS_BIGENDIAN
        unsigned long count : 16;
#endif
        unsigned long code : 8;
        unsigned long extended : 1;
        unsigned long unused : 2;
        unsigned long win_internal : 2;
        unsigned long context : 1;
        unsigned long previous : 1;
        unsigned long transition : 1;
#ifdef BITFIELDS_BIGENDIAN
        unsigned long count : 16;
#endif
    } lp1;
    unsigned long lp2;
} KEYLP;

/* key state table bits:
  0x80 -> key is pressed
  0x40 -> key got pressed since last time
  0x01 -> key is toggled
*/
BYTE key_state_table[256];

static BYTE TrackSysKey = 0; /* determine whether ALT key up will cause a WM_SYSKEYUP
                                or a WM_KEYUP message */

static int NumLockMask, AltGrMask; /* mask in the XKeyEvent state */
static int kcControl, kcAlt, kcShift, kcNumLock, kcCapsLock; /* keycodes */

static const WORD keycode_to_vkey[128] =
{
    0x41,                   // 0x00 a
    0x53,                   // 0x01 s
    0x44,                   // 0x02 d
    0x46,                   // 0x03 f
    0x48,                   // 0x04 h
    0x47,                   // 0x05 g
    0x5A,                   // 0x06 z
    0x58,                   // 0x07 x
    0x43,                   // 0x08 c
    0x56,                   // 0x09 v
    0,  //   0x0A 
    0x42,                   // 0x0B b
    0x51,                   // 0x0C q
    0x57,                   // 0x0D w
    0x45,                   // 0x0E e
    0x52,                   // 0x0F r 
    0x59,                   // 0x10 y
    0x54,                   // 0x11 t
    0x31,                   // 0x12 1
    0x32,                   // 0x13 2
    0x33,                   // 0x14 3
    0x34,                   // 0x15 4
    0x36,                   // 0x16 6
    0x35,                   // 0x17 5
    VK_OEM_8,               // 0x18 =
    0x39,                   // 0x19 9
    0x37,                   // 0x1A 7
    VK_SUBTRACT,            // 0x1B -
    0x38,                   // 0x1C 8
    0x30,                   // 0x1D 0
    0,                      // 0x1E Right Bracket
    0x4F,                   // 0x1F o
    0x55,                   // 0x20 u
    0,                      // 0x21 Left Bracket
    0x49,                   // 0x22 i
    0x50,                   // 0x23 p
    VK_RETURN,              // 0x24 Return
    0x4C,                   // 0x25 l
    0x4A,                   // 0x26 j
    0,                      // 0x27 '
    0x4B,                   // 0x28 k
    0,                      // 0x29 ;
    0,                      // 0x2A Back Slash
    0,                      // 0x2B ,
    VK_DIVIDE,              // 0x2C Slash
    0x4E,                   // 0x2D n
    0x4D,                   // 0x2E m
    VK_DECIMAL,             // 0x2F .
    VK_TAB,                 // 0x30 Tab
    VK_SPACE,               // 0x31 Space  
    0,                      // 0x32 Back Quote
    VK_BACK,                // 0x33 Back Space
    0,  //   0x34
    VK_ESCAPE,              // 0x35 Escape               
    0,                      // 0x36 Right Command
    0,                      // 0x37 Left Command
    VK_LSHIFT,               // 0x38 Left Shift
    0,                      // 0x39 Caps Lock
    VK_LMENU,                // 0x3A Left Alt
    VK_LCONTROL,             // 0x3B Left Ctrl
    VK_RSHIFT,               // 0x3C Right Shift
    VK_RMENU,                // 0x3D Right Alt
    VK_RCONTROL,             // 0x3E Right Ctrl
    0, // 3F
    0, // 40
    VK_DECIMAL,             // 0x41 KeyPad .
    0, // 42
    VK_MULTIPLY,            // 0x43 KeyPad *
    0, // 44
    VK_ADD,                 // 0x45 KeyPad +
    0, // 46
    VK_NUMLOCK,             // 0x47 Numlock
    0, // 48
    0, // 49
    0, // 4A
    VK_DIVIDE,              // 0x4B KeyPad /
    VK_RETURN,              // 0x4C KeyPad Return
    0, // 4D
    VK_SUBTRACT,            // 0x4E KeyPad -
    0, // 4F
    0, // 50
    VK_OEM_8,               // 0x51 KeyPad =
    VK_NUMPAD0,             // 0x52 KeyPad 0
    VK_NUMPAD1,             // 0x53 KeyPad 1
    VK_NUMPAD2,             // 0x54 KeyPad 2
    VK_NUMPAD3,             // 0x55 KeyPad 3
    VK_NUMPAD4,             // 0x56 KeyPad 4
    VK_NUMPAD5,             // 0x57 KeyPad 5
    VK_NUMPAD6,             // 0x58 KeyPad 6
    VK_NUMPAD7,             // 0x59 KeyPad 7
    0, // 5A
    VK_NUMPAD8,             // 0x5B KeyPad 8
    VK_NUMPAD9,             // 0x5C KeyPad 9
    0, // 5D
    0, // 5E
    0, // 5F    
    VK_F5,                  // 0x60 FKey5
    VK_F6,                  // 0x61 FKey6
    VK_F7,                  // 0x62 FKey7
    VK_F3,                  // 0x63 FKey3
    VK_F8,                  // 0x64 FKey8
    VK_F9,                  // 0x65 FKey9
    0, // 66
    VK_F11,                 // 0x67 FKey11
    0, // 68
    VK_PRINT,               // 0x69 Print
    0, // 6A
    0,                      // 0x6B ScrollLock
    0, // 6C
    VK_F10,                 // 0x6D FKey10
    0, // 6E
    VK_F12,                 // 0x6F FKey12
    0, // 70
    VK_PAUSE,               // 0x71 Pause
    VK_INSERT,              // 0x72 Insert
    VK_HOME,                // 0x73 Home
    VK_PRIOR,               // 0x74 PageUp
    VK_DELETE,              // 0x75 Delete
    VK_F4,                  // 0x76 FKey4
    VK_END,                 // 0x77 End
    VK_F2,                  // 0x78 FKey2
    VK_NEXT,                // 0x79 PageDown
    VK_F1,                  // 0x7A FKey1
    VK_LEFT,                // 0x7B Left
    VK_RIGHT,               // 0x7C Right
    VK_DOWN,                // 0x7D Down
    VK_UP,                  // 0x7E Up
    0                       // 0x7F Power
};

/* Fake Layout table. */
static const struct {
    LCID lcid; /* input locale identifier, look for LOCALE_ILANGUAGE
                 in the appropriate dlls/kernel/nls/.nls file */
    const char *comment;
} main_key_tab[]={
 {0x0409, "United States keyboard layout"},
 {0x0809, "British keyboard layout"},
 {0x0407, "German keyboard layout"},
 {0x0807, "Swiss German keyboard layout"},
 {0x100c, "Swiss French keyboard layout"},
 {0x041d, "Swedish keyboard layout"},
 {0x0425, "Estonian keyboard layout"},
 {0x0414, "Norwegian keyboard layout"},
 {0x0406, "Danish keyboard layout"},
 {0x040c, "French keyboard layout"},
 {0x0c0c, "Canadian French keyboard layout"},
 {0x080c, "Belgian keyboard layout"},
 {0x0816, "Portuguese keyboard layout"},
 {0x0416, "Brazilian keyboard layout"},
 {0x040b, "Finnish keyboard layout"},
 {0x0402, "Bulgarian  keyboard layout"},
 {0x0423, "Belarusian keyboard layout"},
 {0x0419, "Russian keyboard layout"},
 {0x0422, "Ukrainian keyboard layout"},
 {0x040a, "Spanish keyboard layout"},
 {0x0410, "Italian keyboard layout"},
 {0x040f, "Icelandic keyboard layout"},
 {0x040e, "Hungarian keyboard layout"},
 /*
 {0x0415, "Polish (programmer's) keyboard layout", &main_key_PL, &main_key_scan_qwerty, &main_key_vkey_qwerty},
 {0x0424, "Slovenian keyboard layout", &main_key_SI, &main_key_scan_qwerty, &main_key_vkey_qwertz},
 {0x0c1a, "Serbian keyboard layout", &main_key_SR, &main_key_scan_qwerty, &main_key_vkey_qwerty}, 
 {0x041a, "Croatian keyboard layout", &main_key_HR, &main_key_scan_qwerty, &main_key_vkey_qwertz},
 {0x0411, "Japanese 106 keyboard layout", &main_key_JA_jp106, &main_key_scan_qwerty_jp106, &main_key_vkey_qwerty_jp106},
 {0x041b, "Slovak and Czech keyboard", &main_key_SK_prog, &main_key_scan_qwerty, &main_key_vkey_qwerty},
 {0x0405, "Czech keyboard layout", &main_key_CS, &main_key_scan_qwerty, &main_key_vkey_qwerty},
 {0x040a, "Latin American keyboard layout", &main_key_LA, &main_key_scan_qwerty, &main_key_vkey_qwerty},
 {0x0427, "Lithuanian (Baltic) keyboard layout", &main_key_LT_B, &main_key_scan_qwerty, &main_key_vkey_qwerty},
 {0x041f, "Turkish keyboard layout", &main_key_TK, &main_key_scan_qwerty, &main_key_vkey_qwerty},
 {0x040d, "Israelian keyboard layout", &main_key_IL, &main_key_scan_qwerty, &main_key_vkey_qwerty},
 {0x0409, "VNC keyboard layout", &main_key_vnc, &main_key_scan_vnc, &main_key_vkey_vnc},
 {0x0408, "Greek keyboard layout"},
 {0x041e, "Thai (Kedmanee)  keyboard layout"},
 {0x0413, "Dutch keyboard layout"},*/

 {0, NULL} /* sentinel */
};
static unsigned kbd_layout=0; /* index into above table of layouts */
static BOOL NumState=FALSE, CapsState=FALSE;


/***********************************************************************
*           QDRV_send_keyboard_input
*/
void QDRV_send_keyboard_input( WORD wVk, WORD wScan, DWORD dwFlags, DWORD time,
                               DWORD dwExtraInfo, UINT injected_flags )
{
    UINT message;
    KEYLP keylp;
    KBDLLHOOKSTRUCT hook;
    WORD wVkStripped;
    
    wVk = LOBYTE(wVk);
    
    /* strip left/right for menu, control, shift */
    if (wVk == VK_LMENU || wVk == VK_RMENU)
        wVkStripped = VK_MENU;
    else if (wVk == VK_LCONTROL || wVk == VK_RCONTROL)
        wVkStripped = VK_CONTROL;
    else if (wVk == VK_LSHIFT || wVk == VK_RSHIFT)
        wVkStripped = VK_SHIFT;
    else
        wVkStripped = wVk;
        
    keylp.lp2 = 0;
    keylp.lp1.count = 1;
    keylp.lp1.code = wScan;
    keylp.lp1.extended = (dwFlags & KEYEVENTF_EXTENDEDKEY) != 0;
    keylp.lp1.win_internal = 0; /* this has something to do with dialogs,
        * don't remember where I read it - AK */
    /* it's '1' under windows, when a dialog box appears
        * and you press one of the underlined keys - DF*/
    
    /* note that there is a test for all this */
    if (dwFlags & KEYEVENTF_KEYUP )
    {
        message = WM_KEYUP;
        
        if ((key_state_table[VK_MENU] & 0x80) &&
                ((wVkStripped == VK_MENU) || (wVkStripped == VK_CONTROL)
                || !(key_state_table[VK_CONTROL] & 0x80)))
        {
            if( TrackSysKey == VK_MENU || /* <ALT>-down/<ALT>-up sequence */
                (wVkStripped != VK_MENU)) /* <ALT>-down...<something else>-up */
                message = WM_SYSKEYUP;
            TrackSysKey = 0;
        }
        key_state_table[wVkStripped] &= ~0x80;
        keylp.lp1.previous = 1;
        keylp.lp1.transition = 1;
    }
    else
    {
        keylp.lp1.previous = (key_state_table[wVk] & 0x80) != 0;
        keylp.lp1.transition = 0;
        if (!(key_state_table[wVk] & 0x80)) key_state_table[wVk] ^= 0x01;
        key_state_table[wVkStripped] |= 0xc0;

        message = WM_KEYDOWN;
        if ((key_state_table[VK_MENU] & 0x80) && !(key_state_table[VK_CONTROL] & 0x80))
        {
            message = WM_SYSKEYDOWN;
            TrackSysKey = wVkStripped;
        }
    }
    
    keylp.lp1.context = (key_state_table[VK_MENU] & 0x80) != 0; /* 1 if alt */
    
    TRACE_(key)(" wParam=%04x, lParam=%08lx, InputKeyState=%x\n",
                wVk, keylp.lp2, key_state_table[wVk] );
    
    hook.vkCode      = wVk;
    hook.scanCode    = wScan;
    hook.flags       = (keylp.lp2 >> 24) | injected_flags;
    hook.time        = time;
    hook.dwExtraInfo = dwExtraInfo;
    if (HOOK_CallHooks( WH_KEYBOARD_LL, HC_ACTION, message, (LPARAM)&hook, TRUE )) return;
    
    SERVER_START_REQ( send_hardware_message )
    {
        req->id       = (injected_flags & LLKHF_INJECTED) ? 0 : GetCurrentThreadId();
        req->win      = 0;
        req->msg      = message;
        req->wparam   = wVk;
        req->lparam   = keylp.lp2;
        req->x        = cursor_pos.x;
        req->y        = cursor_pos.y;
        req->time     = time;
        req->info     = dwExtraInfo;
        wine_server_call( req );
    }
    SERVER_END_REQ;
}


/**********************************************************************
*		KEYBOARD_GenerateMsg
*
* Generate Down+Up messages when NumLock or CapsLock is pressed.
*
* Convention : called with vkey only VK_NUMLOCK or VK_CAPITAL
*
*/
static void KEYBOARD_GenerateMsg( WORD vkey, WORD scan, int Evtype, DWORD event_time )
{
    BOOL * State = (vkey==VK_NUMLOCK? &NumState : &CapsState);
    DWORD up, down;
    
    if (*State) {
        /* The INTERMEDIARY state means : just after a 'press' event, if a 'release' event comes,
        don't treat it. It's from the same key press. Then the state goes to ON.
        And from there, a 'release' event will switch off the toggle key. */
        *State=FALSE;
        TRACE("INTERM : don't treat release of toggle key. key_state_table[%#x] = %#x\n",
              vkey,key_state_table[vkey]);
    } else
    {
        down = (vkey==VK_NUMLOCK ? KEYEVENTF_EXTENDEDKEY : 0);
        up = (vkey==VK_NUMLOCK ? KEYEVENTF_EXTENDEDKEY : 0) | KEYEVENTF_KEYUP;
	if ( key_state_table[vkey] & 0x1 ) /* it was ON */
        {
	    if (Evtype!=kKeyPress)
            {
		TRACE("ON + kKeyRelease => generating DOWN and UP messages.\n");
	        QDRV_send_keyboard_input( vkey, scan, down, event_time, 0, 0 );
	        QDRV_send_keyboard_input( vkey, scan, up, event_time, 0, 0 );
		*State=FALSE;
		key_state_table[vkey] &= ~0x01; /* Toggle state to off. */
            }
        }
	else /* it was OFF */
            if (Evtype==kKeyPress)
	    {
                TRACE("OFF + Keypress => generating DOWN and UP messages.\n");
                QDRV_send_keyboard_input( vkey, scan, down, event_time, 0, 0 );
                QDRV_send_keyboard_input( vkey, scan, up, event_time, 0, 0 );
                *State=TRUE; /* Goes to intermediary state before going to ON */
                key_state_table[vkey] |= 0x01; /* Toggle state to on. */
	    }
    }
}

void QDRV_KeyEvent(int pressed, int keyCode, UniChar c, unsigned int modifier)
{
    char Str[24];
    WORD vkey = 0, bScan;
    DWORD dwFlags;
    DWORD event_time = EVENT_x11_time_to_win32_time(GetTickCount());
    
    FIXME_(key)("semi-stub: pressed %d, keycode %d char %02x (%c) modifier %x\n", pressed, keyCode, c, (char) c, modifier);

    TRACE_(key)("CapsLockKeyMask : %s\n", (modifier & CapsLockKeyMask)?"YES":"NO");
    TRACE_(key)("ShiftKeyMask : %s\n", (modifier & ShiftKeyMask)?"YES":"NO");
    TRACE_(key)("ControlKeyMask : %s\n", (modifier & ControlKeyMask)?"YES":"NO");
    TRACE_(key)("AlternateKeyMask : %s\n", (modifier & AlternateKeyMask)?"YES":"NO");
    TRACE_(key)("CommandKeyMask : %s\n", (modifier & CommandKeyMask)?"YES":"NO");
    TRACE_(key)("NumericPadKeyMask : %s\n", (modifier & NumericPadKeyMask)?"YES":"NO");
    TRACE_(key)("HelpKeyMask : %s\n", (modifier & HelpKeyMask)?"YES":"NO");
    TRACE_(key)("FunctionKeyMask : %s\n", (modifier & FunctionKeyMask)?"YES":"NO");
    TRACE_(key)("DeviceIndependentModifierFlagsMask : %s\n", (modifier & DeviceIndependentModifierFlagsMask)?"YES":"NO");
        
    /* FIXME make menu shortcut : alt+key work but ... */
    if (pressed == kKeyPress && (modifier & AlternateKeyMask) )
    {
        vkey = VK_MENU;
        dwFlags = 0;
        QDRV_send_keyboard_input( vkey & 0xff, 0, dwFlags, event_time, 0, 0 );
    }
   
    /* set Command+Alt for AltGr */
    AltGrMask = modifier & (CommandKeyMask | AlternateKeyMask);
    
    vkey = keycode_to_vkey[keyCode];
    
    TRACE_(key)("keycode 0x%x converted to vkey 0x%x\n",
                keyCode, vkey);
    
    if (vkey)
    {
        switch (vkey & 0xff)
        {
            case VK_NUMLOCK:
                KEYBOARD_GenerateMsg( VK_NUMLOCK, 0x45, pressed, event_time );
                break;
            case VK_CAPITAL:
                TRACE("Caps Lock event. (pressed %d). State before : %#.2x\n", pressed, key_state_table[vkey]);
                KEYBOARD_GenerateMsg( VK_CAPITAL, 0x3A, pressed, event_time );
                TRACE("State after : %#.2x\n", key_state_table[vkey]);
                break;
            default:
                /* Adjust the NUMLOCK state if it has been changed outside wine */
                /* Key Pad is always 'lock' on Mac */
            #if 0
                if (!(key_state_table[VK_NUMLOCK] & 0x01) != !(event->state & NumLockMask))
                {
                    TRACE("Adjusting NumLock state.\n");
                    KEYBOARD_GenerateMsg( VK_NUMLOCK, 0x45, kKeyPress, event_time );
                    KEYBOARD_GenerateMsg( VK_NUMLOCK, 0x45, kKeyRelease, event_time );
                }
            #endif
                /* Adjust the CAPSLOCK state if it has been changed outside wine */
                if (!(key_state_table[VK_CAPITAL] & 0x01) != !(modifier & CapsLockKeyMask))
                {
                    TRACE("Adjusting Caps Lock state.\n");
                    KEYBOARD_GenerateMsg( VK_CAPITAL, 0x3A, kKeyPress, event_time );
                    KEYBOARD_GenerateMsg( VK_CAPITAL, 0x3A, kKeyRelease, event_time );
                }
                /* Not Num nor Caps : end of intermediary states for both. */
                NumState = FALSE;
                CapsState = FALSE;
                
                bScan = (WORD) c; //keyc2scan[event->keycode] & 0xFF;
                TRACE_(key)("bScan = 0x%02x.\n", bScan);
                
                dwFlags = 0;
                if (pressed == kKeyRelease) dwFlags |= KEYEVENTF_KEYUP;
                if ( vkey & 0x100 )         dwFlags |= KEYEVENTF_EXTENDEDKEY;

                QDRV_send_keyboard_input( vkey & 0xff, bScan, dwFlags, event_time, 0, 0 );
                
                /* FIXME make menu shortcut : alt+key work but ... */
                if ( pressed == kKeyRelease && (modifier & AlternateKeyMask) )
                {
                    vkey = VK_MENU;
                    dwFlags = 0;
                    if (pressed == kKeyRelease) dwFlags |= KEYEVENTF_KEYUP;
                    QDRV_send_keyboard_input( vkey & 0xff, bScan, dwFlags, event_time, 0, 0 );
                }
        }
    }
}

UINT QDRV_GetKeyboardLayoutList(INT size, HKL *hkl)
{
    INT i;

    TRACE("%d, %p\n", size, hkl);

    if (!size)
    {
        size = 4096; /* hope we will never have that many */
        hkl = NULL;
    }

    for (i = 0; main_key_tab[i].comment && (i < size); i++)
    {
        if (hkl)
        {
            ULONG_PTR layout = main_key_tab[i].lcid;
            LANGID langid;

            /* see comment for GetKeyboardLayout */
            langid = PRIMARYLANGID(LANGIDFROMLCID(layout));
            if (langid == LANG_CHINESE || langid == LANG_JAPANESE || langid == LANG_KOREAN)
                layout |= 0xe001 << 16; /* FIXME */
            else
                layout |= layout << 16;

            hkl[i] = (HKL)layout;
        }
    }
    return i;
}


/***********************************************************************
 *		GetKeyboardLayout (X11DRV.@)
 */
HKL QDRV_GetKeyboardLayout(DWORD dwThreadid)
{
    ULONG_PTR layout;
    LANGID langid;

    if (dwThreadid && dwThreadid != GetCurrentThreadId())
        FIXME("couldn't return keyboard layout for thread %04lx\n", dwThreadid);

#if 0
    layout = main_key_tab[kbd_layout].lcid;
#else
    /* FIXME:
     * Winword uses return value of GetKeyboardLayout as a codepage
     * to translate ANSI keyboard messages to unicode. But we have
     * a problem with it: for instance Polish keyboard layout is
     * identical to the US one, and therefore instead of the Polish
     * locale id we return the US one.
     */
    layout = GetUserDefaultLCID();
#endif
    /* 
     * Microsoft Office expects this value to be something specific
     * for Japanese and Korean Windows with an IME the value is 0xe001
     * We should probably check to see if an IME exists and if so then
     * set this word properly.
     */
    langid = PRIMARYLANGID(LANGIDFROMLCID(layout));
    if (langid == LANG_CHINESE || langid == LANG_JAPANESE || langid == LANG_KOREAN)
        layout |= 0xe001 << 16; /* FIXME */
    else
        layout |= layout << 16;

    return (HKL)layout;
}


/***********************************************************************
 *		GetKeyboardLayoutName (X11DRV.@)
 */
BOOL QDRV_GetKeyboardLayoutName(LPWSTR name)
{
    static const WCHAR formatW[] = {'%','0','8','l','x',0};
    DWORD layout;
    LANGID langid;

    layout = main_key_tab[kbd_layout].lcid;
    /* see comment for GetKeyboardLayout */
    langid = PRIMARYLANGID(LANGIDFROMLCID(layout));
    if (langid == LANG_CHINESE || langid == LANG_JAPANESE || langid == LANG_KOREAN)
        layout |= 0xe001 << 16; /* FIXME */
    else
        layout |= layout << 16;

    sprintfW(name, formatW, layout);
    TRACE("returning %s\n", debugstr_w(name));
    return TRUE;
}


/***********************************************************************
 *		LoadKeyboardLayout (X11DRV.@)
 */
HKL QDRV_LoadKeyboardLayout(LPCWSTR name, UINT flags)
{
    FIXME("%s, %04x: stub!\n", debugstr_w(name), flags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}


/***********************************************************************
 *		UnloadKeyboardLayout (X11DRV.@)
 */
BOOL QDRV_UnloadKeyboardLayout(HKL hkl)
{
    FIXME("%p: stub!\n", hkl);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/***********************************************************************
 *		ActivateKeyboardLayout (X11DRV.@)
 */
HKL QDRV_ActivateKeyboardLayout(HKL hkl, UINT flags)
{
    FIXME("%p, %04x: stub!\n", hkl, flags);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return 0;
}


/***********************************************************************
 *		ToUnicodeEx (X11DRV.@)
 *
 * The ToUnicode function translates the specified virtual-key code and keyboard
 * state to the corresponding Windows character or characters.
 *
 * If the specified key is a dead key, the return value is negative. Otherwise,
 * it is one of the following values:
 * Value	Meaning
 * 0	The specified virtual key has no translation for the current state of the keyboard.
 * 1	One Windows character was copied to the buffer.
 * 2	Two characters were copied to the buffer. This usually happens when a
 *      dead-key character (accent or diacritic) stored in the keyboard layout cannot
 *      be composed with the specified virtual key to form a single character.
 *
 * FIXME : should do the above (return 2 for non matching deadchar+char combinations)
 *
 */
INT QDRV_ToUnicodeEx(UINT virtKey, UINT scanCode, LPBYTE lpKeyState,
		     LPWSTR bufW, int bufW_size, UINT flags, HKL hkl)
{
    INT ret;
    int keyc;
    char lpChar[10];
    HWND focus;

    FIXME(":stub virtKey=%x scanCode=%x flags=0x%08x\n", virtKey, scanCode, flags);
    lpChar[0] = scanCode & 0xff;
    
    if (scanCode & 0x8000)
    {
        TRACE("Key UP, doing nothing\n" );
        return 0;
    }

    if (hkl != QDRV_GetKeyboardLayout(0))
        FIXME("keyboard layout %p is not supported\n", hkl);

    if ((lpKeyState[VK_MENU] & 0x80) && (lpKeyState[VK_CONTROL] & 0x80))
    {
        TRACE("Ctrl+Alt+[key] won't generate a character\n");
        return 0;
    }


    focus = GetFocus();
    if (focus) focus = GetAncestor( focus, GA_ROOT );
    if (!focus) focus = GetActiveWindow();
//   e.window = X11DRV_get_whole_window( focus );
 //  xic = X11DRV_get_ic( focus );

    if (lpKeyState[VK_SHIFT] & 0x80)
    {
//	e.state |= ShiftMask;
    }
    if (lpKeyState[VK_CAPITAL] & 0x01)
    {
//	e.state |= LockMask;
    }
    if (lpKeyState[VK_CONTROL] & 0x80)
    {
//	e.state |= ControlMask;
    }
    if (lpKeyState[VK_NUMLOCK] & 0x01)
    {
//	e.state |= NumLockMask;
    }

#if 0
    /* Restore saved AltGr state */
    TRACE("AltGrMask = %04x\n", AltGrMask);
    e.state |= AltGrMask;

    TRACE_(key)("(%04X, %04X) : faked state = 0x%04x\n",
		virtKey, scanCode, e.state);
    wine_tsx11_lock();
    /* We exit on the first keycode found, to speed up the thing. */
    for (keyc=min_keycode; (keyc<=max_keycode) && (!e.keycode) ; keyc++)
      { /* Find a keycode that could have generated this virtual key */
          if  ((keyc2vkey[keyc] & 0xFF) == virtKey)
          { /* We filter the extended bit, we don't know it */
              e.keycode = keyc; /* Store it temporarily */
              if ((EVENT_event_to_vkey(xic,&e) & 0xFF) != virtKey) {
                  e.keycode = 0; /* Wrong one (ex: because of the NumLock
                         state), so set it to 0, we'll find another one */
              }
	  }
      }

    if ((virtKey>=VK_NUMPAD0) && (virtKey<=VK_NUMPAD9))
        e.keycode = XKeysymToKeycode(e.display, virtKey-VK_NUMPAD0+XK_KP_0);

    if (virtKey==VK_DECIMAL)
        e.keycode = XKeysymToKeycode(e.display, XK_KP_Decimal);

    if (!e.keycode && virtKey != VK_NONAME)
      {
	WARN("Unknown virtual key %X !!!\n", virtKey);
        wine_tsx11_unlock();
	return virtKey; /* whatever */
      }
    else TRACE("Found keycode %d (0x%2X)\n",e.keycode,e.keycode);

    TRACE_(key)("type %d, window %lx, state 0x%04x, keycode 0x%04x\n",
		e.type, e.window, e.state, e.keycode);

    if (xic)
        ret = XmbLookupString(xic, &e, lpChar, sizeof(lpChar), &keysym, &status);
    else
        ret = XLookupString(&e, lpChar, sizeof(lpChar), &keysym, NULL);
    wine_tsx11_unlock();

    if (ret == 0)
    {
	char dead_char;

#ifdef XK_EuroSign
        /* An ugly hack for EuroSign: X can't translate it to a character
           for some locales. */
        if (keysym == XK_EuroSign)
        {
            bufW[0] = 0x20AC;
            ret = 1;
            goto found;
        }
#endif
        /* Special case: X turns shift-tab into ISO_Left_Tab. */
        /* Here we change it back. */
        if (keysym == XK_ISO_Left_Tab)
        {
            bufW[0] = 0x09;
            ret = 1;
            goto found;
        }

	dead_char = KEYBOARD_MapDeadKeysym(keysym);
	if (dead_char)
	    {
	    MultiByteToWideChar(CP_UNIXCP, 0, &dead_char, 1, bufW, bufW_size);
	    ret = -1;
	    }
	else
	    {
	    char	*ksname;

            wine_tsx11_lock();
	    ksname = XKeysymToString(keysym);
            wine_tsx11_unlock();
	    if (!ksname)
		ksname = "No Name";
	    if ((keysym >> 8) != 0xff)
		{
		ERR("Please report: no char for keysym %04lX (%s) :\n",
                    keysym, ksname);
		ERR("(virtKey=%X,scanCode=%X,keycode=%X,state=%X)\n",
                    virtKey, scanCode, e.keycode, e.state);
		}
	    }
	}
    else {  /* ret != 0 */
        /* We have a special case to handle : Shift + arrow, shift + home, ...
           X returns a char for it, but Windows doesn't. Let's eat it. */
        if (!(e.state & NumLockMask)  /* NumLock is off */
            && (e.state & ShiftMask) /* Shift is pressed */
            && (keysym>=XK_KP_0) && (keysym<=XK_KP_9))
        {
            lpChar[0] = 0;
            ret = 0;
        }

        /* more areas where X returns characters but Windows does not
           CTRL + number or CTRL + symbol */
        if (e.state & ControlMask)
        {
            if (((keysym>=33) && (keysym < 'A')) ||
                ((keysym > 'Z') && (keysym < 'a')))
            {
                lpChar[0] = 0;
                ret = 0;
            }
        }

        /* We have another special case for delete key (XK_Delete) on an
         extended keyboard. X returns a char for it, but Windows doesn't */
        if (keysym == XK_Delete)
        {
            lpChar[0] = 0;
            ret = 0;
        }
	else if((lpKeyState[VK_SHIFT] & 0x80) /* Shift is pressed */
		&& (keysym == XK_KP_Decimal))
        {
            lpChar[0] = 0;
            ret = 0;
        }

	/* perform translation to unicode */
	if(ret)
	{
	    TRACE_(key)("Translating char 0x%02x to unicode\n", *(BYTE *)lpChar);
	    ret = MultiByteToWideChar(CP_UNIXCP, 0, lpChar, ret, bufW, bufW_size);
	}
    }
#endif
    ret = MultiByteToWideChar(CP_UNIXCP, 0, lpChar, 1, bufW, bufW_size);
found:
    TRACE_(key)("ToUnicode about to return %d with char %x %s\n",
		ret, (ret && bufW) ? bufW[0] : 0, bufW ? "" : "(no buffer)");
    return ret;
}

void QDRV_Beep(void)
{
    extern void NSBeep(void);
    NSBeep();
}


