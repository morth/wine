/*
 * Winemenubuilder support for Mac OS X Application Bundles
 *
 * Copyright 2011 Steven Edwards
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
 * - Add support for writing bundles to the Desktop
 * - Convert to using CoreFoundation API rather than standard unix file ops
 * - See if there is anything else in the rsrc section of the target that
 *   we might want to dump in a *.plist. Version information for the target
 *   and or Wine Version information come to mind.
 * - Association Support
 * - sha1hash of target application in bundle plist
 */

#include "config.h"
#include "wine/port.h"

#include <stdio.h>
#include <errno.h>

#include <shlobj.h>

#ifdef __APPLE__
# include <CoreFoundation/CoreFoundation.h>
#endif

#include "wine/debug.h"
#include "wine/library.h"


WINE_DEFAULT_DEBUG_CHANNEL(menubuilder);

char *path_to_bundle = NULL;
char *mac_desktop_dir = NULL;
char *wine_applications_dir = NULL;
char* heap_printf(const char *format, ...);
BOOL create_directories(char *directory);
DWORD register_menus_entry(const char *unix_file, const char *windows_file);

#ifdef __APPLE__

//CFPropertyListRef CreateMyPropertyListFromFile(CFURLRef fileURL);
void WriteMyPropertyListToFile(CFPropertyListRef propertyList, CFURLRef fileURL );


CFDictionaryRef CreateMyDictionary(const char *linkname)
{
   CFMutableDictionaryRef dict;
   CFStringRef linkstr;

   linkstr = CFStringCreateWithCString(NULL, linkname, CFStringGetSystemEncoding());


   /* Create a dictionary that will hold the data. */
   dict = CFDictionaryCreateMutable( kCFAllocatorDefault,
            0,
            &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks );

   /* Put the various items into the dictionary. */
   /* FIXME - Some values assumed the ought not to be */
   CFDictionarySetValue( dict, CFSTR("CFBundleDevelopmentRegion"), CFSTR("English") );
   CFDictionarySetValue( dict, CFSTR("CFBundleExecutable"), linkstr );
   /* FIXME - Avoid identifier if not unique. */
   //CFDictionarySetValue( dict, CFSTR("CFBundleIdentifier"), CFSTR("org.winehq.wine") );
   CFDictionarySetValue( dict, CFSTR("CFBundleInfoDictionaryVersion"), CFSTR("6.0") );
   CFDictionarySetValue( dict, CFSTR("CFBundleName"), linkstr );
   CFDictionarySetValue( dict, CFSTR("CFBundlePackageType"), CFSTR("APPL") );
   CFDictionarySetValue( dict, CFSTR("CFBundleVersion"), CFSTR("1.0") );
   // Not needed CFDictionarySetValue( dict, CFSTR("CFBundleSignature"), CFSTR("????") );
   /* Fixme - install a default icon */
   //CFDictionarySetValue( dict, CFSTR("CFBundleIconFile"), CFSTR("wine.icns") );

   return dict;
}

void WriteMyPropertyListToFile( CFPropertyListRef propertyList, CFURLRef fileURL )
{
   CFDataRef xmlData;
   Boolean status;
   SInt32 errorCode;

   /* Convert the property list into XML data */
   xmlData = CFPropertyListCreateXMLData( kCFAllocatorDefault, propertyList );

   /* Write the XML data to the file */
   status = CFURLWriteDataAndPropertiesToResource (
               fileURL,
               xmlData,
               NULL,
               &errorCode);

   // CFRelease(xmlData);
}

static CFPropertyListRef CreateMyPropertyListFromFile( CFURLRef fileURL )
{
    CFPropertyListRef propertyList;
    CFStringRef       errorString;
    CFDataRef         resourceData;
    Boolean           status;
    SInt32            errorCode;

    /* Read the XML file */
    status = CFURLCreateDataAndPropertiesFromResource(
               kCFAllocatorDefault,
               fileURL,
               &resourceData,
               NULL,
               NULL,
               &errorCode);

    /* Reconstitute the dictionary using the XML data. */
    propertyList = CFPropertyListCreateFromXMLData( kCFAllocatorDefault,
               resourceData,
               kCFPropertyListImmutable,
               &errorString);

    //CFRelease( resourceData );
    return propertyList;
}

BOOL modify_plist_value(char *plist_path, const char *key, char *value)
{
    CFPropertyListRef propertyList;
    CFMutableDictionaryRef dict;
    CFStringRef pathstr;
    CFStringRef keystr;
    CFStringRef valuestr;
    CFURLRef fileURL;

    WINE_TRACE("Modifying Bundle Info.plist at %s\n", plist_path);

    /* Convert strings to something these Mac APIs can handle */
    pathstr = CFStringCreateWithCString(NULL, plist_path, CFStringGetSystemEncoding());
    keystr = CFStringCreateWithCString(NULL, key, CFStringGetSystemEncoding());
    valuestr = CFStringCreateWithCString(NULL, value, CFStringGetSystemEncoding());


    /* Create a URL that specifies the file we will create to hold the XML data. */
    fileURL = CFURLCreateWithFileSystemPath( kCFAllocatorDefault,
                                             pathstr,
                                             kCFURLPOSIXPathStyle,
                                             false );

    /* Open File */
    propertyList = CreateMyPropertyListFromFile( fileURL );

    /* Create a dictionary that will hold the data. */
    dict = CFDictionaryCreateMutableCopy( kCFAllocatorDefault, 0, propertyList );

    /* Modify a value */
    CFDictionaryAddValue( dict, keystr, valuestr );
    //CFDictionarySetValue( propertyList, value_to_change, variable_to_write );

    /* Write back to the file */
    WriteMyPropertyListToFile( dict, fileURL );

    //CFRelease(propertyList);
    // CFRelease(fileURL);

    WINE_TRACE("Modifed Bundle Info.plist at %s\n", wine_dbgstr_a(plist_path));

    return TRUE;
}

