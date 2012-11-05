
#include "config.h"

#ifdef HAVE_CARBON_CARBON_H

#define GetCurrentProcess GetCurrentProcess_Mac
#define GetCurrentThread GetCurrentThread_Mac
#define LoadResource LoadResource_Mac
#define EqualRect EqualRect_Mac
#define FillRect FillRect_Mac
#define FrameRect FrameRect_Mac
#define GetCursor GetCursor_Mac
#define InvertRect InvertRect_Mac
#define OffsetRect OffsetRect_Mac
#define PtInRect PtInRect_Mac
#define SetCursor SetCursor_Mac
#define SetRect SetRect_Mac
#define ShowCursor ShowCursor_Mac
#define UnionRect UnionRect_Mac
#define Polygon Polygon_Mac
#define CheckMenuItem CheckMenuItem_Mac
#define DeleteMenu DeleteMenu_Mac
#define DrawMenuBar DrawMenuBar_Mac
#define EnableMenuItem EnableMenuItem_Mac
#define GetMenu GetMenu_Mac
#define IsWindowVisible IsWindowVisible_Mac
#define MoveWindow MoveWindow_Mac
#define ShowWindow ShowWindow_Mac
#include <Carbon/Carbon.h>
#undef GetCurrentProcess
#undef GetCurrentThread
#undef LoadResource
#undef EqualRect
#undef FillRect
#undef FrameRect
#undef GetCursor
#undef InvertRect
#undef OffsetRect
#undef PtInRect
#undef SetCursor
#undef SetRect
#undef ShowCursor
#undef UnionRect
#undef Polygon
#undef CheckMenuItem
#undef DeleteMenu
#undef DrawMenuBar
#undef EnableMenuItem
#undef GetMenu
#undef IsWindowVisible
#undef MoveWindow
#undef ShowWindow
#undef DPRINTF

#include "winehostproxy.h"

#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(winehostproxy);

OSErr launch_appleevent_callback(const AppleEvent *event, AppleEvent *reply, SRefCon handlerRefcon)
{
    AEDesc *dstDesc = (AEDesc*)handlerRefcon;

    TRACE("event %p\n", event);

    AEDisposeDesc(dstDesc);
    AEDuplicateDesc(event, dstDesc);
    return noErr;
}

int get_launch_appleevent(AEDesc *dst)
{
    OSStatus oserr;

    AEInitializeDesc(dst);

    oserr = AEInstallEventHandler(typeWildCard, typeWildCard, launch_appleevent_callback, (SRefCon)dst, false);
    if (oserr)
    {
        WINE_ERR("AEInstallEventHandler failed %d.\n", (int)oserr);
        return -1;
    }

    while (dst->descriptorType == typeNull)
    {
        static const EventTypeSpec appleEventSpec = { kEventClassAppleEvent, kEventAppleEvent };
        EventRef event;

        /* Wait up to 10 seconds for an AppleEvent. */
        oserr = ReceiveNextEvent(1, &appleEventSpec, 10.0, kEventRemoveFromQueue, &event);
        if (oserr)
        {
            if (oserr == eventLoopTimedOutErr)
                TRACE("timed out\n");
            else
                WINE_ERR("ReceiveNextEvent err = %d\n", (int)oserr);
            break;
        }
        oserr = AEProcessEvent(event);
        ReleaseEvent(event);
        if (oserr)
        {
            WINE_ERR("AEProcessEvent err = %d\n", (int)oserr);
            break;
        }

        if (dst->descriptorType == typeAppleEvent && WINE_TRACE_ON(winehostproxy))
        {
            AEEventClass evcl;
            AEEventID evid;

            if (!AEGetAttributePtr(dst, keyEventClassAttr, typeWildCard, NULL, &evcl, sizeof(evcl), NULL)
                    && !AEGetAttributePtr(dst, keyEventIDAttr, typeWildCard, NULL, &evid, sizeof(evid), NULL))
            {
                TRACE("event is %c%c%c%c/%c%c%c%c\n", 
                        (int)(evcl >> 24) & 0xFF, (int)(evcl >> 16) & 0xFF, (int)(evcl >> 8) & 0xFF, (int)evcl & 0xFF,
                        (int)(evid >> 24) & 0xFF, (int)(evid >> 16) & 0xFF, (int)(evid >> 8) & 0xFF, (int)evid & 0xFF);
            }
        }
    }

    AERemoveEventHandler(typeWildCard, typeWildCard, launch_appleevent_callback, false);
    return 0;
}


