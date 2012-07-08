/*
 * Copyright 2012 Per Johansson
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
 * TODO
 *  Test the wait flag, complicated since if we start the process it will
 * wait for us to exit.
 *  Test thumbnail_lnk (-t flag).
 *  Test files contents (might be overkill).
 */

#define COBJMACROS
#define NONAMELESSUNION

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <psapi.h>
#include <intshcut.h>

#include <stdio.h>

#include "wine/test.h"
#include "winemenubuilder_test.h"

#ifdef __APPLE__
static void setup_environment(void)
{
    HKEY key;

    putenv((char*)"WINE_APPLICATIONS_DIR=apps");
    putenv((char*)"WINE_ASSOCIATIONS_DIR=assocs");

    if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\MenuBuilder", &key))
        delete_key(key);
    ok(!RegCreateKeyA(HKEY_CURRENT_USER, "Software\\Wine\\MenuBuilder", &key), "Failed to create dispatch key\n");
    ok(!RegSetValueExA(key, "Dispatch", 0, REG_SZ, (BYTE*)"appbundle", sizeof("appbundle")), "Failed to set dispatch value\n");
    RegCloseKey(key);
}

static void cleanup_environment(void)
{
    char buffer[MAX_PATH];
    HKEY key;

    strcpy(buffer, "apps");
    ok(remove_recursive(buffer), "Failed to cleanup apps\n");
    strcpy(buffer, "assocs");
    ok(remove_recursive(buffer), "Failed to cleanup assocs\n");

    /* TODO: restore old contents, if any */
    if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\MenuBuilder", &key))
        delete_key(key);
}

static void test_create_association(void)
{
    setup_association_keys();

    ok(run_winemenubuilder("-a"), "Failed to run winemenubuilder\n");

    verify_file_present("assocs\\wine-extension-wmbtest2.app\\Contents\\Info.plist");
    verify_file_present("assocs\\wine-extension-wmbtest2.app\\Contents\\MacOS\\winelauncher");
    verify_file_present("assocs\\wine-extension-wmbtest2.app\\Contents\\Resources\\cmd.icns");
    verify_file_present("assocs\\wine-extension-wmbtest2.app\\Contents\\Resources\\English.lproj\\InfoPlist.strings");
}

static void test_remove_association(void)
{
    remove_association_keys();

    ok(run_winemenubuilder("-a"), "Failed to run winemenubuilder\n");

    verify_file_not_present("assocs\\wine-extension-wmbtest2.app");
}

static void test_link_desktop_program(void)
{
    WCHAR path[MAX_PATH];
    static const WCHAR link[] = {'\\','w','m','b','t','e','s','t','.','l','n','k','\0'};
    char pathA[MAX_PATH];

    get_common_desktop_directory(path, MAX_PATH);
    ok(lstrlenW(path) + lstrlenW(link) < MAX_PATH, "buffer overflow\n");
    lstrcatW(path, link);

    create_link(path, "cmd.exe", NULL, NULL, NULL, NULL, 0);

    wait_for_menubuilder();

    get_private_desktop_directoryA(pathA, MAX_PATH);
    strcat(pathA, "\\wmbtest.app\\Contents\\MacOS\\winelauncher");
    verify_file_present(pathA);
}

static void test_link_menu_program(void)
{
    WCHAR path[MAX_PATH];
    static const WCHAR link[] = {'\\','w','m','b','t','e','s','t','.','l','n','k','\0'};

    get_common_start_menu_directory(path, MAX_PATH);
    ok(lstrlenW(path) + lstrlenW(link) < MAX_PATH, "buffer overflow\n");
    lstrcatW(path, link);

    create_link(path, "cmd.exe", "/c exit 0", "C:\\", "WMB test link", "cmd.exe", 0);

    wait_for_menubuilder();

    verify_file_present("apps\\wmbtest.app\\Contents\\Info.plist");
    verify_file_present("apps\\wmbtest.app\\Contents\\MacOS\\winelauncher");
    verify_file_present("apps\\wmbtest.app\\Contents\\Resources\\cmd.icns");
    verify_file_present("apps\\wmbtest.app\\Contents\\Resources\\English.lproj\\InfoPlist.strings");
}

