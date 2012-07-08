/*
 * Winemenubuilder support for Mac OS X Application Bundles
 *
 * Copyright 2011 Steven Edwards
 * Copyright 2011 - 2012 Per Johansson
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 *
 * NOTES: An Application Bundle generally has the following layout
 *
 * foo.app/Contents
 * foo.app/Contents/Info.plist
 * foo.app/Contents/MacOS/foo (can be script or real binary)
 * foo.app/Contents/Resources/appIcon.icns (Apple Icon format)
 * foo.app/Contents/Resources/English.lproj/infoPlist.strings
 * foo.app/Contents/Resources/English.lproj/MainMenu.nib (Menu Layout)
 *
 * There can be more to a bundle depending on the target, what resources
 * it contains and what the target platform but this simplifed format
 * is all we really need for now for Wine.
 *
 * TODO:
 * - Convert to using CoreFoundation API rather than standard unix file ops
 * - See if there is anything else in the rsrc section of the target that
 *   we might want to dump in a *.plist. Version information for the target
 *   and or Wine Version information come to mind.
 * - sha1hash of target application in bundle plist
 */

#ifdef __APPLE__

#include "config.h"
#include "wine/port.h"

#include <stdio.h>
#include <errno.h>

#include <CoreFoundation/CoreFoundation.h>
#ifdef HAVE_APPLICATIONSERVICES_APPLICATIONSERVICES_H
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
#include <ApplicationServices/ApplicationServices.h>
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
#undef DPRINTF
#endif

#define COBJMACROS
#define NONAMELESSUNION

#include <windows.h>
#include <shlobj.h>
#include <objidl.h>
#include <shlguid.h>
#include <appmgmt.h>
#include <tlhelp32.h>
#include <intshcut.h>
#include <shlwapi.h>
#include <initguid.h>

#include "wine/debug.h"
#include "wine/library.h"

#include "winemenubuilder.h"


WINE_DEFAULT_DEBUG_CHANNEL(menubuilder);

static char *mac_desktop_dir = NULL;
static char *wine_applications_dir = NULL;
static char *wine_associations_dir = NULL;

DEFINE_GUID(CLSID_WICIcnsEncoder, 0x312fb6f1,0xb767,0x409d,0x8a,0x6d,0x0f,0xc1,0x54,0xd4,0xf0,0x5c);

#define ICNS_SLOTS 6

static inline int size_to_slot(int size)
{
    switch (size)
    {
        case 16: return 0;
        case 32: return 1;
        case 48: return 2;
        case 128: return 3;
        case 256: return 4;
        case 512: return 5;
    }

    return -1;
}

HRESULT write_bundle_icon(IStream *icoStream, int exeIndex, LPCWSTR icoPathW,
        const char *destFilename, char *icnsName)
{
    ICONDIRENTRY *iconDirEntries = NULL;
    int numEntries;
    struct {
        int index;
        int maxBits;
    } best[ICNS_SLOTS];
    int indexes[ICNS_SLOTS];
    int i;
    LARGE_INTEGER zero;
    HRESULT hr;

    hr = read_ico_direntries(icoStream, &iconDirEntries, &numEntries);
    if (FAILED(hr))
        goto end;
    for (i = 0; i < ICNS_SLOTS; i++)
    {
        best[i].index = -1;
        best[i].maxBits = 0;
    }
    for (i = 0; i < numEntries; i++)
    {
        int slot;
        int width = iconDirEntries[i].bWidth ? iconDirEntries[i].bWidth : 256;
        int height = iconDirEntries[i].bHeight ? iconDirEntries[i].bHeight : 256;

        WINE_TRACE("[%d]: %d x %d @ %d\n", i, width, height, iconDirEntries[i].wBitCount);
        if (height != width)
            continue;
        slot = size_to_slot(width);
        if (slot < 0)
            continue;
        if (iconDirEntries[i].wBitCount >= best[slot].maxBits)
        {
            best[slot].index = i;
            best[slot].maxBits = iconDirEntries[i].wBitCount;
        }
    }
    numEntries = 0;
    for (i = 0; i < ICNS_SLOTS; i++)
    {
        if (best[i].index >= 0)
        {
            indexes[numEntries] = best[i].index;
            numEntries++;
        }
    }

    zero.QuadPart = 0;
    hr = IStream_Seek(icoStream, zero, STREAM_SEEK_SET, NULL);
    if (FAILED(hr))
    {
        WINE_WARN("seeking icon stream failed, error 0x%08X\n", hr);
        goto end;
    }
    hr = convert_to_native_icon(icoStream, indexes, numEntries, &CLSID_WICIcnsEncoder,
            icnsName, icoPathW);
    if (FAILED(hr))
    {
        WINE_WARN("converting %s to %s failed, error 0x%08X\n",
                wine_dbgstr_w(icoPathW), wine_dbgstr_a(icnsName), hr);
        goto end;
    }

end:
    HeapFree(GetProcessHeap(), 0, iconDirEntries);
    return hr;
}

