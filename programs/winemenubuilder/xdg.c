/*
 * XDG implementation for winemenubuilder.
 *
 * Copyright 1997 Marcus Meissner
 * Copyright 1998 Juergen Schmied
 * Copyright 2003 Mike McCormack for CodeWeavers
 * Copyright 2004 Dmitry Timoshkov
 * Copyright 2005 Bill Medland
 * Copyright 2008 Damjan Jovanovic
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
 */

#include "config.h"
#include "wine/port.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <stdarg.h>
#ifdef HAVE_FNMATCH_H
#include <fnmatch.h>
#endif
#include <dirent.h>
#include <limits.h>


#define COBJMACROS

#include <windows.h>
#include <shlobj.h>
#include <objidl.h>
#include <shlguid.h>
#include <appmgmt.h>
#include <tlhelp32.h>
#include <intshcut.h>
#include <shlwapi.h>
#include <wincodec.h>

#include "wine/unicode.h"
#include "wine/debug.h"
#include "wine/library.h"
#include "wine/list.h"
#include "wine/rbtree.h"


#include "winemenubuilder.h"

WINE_DEFAULT_DEBUG_CHANNEL(menubuilder);

struct xdg_file_type_user_data
{
    char *mime_dir;
    char *packages_dir;
    char *applications_dir;

    struct list *native_mime_types;
};

struct xdg_mime_type
{
    char *mimeType;
    char *glob;
    char *lower_glob;
    struct list entry;
};

static char *xdg_config_dir;
static char *xdg_data_dir;
static char *xdg_desktop_dir;

static void write_xml_text(FILE *file, const char *text)
{
    int i;
    for (i = 0; text[i]; i++)
    {
        if (text[i] == '&')
            fputs("&amp;", file);
        else if (text[i] == '<')
            fputs("&lt;", file);
        else if (text[i] == '>')
            fputs("&gt;", file);
        else if (text[i] == '\'')
            fputs("&apos;", file);
        else if (text[i] == '"')
            fputs("&quot;", file);
        else
            fputc(text[i], file);
    }
}

static void refresh_icon_cache(const char *iconsDir)
{
    /* The icon theme spec only requires the mtime on the "toplevel"
     * directory (whatever that is) to be changed for a refresh,
     * but on GNOME you have to create a file in that directory
     * instead. Creating a file also works on KDE, Xfce and LXDE.
     */
    char *filename = heap_printf("%s/.wine-refresh-XXXXXX", iconsDir);
    if (filename != NULL)
    {
        int fd = mkstemps(filename, 0);
        if (fd >= 0)
        {
            close(fd);
            unlink(filename);
        }
        HeapFree(GetProcessHeap(), 0, filename);
    }
}

