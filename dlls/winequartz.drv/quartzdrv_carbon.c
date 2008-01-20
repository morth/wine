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

#import <Carbon/Carbon.h>
#include <dlfcn.h>
#include "wine_carbon.h"

#define AKObjectRef WindowRef
#define TRACE

extern unsigned int screen_width;
extern unsigned int screen_height;
extern AKObjectRef root_window;

#define check_err(err, ...) do { if (err != noErr) {fprintf(stderr, __VA_ARGS__);goto error;}} while (0)

static void *HIToolBoxDLHandle;

#define CARBON_FUNCT(fct) static typeof(fct) * carbonPtr_##fct;
CARBON_FUNCT(MoveWindowStructure)
CARBON_FUNCT(SetRect)
CARBON_FUNCT(SizeWindow)
CARBON_FUNCT(DisposeRgn)
CARBON_FUNCT(GetPortBounds)
CARBON_FUNCT(GetWindowPort)
CARBON_FUNCT(GetWindowRegion)
CARBON_FUNCT(NewRgn)
CARBON_FUNCT(ShowWindow)
CARBON_FUNCT(HideWindow)

static OSStatus QDRV_WindowEventHandler(EventHandlerCallRef handler, EventRef event, void *userData);
static OSStatus QDRV_AppleEventHandler(EventRef event);


static OSStatus QuitHandler (EventHandlerCallRef myHandler, EventRef event, void *userData) {
    OSStatus status = noErr;
#ifdef TRACE
    fprintf (stderr, "QuitHandler myHandler=%p event=%p userData=%p\n", myHandler, event, userData);
#endif
    extern void PostQuitMessage(int );
    PostQuitMessage(0);
    return status;
}

static OSStatus QuitMenuHandler (EventHandlerCallRef myHandler, EventRef event, void *userData) {
    OSStatus err = eventNotHandledErr;
    HICommand commandStruct;
#ifdef TRACE
    fprintf (stderr, "QuitMenuHandler stub: myHandler=%p event=%p userData=%p\n", myHandler, event, userData);
#endif
    err = GetEventParameter (event, kEventParamDirectObject, typeHICommand, NULL, sizeof(HICommand), NULL, &commandStruct);
    require_noerr(err, error);
    
    if (commandStruct.commandID == kHICommandQuit) {
        EventRef e;
        
        err = CreateEvent(NULL, kEventClassApplication, kEventAppQuit, 0, kEventAttributeNone, &e);
        require_noerr(err, error);
        
        // should this be the application target?
        err = SendEventToEventTarget(e, GetEventDispatcherTarget());
        require_noerr(err, error);
        
        HiliteMenu(0);
        err = noErr;
    }
    
error:
        return err;
}

static void InstallEventHandlers(void)
{
    EventTypeSpec EventType;
    EventHandlerUPP handler;
    
    handler = NewEventHandlerUPP(QuitHandler);
    EventType.eventClass = kEventClassApplication;
    EventType.eventKind = kEventAppQuit;
    InstallApplicationEventHandler(handler, 1, &EventType, NULL, NULL);
    
    handler = NewEventHandlerUPP(QuitMenuHandler);
    EventType.eventClass = kEventClassCommand;
    EventType.eventKind = kEventCommandProcess;
    InstallApplicationEventHandler(handler, 1, &EventType, NULL, NULL);	
}