static HRESULT create_icon_identifier(LPCWSTR icoPathW, char **nativeIdentifier)
{
    char *str, *p, *q;

    str = wchars_to_utf8_chars(icoPathW);
    p = strrchr(str, '\\');
    if (p == NULL)
        p = str;
    else
    {
        *p = 0;
        p++;
    }
    q = strrchr(p, '.');
    if (q)
        *q = 0;
    *nativeIdentifier = heap_printf("%s.icns", p);

    HeapFree(GetProcessHeap(), 0, str);
    return S_OK;
}

HRESULT appbundle_write_icon(IStream *icoStream, int exeIndex, LPCWSTR icoPathW,
        const char *destFilename, char **nativeIdentifier)
{
    if (*nativeIdentifier)
        return write_bundle_icon(icoStream, exeIndex, icoPathW, destFilename, *nativeIdentifier);
    return create_icon_identifier(icoPathW, nativeIdentifier);
}

CFDictionaryRef create_info_plist_dictionary(const char *pathname, const char *linkname, const char *icon)
{
    CFMutableDictionaryRef dict = NULL;
    CFStringRef pathstr;
    CFStringRef linkstr;

    pathstr = CFStringCreateWithCString(NULL, pathname, CFStringGetSystemEncoding());
    linkstr = CFStringCreateWithCString(NULL, linkname, CFStringGetSystemEncoding());
    if (!pathstr || !linkstr)
        goto cleanup;

    /* Create a dictionary that will hold the data. */
    dict = CFDictionaryCreateMutable( kCFAllocatorDefault,
            0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks );
    if (!dict)
        goto cleanup;

    /* Put the various items into the dictionary. */
    /* FIXME - Some values assumed the ought not to be */
    CFDictionarySetValue( dict, CFSTR("CFBundleDevelopmentRegion"), CFSTR("English") );
    CFDictionarySetValue( dict, CFSTR("CFBundleExecutable"), linkstr );
    /* FIXME - Avoid identifier if not unique. */
    /* CFDictionarySetValue( dict, CFSTR("CFBundleIdentifier"), CFSTR("org.winehq.wine") ); */
    CFDictionarySetValue( dict, CFSTR("CFBundleInfoDictionaryVersion"), CFSTR("6.0") );
    CFDictionarySetValue( dict, CFSTR("CFBundleName"), linkstr );
    CFDictionarySetValue( dict, CFSTR("CFBundleDisplayName"), pathstr );
    CFDictionarySetValue( dict, CFSTR("CFBundlePackageType"), CFSTR("APPL") );
    CFDictionarySetValue( dict, CFSTR("CFBundleVersion"), CFSTR("1.0") );

    if (icon)
    {
        CFStringRef iconstr = CFStringCreateWithCString(NULL, icon, CFStringGetSystemEncoding());

        if (iconstr)
        {
            CFDictionarySetValue( dict, CFSTR("CFBundleIconFile"), iconstr );
            CFRelease(iconstr);
        }
    }
    else
    {
        /* Fixme - install a default icon */
        /* CFDictionarySetValue( dict, CFSTR("CFBundleIconFile"), CFSTR("wine.icns") ); */
    }

cleanup:
    if (pathstr)
        CFRelease(pathstr);
    if (linkstr)
        CFRelease(linkstr);

    return dict;
}

BOOL write_property_list( const char *path, CFPropertyListRef propertyList, CFPropertyListFormat format)
{
    CFDataRef data = NULL;
    CFStringRef pathstr = NULL;
    CFURLRef fileURL = NULL;
    CFErrorRef err = NULL;
    BOOL ret = FALSE;
    SInt32 errorCode;

    pathstr = CFStringCreateWithCString(NULL, path, CFStringGetSystemEncoding());
    if (!pathstr)
        goto cleanup;

    fileURL = CFURLCreateWithFileSystemPath( kCFAllocatorDefault,
            pathstr,
            kCFURLPOSIXPathStyle,
            false );
    if (!fileURL)
        goto cleanup;

    data = CFPropertyListCreateData(NULL, propertyList, format, 0, &err);
    if (!data)
        goto cleanup;

    ret = CFURLWriteDataAndPropertiesToResource (
            fileURL,
            data,
            NULL,
            &errorCode);

cleanup:
    if (pathstr)
        CFRelease(pathstr);
    if (fileURL)
        CFRelease(fileURL);
    if (data)
        CFRelease(data);
    if (err)
        CFRelease(err);

    return ret;
}

