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

#include <initguid.h>
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <psapi.h>
#include <intshcut.h>

#include <stdio.h>

#include "wine/test.h"

/* delete key and all its subkeys */
DWORD delete_key( HKEY hkey )
{
    char name[MAX_PATH];
    DWORD ret;

    while (!(ret = RegEnumKeyA(hkey, 0, name, sizeof(name))))
    {
        HKEY tmp;
        if (!(ret = RegOpenKeyExA( hkey, name, 0, KEY_ENUMERATE_SUB_KEYS, &tmp )))
        {
            ret = delete_key( tmp );
            RegCloseKey( tmp );
        }
        if (ret) break;
    }
    if (ret != ERROR_NO_MORE_ITEMS) return ret;
    RegDeleteKeyA( hkey, "" );
    return 0;
}

BOOL remove_recursive(char *path)
{
    char *epath;
    HANDLE fh;
    WIN32_FIND_DATAA fdata;

    if (DeleteFileA(path))
        return TRUE;

    epath = path + strlen(path);
    strcpy(epath, "\\*");

    fh = FindFirstFileA(path, &fdata);
    do {
        if (strcmp(fdata.cFileName, ".") != 0 && strcmp(fdata.cFileName, "..") != 0) {
            strcpy(epath + 1, fdata.cFileName);
            if (!remove_recursive(path))
                return FALSE;
        }
    } while (FindNextFileA(fh, &fdata));

    FindClose(fh);
    *epath = '\0';
    return RemoveDirectoryA(path);
}

BOOL winemenubuilder_available(void)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[] = "winemenubuilder";

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return TRUE;
    }
    return FALSE;
}

BOOL run_winemenubuilder(const char *args)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char buffer[1024];
    DWORD dr;
    DWORD code;

    snprintf(buffer, sizeof(buffer), "winemenubuilder %s", args);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, buffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return FALSE;

    dr = WaitForSingleObject(pi.hProcess, 30000);
    if(dr == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 1);
        return FALSE;
    }

    if (!GetExitCodeProcess(pi.hProcess, &code))
        return FALSE;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return code == 0;
}

void wait_for_boot(void)
{
    /*
     * We might end up in a race condition with the automatically run winemenubuilder.
     * Wait for the boot event to finish.
     */
    static const WCHAR wineboot_eventW[] = {'_','_','w','i','n','e','b','o','o','t','_','e','v','e','n','t',0};
    HANDLE wbEvent;

    wbEvent = OpenEventW( SYNCHRONIZE, FALSE, wineboot_eventW);
    if (wbEvent) {
        WaitForSingleObject(wbEvent, INFINITE);
        CloseHandle(wbEvent);
        trace("Waited for boot\n");
    }
}

void verify_file_present(const char *path)
{
    HANDLE handle;
    WIN32_FIND_DATAA data;

    handle = FindFirstFileA(path, &data);
    ok(handle != INVALID_HANDLE_VALUE, "File %s not found\n", path);
    if (handle != INVALID_HANDLE_VALUE)
        FindClose(handle);
}

void verify_file_not_present(const char *path)
{
    HANDLE handle;
    WIN32_FIND_DATAA data;
    int err;

    handle = FindFirstFileA(path, &data);
    err = GetLastError();
    ok(handle == INVALID_HANDLE_VALUE, "File %s found when should be removed\n", path);
    ok(err == ERROR_FILE_NOT_FOUND, "Bad error %x searching for %s\n", err, path);
}

void create_link(const WCHAR *link, const char *cmd, const char *args, const char *workdir,
        const char *desc, const char *iconPath, int iconId)
{
    HRESULT r;
    IShellLinkA *sl;
    IPersistFile *pf;

    r = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                         &IID_IShellLinkA, (LPVOID*)&sl);
    ok(r == S_OK, "no IID_IShellLinkA (0x%08x)\n", r);
    if (r != S_OK)
        return;

    if (cmd)
    {
        r = IShellLinkA_SetPath(sl, cmd);
        ok(SUCCEEDED(r), "SetPath failed (0x%08x)\n", r);
    }
    if (args)
    {
        r = IShellLinkA_SetArguments(sl, args);
        ok(r == S_OK, "SetArguments failed (0x%08x)\n", r);
    }
    if (workdir)
    {
        r = IShellLinkA_SetWorkingDirectory(sl, workdir);
        ok(r == S_OK, "SetWorkingDirectory failed (0x%08x)\n", r);
    }
    if (desc)
    {
        r = IShellLinkA_SetDescription(sl, desc);
        ok(r == S_OK, "SetDescription failed (0x%08x)\n", r);
    }
    if (iconPath)
    {
        r = IShellLinkA_SetIconLocation(sl, iconPath, iconId);
        ok(r == S_OK, "SetIconLocation failed (0x%08x)\n", r);
    }

    r = IShellLinkA_QueryInterface(sl, &IID_IPersistFile, (void**)&pf);
    ok(r == S_OK, "no IID_IPersistFile (0x%08x)\n", r);
    if (r == S_OK)
    {
        r = IPersistFile_Save(pf, link, TRUE);
        ok(r == S_OK, "save %s failed (0x%08x)\n", wine_dbgstr_w(link), r);

        IPersistFile_Release(pf);
    }

    IShellLinkA_Release(sl);
}