WindowRef QDRV_CreateNewWindow(int x, int y, int width, int height)
{
    static EventHandlerUPP QDRV_WindowEventHandlerUPP = NULL;
    OSStatus err;
    WindowRef window;
    WindowAttributes attrs;
    Rect bounds;
    const EventTypeSpec	windowEvents[]	=   {   { kEventClassCommand,   kEventCommandProcess },
                                                { kEventClassCommand,   kEventCommandUpdateStatus },
                                                { kEventClassKeyboard, 	kEventRawKeyDown },
                                                { kEventClassWindow,    kEventWindowClickContentRgn },
                                                { kEventClassWindow, 	kEventWindowDrawContent },
                                                { kEventClassWindow,    kEventWindowBoundsChanged },
                                                { kEventClassWindow,    kEventWindowClose },
                                                { kEventClassMouse,     kEventMouseEntered },
                                                { kEventClassMouse,     kEventMouseExited }
                                            };
#ifdef TRACE
    fprintf (stderr, "QDRV_CreateNewWindow stub: x %d y %d w %d h %d\n", x, y, width, height);
#endif
    attrs = kWindowStandardHandlerAttribute | kWindowCompositingAttribute | kWindowNoTitleBarAttribute;

    CARBON_SetRect(&bounds, x, y, width, height);
    err = CreateNewWindow(kDocumentWindowClass, attrs, &bounds, &window);
    check_err(err, "Can't create window\n");
           
    if (QDRV_WindowEventHandlerUPP == NULL)
    {
        QDRV_WindowEventHandlerUPP = NewEventHandlerUPP( QDRV_WindowEventHandler );
        if (QDRV_WindowEventHandlerUPP == NULL)
            return NULL;
    }
    
    err = InstallWindowEventHandler( window, QDRV_WindowEventHandlerUPP, GetEventTypeCount(windowEvents), windowEvents, window, NULL );
    check_err(err, "Can't install window event handler\n");
error:
    return window;
}

#ifdef TRACE
static char *event_class_str(UInt32 eventClass)
{
    switch (eventClass)
    {
    case kEventClassMouse: return "kEventClassMouse";
    case kEventClassKeyboard: return "kEventClassKeyboard";
    case kEventClassTextInput: return "kEventClassTextInput";
    case kEventClassApplication: return "kEventClassApplication";
    case kEventClassAppleEvent: return "kEventClassAppleEvent";
    case kEventClassMenu: return "kEventClassMenu";
    case kEventClassControl: return "kEventClassControl";
    case kEventClassAccessibility: return "kEventClassAccessibility";
    case kEventClassAppearance: return "kEventClassAppearance";
    case kEventClassService: return "kEventClassService";
    case kEventClassCommand: return "kEventClassCommand";
    default: return "unknownClass";
    }
}

static char *event_window_kind_str(UInt32 eventKind)
{
    switch (eventKind)
    {
    case kEventWindowDrawContent: return "kEventWindowDrawContent";
    case kEventWindowClose: return "kEventWindowClose";      
    case kEventWindowClickContentRgn: return "kEventWindowClickContentRgn";
    case kEventWindowBoundsChanged: return "kEventWindowBoundsChanged";
    default: return "unknownWindowKind";
    }
}

static char *event_application_kind_str(UInt32 eventKind)
{
    switch (eventKind)
    {
    case kEventAppActivated: return "kEventAppActivated";
    case kEventAppDeactivated: return "kEventAppDeactivated";      
    case kEventAppQuit: return "kEventAppQuit";
    case kEventAppLaunchNotification: return "kEventAppLaunchNotification";
    case kEventAppLaunched: return "kEventAppLaunched";
    case kEventAppTerminated: return "kEventAppTerminated";
    case kEventAppFrontSwitched: return "kEventAppFrontSwitched";
    case kEventAppFocusMenuBar: return "kEventAppFocusMenuBar";
    case kEventAppGetDockTileMenu: return "kEventAppGetDockTileMenu";
    case kEventAppIsEventInInstantMouser: return "kEventAppIsEventInInstantMouser";
    case kEventAppHidden: return "kEventAppHidden";
    case kEventAppShown: return "kEventAppShown";
    case kEventAppSystemUIModeChanged: return "kEventAppSystemUIModeChanged";
    case kEventAppAvailableWindowBoundsChanged: return "kEventAppAvailableWindowBoundsChanged";
    case kEventAppActiveWindowChanged: return "kEventAppActiveWindowChanged";
        
    default: return "unknownAppKind";
    }
}


static char *event_appleevent_kind_str(UInt32 eventKind)
{
    switch (eventKind)
    {
    case kEventAppleEvent: return "kEventAppleEvent";
    default: return "unknownAppleEventKind";
    }
}