static BOOL write_info_plist(const char *path_to_bundle_contents, CFPropertyListRef propertyList)
{
    char *plist_path;
    BOOL ret = FALSE;

    plist_path = heap_printf("%s/Info.plist", path_to_bundle_contents);
    if (!plist_path)
        return FALSE;

    WINE_TRACE("Creating Bundle Info.plist at %s\n", wine_dbgstr_a(plist_path));
    ret = write_property_list( plist_path, propertyList, kCFPropertyListXMLFormat_v1_0 );

    HeapFree(GetProcessHeap(), 0, plist_path);

    return ret;
}

CFDictionaryRef create_strings_dictionary(const char *linkname)
{
    CFMutableDictionaryRef dict;
    CFStringRef linkstr;

    linkstr = CFStringCreateWithCString(NULL, linkname, CFStringGetSystemEncoding());
    if (!linkstr)
        return NULL;

    dict = CFDictionaryCreateMutable( kCFAllocatorDefault,
            1,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks );
    if (dict)
        CFDictionarySetValue( dict, CFSTR("CFBundleDisplayName"), linkstr );

    CFRelease(linkstr);

    return dict;
}

static BOOL generate_plist_strings(const char *path_to_bundle_resources_lang, const char *linkname)
{
    char *strings_path;
    CFPropertyListRef propertyList = NULL;
    BOOL ret = FALSE;

    strings_path = heap_printf("%s/InfoPlist.strings", path_to_bundle_resources_lang);
    if (!strings_path)
        goto cleanup;

    propertyList = create_strings_dictionary(linkname);
    if (!propertyList)
        goto cleanup;

    WINE_TRACE("Creating InfoPlist.strings at %s\n", wine_dbgstr_a(strings_path));
    ret = write_property_list( strings_path, propertyList, kCFPropertyListBinaryFormat_v1_0 );

cleanup:
    HeapFree(GetProcessHeap(), 0, strings_path);
    if (propertyList)
        CFRelease(propertyList);

    return ret;
}


/* inspired by write_desktop_entry() in xdg support code */
static BOOL generate_bundle_script(const char *path_to_bundle_macos, const char *path,
        const char *args, const char *workdir, const char *linkname)
{
    FILE *file;
    char *bundle_and_script;
    const char *libpath;

    bundle_and_script = heap_printf("%s/%s", path_to_bundle_macos, linkname);

    WINE_TRACE("Creating Bundle helper script at %s\n", wine_dbgstr_a(bundle_and_script));

    file = fopen(bundle_and_script, "w");
    if (file == NULL)
        return FALSE;

    fprintf(file, "#!/bin/sh\n");
    fprintf(file, "#Helper script for %s\n\n", linkname);

    fprintf(file, "PATH=\"%s\"\nexport PATH\n", getenv("PATH"));
    libpath = getenv("DYLD_FALLBACK_LIBRARY_PATH");
    if (libpath)
        fprintf(file, "DYLD_FALLBACK_LIBRARY_PATH=\"%s\"\nexport DYLD_FALLBACK_LIBRARY_PATH\n", libpath);
    fprintf(file, "WINEPREFIX=\"%s\"\nexport WINEPREFIX\n\n", wine_get_config_dir());

    if (workdir)
        fprintf(file, "cd \"%s\"\n", workdir);
    fprintf(file, "exec sh -c \"exec wine %s %s\"\n\n", path, args);

    fprintf(file, "#EOF\n");

    fclose(file);
    chmod(bundle_and_script, 0755);

    return TRUE;
}