void create_url(const WCHAR *link, const char *url, WCHAR *iconPath, int iconId)
{
    HRESULT r;
    IUniformResourceLocatorA *iurl;
    IPersistFile *pf;

    r = CoCreateInstance( &CLSID_InternetShortcut, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUniformResourceLocatorA, (LPVOID *) &iurl );
    ok(SUCCEEDED(r), "COM error %x\n", r);
    if (!SUCCEEDED(r))
        return;

    r = iurl->lpVtbl->SetURL(iurl, url, 0);
    ok(SUCCEEDED(r), "SetURL %x\n", r);

    if (iconPath) {
        IPropertySetStorage *pPropSetStg;

        r = iurl->lpVtbl->QueryInterface(iurl, &IID_IPropertySetStorage, (void **) &pPropSetStg);
        ok(SUCCEEDED(r), "IPropertySetStorage %x\n", r);

        if (SUCCEEDED(r)) {
            IPropertyStorage *pPropStg;
            PROPSPEC ps[2];
            PROPVARIANT pv[2];

            ps[0].ulKind = PRSPEC_PROPID;
            ps[0].u.propid = PID_IS_ICONFILE;
            pv[0].vt = VT_LPWSTR;
            pv[0].u.pwszVal = iconPath;
            ps[1].ulKind = PRSPEC_PROPID;
            ps[1].u.propid = PID_IS_ICONINDEX;
            pv[1].vt = VT_I2;
            pv[1].u.iVal = iconId;

            r = IPropertySetStorage_Open(pPropSetStg, &FMTID_Intshcut, STGM_READWRITE | STGM_SHARE_EXCLUSIVE, &pPropStg);
            ok(SUCCEEDED(r), "IPropertySetStorage_Open %x\n", r);

            if (SUCCEEDED(r)) {
                r = IPropertyStorage_WriteMultiple(pPropStg, 2, ps, pv, PID_FIRST_USABLE);
                ok(SUCCEEDED(r), "IPropertyStorage_WriteMultiple %x\n", r);

                IPropertyStorage_Release(pPropStg);
            }
            IPropertySetStorage_Release(pPropSetStg);
        }
    }

    r = iurl->lpVtbl->QueryInterface( iurl, &IID_IPersistFile, (LPVOID*) &pf );
    ok(r == S_OK, "no IID_IPersistFile (0x%08x)\n", r);
    if (r == S_OK)
    {
        r = IPersistFile_Save(pf, link, TRUE);
        ok(r == S_OK, "save %s failed (0x%08x)\n", wine_dbgstr_w(link), r);

        IPersistFile_Release(pf);
    }

    iurl->lpVtbl->Release( iurl );
}

void wait_for_menubuilder(void)
{
    /* Wait for ONE running winemenubuilder to exit. */
    DWORD pids[64];
    DWORD pidsBytes;
    int i;
    static HINSTANCE hpsapi;
    static DWORD (WINAPI *pGetProcessImageFileNameA)(HANDLE, LPSTR, DWORD);
    
    if (!hpsapi) {
        hpsapi = GetModuleHandleA("psapi");
        pGetProcessImageFileNameA = (void *) GetProcAddress(hpsapi, "GetProcessImageFileNameA");
    }

    if (!pGetProcessImageFileNameA) {
        win_skip("GetProcessImageFileNameA is not available\n");
        return;
    }

    ok(EnumProcesses(pids, sizeof(pids), &pidsBytes), "Faild to enum processes\n");
    ok(pidsBytes < sizeof(pids), "Too many processes\n");

    for (i = 0 ; i < pidsBytes / sizeof(*pids) ; i++) {
        HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pids[i]);
        char imgPath[MAX_PATH];
        char *wmbp;

        if (!hp)
            continue;

        ok(pGetProcessImageFileNameA(hp, imgPath, sizeof(imgPath)), "Failed to get image path\n");

        if ((wmbp = strstr(imgPath, "winemenubuilder.exe")) && wmbp[sizeof("winemenubuilder.exe") - 1] == '\0') {
            DWORD es;
            DWORD dr;

            do {
                dr = WaitForSingleObject(hp, 30000);
                ok(dr == WAIT_OBJECT_0, "Failed to wait for winemenubuilder (%x, %x)\n", dr, GetLastError());
		if (dr == WAIT_TIMEOUT)
			TerminateProcess(hp, 1); /* Possible -w deadlock */
            } while (dr == WAIT_OBJECT_0 && GetExitCodeProcess(hp, &es) && es == STILL_ACTIVE);
            CloseHandle(hp);
            trace("Waited for winemenubuilder\n");
            return;
        }

        CloseHandle(hp);
    }
    trace("Didn't find winemenubuilder process\n");
}