static char *event_mouse_kind_str(UInt32 eventKind)
{
    switch (eventKind)
    {
    case kEventMouseDown: return "kEventMouseDown";
    case kEventMouseUp: return "kEventMouseUp";      
    case kEventMouseMoved: return "kEventMouseMoved";
    case kEventMouseDragged: return "kEventMouseDragged";
    case kEventMouseEntered: return "kEventMouseEntered";
    case kEventMouseExited: return "kEventMouseExited";
    case kEventMouseWheelMoved: return "kEventMouseWheelMoved";
    default: return "unknownMouseKind";
    }
}

static char *event_command_kind_str(UInt32 eventKind)
{
    switch (eventKind)
    {
    case kEventCommandUpdateStatus: return "kEventCommandUpdateStatus";
    case kEventCommandProcess: return "kEventCommandProcess";
    default: return "unknownCommandKind";
    }
}
#endif

static OSStatus QDRV_WindowEventHandler(EventHandlerCallRef handler, EventRef event, void *userData)
{
    extern void QDRV_Expose(WindowRef );
    
    OSStatus err = eventNotHandledErr;
    UInt32 eventKind;
    UInt32 eventClass;
    WindowRef window;
    
    window  = (WindowRef)userData;
    eventKind = GetEventKind(event);
    eventClass = GetEventClass(event);
#ifdef TRACE
    fprintf (stderr, "%s handler=%p event=%p userData=%p eventClass=%4.4s ( %s )\n",
             __FUNCTION__, handler, event, userData, (char *) &eventClass, event_class_str(eventClass));
#endif
    switch (eventClass)
    {
        case kEventClassMouse:
#ifdef TRACE
            fprintf (stderr, "%s eventKind %u ( %s) \n", __FUNCTION__, eventKind, event_mouse_kind_str(eventKind));
#endif
            if (eventKind == kEventMouseEntered )
            {
                Point   where;
                GetEventParameter(event, kEventParamMouseLocation, typeQDPoint, NULL, sizeof(Point), NULL, &where);
                QDGlobalToLocalPoint(GetWindowPort(window), &where);
            
                QDRV_EnterNotify(window, where.h, where.v);
                err = noErr;
            }
            else
                if (eventKind == kEventMouseExited )
                {
                }
            break;
            
        case kEventClassKeyboard:
            if (eventKind == kEventRawKeyDown )
            {
            }
            break;
            
        case kEventClassWindow:
#ifdef TRACE
            fprintf (stderr, "%s eventKind %u ( %s) \n", __FUNCTION__, eventKind, event_window_kind_str(eventKind));
#endif
            if ( eventKind == kEventWindowClickContentRgn )
            {
            }
            else if ( eventKind == kEventWindowBoundsChanged )
                {
                }
            break;
            
        case kEventClassCommand:
        {
            HICommandExtended   command;
            GetEventParameter( event, kEventParamDirectObject, typeHICommand, NULL, sizeof(HICommand), NULL, &command );
#ifdef TRACE
            fprintf (stderr, "%s eventKind %u ( %s ) commandID %4.4s\n", __FUNCTION__, eventKind, event_command_kind_str(eventKind), (char *)&command.commandID);
#endif
        }
            break;
    }
    
    return err;
}


void QDRV_SetWindowFrame(WindowRef win, int x, int y, int width, int height)
{
#ifdef TRACE
    fprintf (stderr, "%s win=%p [%d %d] [%d %d]\n", __FUNCTION__, win, x, y, width, height);
#endif
    CARBON_MoveWindowStructure(win, x, y);
    CARBON_SizeWindow(win, width, height, 1);
}

void QDRV_SetWindowTitle(AKObjectRef win, char *text)
{
    CFStringRef title;
    
    title = CFStringCreateWithCString(kCFAllocatorDefault, title, kCFStringEncodingWindowsLatin1);
    SetWindowTitleWithCFString(win, title);
    
    CFRelease(title);
}