static HRESULT xdg_write_icon(IStream *icoStream, int exeIndex, LPCWSTR icoPathW,
                                   const char *destFilename, char **nativeIdentifier)
{
    ICONDIRENTRY *iconDirEntries = NULL;
    int numEntries;
    int i;
    char *iconsDir = NULL;
    HRESULT hr = S_OK;
    LARGE_INTEGER zero;

    hr = read_ico_direntries(icoStream, &iconDirEntries, &numEntries);
    if (FAILED(hr))
        goto end;

    if (destFilename)
        *nativeIdentifier = heap_printf("%s", destFilename);
    else
        *nativeIdentifier = compute_native_identifier(exeIndex, icoPathW);
    if (*nativeIdentifier == NULL)
    {
        hr = E_OUTOFMEMORY;
        goto end;
    }
    iconsDir = heap_printf("%s/icons/hicolor", xdg_data_dir);
    if (iconsDir == NULL)
    {
        hr = E_OUTOFMEMORY;
        goto end;
    }

    for (i = 0; i < numEntries; i++)
    {
        int bestIndex = i;
        int j;
        BOOLEAN duplicate = FALSE;
        int w, h;
        char *iconDir = NULL;
        char *pngPath = NULL;

        WINE_TRACE("[%d]: %d x %d @ %d\n", i, iconDirEntries[i].bWidth,
            iconDirEntries[i].bHeight, iconDirEntries[i].wBitCount);

        for (j = 0; j < i; j++)
        {
            if (iconDirEntries[j].bWidth == iconDirEntries[i].bWidth &&
                iconDirEntries[j].bHeight == iconDirEntries[i].bHeight)
            {
                duplicate = TRUE;
                break;
            }
        }
        if (duplicate)
            continue;
        for (j = i + 1; j < numEntries; j++)
        {
            if (iconDirEntries[j].bWidth == iconDirEntries[i].bWidth &&
                iconDirEntries[j].bHeight == iconDirEntries[i].bHeight &&
                iconDirEntries[j].wBitCount >= iconDirEntries[bestIndex].wBitCount)
            {
                bestIndex = j;
            }
        }
        WINE_TRACE("Selected: %d\n", bestIndex);

        w = iconDirEntries[bestIndex].bWidth ? iconDirEntries[bestIndex].bWidth : 256;
        h = iconDirEntries[bestIndex].bHeight ? iconDirEntries[bestIndex].bHeight : 256;
        iconDir = heap_printf("%s/%dx%d/apps", iconsDir, w, h);
        if (iconDir == NULL)
        {
            hr = E_OUTOFMEMORY;
            goto endloop;
        }
        create_directories(iconDir);
        pngPath = heap_printf("%s/%s.png", iconDir, *nativeIdentifier);
        if (pngPath == NULL)
        {
            hr = E_OUTOFMEMORY;
            goto endloop;
        }
        zero.QuadPart = 0;
        hr = IStream_Seek(icoStream, zero, STREAM_SEEK_SET, NULL);
        if (FAILED(hr))
            goto endloop;
        hr = convert_to_native_icon(icoStream, &bestIndex, 1, &CLSID_WICPngEncoder,
                                    pngPath, icoPathW);

    endloop:
        HeapFree(GetProcessHeap(), 0, iconDir);
        HeapFree(GetProcessHeap(), 0, pngPath);
    }
    refresh_icon_cache(iconsDir);

end:
    HeapFree(GetProcessHeap(), 0, iconDirEntries);
    HeapFree(GetProcessHeap(), 0, iconsDir);
    return hr;
}

static BOOL write_desktop_entry(const char *unix_link, const char *location, const char *linkname,
                                const char *path, const char *args, const char *descr,
                                const char *workdir, const char *icon)
{
    FILE *file;

    WINE_TRACE("(%s,%s,%s,%s,%s,%s,%s,%s)\n", wine_dbgstr_a(unix_link), wine_dbgstr_a(location),
               wine_dbgstr_a(linkname), wine_dbgstr_a(path), wine_dbgstr_a(args),
               wine_dbgstr_a(descr), wine_dbgstr_a(workdir), wine_dbgstr_a(icon));

    file = fopen(location, "w");
    if (file == NULL)
        return FALSE;

    fprintf(file, "[Desktop Entry]\n");
    fprintf(file, "Name=%s\n", linkname);
    fprintf(file, "Exec=env WINEPREFIX=\"%s\" wine %s %s\n",
            wine_get_config_dir(), path, args);
    fprintf(file, "Type=Application\n");
    fprintf(file, "StartupNotify=true\n");
    if (descr && lstrlenA(descr))
        fprintf(file, "Comment=%s\n", descr);
    if (workdir && lstrlenA(workdir))
        fprintf(file, "Path=%s\n", workdir);
    if (icon && lstrlenA(icon))
        fprintf(file, "Icon=%s\n", icon);

    fclose(file);

    if (unix_link)
    {
        DWORD ret = register_menus_entry(location, unix_link);
        if (ret != ERROR_SUCCESS)
            return FALSE;
    }

    return TRUE;
}

static BOOL write_directory_entry(const char *directory, const char *location)
{
    FILE *file;

    WINE_TRACE("(%s,%s)\n", wine_dbgstr_a(directory), wine_dbgstr_a(location));

    file = fopen(location, "w");
    if (file == NULL)
        return FALSE;

    fprintf(file, "[Desktop Entry]\n");
    fprintf(file, "Type=Directory\n");
    if (strcmp(directory, "wine") == 0)
    {
        fprintf(file, "Name=Wine\n");
        fprintf(file, "Icon=wine\n");
    }
    else
    {
        fprintf(file, "Name=%s\n", directory);
        fprintf(file, "Icon=folder\n");
    }

    fclose(file);
    return TRUE;
}