/* build out the directory structure for the bundle and then populate */
BOOL build_app_bundle(const char *unix_link, const char *path, const char *args, const char *workdir, const char *dir, const char *link, const char *linkname, char **icon, CFPropertyListRef *infoplist)
{
    BOOL ret = FALSE;
    char *path_to_bundle, *bundle_name, *path_to_bundle_contents, *path_to_bundle_macos;
    char *path_to_bundle_resources, *path_to_bundle_resources_lang;
    static const char resources_lang[] = "English.lproj"; /* FIXME */
    CFPropertyListRef info;

    WINE_TRACE("bundle name %s\n", wine_dbgstr_a(linkname));

    bundle_name = heap_printf("%s.app", link);
    path_to_bundle = heap_printf("%s/%s", dir, bundle_name);
    path_to_bundle_contents = heap_printf("%s/Contents", path_to_bundle);
    path_to_bundle_macos =  heap_printf("%s/MacOS", path_to_bundle_contents);
    path_to_bundle_resources = heap_printf("%s/Resources", path_to_bundle_contents);
    path_to_bundle_resources_lang = heap_printf("%s/%s", path_to_bundle_resources, resources_lang);

    create_directories(path_to_bundle);
    create_directories(path_to_bundle_contents);
    create_directories(path_to_bundle_macos);
    create_directories(path_to_bundle_resources);
    create_directories(path_to_bundle_resources_lang);

    WINE_TRACE("created bundle %s\n", wine_dbgstr_a(path_to_bundle));

    ret = generate_bundle_script(path_to_bundle_macos, path, args, workdir, linkname);
    if(ret==FALSE)
        return ret;

    info = create_info_plist_dictionary(link, linkname, *icon);
    if (infoplist)
        *infoplist = info;
    else
    {
        ret = write_info_plist(path_to_bundle_contents, info);
        CFRelease(info);
        if(ret==FALSE)
            return ret;
    }

    ret = generate_plist_strings(path_to_bundle_resources_lang, linkname);
    if (ret == FALSE)
        return ret;

    if (unix_link)
    {
        DWORD ret = register_menus_entry(path_to_bundle, unix_link);
        if (ret != ERROR_SUCCESS)
            return FALSE;
    }

    if (*icon)
    {
        char *tmp = heap_printf("%s/%s", path_to_bundle_resources, *icon);
        HeapFree(GetProcessHeap(), 0, *icon);
        *icon = tmp;
    }

    return TRUE;
}

BOOL register_bundle(const char *path_to_bundle)
{
    CFStringRef pathstr;
    CFURLRef bundleURL;

    pathstr = CFStringCreateWithCString(NULL, path_to_bundle, CFStringGetSystemEncoding());
    if (!pathstr)
        return FALSE;

    bundleURL = CFURLCreateWithFileSystemPath( kCFAllocatorDefault, pathstr, kCFURLPOSIXPathStyle, false );
    if (!bundleURL)
    {
        CFRelease(pathstr);
        return FALSE;
    }

    LSRegisterURL(bundleURL, true);

    CFRelease(bundleURL);
    CFRelease(pathstr);

    return TRUE;
}

int appbundle_build_desktop_link(const char *unix_link, const char *link, const char *link_name, const char *path,
        const char *args, const char *descr, const char *workdir, char **icon)
{
    return !build_app_bundle(unix_link, path, args, workdir, mac_desktop_dir, link_name, link_name, icon, NULL);
}

int appbundle_build_menu_link(const char *unix_link, const char *link, const char *link_name, const char *path,
        const char *args, const char *descr, const char *workdir, char **icon)
{
    return !build_app_bundle(unix_link, path, args, workdir, wine_applications_dir, link, link_name, icon, NULL);
}

void *appbundle_refresh_file_type_associations_init(void)
{
	WINE_FIXME("FileType Associations are currently unsupported on this platform\n");
	return NULL;
}

BOOL appbundle_init(void)
{
    WCHAR shellDesktopPath[MAX_PATH];

    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, SHGFP_TYPE_CURRENT, shellDesktopPath);
    if (SUCCEEDED(hr))
        mac_desktop_dir = wine_get_unix_file_name(shellDesktopPath);

    if (mac_desktop_dir == NULL)
    {
        WINE_ERR("error looking up the desktop directory\n");
        return FALSE;
    }

    if (getenv("HOME"))
    {
        wine_applications_dir = heap_printf("%s/Applications/Wine", getenv("HOME"));
        if (!wine_applications_dir)
            return FALSE;
        create_directories(wine_applications_dir);
        WINE_TRACE("Applications in %s\n", wine_applications_dir);

        wine_associations_dir = heap_printf("%s/Library/Wine/Associations", getenv("HOME"));
        if (!wine_associations_dir)
            return FALSE;
        create_directories(wine_associations_dir);
        WINE_TRACE("Associations in %s\n", wine_associations_dir);

        return TRUE;
    }
    else
    {
        WINE_ERR("No HOME environment variable\n");
        return FALSE;
    }
}

const struct winemenubuilder_dispatch appbundle_dispatch =
{
    appbundle_init,

    appbundle_build_desktop_link,
    appbundle_build_menu_link,

    appbundle_write_icon,

    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

#endif