static int QDRV_ProcessMouseEvent(EventRef event, UInt32 kind)
{
    Point point;
    WindowRef window;
    WindowPartCode partCode;
        
    GetEventParameter(event, kEventParamMouseLocation, typeQDPoint, NULL,
                                sizeof(Point), NULL, &point);
                    
    partCode = MacFindWindow(point, &window);
                                  
#ifdef TRACE
    fprintf (stderr, "%s window=%p location (%d %d) partCode=%d\n", __FUNCTION__, window, point.h, point.v, (int) partCode);
#endif
    switch (kind)
    {
        case kEventMouseDown:
            if (partCode == inMenuBar)
            {
                MenuSelect(point);
            }
            else
            {
                UInt16 button;
                GetEventParameter(event, kEventParamMouseButton, typeMouseButton, NULL,
                                  sizeof(UInt16), NULL, &button);
                
                QDGlobalToLocalPoint(GetWindowPort(window), &point);
                QDRV_ButtonPress(window, point.h, point.v, (int) button);
            }
            goto done;
            break;
        case kEventMouseUp:    
            if (partCode != inMenuBar)
            {
                UInt16 button;
                GetEventParameter(event, kEventParamMouseButton, typeMouseButton, NULL,
                                  sizeof(UInt16), NULL, &button);
                
                QDGlobalToLocalPoint(GetWindowPort(window), &point);
                QDRV_ButtonRelease(window, point.h, point.v, (int) button);
                goto done;
            }
            break;
        case kEventMouseMoved:
            if (partCode == inContent)
                QDRV_MotionNotify(window, point.h, point.v);
            else /* not one of our Windows so pass the desktop window */
                QDRV_MotionNotify((WindowRef) CGMainDisplayID(), point.h, point.v);
            goto done;
            break;
    }

    return 0;
done:
    return 1;
}

int QDRV_AppKitGetEvent(void)
{
    EventRef event;
    WindowRef window;
    if (ReceiveNextEvent(0, NULL, kEventDurationNoWait, true, &event) == noErr) {
    
        UInt32 eventKind = GetEventKind(event);
        UInt32 eventClass = GetEventClass(event);
        
#ifdef TRACE
        fprintf (stderr, "%s eventClass %4.4s ( %s ) eventKind 0x%08x ( ", __FUNCTION__, (char *) &eventClass, event_class_str(eventClass), eventKind);
        if (eventClass == kEventClassApplication)
            fprintf (stderr, "%s", event_application_kind_str(eventKind) );
        if (eventClass == kEventClassMouse)
            fprintf (stderr, "%s", event_mouse_kind_str(eventKind) );
        if (eventClass == kEventClassAppleEvent)
            fprintf (stderr, "%s", event_appleevent_kind_str(eventKind));
        fprintf (stderr, " )\n");
#endif
        switch (eventClass)
        {
            case kEventClassMouse:
                if (QDRV_ProcessMouseEvent(event, eventKind))
                    goto finish;
                break;
                
            case kEventClassAppleEvent:
                if (eventKind == kEventAppleEvent)
                    QDRV_AppleEventHandler(event);
                break;
                
            case kEventClassApplication:
                if (eventKind == kEventAppActiveWindowChanged)
                {
                    GetEventParameter(event, kEventParamCurrentWindow, typeWindowRef, NULL,
                                      sizeof(WindowRef), NULL, &window);
                                      
                    QDRV_Map(window);
                    goto finish;
                }
                break;
        }
        
	SendEventToEventTarget(event, GetEventDispatcherTarget());
finish:
	ReleaseEvent(event);
	return 1;
    }
    return 0;
}

/* From an Apple Sample Code */
OSStatus QDRV_AppleEventHandler(EventRef event)
{
    int release = 0;
    EventRecord eventRecord;
    OSErr err;
#ifdef TRACE
    fprintf (stderr, "%s event=%p\n", __FUNCTION__, event);
#endif
    // Events of type kEventAppleEvent must be removed from the queue 
    //  before being passed to AEProcessAppleEvent.
    if (IsEventInQueue(GetMainEventQueue(), event))
    {
        // RemoveEventFromQueue will release the event, which will 
        //  destroy it if we don't retain it first.
        RetainEvent(event);
        release = 1;
        RemoveEventFromQueue(GetMainEventQueue(), event);
    }
 
    // Convert the event ref to the type AEProcessAppleEvent expects.
    ConvertEventRefToEventRecord(event, &eventRecord);
    err = AEProcessAppleEvent(&eventRecord);
 
    if (release)
        ReleaseEvent(event);
 
    // This Carbon event has been handled, even if no AppleEvent handlers 
    //  were installed for the Apple event.
    return noErr;
}