WCHAR *get_common_desktop_directory(WCHAR *buffer, size_t buflen)
{
    DWORD len;

    buffer[0] = '\0';

    ok(SHGetSpecialFolderPathW(NULL, buffer, CSIDL_COMMON_DESKTOPDIRECTORY, TRUE), "Failed to get/create common desktop\n");

    len = lstrlenW(buffer);
    ok(len < buflen, "buffer overflow\n");

    return buffer;
}

WCHAR *get_common_start_menu_directory(WCHAR *buffer, size_t buflen)
{
    DWORD len;

    buffer[0] = '\0';

    ok(SHGetSpecialFolderPathW(NULL, buffer, CSIDL_COMMON_STARTMENU, TRUE), "Failed to get/create common start menu\n");

    len = lstrlenW(buffer);
    ok(len < buflen, "buffer overflow\n");

    return buffer;
}

char *get_private_desktop_directoryA(char *buffer, size_t buflen)
{
    DWORD len;

    buffer[0] = '\0';

    ok(SHGetSpecialFolderPathA(NULL, buffer, CSIDL_DESKTOPDIRECTORY, TRUE), "Failed to get/create private start menu\n");

    len = strlen(buffer);
    ok(len < buflen, "buffer overflow\n");

    return buffer;
}

void setup_association_keys(void)
{
    HKEY key, subkey[3];

    /* Make sure we don't have any old cache keys */
    if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\FileOpenAssociations\\.wmbtest1", &key))
        delete_key(key);
    if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\FileOpenAssociations\\.wmbtest2", &key))
        delete_key(key);

    /* Two cases, one where all assoc queries fail and one where they all succeed. */
    if (!RegOpenKeyA(HKEY_LOCAL_MACHINE, "Software\\Classes\\.wmbtest1", &key))
        delete_key(key);
    ok(!RegCreateKeyA(HKEY_LOCAL_MACHINE, "Software\\Classes\\.wmbtest1", &key), "Failed to create wmbtest1 key\n");
    RegCloseKey(key);

    if (!RegOpenKeyA(HKEY_LOCAL_MACHINE, "Software\\Classes\\.wmbtest2", &key))
        delete_key(key);
    ok(!RegCreateKeyA(HKEY_LOCAL_MACHINE, "Software\\Classes\\.wmbtest2", &key), "Failed to create wmbtest2 key\n");
    ok(!RegSetValueExA(key, NULL, 0, REG_SZ, (BYTE*)"wmbtestfile", sizeof("wmbtestfile")), "Failed to set wmbtest2 value\n");
    RegCloseKey(key);

    if (!RegOpenKeyA(HKEY_LOCAL_MACHINE, "Software\\Classes\\wmbtestfile", &key))
        delete_key(key);
    ok(!RegCreateKeyA(HKEY_LOCAL_MACHINE, "Software\\Classes\\wmbtestfile", &key), "Failed to create wmbtestfile key\n");
    ok(!RegSetValueExA(key, NULL, 0, REG_SZ, (BYTE*)"WMB test file", sizeof("WMB test file")), "Failed to set wmbtestfile description\n");
    ok(!RegSetValueExA(key, "Content-Type", 0, REG_SZ, (BYTE*)"application/x-wmb-test", sizeof("application/x-wmb-test")), "Failed to set wmbtestfile Content-Type\n");
    ok(!RegCreateKeyA(key, "DefaultIcon", &subkey[0]), "Failed to create DefaultIcon key\n");
    ok(!RegSetValueExA(subkey[0], NULL, 0, REG_SZ, (BYTE*)"url.dll,0", sizeof("url.dll,0")), "Failed to set DefaultIcon value\n");
    RegCloseKey(subkey[0]);
    ok(!RegCreateKeyA(key, "shell", &subkey[0]), "Failed to create shell key\n");
    ok(!RegCreateKeyA(subkey[0], "open", &subkey[1]), "Failed to create open key\n");
    ok(!RegCreateKeyA(subkey[1], "command", &subkey[2]), "Failed to create command key\n");
    /* Icon extraction is based on this value, assume cmd.exe has one. */
    ok(!RegSetValueExA(subkey[2], NULL, 0, REG_SZ, (BYTE*)"cmd.exe /c echo \"%1\"", sizeof("cmd.exe /c echo \"%1\"")), "Failed to set command value\n");
    RegCloseKey(subkey[2]);
    RegCloseKey(subkey[1]);
    RegCloseKey(subkey[0]);
    RegCloseKey(key);
}