static OSStatus get_files_from_odoc_appleevent(AEDesc *event, WCHAR ***out_args, int *nargs)
{
    OSStatus oserr;
    AEDesc doclist;
    long count;
    long i;
    Size urllen;

    oserr = AEGetParamDesc(event, keyDirectObject, typeAEList, &doclist);
    if (oserr)
        return oserr;

    oserr = AECountItems(&doclist, &count);
    if (oserr)
        goto out;

    *out_args = HeapAlloc(GetProcessHeap(), 0, count * sizeof (**out_args));
    if (!*out_args)
    {
        oserr = kPOSIXErrorENOMEM;
        goto out;
    }

    *nargs = 0;
    for (i = 1 ; i <= count ; i++)
    {
        CFURLRef url;
        unsigned char urlbuf[4096];

        oserr = AEGetNthPtr(&doclist, i, typeFileURL, NULL, NULL, urlbuf, sizeof(urlbuf), &urllen);
        if (oserr)
        {
            WINE_ERR("AEGetNthPtr failed err = %d\n", (int)oserr);
            if (oserr == errAECoercionFail && WINE_TRACE_ON(winehostproxy))
            {
                AEDesc dataDesc;

                oserr = AEGetNthDesc(&doclist, i, typeWildCard, NULL, &dataDesc);
                if (!oserr)
                {
                    TRACE("Actual type is %c%c%c%c\n", 
                            (int)(dataDesc.descriptorType >> 24) & 0xFF, (int)(dataDesc.descriptorType >> 16) & 0xFF, (int)(dataDesc.descriptorType >> 8) & 0xFF, (int)dataDesc.descriptorType & 0xFF);
                    AEDisposeDesc(&dataDesc);
                }
            }
            continue;
        }
        if (urllen > sizeof(urlbuf))
        {
            WINE_ERR("buffer overflow\n");
            continue;
        }

        url = CFURLCreateWithBytes(NULL, urlbuf, urllen, kCFStringEncodingUTF8, NULL);
        if (url)
        {
            char pathbuf[PATH_MAX];
            WCHAR *file;
            int file_len;

            CFURLGetFileSystemRepresentation(url, true, (UInt8*)pathbuf, sizeof(pathbuf));

            file_len = MultiByteToWideChar(CP_UTF8, 0, pathbuf, strlen(pathbuf), NULL, 0);
            file = HeapAlloc(GetProcessHeap(), 0, (file_len + 1) * sizeof(WCHAR));
            if (file)
            {
                MultiByteToWideChar(CP_UTF8, 0, pathbuf, strlen(pathbuf), file, file_len);

                TRACE("path = %s\n", wine_dbgstr_w(file));
                (*out_args)[(*nargs)++] = file;
            }
            else
                WINE_ERR("out of memory\n");

            CFRelease(url);
        }
    }

    oserr = noErr;

out:
    AEDisposeDesc(&doclist);
    return oserr;
}

static OSStatus get_url_from_gurl_appleevent(AEDesc *event, WCHAR ***out_args)
{
    OSStatus oserr;
    char urlbuf[4096];
    Size urllen;
    WCHAR *file = NULL;
    int file_len;

    oserr = AEGetParamPtr(event, keyDirectObject, typeUTF8Text, NULL, urlbuf, sizeof(urlbuf), &urllen);
    if (oserr)
        return oserr;
    if (urllen > sizeof(urlbuf))
        return kPOSIXErrorEOVERFLOW;

    file_len = MultiByteToWideChar(CP_UTF8, 0, urlbuf, urllen, NULL, 0);
    file = HeapAlloc(GetProcessHeap(), 0, (file_len + 1) * sizeof(WCHAR));
    if (!file)
        return kPOSIXErrorENOMEM;
    MultiByteToWideChar(CP_UTF8, 0, urlbuf, urllen, file, file_len);
    file[file_len] = '\0';
    TRACE("URL = %s\n", wine_dbgstr_w(file));

    *out_args = HeapAlloc(GetProcessHeap(), 0, sizeof (**out_args));
    if (!*out_args)
    {
        HeapFree(GetProcessHeap(), 0, file);
        return kPOSIXErrorENOMEM;
    }

    **out_args = file;
    return noErr;
}

int get_appleevent_launch_args(WCHAR ***out_args, int *out_isurl)
{
    AEDesc desc;
    AEEventClass evcl;
    AEEventID evid;
    OSStatus oserr = 0;
    int nargs = 0;

    get_launch_appleevent(&desc);
    if (desc.descriptorType != typeAppleEvent)
        goto out;

    oserr = AEGetAttributePtr(&desc, keyEventClassAttr, typeWildCard, NULL, &evcl, sizeof(evcl), NULL);
    if (oserr)
        goto out;
    oserr = AEGetAttributePtr(&desc, keyEventIDAttr, typeWildCard, NULL, &evid, sizeof(evid), NULL);
    if (oserr)
        goto out;

    switch (evcl) {
    case kCoreEventClass:
        switch (evid) {
#if 0
        case kAEOpenApplication:
            break;
#endif
        case kAEOpenDocuments:
            *out_isurl = 0;
            oserr = get_files_from_odoc_appleevent(&desc, out_args, &nargs);
            break;
#if 0
        case kAEOpenContents:
            break;
#endif
        }
        break;
    case kInternetEventClass:
        switch (evid) {
        case kAEGetURL:
            *out_isurl = 1;
            nargs = 1;
            oserr = get_url_from_gurl_appleevent(&desc, out_args);
            break;
        }
        break;
    }

out:
    AEDisposeDesc(&desc);
    if (oserr)
    {
        WINE_WARN("Failed with error %d\n", (int)oserr);
        return -1;
    }
    return nargs;
}

#else /*HAVE_CARBON_CARBON_H*/

int get_appleevent_launch_args(WCHAR ***out_args, int *out_isurl)
{
    return 0;
}

#endif
