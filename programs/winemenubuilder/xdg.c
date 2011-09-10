
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

struct xdg_mime_type
{
    char *mimeType;
    char *glob;
    char *lower_glob;
    struct list entry;
};

struct rb_string_entry
{
    char *string;
    struct wine_rb_entry entry;
};

static char *xdg_config_dir;
static char *xdg_data_dir;
static char *xdg_desktop_dir;

static char *strdupA( const char *str )
{
    char *ret;

    if (!str) return NULL;
    if ((ret = HeapAlloc( GetProcessHeap(), 0, strlen(str) + 1 ))) strcpy( ret, str );
    return ret;
}

static int winemenubuilder_rb_string_compare(const void *key, const struct wine_rb_entry *entry)
{
    const struct rb_string_entry *t = WINE_RB_ENTRY_VALUE(entry, const struct rb_string_entry, entry);

    return strcmp((char*)key, t->string);
}

static void *winemenubuilder_rb_alloc(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void *winemenubuilder_rb_realloc(void *ptr, size_t size)
{
    return HeapReAlloc(GetProcessHeap(), 0, ptr, size);
}

static void winemenubuilder_rb_free(void *ptr)
{
    HeapFree(GetProcessHeap(), 0, ptr);
}

static void winemenubuilder_rb_destroy(struct wine_rb_entry *entry, void *context)
{
    struct rb_string_entry *t = WINE_RB_ENTRY_VALUE(entry, struct rb_string_entry, entry);
    HeapFree(GetProcessHeap(), 0, t->string);
    HeapFree(GetProcessHeap(), 0, t);
}

static const struct wine_rb_functions winemenubuilder_rb_functions =
{
    winemenubuilder_rb_alloc,
    winemenubuilder_rb_realloc,
    winemenubuilder_rb_free,
    winemenubuilder_rb_string_compare,
};

static char *slashes_to_minuses(const char *string)
{
    int i;
    char *ret = HeapAlloc(GetProcessHeap(), 0, lstrlenA(string) + 1);
    if (ret)
    {
        for (i = 0; string[i]; i++)
        {
            if (string[i] == '/')
                ret[i] = '-';
            else
                ret[i] = string[i];
        }
        ret[i] = 0;
        return ret;
    }
    return NULL;
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

static unsigned short crc16(const char* string)
{
    unsigned short crc = 0;
    int i, j, xor_poly;

    for (i = 0; string[i] != 0; i++)
    {
        char c = string[i];
        for (j = 0; j < 8; c >>= 1, j++)
        {
            xor_poly = (c ^ crc) & 1;
            crc >>= 1;
            if (xor_poly)
                crc ^= 0xa001;
        }
    }
    return crc;
}

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

HRESULT xdg_write_icon(IStream *icoStream, int exeIndex, LPCWSTR icoPathW,
                                   const char *destFilename, char **nativeIdentifier)
{
    ICONDIRENTRY *iconDirEntries = NULL;
    int numEntries;
    int i;
    char *icoPathA = NULL;
    char *iconsDir = NULL;
    unsigned short crc;
    char *p, *q;
    HRESULT hr = S_OK;
    LARGE_INTEGER zero;

    if (*nativeIdentifier)
    {
        /* Did all work in the first call. */
        return hr;
    }

    hr = read_ico_direntries(icoStream, &iconDirEntries, &numEntries);
    if (FAILED(hr))
        goto end;

    icoPathA = wchars_to_utf8_chars(icoPathW);
    if (icoPathA == NULL)
    {
        hr = E_OUTOFMEMORY;
        goto end;
    }
    crc = crc16(icoPathA);
    p = strrchr(icoPathA, '\\');
    if (p == NULL)
        p = icoPathA;
    else
    {
        *p = 0;
        p++;
    }
    q = strrchr(p, '.');
    if (q)
        *q = 0;
    if (destFilename)
        *nativeIdentifier = heap_printf("%s", destFilename);
    else
        *nativeIdentifier = heap_printf("%04X_%s.%d", crc, p, exeIndex);
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
    HeapFree(GetProcessHeap(), 0, icoPathA);
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

static BOOL write_menu_entry(const char *unix_link, const char *link, const char *link_name, const char *path,
                             const char *args, const char *descr, const char *workdir, const char *icon)
{
    char *desktopPath = NULL;
    char *desktopDir;
    char *filename = NULL;
    BOOL ret = TRUE;

    WINE_TRACE("(%s, %s, %s, %s, %s, %s, %s)\n", wine_dbgstr_a(unix_link), wine_dbgstr_a(link),
               wine_dbgstr_a(path), wine_dbgstr_a(args), wine_dbgstr_a(descr),
               wine_dbgstr_a(workdir), wine_dbgstr_a(icon));

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
    if (!write_desktop_entry(unix_link, desktopPath, link_name, path, args, descr, workdir, icon))
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

static BOOL freedesktop_mime_type_for_extension(struct list *native_mime_types,
                                                const char *extensionA,
                                                LPCWSTR extensionW,
                                                char **mime_type)
{
    WCHAR *lower_extensionW;
    INT len;
    BOOL ret = match_glob(native_mime_types, extensionA, 0, mime_type);
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
            ret = match_glob(native_mime_types, lower_extensionA, 1, mime_type);
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

static BOOL write_freedesktop_mime_type_entry(const char *packages_dir, const char *dot_extension,
                                              const char *mime_type, const char *comment)
{
    BOOL ret = FALSE;
    char *filename;

    WINE_TRACE("writing MIME type %s, extension=%s, comment=%s\n", wine_dbgstr_a(mime_type),
               wine_dbgstr_a(dot_extension), wine_dbgstr_a(comment));

    filename = heap_printf("%s/x-wine-extension-%s.xml", packages_dir, &dot_extension[1]);
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

static BOOL write_freedesktop_association_entry(const char *desktopPath, const char *dot_extension,
                                                const char *friendlyAppName, const char *mimeType,
                                                const char *progId, const char *openWithIcon)
{
    BOOL ret = FALSE;
    FILE *desktop;

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

static BOOL is_extension_blacklisted(LPCWSTR extension)
{
    /* These are managed through external tools like wine.desktop, to evade malware created file type associations */
    static const WCHAR comW[] = {'.','c','o','m',0};
    static const WCHAR exeW[] = {'.','e','x','e',0};
    static const WCHAR msiW[] = {'.','m','s','i',0};

    if (!strcmpiW(extension, comW) ||
        !strcmpiW(extension, exeW) ||
        !strcmpiW(extension, msiW))
        return TRUE;
    return FALSE;
}

static const char* get_special_mime_type(LPCWSTR extension)
{
    static const WCHAR lnkW[] = {'.','l','n','k',0};
    if (!strcmpiW(extension, lnkW))
        return "application/x-ms-shortcut";
    return NULL;
}

static WCHAR* reg_get_valW(HKEY key, LPCWSTR subkey, LPCWSTR name)
{
    DWORD size;
    if (RegGetValueW(key, subkey, name, RRF_RT_REG_SZ, NULL, NULL, &size) == ERROR_SUCCESS)
    {
        WCHAR *ret = HeapAlloc(GetProcessHeap(), 0, size);
        if (ret)
        {
            if (RegGetValueW(key, subkey, name, RRF_RT_REG_SZ, NULL, ret, &size) == ERROR_SUCCESS)
                return ret;
        }
        HeapFree(GetProcessHeap(), 0, ret);
    }
    return NULL;
}

static HKEY open_associations_reg_key(void)
{
    static const WCHAR Software_Wine_FileOpenAssociationsW[] = {
        'S','o','f','t','w','a','r','e','\\','W','i','n','e','\\','F','i','l','e','O','p','e','n','A','s','s','o','c','i','a','t','i','o','n','s',0};
    HKEY assocKey;
    if (RegCreateKeyW(HKEY_CURRENT_USER, Software_Wine_FileOpenAssociationsW, &assocKey) == ERROR_SUCCESS)
        return assocKey;
    return NULL;
}

static CHAR* reg_get_val_utf8(HKEY key, LPCWSTR subkey, LPCWSTR name)
{
    WCHAR *valW = reg_get_valW(key, subkey, name);
    if (valW)
    {
        char *val = wchars_to_utf8_chars(valW);
        HeapFree(GetProcessHeap(), 0, valW);
        return val;
    }
    return NULL;
}

static BOOL has_association_changed(LPCWSTR extensionW, LPCSTR mimeType, LPCWSTR progId,
    LPCSTR appName, LPCSTR openWithIcon)
{
    static const WCHAR ProgIDW[] = {'P','r','o','g','I','D',0};
    static const WCHAR MimeTypeW[] = {'M','i','m','e','T','y','p','e',0};
    static const WCHAR AppNameW[] = {'A','p','p','N','a','m','e',0};
    static const WCHAR OpenWithIconW[] = {'O','p','e','n','W','i','t','h','I','c','o','n',0};
    HKEY assocKey;
    BOOL ret;

    if ((assocKey = open_associations_reg_key()))
    {
        CHAR *valueA;
        WCHAR *value;

        ret = FALSE;

        valueA = reg_get_val_utf8(assocKey, extensionW, MimeTypeW);
        if (!valueA || lstrcmpA(valueA, mimeType))
            ret = TRUE;
        HeapFree(GetProcessHeap(), 0, valueA);

        value = reg_get_valW(assocKey, extensionW, ProgIDW);
        if (!value || strcmpW(value, progId))
            ret = TRUE;
        HeapFree(GetProcessHeap(), 0, value);

        valueA = reg_get_val_utf8(assocKey, extensionW, AppNameW);
        if (!valueA || lstrcmpA(valueA, appName))
            ret = TRUE;
        HeapFree(GetProcessHeap(), 0, valueA);

        valueA = reg_get_val_utf8(assocKey, extensionW, OpenWithIconW);
        if ((openWithIcon && !valueA) ||
            (!openWithIcon && valueA) ||
            (openWithIcon && valueA && lstrcmpA(valueA, openWithIcon)))
            ret = TRUE;
        HeapFree(GetProcessHeap(), 0, valueA);

        RegCloseKey(assocKey);
    }
    else
    {
        WINE_ERR("error opening associations registry key\n");
        ret = FALSE;
    }
    return ret;
}

static void update_association(LPCWSTR extension, LPCSTR mimeType, LPCWSTR progId,
    LPCSTR appName, LPCSTR desktopFile, LPCSTR openWithIcon)
{
    static const WCHAR ProgIDW[] = {'P','r','o','g','I','D',0};
    static const WCHAR MimeTypeW[] = {'M','i','m','e','T','y','p','e',0};
    static const WCHAR AppNameW[] = {'A','p','p','N','a','m','e',0};
    static const WCHAR DesktopFileW[] = {'D','e','s','k','t','o','p','F','i','l','e',0};
    static const WCHAR OpenWithIconW[] = {'O','p','e','n','W','i','t','h','I','c','o','n',0};
    HKEY assocKey = NULL;
    HKEY subkey = NULL;
    WCHAR *mimeTypeW = NULL;
    WCHAR *appNameW = NULL;
    WCHAR *desktopFileW = NULL;
    WCHAR *openWithIconW = NULL;

    assocKey = open_associations_reg_key();
    if (assocKey == NULL)
    {
        WINE_ERR("could not open file associations key\n");
        goto done;
    }

    if (RegCreateKeyW(assocKey, extension, &subkey) != ERROR_SUCCESS)
    {
        WINE_ERR("could not create extension subkey\n");
        goto done;
    }

    mimeTypeW = utf8_chars_to_wchars(mimeType);
    if (mimeTypeW == NULL)
    {
        WINE_ERR("out of memory\n");
        goto done;
    }

    appNameW = utf8_chars_to_wchars(appName);
    if (appNameW == NULL)
    {
        WINE_ERR("out of memory\n");
        goto done;
    }

    desktopFileW = utf8_chars_to_wchars(desktopFile);
    if (desktopFileW == NULL)
    {
        WINE_ERR("out of memory\n");
        goto done;
    }

    if (openWithIcon)
    {
        openWithIconW = utf8_chars_to_wchars(openWithIcon);
        if (openWithIconW == NULL)
        {
            WINE_ERR("out of memory\n");
            goto done;
        }
    }

    RegSetValueExW(subkey, MimeTypeW, 0, REG_SZ, (const BYTE*) mimeTypeW, (lstrlenW(mimeTypeW) + 1) * sizeof(WCHAR));
    RegSetValueExW(subkey, ProgIDW, 0, REG_SZ, (const BYTE*) progId, (lstrlenW(progId) + 1) * sizeof(WCHAR));
    RegSetValueExW(subkey, AppNameW, 0, REG_SZ, (const BYTE*) appNameW, (lstrlenW(appNameW) + 1) * sizeof(WCHAR));
    RegSetValueExW(subkey, DesktopFileW, 0, REG_SZ, (const BYTE*) desktopFileW, (lstrlenW(desktopFileW) + 1) * sizeof(WCHAR));
    if (openWithIcon)
        RegSetValueExW(subkey, OpenWithIconW, 0, REG_SZ, (const BYTE*) openWithIconW, (lstrlenW(openWithIconW) + 1) * sizeof(WCHAR));
    else
        RegDeleteValueW(subkey, OpenWithIconW);

done:
    RegCloseKey(assocKey);
    RegCloseKey(subkey);
    HeapFree(GetProcessHeap(), 0, mimeTypeW);
    HeapFree(GetProcessHeap(), 0, appNameW);
    HeapFree(GetProcessHeap(), 0, desktopFileW);
    HeapFree(GetProcessHeap(), 0, openWithIconW);
}

static BOOL generate_associations(const char *xdg_data_home, const char *packages_dir, const char *applications_dir)
{
    static const WCHAR openW[] = {'o','p','e','n',0};
    struct wine_rb_tree mimeProgidTree;
    struct list *nativeMimeTypes = NULL;
    LSTATUS ret = 0;
    int i;
    BOOL hasChanged = FALSE;

    if (wine_rb_init(&mimeProgidTree, &winemenubuilder_rb_functions))
    {
        WINE_ERR("wine_rb_init failed\n");
        return FALSE;
    }
    if (!build_native_mime_types(xdg_data_home, &nativeMimeTypes))
    {
        WINE_ERR("could not build native MIME types\n");
        return FALSE;
    }

    for (i = 0; ; i++)
    {
        WCHAR *extensionW = NULL;
        DWORD size = 1024;

        do
        {
            HeapFree(GetProcessHeap(), 0, extensionW);
            extensionW = HeapAlloc(GetProcessHeap(), 0, size * sizeof(WCHAR));
            if (extensionW == NULL)
            {
                WINE_ERR("out of memory\n");
                ret = ERROR_OUTOFMEMORY;
                break;
            }
            ret = RegEnumKeyExW(HKEY_CLASSES_ROOT, i, extensionW, &size, NULL, NULL, NULL, NULL);
            size *= 2;
        } while (ret == ERROR_MORE_DATA);

        if (ret == ERROR_SUCCESS && extensionW[0] == '.' && !is_extension_blacklisted(extensionW))
        {
            char *extensionA = NULL;
            WCHAR *commandW = NULL;
            WCHAR *executableW = NULL;
            char *openWithIconA = NULL;
            WCHAR *friendlyDocNameW = NULL;
            char *friendlyDocNameA = NULL;
            WCHAR *iconW = NULL;
            char *iconA = NULL;
            WCHAR *contentTypeW = NULL;
            char *mimeTypeA = NULL;
            WCHAR *friendlyAppNameW = NULL;
            char *friendlyAppNameA = NULL;
            WCHAR *progIdW = NULL;
            char *progIdA = NULL;
            char *mimeProgId = NULL;

            extensionA = wchars_to_utf8_chars(strlwrW(extensionW));
            if (extensionA == NULL)
            {
                WINE_ERR("out of memory\n");
                goto end;
            }

            friendlyDocNameW = assoc_query(ASSOCSTR_FRIENDLYDOCNAME, extensionW, NULL);
            if (friendlyDocNameW)
            {
                friendlyDocNameA = wchars_to_utf8_chars(friendlyDocNameW);
                if (friendlyDocNameA == NULL)
                {
                    WINE_ERR("out of memory\n");
                    goto end;
                }
            }

            iconW = assoc_query(ASSOCSTR_DEFAULTICON, extensionW, NULL);

            contentTypeW = assoc_query(ASSOCSTR_CONTENTTYPE, extensionW, NULL);
            if (contentTypeW)
                strlwrW(contentTypeW);

            if (!freedesktop_mime_type_for_extension(nativeMimeTypes, extensionA, extensionW, &mimeTypeA))
                goto end;

            if (mimeTypeA == NULL)
            {
                if (contentTypeW != NULL && strchrW(contentTypeW, '/'))
                    mimeTypeA = wchars_to_utf8_chars(contentTypeW);
                else if ((get_special_mime_type(extensionW)))
                    mimeTypeA = strdupA(get_special_mime_type(extensionW));
                else
                    mimeTypeA = heap_printf("application/x-wine-extension-%s", &extensionA[1]);

                if (mimeTypeA != NULL)
                {
                    /* GNOME seems to ignore the <icon> tag in MIME packages,
                     * and the default name is more intuitive anyway.
                     */
                    if (iconW)
                    {
                        char *flattened_mime = slashes_to_minuses(mimeTypeA);
                        if (flattened_mime)
                        {
                            int index = 0;
                            WCHAR *comma = strrchrW(iconW, ',');
                            if (comma)
                            {
                                *comma = 0;
                                index = atoiW(comma + 1);
                            }
                            extract_icon(iconW, index, flattened_mime, FALSE, &iconA);
                            HeapFree(GetProcessHeap(), 0, flattened_mime);
                        }
                    }

                    write_freedesktop_mime_type_entry(packages_dir, extensionA, mimeTypeA, friendlyDocNameA);
                    hasChanged = TRUE;
                }
                else
                {
                    WINE_FIXME("out of memory\n");
                    goto end;
                }
            }

            commandW = assoc_query(ASSOCSTR_COMMAND, extensionW, openW);
            if (commandW == NULL)
                /* no command => no application is associated */
                goto end;

            executableW = assoc_query(ASSOCSTR_EXECUTABLE, extensionW, openW);
            if (executableW)
                extract_icon(executableW, 0, NULL, FALSE, &openWithIconA);

            friendlyAppNameW = assoc_query(ASSOCSTR_FRIENDLYAPPNAME, extensionW, openW);
            if (friendlyAppNameW)
            {
                friendlyAppNameA = wchars_to_utf8_chars(friendlyAppNameW);
                if (friendlyAppNameA == NULL)
                {
                    WINE_ERR("out of memory\n");
                    goto end;
                }
            }
            else
            {
                friendlyAppNameA = heap_printf("A Wine application");
                if (friendlyAppNameA == NULL)
                {
                    WINE_ERR("out of memory\n");
                    goto end;
                }
            }

            progIdW = reg_get_valW(HKEY_CLASSES_ROOT, extensionW, NULL);
            if (progIdW)
            {
                progIdA = escape(progIdW);
                if (progIdA == NULL)
                {
                    WINE_ERR("out of memory\n");
                    goto end;
                }
            }
            else
                goto end; /* no progID => not a file type association */

            /* Do not allow duplicate ProgIDs for a MIME type, it causes unnecessary duplication in Open dialogs */
            mimeProgId = heap_printf("%s=>%s", mimeTypeA, progIdA);
            if (mimeProgId)
            {
                struct rb_string_entry *entry;
                if (wine_rb_get(&mimeProgidTree, mimeProgId))
                {
                    HeapFree(GetProcessHeap(), 0, mimeProgId);
                    goto end;
                }
                entry = HeapAlloc(GetProcessHeap(), 0, sizeof(struct rb_string_entry));
                if (!entry)
                {
                    WINE_ERR("out of memory allocating rb_string_entry\n");
                    goto end;
                }
                entry->string = mimeProgId;
                if (wine_rb_put(&mimeProgidTree, mimeProgId, &entry->entry))
                {
                    WINE_ERR("error updating rb tree\n");
                    goto end;
                }
            }

            if (has_association_changed(extensionW, mimeTypeA, progIdW, friendlyAppNameA, openWithIconA))
            {
                char *desktopPath = heap_printf("%s/wine-extension-%s.desktop", applications_dir, &extensionA[1]);
                if (desktopPath)
                {
                    if (write_freedesktop_association_entry(desktopPath, extensionA, friendlyAppNameA, mimeTypeA, progIdA, openWithIconA))
                    {
                        hasChanged = TRUE;
                        update_association(extensionW, mimeTypeA, progIdW, friendlyAppNameA, desktopPath, openWithIconA);
                    }
                    HeapFree(GetProcessHeap(), 0, desktopPath);
                }
            }

        end:
            HeapFree(GetProcessHeap(), 0, extensionA);
            HeapFree(GetProcessHeap(), 0, commandW);
            HeapFree(GetProcessHeap(), 0, executableW);
            HeapFree(GetProcessHeap(), 0, openWithIconA);
            HeapFree(GetProcessHeap(), 0, friendlyDocNameW);
            HeapFree(GetProcessHeap(), 0, friendlyDocNameA);
            HeapFree(GetProcessHeap(), 0, iconW);
            HeapFree(GetProcessHeap(), 0, iconA);
            HeapFree(GetProcessHeap(), 0, contentTypeW);
            HeapFree(GetProcessHeap(), 0, mimeTypeA);
            HeapFree(GetProcessHeap(), 0, friendlyAppNameW);
            HeapFree(GetProcessHeap(), 0, friendlyAppNameA);
            HeapFree(GetProcessHeap(), 0, progIdW);
            HeapFree(GetProcessHeap(), 0, progIdA);
        }
        HeapFree(GetProcessHeap(), 0, extensionW);
        if (ret != ERROR_SUCCESS)
            break;
    }

    wine_rb_destroy(&mimeProgidTree, winemenubuilder_rb_destroy, NULL);
    free_native_mime_types(nativeMimeTypes);
    return hasChanged;
}

static BOOL cleanup_associations(void)
{
    static const WCHAR openW[] = {'o','p','e','n',0};
    static const WCHAR DesktopFileW[] = {'D','e','s','k','t','o','p','F','i','l','e',0};
    HKEY assocKey;
    BOOL hasChanged = FALSE;
    if ((assocKey = open_associations_reg_key()))
    {
        int i;
        BOOL done = FALSE;
        for (i = 0; !done;)
        {
            WCHAR *extensionW = NULL;
            DWORD size = 1024;
            LSTATUS ret;

            do
            {
                HeapFree(GetProcessHeap(), 0, extensionW);
                extensionW = HeapAlloc(GetProcessHeap(), 0, size * sizeof(WCHAR));
                if (extensionW == NULL)
                {
                    WINE_ERR("out of memory\n");
                    ret = ERROR_OUTOFMEMORY;
                    break;
                }
                ret = RegEnumKeyExW(assocKey, i, extensionW, &size, NULL, NULL, NULL, NULL);
                size *= 2;
            } while (ret == ERROR_MORE_DATA);

            if (ret == ERROR_SUCCESS)
            {
                WCHAR *command;
                command = assoc_query(ASSOCSTR_COMMAND, extensionW, openW);
                if (command == NULL)
                {
                    char *desktopFile = reg_get_val_utf8(assocKey, extensionW, DesktopFileW);
                    if (desktopFile)
                    {
                        WINE_TRACE("removing file type association for %s\n", wine_dbgstr_w(extensionW));
                        remove_unix_link(desktopFile);
                    }
                    RegDeleteKeyW(assocKey, extensionW);
                    hasChanged = TRUE;
                    HeapFree(GetProcessHeap(), 0, desktopFile);
                }
                else
                    i++;
                HeapFree(GetProcessHeap(), 0, command);
            }
            else
            {
                if (ret != ERROR_NO_MORE_ITEMS)
                    WINE_ERR("error %d while reading registry\n", ret);
                done = TRUE;
            }
            HeapFree(GetProcessHeap(), 0, extensionW);
        }
        RegCloseKey(assocKey);
    }
    else
        WINE_ERR("could not open file associations key\n");
    return hasChanged;
}

void xdg_refresh_file_type_associations(void)
{
    HANDLE hSem = NULL;
    char *mime_dir = NULL;
    char *packages_dir = NULL;
    char *applications_dir = NULL;
    BOOL hasChanged;

    hSem = CreateSemaphoreA( NULL, 1, 1, "winemenubuilder_semaphore");
    if( WAIT_OBJECT_0 != MsgWaitForMultipleObjects( 1, &hSem, FALSE, INFINITE, QS_ALLINPUT ) )
    {
        WINE_ERR("failed wait for semaphore\n");
        CloseHandle(hSem);
        hSem = NULL;
        goto end;
    }

    mime_dir = heap_printf("%s/mime", xdg_data_dir);
    if (mime_dir == NULL)
    {
        WINE_ERR("out of memory\n");
        goto end;
    }
    create_directories(mime_dir);

    packages_dir = heap_printf("%s/packages", mime_dir);
    if (packages_dir == NULL)
    {
        WINE_ERR("out of memory\n");
        goto end;
    }
    create_directories(packages_dir);

    applications_dir = heap_printf("%s/applications", xdg_data_dir);
    if (applications_dir == NULL)
    {
        WINE_ERR("out of memory\n");
        goto end;
    }
    create_directories(applications_dir);

    hasChanged = generate_associations(xdg_data_dir, packages_dir, applications_dir);
    hasChanged |= cleanup_associations();
    if (hasChanged)
    {
        const char *argv[3];

        argv[0] = "update-mime-database";
        argv[1] = mime_dir;
        argv[2] = NULL;
        spawnvp( _P_DETACH, argv[0], argv );

        argv[0] = "update-desktop-database";
        argv[1] = applications_dir;
        spawnvp( _P_DETACH, argv[0], argv );
    }

end:
    if (hSem)
    {
        ReleaseSemaphore(hSem, 1, NULL);
        CloseHandle(hSem);
    }
    HeapFree(GetProcessHeap(), 0, mime_dir);
    HeapFree(GetProcessHeap(), 0, packages_dir);
    HeapFree(GetProcessHeap(), 0, applications_dir);
}

int xdg_build_desktop_link(const char *unix_link, const char *link, const char *link_name, const char *path,
                                const char *args, const char *descr, const char *workdir, const char *icon)
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

int xdg_build_menu_link(const char *unix_link, const char *link, const char *link_name, const char *path,
                             const char *args, const char *descr, const char *workdir, const char *icon)
{
    return !write_menu_entry(unix_link, link, link_name, path, args, descr, workdir, icon);
}


BOOL xdg_init(void)
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

const struct winemenubuilder_dispatch xdg_dispatch =
{
    xdg_init,

    xdg_build_desktop_link,
    xdg_build_menu_link,

    xdg_write_icon,

    xdg_refresh_file_type_associations,
};