void remove_association_keys(void)
{
    HKEY key;

    if (!RegOpenKeyA(HKEY_LOCAL_MACHINE, "Software\\Classes\\.wmbtest1", &key))
        delete_key(key);
    if (!RegOpenKeyA(HKEY_LOCAL_MACHINE, "Software\\Classes\\.wmbtest2", &key))
        delete_key(key);
    if (!RegOpenKeyA(HKEY_LOCAL_MACHINE, "Software\\Classes\\wmbtestfile", &key))
        delete_key(key);
}

static void setup_environment(void)
{
    HKEY key;

    putenv((char*)"XDG_CONFIG_HOME=xdg_config");
    putenv((char*)"XDG_DATA_HOME=xdg_data");

    if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\MenuBuilder", &key))
        delete_key(key);
    ok(!RegCreateKeyA(HKEY_CURRENT_USER, "Software\\Wine\\MenuBuilder", &key), "Failed to create dispatch key\n");
    ok(!RegSetValueExA(key, "Dispatch", 0, REG_SZ, (BYTE*)"xdg", sizeof("xdg")), "Failed to set dispatch value\n");
    RegCloseKey(key);
}

static void cleanup_environment(void)
{
    HKEY key;
    char buffer[MAX_PATH];

    strcpy(buffer, "xdg_config");
    ok(remove_recursive(buffer), "Failed to cleanup xdg_config\n");
    strcpy(buffer, "xdg_data");
    ok(remove_recursive(buffer), "Failed to cleanup xdg_data\n");

    /* TODO: restore old contents, if any */
    if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\MenuBuilder", &key))
        delete_key(key);
}

static void test_create_association(void)
{
    setup_association_keys();

    ok(run_winemenubuilder("-a"), "Failed to run winemenubuilder\n");

    verify_file_present("xdg_data\\mime\\packages\\x-wine-extension-wmbtest1.xml");
    verify_file_present("xdg_data\\mime\\packages\\x-wine-extension-wmbtest2.xml");
#ifndef __APPLE__ /* Icons go to /tmp/ on OS X */
    verify_file_present("xdg_data\\icons\\hicolor\\16x16\\apps\\application-x-wine-extension-wmbtest2.png");
    verify_file_present("xdg_data\\icons\\hicolor\\32x32\\apps\\application-x-wine-extension-wmbtest2.png");
    verify_file_present("xdg_data\\icons\\hicolor\\48x48\\apps\\application-x-wine-extension-wmbtest2.png");
#endif
    verify_file_present("xdg_data\\applications\\wine-extension-wmbtest2.desktop");
#ifndef __APPLE__
    verify_file_present("xdg_data\\icons\\hicolor\\16x16\\apps\\*_cmd.0.png");
    verify_file_present("xdg_data\\icons\\hicolor\\32x32\\apps\\*_cmd.0.png");
    verify_file_present("xdg_data\\icons\\hicolor\\48x48\\apps\\*_cmd.0.png");
#endif
}

static void test_remove_association(void)
{
    remove_association_keys();

    ok(run_winemenubuilder("-a"), "Failed to run winemenubuilder\n");

    verify_file_not_present("xdg_data\\applications\\wine-extension-wmbtest2.desktop");
    /* Other files are still present */
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
    strcat(pathA, "\\wmbtest.desktop");
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

    verify_file_present("xdg_config\\menus\\applications-merged\\wine-wmbtest.menu");
    verify_file_present("xdg_data\\applications\\wine\\wmbtest.desktop");
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
    strcat(pathA, "\\wmbtesturl.desktop");
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

    verify_file_present("xdg_config\\menus\\applications-merged\\wine-wmbtest.menu");
    verify_file_present("xdg_data\\applications\\wine\\wmbtesturl.desktop");
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

    get_private_desktop_directoryA(pathA, MAX_PATH);
    strcat(pathA, "\\wmbtest.desktop");
    verify_file_not_present(pathA);
    verify_file_not_present("xdg_config\\menus\\applications-merged\\wine-wmbtest.menu");
    verify_file_not_present("xdg_data\\applications\\wine\\wmbtest.desktop");
    verify_file_not_present("xdg_config\\menus\\applications-merged\\wine-wmbtesturl.menu");
    verify_file_not_present("xdg_data\\applications\\wine\\wmbtesturl.desktop");

    get_private_desktop_directoryA(pathA, MAX_PATH);
    strcat(pathA, "\\wmbtesturl.desktop");
    /* winemenubuilder does not track URLs on the desktop for some reason. */
#if 0
    verify_file_not_present(pathA);
#else
    DeleteFileA(pathA);
#endif
}

START_TEST(xdg)
{
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
}