static BOOL write_menu_file(const char *unix_link, const char *filename)
{
    char *tempfilename;
    FILE *tempfile = NULL;
    char *lastEntry;
    char *name = NULL;
    char *menuPath = NULL;
    int i;
    int count = 0;
    BOOL ret = FALSE;

    WINE_TRACE("(%s)\n", wine_dbgstr_a(filename));

    while (1)
    {
        tempfilename = heap_printf("%s/wine-menu-XXXXXX", xdg_config_dir);
        if (tempfilename)
        {
            int tempfd = mkstemps(tempfilename, 0);
            if (tempfd >= 0)
            {
                tempfile = fdopen(tempfd, "w");
                if (tempfile)
                    break;
                close(tempfd);
                goto end;
            }
            else if (errno == EEXIST)
            {
                HeapFree(GetProcessHeap(), 0, tempfilename);
                continue;
            }
            HeapFree(GetProcessHeap(), 0, tempfilename);
        }
        return FALSE;
    }

    fprintf(tempfile, "<!DOCTYPE Menu PUBLIC \"-//freedesktop//DTD Menu 1.0//EN\"\n");
    fprintf(tempfile, "\"http://www.freedesktop.org/standards/menu-spec/menu-1.0.dtd\">\n");
    fprintf(tempfile, "<Menu>\n");
    fprintf(tempfile, "  <Name>Applications</Name>\n");

    name = HeapAlloc(GetProcessHeap(), 0, lstrlenA(filename) + 1);
    if (name == NULL) goto end;
    lastEntry = name;
    for (i = 0; filename[i]; i++)
    {
        name[i] = filename[i];
        if (filename[i] == '/')
        {
            char *dir_file_name;
            struct stat st;
            name[i] = 0;
            fprintf(tempfile, "  <Menu>\n");
            fprintf(tempfile, "    <Name>%s", count ? "" : "wine-");
            write_xml_text(tempfile, name);
            fprintf(tempfile, "</Name>\n");
            fprintf(tempfile, "    <Directory>%s", count ? "" : "wine-");
            write_xml_text(tempfile, name);
            fprintf(tempfile, ".directory</Directory>\n");
            dir_file_name = heap_printf("%s/desktop-directories/%s%s.directory",
                xdg_data_dir, count ? "" : "wine-", name);
            if (dir_file_name)
            {
                if (stat(dir_file_name, &st) != 0 && errno == ENOENT)
                    write_directory_entry(lastEntry, dir_file_name);
                HeapFree(GetProcessHeap(), 0, dir_file_name);
            }
            name[i] = '-';
            lastEntry = &name[i+1];
            ++count;
        }
    }
    name[i] = 0;

    fprintf(tempfile, "    <Include>\n");
    fprintf(tempfile, "      <Filename>");
    write_xml_text(tempfile, name);
    fprintf(tempfile, "</Filename>\n");
    fprintf(tempfile, "    </Include>\n");
    for (i = 0; i < count; i++)
         fprintf(tempfile, "  </Menu>\n");
    fprintf(tempfile, "</Menu>\n");

    menuPath = heap_printf("%s/%s", xdg_config_dir, name);
    if (menuPath == NULL) goto end;
    strcpy(menuPath + strlen(menuPath) - strlen(".desktop"), ".menu");
    ret = TRUE;

end:
    if (tempfile)
        fclose(tempfile);
    if (ret)
        ret = (rename(tempfilename, menuPath) == 0);
    if (!ret && tempfilename)
        remove(tempfilename);
    HeapFree(GetProcessHeap(), 0, tempfilename);
    if (ret)
        register_menus_entry(menuPath, unix_link);
    HeapFree(GetProcessHeap(), 0, name);
    HeapFree(GetProcessHeap(), 0, menuPath);
    return ret;
}