static void BindCarbonFunctions(void)
{    
    HIToolBoxDLHandle = dlopen("/System/Library/Frameworks/Carbon.framework/Frameworks/HIToolbox.framework/HIToolbox", RTLD_LAZY | RTLD_LOCAL);
    
    if (!HIToolBoxDLHandle)
    {
        fprintf(stderr, "%s impossible d'ouvrir HIToolBoxDLHandle\n", __FUNCTION__);
        return;
    }
#define LOAD_FUNCTION(f) \
    if((carbonPtr_##f = dlsym(HIToolBoxDLHandle, #f)) == NULL) \
    { \
        fprintf(stderr, "%s Can't find symbol %s\n", __FUNCTION__,  #f); \
        return;                                  \
    }
    LOAD_FUNCTION(MoveWindowStructure)
    LOAD_FUNCTION(SizeWindow)
    LOAD_FUNCTION(ShowWindow)
    LOAD_FUNCTION(HideWindow)
    LOAD_FUNCTION(SetRect)
    LOAD_FUNCTION(DisposeRgn)
    LOAD_FUNCTION(GetPortBounds)
    LOAD_FUNCTION(GetWindowPort)
    LOAD_FUNCTION(GetWindowRegion)
    LOAD_FUNCTION(NewRgn)
#undef LOAD_FUNCTION
}

void QDRV_InitializeCarbon(void)
{
    ProcessSerialNumber psn;
        
    GetProcessForPID(getpid(), &psn);
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);
    SetFrontProcess(&psn);
    
    InstallEventHandlers();
    
    BindCarbonFunctions();
}

void QDRV_FinalizeCarbon(void)
{
    dlclose(HIToolBoxDLHandle);
}

OSStatus CARBON_MoveWindowStructure(WindowRef w, short x, short y)
{
    if (carbonPtr_MoveWindowStructure) return carbonPtr_MoveWindowStructure(w, x, y);
    return 1;
}

void CARBON_SetRect(Rect *r, short x, short y, short width, short height)
{
    if (carbonPtr_SetRect) carbonPtr_SetRect(r, x, y, width, height);
}

void CARBON_SizeWindow(WindowRef window, short w, short h, Boolean fUpdate)
{
    if (carbonPtr_SizeWindow) carbonPtr_SizeWindow(window, w, h, fUpdate);
}

void CARBON_DisposeRgn(RgnHandle rgn)
{
    if (carbonPtr_DisposeRgn) return carbonPtr_DisposeRgn(rgn);
}

Rect *CARBON_GetPortBounds(CGrafPtr port, Rect *rect)
{
    if (carbonPtr_GetPortBounds) return carbonPtr_GetPortBounds(port, rect);
    return rect;
}

CGrafPtr CARBON_GetWindowPort(WindowRef w)
{
    if (carbonPtr_GetWindowPort) return carbonPtr_GetWindowPort(w);
    return NULL;
}

OSStatus CARBON_GetWindowRegion(WindowRef window, WindowRegionCode inRegionCode, RgnHandle ioWinRgn)
{
    if (carbonPtr_GetWindowRegion) return carbonPtr_GetWindowRegion(window, inRegionCode, ioWinRgn);
    return 1;
}

RgnHandle CARBON_NewRgn(void)
{
    if (carbonPtr_NewRgn) return carbonPtr_NewRgn();
    return NULL;
}

void CARBON_ShowWindow(WindowRef w)
{
    if (carbonPtr_ShowWindow) carbonPtr_ShowWindow(w);
}

void CARBON_HideWindow(WindowRef w)
{
    if (carbonPtr_HideWindow) carbonPtr_HideWindow(w);
}