static BOOL generate_plist(const char *path_to_bundle_contents, const char *linkname)
{
    char *plist_path;
    static const char info_dot_plist_file[] = "Info.plist";
    CFPropertyListRef propertyList;
    CFStringRef pathstr;
    CFURLRef fileURL;

    /* Append all of the filename and path stuff and shove it in to CFStringRef */
    plist_path = heap_printf("%s/%s", path_to_bundle_contents, info_dot_plist_file);
    pathstr = CFStringCreateWithCString(NULL, plist_path, CFStringGetSystemEncoding());

    /* Construct a complex dictionary object */
    propertyList = CreateMyDictionary(linkname);

    /* Create a URL that specifies the file we will create to hold the XML data. */
    fileURL = CFURLCreateWithFileSystemPath( kCFAllocatorDefault,
                                             pathstr,
                                             kCFURLPOSIXPathStyle,
                                             false );

    /* Write the property list to the file */
    WriteMyPropertyListToFile( propertyList, fileURL );
    CFRelease(propertyList);

#if 0
    /* Recreate the property list from the file */
    propertyList = CreateMyPropertyListFromFile( fileURL );

    /* Release any objects to which we have references */
    CFRelease(propertyList);
#endif
    CFRelease(fileURL);

    WINE_TRACE("Creating Bundle Info.plist at %s\n", wine_dbgstr_a(plist_path));

    return TRUE;
}
#endif /* __APPLE__ */


#if 0
/* TODO: If I understand this file correctly, it is used for associations */
static BOOL generate_pkginfo_file(const char* path_to_bundle_contents)
{
    FILE *file;
    char *bundle_and_pkginfo;
    static const char pkginfo_file[] = "PkgInfo";

    bundle_and_pkginfo = heap_printf("%s/%s", path_to_bundle_contents, pkginfo_file);

    WINE_TRACE("Creating Bundle PkgInfo at %s\n", wine_dbgstr_a(bundle_and_pkginfo));

    file = fopen(bundle_and_pkginfo, "w");
    if (file == NULL)
        return FALSE;

    fprintf(file, "APPL????");

    fclose(file);
    return TRUE;
}
#endif


/* inspired by write_desktop_entry() in xdg support code */
static BOOL generate_bundle_script(const char *path_to_bundle_macos, const char *path,
                                   const char *args, const char *linkname)
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

    fprintf(file, "exec sh -c \"exec wine %s %s\"\n\n", path, args);

    fprintf(file, "#EOF\n");

    fclose(file);
    chmod(bundle_and_script, 0755);

    return TRUE;
}

/* build out the directory structure for the bundle and then populate */
BOOL build_app_bundle(const char *unix_link, const char *path, const char *args, const char *dir, const char *link, const char *linkname)
{
#ifdef __APPLE__
    BOOL ret = FALSE;
    char *bundle_name, *path_to_bundle_contents, *path_to_bundle_macos;
    char *path_to_bundle_resources, *path_to_bundle_resources_lang;
    static const char extentsion[] = "app";
    static const char contents[] = "Contents";
    static const char macos[] = "MacOS";
    static const char resources[] = "Resources";
    static const char resources_lang[] = "English.lproj"; /* FIXME */

    WINE_TRACE("bundle file name %s\n", wine_dbgstr_a(linkname));

    if (!dir)
	dir = wine_applications_dir;

    bundle_name = heap_printf("%s.%s", link, extentsion);
    path_to_bundle = heap_printf("%s/%s", dir, bundle_name);
    path_to_bundle_contents = heap_printf("%s/%s", path_to_bundle, contents);
    path_to_bundle_macos =  heap_printf("%s/%s", path_to_bundle_contents, macos);
    path_to_bundle_resources = heap_printf("%s/%s", path_to_bundle_contents, resources);
    path_to_bundle_resources_lang = heap_printf("%s/%s", path_to_bundle_resources, resources_lang);

    create_directories(path_to_bundle);
    create_directories(path_to_bundle_contents);
    create_directories(path_to_bundle_macos);
    create_directories(path_to_bundle_resources);
    create_directories(path_to_bundle_resources_lang);

    WINE_TRACE("created bundle %s\n", wine_dbgstr_a(path_to_bundle));

    ret = generate_bundle_script(path_to_bundle_macos, path, args, linkname);
    if(ret==FALSE)
       return ret;

#if 0
    ret = generate_pkginfo_file(path_to_bundle_contents);
    if(ret==FALSE)
       return ret;
#endif

    ret = generate_plist(path_to_bundle_contents, linkname);
    if(ret==FALSE)
       return ret;

    if (unix_link)
    {
        DWORD ret = register_menus_entry(path_to_bundle, unix_link);
        if (ret != ERROR_SUCCESS)
            return FALSE;
    }

#endif /* __APPLE__ */
    return TRUE;
}


BOOL init_apple_de(void)
{
#ifdef __APPLE__
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
        create_directories(wine_applications_dir);
        WINE_TRACE("%s\n", wine_applications_dir);

        return TRUE;
    }
    else
    {
        WINE_ERR("out of memory\n");
        return FALSE;
    }
#else
    return FALSE;
#endif
}