static void test_link_desktop_url(void)
{
    WCHAR path[MAX_PATH];
    static const WCHAR link[] = {'\\','w','m','b','t','e','s','t','u','r','l','.','u','r','l','\0'};
    char pathA[MAX_PATH];

    get_common_desktop_directory(path, MAX_PATH);
    ok(lstrlenW(path) + lstrlenW(link) < MAX_PATH, "buffer overflow\n");
    lstrcatW(path, link);

    create_url(path, "file:///", NULL, 0);

    wait_for_menubuilder();

    get_private_desktop_directoryA(pathA, MAX_PATH);
    strcat(pathA, "\\wmbtesturl.app\\Contents\\MacOS\\winelauncher");
    verify_file_present(pathA);
}

static void test_link_menu_url(void)
{
    WCHAR path[MAX_PATH];
    static const WCHAR link[] = {'\\','w','m','b','t','e','s','t','u','r','l','.','u','r','l','\0'};
    static WCHAR cmdExeW[] = {'c','m','d','.','e','x','e','\0'};

    get_common_start_menu_directory(path, MAX_PATH);
    ok(lstrlenW(path) + lstrlenW(link) < MAX_PATH, "buffer overflow\n");
    lstrcatW(path, link);

    create_url(path, "file:///", cmdExeW, 0);

    wait_for_menubuilder();

    verify_file_present("apps\\wmbtesturl.app\\Contents\\Info.plist");
    verify_file_present("apps\\wmbtesturl.app\\Contents\\MacOS\\winelauncher");
    verify_file_present("apps\\wmbtesturl.app\\Contents\\Resources\\cmd.icns");
    verify_file_present("apps\\wmbtesturl.app\\Contents\\Resources\\English.lproj\\InfoPlist.strings");
}

static void test_cleanup(void)
{
    WCHAR path[MAX_PATH];
    static const WCHAR lnk_link[] = {'\\','w','m','b','t','e','s','t','.','l','n','k','\0'};
    static const WCHAR url_link[] = {'\\','w','m','b','t','e','s','t','u','r','l','.','u','r','l','\0'};
    char pathA[MAX_PATH];

    get_common_desktop_directory(path, MAX_PATH);
    lstrcatW(path, lnk_link);
    DeleteFileW(path);

    get_common_start_menu_directory(path, MAX_PATH);
    lstrcatW(path, lnk_link);
    DeleteFileW(path);

    get_common_desktop_directory(path, MAX_PATH);
    lstrcatW(path, url_link);
    DeleteFileW(path);

    get_common_start_menu_directory(path, MAX_PATH);
    lstrcatW(path, url_link);
    DeleteFileW(path);

    ok(run_winemenubuilder("-r"), "Failed to run winemenubuilder\n");

    verify_file_not_present("apps\\wmbtest.app");

    get_private_desktop_directoryA(pathA, MAX_PATH);
    strcat(pathA, "\\wmbtest.app");
    verify_file_not_present(pathA);

    verify_file_not_present("apps\\wmbtesturl.app");

    get_private_desktop_directoryA(pathA, MAX_PATH);
    strcat(pathA, "\\wmbtesturl.app");
    /* winemenubuilder does not track URLs on the desktop for some reason. */
#if 0
    verify_file_not_present(pathA);
#else
    remove_recursive(pathA);
#endif
}
#endif /*__APPLE__*/

START_TEST(appbundle)
{
#ifdef __APPLE__
    HRESULT r;

    if (!winemenubuilder_available()) {
        win_skip("winemenubuilder not installed, skipping tests\n");
        return;
    }

    r = CoInitialize(NULL);
    ok(r == S_OK, "CoInitialize failed (0x%08x)\n", r);
    if (r != S_OK)
        return;

    wait_for_boot();
    wait_for_menubuilder();

    setup_environment();

    test_create_association();
    test_remove_association();

    test_link_desktop_program();
    test_link_menu_program();
    test_link_desktop_url();
    test_link_menu_url();

    test_cleanup();

    cleanup_environment();
#endif
}