static BOOL write_menu_entry(const char *unix_link, const char *link, const char *path, const char *args,
                             const char *descr, const char *workdir, const char *icon)
{
    const char *linkname;
    char *desktopPath = NULL;
    char *desktopDir;
    char *filename = NULL;
    BOOL ret = TRUE;

    WINE_TRACE("(%s, %s, %s, %s, %s, %s, %s)\n", wine_dbgstr_a(unix_link), wine_dbgstr_a(link),
               wine_dbgstr_a(path), wine_dbgstr_a(args), wine_dbgstr_a(descr),
               wine_dbgstr_a(workdir), wine_dbgstr_a(icon));

    linkname = strrchr(link, '/');
    if (linkname == NULL)
        linkname = link;
    else
        ++linkname;

    desktopPath = heap_printf("%s/applications/wine/%s.desktop", xdg_data_dir, link);
    if (!desktopPath)
    {
        WINE_WARN("out of memory creating menu entry\n");
        ret = FALSE;
        goto end;
    }
    desktopDir = strrchr(desktopPath, '/');
    *desktopDir = 0;
    if (!create_directories(desktopPath))
    {
        WINE_WARN("couldn't make parent directories for %s\n", wine_dbgstr_a(desktopPath));
        ret = FALSE;
        goto end;
    }
    *desktopDir = '/';
    if (!write_desktop_entry(unix_link, desktopPath, linkname, path, args, descr, workdir, icon))
    {
        WINE_WARN("couldn't make desktop entry %s\n", wine_dbgstr_a(desktopPath));
        ret = FALSE;
        goto end;
    }

    filename = heap_printf("wine/%s.desktop", link);
    if (!filename || !write_menu_file(unix_link, filename))
    {
        WINE_WARN("couldn't make menu file %s\n", wine_dbgstr_a(filename));
        ret = FALSE;
    }

end:
    HeapFree(GetProcessHeap(), 0, desktopPath);
    HeapFree(GetProcessHeap(), 0, filename);
    return ret;
}

