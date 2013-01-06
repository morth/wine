/*
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
 */

typedef struct
{
    BYTE bWidth;
    BYTE bHeight;
    BYTE bColorCount;
    BYTE bReserved;
    WORD wPlanes;
    WORD wBitCount;
    DWORD dwBytesInRes;
    DWORD dwImageOffset;
} ICONDIRENTRY;

typedef struct
{
    WORD idReserved;
    WORD idType;
    WORD idCount;
} ICONDIR;

DEFINE_GUID(CLSID_WICIcnsEncoder, 0x312fb6f1,0xb767,0x409d,0x8a,0x6d,0x0f,0xc1,0x54,0xd4,0xf0,0x5c);

char *strdupA( const char *str );
char* heap_printf(const char *format, ...);
BOOL create_directories(char *directory);
BOOL remove_unix_link(const char *unix_link);
DWORD register_menus_entry(const char *unix_file, const char *windows_file);
char* wchars_to_utf8_chars(LPCWSTR string);
HRESULT read_ico_direntries(IStream *icoStream, ICONDIRENTRY **ppIconDirEntries, int *numEntries);
char* compute_native_identifier(int exeIndex, LPCWSTR icoPathW);
HRESULT convert_to_native_icon(IStream *icoFile, int *indices, int numIndices,
                                      const CLSID *outputFormat, const char *outputFileName, LPCWSTR commentW);

struct winemenubuilder_dispatch
{
    BOOL (*init)(void);

    int (*build_desktop_link)(const char *unix_link, const char *link, const char *link_name, const char *path,
            const char *args, const char *descr, const char *workdir, char *icon);
    int (*build_menu_link)(const char *unix_link, const char *link, const char *link_name, const char *path,
            const char *args, const char *descr, const char *workdir, char *icon);

    HRESULT (*write_icon)(IStream *icoStream, int exeIndex, LPCWSTR icoPathW, const char *destFilename);


    void *(*refresh_file_type_associations_init)(void);
    BOOL (*mime_type_for_extension)(void *user, const char *extensionA, LPCWSTR extensionW, char **mime_type);
    BOOL (*write_mime_type_entry)(void *user, const char *extensionA, const char *mimeTypeA, const char *friendlyDocNameA);
    BOOL (*write_association_entry)(void *user, const char *extensionA, const char *friendlyAppNameA,
            const char *friendlyDocNameA, const char *mimeTypeA, const char *progIdA,
            const char *appIconA);
    BOOL (*remove_file_type_association)(void *user, const char *extensionA, LPCWSTR extensionW);
    void (*refresh_file_type_associations_cleanup)(void *user, BOOL hasChanged);
};
