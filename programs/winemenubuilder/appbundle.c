/*
 * Winemenubuilder support for Mac OS X Application Bundles
 *
 * Copyright 2011 Steven Edwards
 * Copyright 2011 - 2013 Per Johansson
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
 *
 * There can be more to a bundle depending on the target, what resources
 * it contains and what the target platform but this simplifed format
 * is all we really need for now for Wine.
 */

#ifdef __APPLE__

#include "config.h"
#include "wine/port.h"

#include <stdio.h>

#include <CoreFoundation/CoreFoundation.h>

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

#include "wine/debug.h"
#include "wine/library.h"

#include "winemenubuilder.h"


WINE_DEFAULT_DEBUG_CHANNEL(menubuilder);

static char *mac_desktop_dir = NULL;
static char *wine_applications_dir = NULL;

#define ICNS_SLOTS 6

static inline int size_to_slot(int size)
{
    switch (size)
    {
        case 16: return 0;
        case 32: return 1;
        case 48: return 2;
        case 64: return -2;  /* Classic Mode */
        case 128: return 3;
        case 256: return 4;
        case 512: return 5;
    }

    return -1;
}

#define CLASSIC_SLOT 3

HRESULT osx_write_icon(IStream *icoStream, int exeIndex, LPCWSTR icoPathW,
                                   const char *destFilename, char **nativeIdentifier)
{
    ICONDIRENTRY *iconDirEntries = NULL;
    int numEntries;
    struct {
        int index;
        int maxBits;
        BOOL scaled;
    } best[ICNS_SLOTS];
    int indexes[ICNS_SLOTS];
    int i;
    char *icnsPath = NULL;
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
        BOOL scaled = FALSE;

        WINE_TRACE("[%d]: %d x %d @ %d\n", i, width, height, iconDirEntries[i].wBitCount);
        if (height != width)
            continue;
        slot = size_to_slot(width);
        if (slot == -2)
        {
            scaled = TRUE;
            slot = CLASSIC_SLOT;
        }
        else if (slot < 0)
            continue;
        if (scaled && best[slot].maxBits && !best[slot].scaled)
            continue; /* don't replace unscaled with scaled */
        if (iconDirEntries[i].wBitCount >= best[slot].maxBits || (!scaled && best[slot].scaled))
        {
            best[slot].index = i;
            best[slot].maxBits = iconDirEntries[i].wBitCount;
            best[slot].scaled = scaled;
        }
    }
    /* remove the scaled icon if a larger unscaled icon exists */
    if (best[CLASSIC_SLOT].scaled)
    {
        for (i = CLASSIC_SLOT+1; i < ICNS_SLOTS; i++)
            if (best[i].index >= 0 && !best[i].scaled)
            {
                best[CLASSIC_SLOT].index = -1;
                break;
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

    if (destFilename)
        *nativeIdentifier = heap_printf("%s", destFilename);
    else
        *nativeIdentifier = compute_native_identifier(exeIndex, icoPathW);
    if (*nativeIdentifier == NULL)
    {
        hr = E_OUTOFMEMORY;
        goto end;
    }
    icnsPath = heap_printf("/tmp/%s.icns", *nativeIdentifier);
    if (icnsPath == NULL)
    {
        hr = E_OUTOFMEMORY;
        WINE_WARN("out of memory creating ICNS path\n");
        goto end;
    }
    zero.QuadPart = 0;
    hr = IStream_Seek(icoStream, zero, STREAM_SEEK_SET, NULL);
    if (FAILED(hr))
    {
        WINE_WARN("seeking icon stream failed, error 0x%08X\n", hr);
        goto end;
    }
    hr = convert_to_native_icon(icoStream, indexes, numEntries, &CLSID_WICIcnsEncoder,
                                icnsPath, icoPathW);
    if (FAILED(hr))
    {
        WINE_WARN("converting %s to %s failed, error 0x%08X\n",
            wine_dbgstr_w(icoPathW), wine_dbgstr_a(icnsPath), hr);
        goto end;
    }

end:
    HeapFree(GetProcessHeap(), 0, iconDirEntries);
    HeapFree(GetProcessHeap(), 0, icnsPath);
    return hr;
}

/* inspired by write_desktop_entry() in xdg support code */
static BOOL generate_bundle_script(const char *file, const char *path,
        const char *args, const char *workdir)
{
    FILE *fp;
    const char *libpath;

    WINE_TRACE("Creating Bundle helper script at %s\n", wine_dbgstr_a(file));

    fp = fopen(file, "w");
    if (fp == NULL)
        return FALSE;

    fprintf(fp, "#!/bin/sh\n");

    fprintf(fp, "PATH=\"%s\"\nexport PATH\n", getenv("PATH"));
    libpath = getenv("DYLD_FALLBACK_LIBRARY_PATH");
    if (libpath)
        fprintf(fp, "DYLD_FALLBACK_LIBRARY_PATH=\"%s\"\nexport DYLD_FALLBACK_LIBRARY_PATH\n", libpath);
    fprintf(fp, "WINEPREFIX=\"%s\"\nexport WINEPREFIX\n\n", wine_get_config_dir());

    if (workdir)
        fprintf(fp, "cd \"%s\"\n", workdir);
    fprintf(fp, "exec sh -c \"exec wine %s %s\"\n\n", path, args);

    fprintf(fp, "#EOF\n");

    fclose(fp);
    chmod(file, 0755);

    return TRUE;
}

CFDictionaryRef create_info_plist_dictionary(const char *link_name)
{
    CFMutableDictionaryRef dict = NULL;
    CFStringRef namestr;

    namestr = CFStringCreateWithFileSystemRepresentation(NULL, link_name);
    if (!namestr)
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
    CFDictionarySetValue( dict, CFSTR("CFBundleExecutable"), CFSTR("winelauncher") );
    /* FIXME - Avoid identifier if not unique. */
    /* CFDictionarySetValue( dict, CFSTR("CFBundleIdentifier"), CFSTR("org.winehq.wine") ); */
    CFDictionarySetValue( dict, CFSTR("CFBundleInfoDictionaryVersion"), CFSTR("6.0") );
    CFDictionarySetValue( dict, CFSTR("CFBundleName"), namestr );
    CFDictionarySetValue( dict, CFSTR("CFBundleDisplayName"), namestr );
    CFDictionarySetValue( dict, CFSTR("CFBundlePackageType"), CFSTR("APPL") );
    CFDictionarySetValue( dict, CFSTR("CFBundleVersion"), CFSTR("1.0") );

cleanup:
    if (namestr)
        CFRelease(namestr);

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

    pathstr = CFStringCreateWithFileSystemRepresentation(NULL, path);
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

BOOL build_app_bundle(const char *unix_link, const char *dir, const char *link, const char *link_name, const char *path, const char *args, const char *workdir, const char *icon)
{
    BOOL ret = FALSE;
    char *path_to_bundle, *path_to_macos, *path_to_script, *path_to_info;
    CFDictionaryRef info = NULL;

    path_to_bundle = heap_printf("%s/%s.app", dir, link);
    if (!path_to_bundle)
        return FALSE;
    path_to_macos = heap_printf("%s/Contents/MacOS", path_to_bundle);
    path_to_script = heap_printf("%s/Contents/MacOS/winelauncher", path_to_bundle);
    path_to_info = heap_printf("%s/Contents/Info.plist", path_to_bundle);
    if (!path_to_macos || !path_to_script || !path_to_info)
        goto out;

    if (!create_directories(path_to_macos))
        goto out;

    if (!generate_bundle_script(path_to_script, path, args, workdir))
        goto out;

    info = create_info_plist_dictionary(link_name);
    if (!info)
        goto out;

    if (!write_property_list(path_to_info, info, kCFPropertyListXMLFormat_v1_0))
        goto out;

    if (unix_link)
    {
        DWORD r = register_menus_entry(path_to_bundle, unix_link);
        if (r != ERROR_SUCCESS)
            goto out;
    }

    ret = TRUE;

out:
    if (ret == FALSE)
        remove_unix_link(path_to_bundle);
    HeapFree(GetProcessHeap(), 0, path_to_bundle);
    HeapFree(GetProcessHeap(), 0, path_to_macos);
    HeapFree(GetProcessHeap(), 0, path_to_script);
    HeapFree(GetProcessHeap(), 0, path_to_info);
    if (info)
        CFRelease(info);
    return ret;
}

static int appbundle_build_desktop_link(const char *unix_link, const char *link, const char *link_name, const char *path,
        const char *args, const char *descr, const char *workdir, char *icon)
{
    return !build_app_bundle(unix_link, mac_desktop_dir, link_name, link_name, path, args, workdir, icon);
}

static int appbundle_build_menu_link(const char *unix_link, const char *link, const char *link_name, const char *path,
        const char *args, const char *descr, const char *workdir, char *icon)
{
    return !build_app_bundle(unix_link, wine_applications_dir, link, link_name, path, args, workdir, icon);
}

static void *appbundle_refresh_file_type_associations_init(void)
{
    static int ok;

    return &ok;
}

static BOOL appbundle_mime_type_for_extension(void *user, const char *extensionA, LPCWSTR extensionW, char **mime_type)
{
    return FALSE;
}

static BOOL appbundle_write_mime_type_entry(void *user, const char *extensionA, const char *mimeTypeA, const char *friendlyDocNameA)
{
    return FALSE;
}

static BOOL appbundle_write_association_entry(void *user, const char *extensionA, const char *friendlyAppNameA,
        const char *friendlyDocNameA, const char *mimeTypeA, const char *progIdA,
        const char *appIconA)
{
    return FALSE;
}

static BOOL appbundle_remove_file_type_association(void *user, const char *extensionA, LPCWSTR extensionW)
{
    return FALSE;
}

static void appbundle_refresh_file_type_associations_cleanup(void *user, BOOL hasChanged)
{
}

static BOOL appbundle_init(void)
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

    if (getenv("WINE_APPLICATIONS_DIR"))
        wine_applications_dir = strdupA(getenv("WINE_APPLICATIONS_DIR"));
    else
        wine_applications_dir = heap_printf("%s/Applications/Wine", getenv("HOME"));
    if (!wine_applications_dir)
        return FALSE;

    create_directories(wine_applications_dir);
    WINE_TRACE("Applications in %s\n", wine_applications_dir);

    return TRUE;
}

const struct winemenubuilder_dispatch appbundle_dispatch =
{
    appbundle_init,

    appbundle_build_desktop_link,
    appbundle_build_menu_link,

    osx_write_icon,

    appbundle_refresh_file_type_associations_init,
    appbundle_mime_type_for_extension,
    appbundle_write_mime_type_entry,
    appbundle_write_association_entry,
    appbundle_remove_file_type_association,
    appbundle_refresh_file_type_associations_cleanup
};

#endif