static BOOL next_line(FILE *file, char **line, int *size)
{
    int pos = 0;
    char *cr;
    if (*line == NULL)
    {
        *size = 4096;
        *line = HeapAlloc(GetProcessHeap(), 0, *size);
    }
    while (*line != NULL)
    {
        if (fgets(&(*line)[pos], *size - pos, file) == NULL)
        {
            HeapFree(GetProcessHeap(), 0, *line);
            *line = NULL;
            if (feof(file))
                return TRUE;
            return FALSE;
        }
        pos = strlen(*line);
        cr = strchr(*line, '\n');
        if (cr == NULL)
        {
            char *line2;
            (*size) *= 2;
            line2 = HeapReAlloc(GetProcessHeap(), 0, *line, *size);
            if (line2)
                *line = line2;
            else
            {
                HeapFree(GetProcessHeap(), 0, *line);
                *line = NULL;
            }
        }
        else
        {
            *cr = 0;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL add_mimes(const char *xdg_data_dir, struct list *mime_types)
{
    char *globs_filename = NULL;
    BOOL ret = TRUE;
    globs_filename = heap_printf("%s/mime/globs", xdg_data_dir);
    if (globs_filename)
    {
        FILE *globs_file = fopen(globs_filename, "r");
        if (globs_file) /* doesn't have to exist */
        {
            char *line = NULL;
            int size = 0;
            while (ret && (ret = next_line(globs_file, &line, &size)) && line)
            {
                char *pos;
                struct xdg_mime_type *mime_type_entry = NULL;
                if (line[0] != '#' && (pos = strchr(line, ':')))
                {
                    mime_type_entry = HeapAlloc(GetProcessHeap(), 0, sizeof(struct xdg_mime_type));
                    if (mime_type_entry)
                    {
                        *pos = 0;
                        mime_type_entry->mimeType = strdupA(line);
                        mime_type_entry->glob = strdupA(pos + 1);
                        mime_type_entry->lower_glob = strdupA(pos + 1);
                        if (mime_type_entry->lower_glob)
                        {
                            char *l;
                            for (l = mime_type_entry->lower_glob; *l; l++)
                                *l = tolower(*l);
                        }
                        if (mime_type_entry->mimeType && mime_type_entry->glob && mime_type_entry->lower_glob)
                            list_add_tail(mime_types, &mime_type_entry->entry);
                        else
                        {
                            HeapFree(GetProcessHeap(), 0, mime_type_entry->mimeType);
                            HeapFree(GetProcessHeap(), 0, mime_type_entry->glob);
                            HeapFree(GetProcessHeap(), 0, mime_type_entry->lower_glob);
                            HeapFree(GetProcessHeap(), 0, mime_type_entry);
                            ret = FALSE;
                        }
                    }
                    else
                        ret = FALSE;
                }
            }
            HeapFree(GetProcessHeap(), 0, line);
            fclose(globs_file);
        }
        HeapFree(GetProcessHeap(), 0, globs_filename);
    }
    else
        ret = FALSE;
    return ret;
}

static void free_native_mime_types(struct list *native_mime_types)
{
    struct xdg_mime_type *mime_type_entry, *mime_type_entry2;

    LIST_FOR_EACH_ENTRY_SAFE(mime_type_entry, mime_type_entry2, native_mime_types, struct xdg_mime_type, entry)
    {
        list_remove(&mime_type_entry->entry);
        HeapFree(GetProcessHeap(), 0, mime_type_entry->glob);
        HeapFree(GetProcessHeap(), 0, mime_type_entry->lower_glob);
        HeapFree(GetProcessHeap(), 0, mime_type_entry->mimeType);
        HeapFree(GetProcessHeap(), 0, mime_type_entry);
    }
    HeapFree(GetProcessHeap(), 0, native_mime_types);
}

static BOOL build_native_mime_types(const char *xdg_data_home, struct list **mime_types)
{
    char *xdg_data_dirs;
    BOOL ret;

    *mime_types = NULL;

    xdg_data_dirs = getenv("XDG_DATA_DIRS");
    if (xdg_data_dirs == NULL)
        xdg_data_dirs = heap_printf("/usr/local/share/:/usr/share/");
    else
        xdg_data_dirs = strdupA(xdg_data_dirs);

    if (xdg_data_dirs)
    {
        *mime_types = HeapAlloc(GetProcessHeap(), 0, sizeof(struct list));
        if (*mime_types)
        {
            const char *begin;
            char *end;

            list_init(*mime_types);
            ret = add_mimes(xdg_data_home, *mime_types);
            if (ret)
            {
                for (begin = xdg_data_dirs; (end = strchr(begin, ':')); begin = end + 1)
                {
                    *end = '\0';
                    ret = add_mimes(begin, *mime_types);
                    *end = ':';
                    if (!ret)
                        break;
                }
                if (ret)
                    ret = add_mimes(begin, *mime_types);
            }
        }
        else
            ret = FALSE;
        HeapFree(GetProcessHeap(), 0, xdg_data_dirs);
    }
    else
        ret = FALSE;
    if (!ret && *mime_types)
    {
        free_native_mime_types(*mime_types);
        *mime_types = NULL;
    }
    return ret;
}

static BOOL match_glob(struct list *native_mime_types, const char *extension,
                       int ignoreGlobCase, char **match)
{
#ifdef HAVE_FNMATCH
    struct xdg_mime_type *mime_type_entry;
    int matchLength = 0;

    *match = NULL;

    LIST_FOR_EACH_ENTRY(mime_type_entry, native_mime_types, struct xdg_mime_type, entry)
    {
        const char *glob = ignoreGlobCase ? mime_type_entry->lower_glob : mime_type_entry->glob;
        if (fnmatch(glob, extension, 0) == 0)
        {
            if (*match == NULL || matchLength < strlen(glob))
            {
                *match = mime_type_entry->mimeType;
                matchLength = strlen(glob);
            }
        }
    }

    if (*match != NULL)
    {
        *match = strdupA(*match);
        if (*match == NULL)
            return FALSE;
    }
#else
    *match = NULL;
#endif
    return TRUE;
}

static BOOL freedesktop_mime_type_for_extension(void *user,
                                                const char *extensionA,
                                                LPCWSTR extensionW,
                                                char **mime_type)
{
    struct xdg_file_type_user_data *ud = user;
    WCHAR *lower_extensionW;
    INT len;
    BOOL ret = match_glob(ud->native_mime_types, extensionA, 0, mime_type);
    if (ret == FALSE || *mime_type != NULL)
        return ret;
    len = strlenW(extensionW);
    lower_extensionW = HeapAlloc(GetProcessHeap(), 0, (len + 1)*sizeof(WCHAR));
    if (lower_extensionW)
    {
        char *lower_extensionA;
        memcpy(lower_extensionW, extensionW, (len + 1)*sizeof(WCHAR));
        strlwrW(lower_extensionW);
        lower_extensionA = wchars_to_utf8_chars(lower_extensionW);
        if (lower_extensionA)
        {
            ret = match_glob(ud->native_mime_types, lower_extensionA, 1, mime_type);
            HeapFree(GetProcessHeap(), 0, lower_extensionA);
        }
        else
        {
            ret = FALSE;
            WINE_FIXME("out of memory\n");
        }
        HeapFree(GetProcessHeap(), 0, lower_extensionW);
    }
    else
    {
        ret = FALSE;
        WINE_FIXME("out of memory\n");
    }
    return ret;
}

static BOOL write_freedesktop_mime_type_entry(void *user, const char *dot_extension,
                                              const char *mime_type, const char *comment)
{
    struct xdg_file_type_user_data *ud = user;
    BOOL ret = FALSE;
    char *filename;

    WINE_TRACE("writing MIME type %s, extension=%s, comment=%s\n", wine_dbgstr_a(mime_type),
               wine_dbgstr_a(dot_extension), wine_dbgstr_a(comment));

    filename = heap_printf("%s/x-wine-extension-%s.xml", ud->packages_dir, &dot_extension[1]);
    if (filename)
    {
        FILE *packageFile = fopen(filename, "w");
        if (packageFile)
        {
            fprintf(packageFile, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
            fprintf(packageFile, "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n");
            fprintf(packageFile, "  <mime-type type=\"");
            write_xml_text(packageFile, mime_type);
            fprintf(packageFile, "\">\n");
            fprintf(packageFile, "    <glob pattern=\"*");
            write_xml_text(packageFile, dot_extension);
            fprintf(packageFile, "\"/>\n");
            if (comment)
            {
                fprintf(packageFile, "    <comment>");
                write_xml_text(packageFile, comment);
                fprintf(packageFile, "</comment>\n");
            }
            fprintf(packageFile, "  </mime-type>\n");
            fprintf(packageFile, "</mime-info>\n");
            ret = TRUE;
            fclose(packageFile);
        }
        else
            WINE_ERR("error writing file %s\n", filename);
        HeapFree(GetProcessHeap(), 0, filename);
    }
    else
        WINE_ERR("out of memory\n");
    return ret;
}

static BOOL write_freedesktop_association_entry(void *user, const char *dot_extension,
                                                const char *friendlyAppName,
                                                const char *friendlyDocNameA,
                                                const char *mimeType, const char *progId,
                                                const char *openWithIcon)
{
    struct xdg_file_type_user_data *ud = user;
    BOOL ret = FALSE;
    FILE *desktop;
    char *desktopPath = heap_printf("%s/wine-extension-%s.desktop", ud->applications_dir, &dot_extension[1]);

    if (!desktopPath) {
        WINE_ERR("out of memory\n");
        return FALSE;
    }

    WINE_TRACE("writing association for file type %s, friendlyAppName=%s, MIME type %s, progID=%s, icon=%s to file %s\n",
               wine_dbgstr_a(dot_extension), wine_dbgstr_a(friendlyAppName), wine_dbgstr_a(mimeType),
               wine_dbgstr_a(progId), wine_dbgstr_a(openWithIcon), wine_dbgstr_a(desktopPath));

    desktop = fopen(desktopPath, "w");
    if (desktop)
    {
        fprintf(desktop, "[Desktop Entry]\n");
        fprintf(desktop, "Type=Application\n");
        fprintf(desktop, "Name=%s\n", friendlyAppName);
        fprintf(desktop, "MimeType=%s;\n", mimeType);
        fprintf(desktop, "Exec=env WINEPREFIX=\"%s\" wine start /ProgIDOpen %s %%f\n", wine_get_config_dir(), progId);
        fprintf(desktop, "NoDisplay=true\n");
        fprintf(desktop, "StartupNotify=true\n");
        if (openWithIcon)
            fprintf(desktop, "Icon=%s\n", openWithIcon);
        ret = TRUE;
        fclose(desktop);
    }
    else
        WINE_ERR("error writing association file %s\n", wine_dbgstr_a(desktopPath));
    return ret;
}

static BOOL init_xdg(void)
{
    WCHAR shellDesktopPath[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, SHGFP_TYPE_CURRENT, shellDesktopPath);
    if (SUCCEEDED(hr))
        xdg_desktop_dir = wine_get_unix_file_name(shellDesktopPath);
    if (xdg_desktop_dir == NULL)
    {
        WINE_ERR("error looking up the desktop directory\n");
        return FALSE;
    }

    if (getenv("XDG_CONFIG_HOME"))
        xdg_config_dir = heap_printf("%s/menus/applications-merged", getenv("XDG_CONFIG_HOME"));
    else
        xdg_config_dir = heap_printf("%s/.config/menus/applications-merged", getenv("HOME"));
    if (xdg_config_dir)
    {
        create_directories(xdg_config_dir);
        if (getenv("XDG_DATA_HOME"))
            xdg_data_dir = strdupA(getenv("XDG_DATA_HOME"));
        else
            xdg_data_dir = heap_printf("%s/.local/share", getenv("HOME"));
        if (xdg_data_dir)
        {
            char *buffer;
            create_directories(xdg_data_dir);
            buffer = heap_printf("%s/desktop-directories", xdg_data_dir);
            if (buffer)
            {
                mkdir(buffer, 0777);
                HeapFree(GetProcessHeap(), 0, buffer);
            }
            return TRUE;
        }
        HeapFree(GetProcessHeap(), 0, xdg_config_dir);
    }
    WINE_ERR("out of memory\n");
    return FALSE;
}

static int xdg_build_desktop_link(const char *unix_link, const char *link, const char *link_name, const char *path,
                                const char *args, const char *descr, const char *workdir, char *icon)
{
    char *location;
    int r = -1;

    location = heap_printf("%s/%s.desktop", xdg_desktop_dir, link_name);
    if (location)
    {
        r = !write_desktop_entry(unix_link, location, link_name,
                path, args, descr, workdir, icon);
        if (r == 0)
            chmod(location, 0755);
    }
    return r;
}

static int xdg_build_menu_link(const char *unix_link, const char *link, const char *link_name, const char *path,
                             const char *args, const char *descr, const char *workdir, char *icon)
{
    return !write_menu_entry(unix_link, link, path, args, descr, workdir, icon);
}

static void *xdg_refresh_file_type_associations_init(void)
{
    struct xdg_file_type_user_data *ud = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ud));

    if (ud == NULL)
    {
        WINE_ERR("out of memory\n");
        return NULL;
    }

    ud->mime_dir = heap_printf("%s/mime", xdg_data_dir);
    if (ud->mime_dir == NULL)
    {
        WINE_ERR("out of memory\n");
        return NULL;
    }
    create_directories(ud->mime_dir);

    ud->packages_dir = heap_printf("%s/packages", ud->mime_dir);
    if (ud->packages_dir == NULL)
    {
        WINE_ERR("out of memory\n");
        return NULL;
    }
    create_directories(ud->packages_dir);

    ud->applications_dir = heap_printf("%s/applications", xdg_data_dir);
    if (ud->applications_dir == NULL)
    {
        WINE_ERR("out of memory\n");
        return NULL;
    }
    create_directories(ud->applications_dir);

    if (!build_native_mime_types(xdg_data_dir, &ud->native_mime_types))
        return NULL;

    return ud;
}

static BOOL xdg_remove_file_type_association(void *user, const char *dot_extension, LPCWSTR extensionW)
{
    struct xdg_file_type_user_data *ud = user;
    char *desktopPath = heap_printf("%s/wine-extension-%s.desktop", ud->applications_dir, &dot_extension[1]);

    if (desktopPath)
    {
        WINE_TRACE("removing file type association for %s\n", wine_dbgstr_w(extensionW));
        remove(desktopPath);
        HeapFree(GetProcessHeap(), 0, desktopPath);
        return TRUE;
    }

    return FALSE;
}

static void xdg_refresh_file_type_associations_cleanup(void *user, BOOL hasChanged)
{
    struct xdg_file_type_user_data *ud = user;

    if (hasChanged)
    {
        const char *argv[3];

        argv[0] = "update-mime-database";
        argv[1] = ud->mime_dir;
        argv[2] = NULL;
        _spawnvp( _P_DETACH, argv[0], argv );

        argv[0] = "update-desktop-database";
        argv[1] = ud->applications_dir;
        _spawnvp( _P_DETACH, argv[0], argv );
    }

    HeapFree(GetProcessHeap(), 0, ud->mime_dir);
    HeapFree(GetProcessHeap(), 0, ud->packages_dir);
    HeapFree(GetProcessHeap(), 0, ud->applications_dir);
    HeapFree(GetProcessHeap(), 0, ud);
}

const struct winemenubuilder_dispatch xdg_dispatch =
{
    init_xdg,

    xdg_build_desktop_link,
    xdg_build_menu_link,

    xdg_write_icon,

    xdg_refresh_file_type_associations_init,
    freedesktop_mime_type_for_extension,
    write_freedesktop_mime_type_entry,
    write_freedesktop_association_entry,
    xdg_remove_file_type_association,
    xdg_refresh_file_type_associations_cleanup
};
