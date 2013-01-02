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

extern HRESULT osx_write_icon(IStream *icoStream, int exeIndex, LPCWSTR icoPathW,
                                   const char *destFilename, char **nativeIdentifier);

static int appbundle_build_desktop_link(const char *unix_link, const char *link, const char *link_name, const char *path,
        const char *args, const char *descr, const char *workdir, char *icon)
{
    return ERROR_CALL_NOT_IMPLEMENTED;
}

static int appbundle_build_menu_link(const char *unix_link, const char *link, const char *link_name, const char *path,
        const char *args, const char *descr, const char *workdir, char *icon)
{
    return ERROR_CALL_NOT_IMPLEMENTED;
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
