/*
 * Implementation of userenv.dll
 *
 * Copyright 2006 Mike McCormack for CodeWeavers
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

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winternl.h"
#include "winnls.h"
#include "sddl.h"
#include "objbase.h"
#include "userenv.h"

#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL( userenv );

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    TRACE("%p %d %p\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason)
    {
    case DLL_WINE_PREATTACH:
        return FALSE;  /* prefer native version */
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        break;
    }
    return TRUE;
}

static BOOL get_reg_value(WCHAR *env, HKEY hkey, const WCHAR *name, WCHAR *val, DWORD size)
{
    DWORD type, res_size=0;

    if (RegQueryValueExW(hkey, name, 0, &type, NULL, &res_size) != ERROR_SUCCESS)
        return FALSE;

    if (type == REG_SZ)
    {
        if (res_size > size)
            return FALSE;

        return RegQueryValueExW(hkey, name, 0, NULL, (BYTE*)val, &size) == ERROR_SUCCESS;
    }
    else if (type == REG_EXPAND_SZ)
    {
        UNICODE_STRING us_buf, us_expanded;
        WCHAR *buf = HeapAlloc(GetProcessHeap(), 0, res_size);
        if (!buf)
            return FALSE;

        if (RegQueryValueExW(hkey, name, 0, NULL, (BYTE*)buf, &res_size) != ERROR_SUCCESS)
        {
            HeapFree(GetProcessHeap(), 0, buf);
            return FALSE;
        }

        RtlInitUnicodeString(&us_buf, buf);
        us_expanded.Buffer = val;
        us_expanded.MaximumLength = size;
        if (RtlExpandEnvironmentStrings_U(env, &us_buf, &us_expanded, &size) != STATUS_SUCCESS)
        {
            HeapFree(GetProcessHeap(), 0, buf);
            return FALSE;
        }

        HeapFree(GetProcessHeap(), 0, buf);
        return TRUE;
    }

    return FALSE;
}

static void set_registry_variables(WCHAR **env, HKEY hkey, DWORD type, BOOL set_path)
{
    static const WCHAR SystemRootW[] = {'S','y','s','t','e','m','R','o','o','t',0};
    static const WCHAR SystemDriveW[] = {'S','y','s','t','e','m','D','r','i','v','e',0};
    static const WCHAR PATHW[] = {'P','A','T','H'};

    UNICODE_STRING us_name, us_value;
    WCHAR name[1024], value[1024];
    DWORD ret, index, size;

    for (index = 0; ; index++)
    {
        size = sizeof(name)/sizeof(WCHAR);
        ret = RegEnumValueW(hkey, index, name, &size, NULL, NULL, NULL, NULL);
        if (ret != ERROR_SUCCESS)
            break;

        if (!memicmpW(name, SystemRootW, sizeof(SystemRootW)/sizeof(WCHAR)))
            continue;
        if (!memicmpW(name, SystemDriveW, sizeof(SystemDriveW)/sizeof(WCHAR)))
            continue;

        RtlInitUnicodeString(&us_name, name);
        us_value.Buffer = value;
        us_value.MaximumLength = sizeof(value);
        if (!memicmpW(name, PATHW, sizeof(PATHW)/sizeof(WCHAR)) &&
                !RtlQueryEnvironmentVariable_U(*env, &us_name, &us_value))
        {
            if (!set_path)
                continue;

            size = strlenW(value)+1;
            if (!get_reg_value(*env, hkey, name, value+size,
                        sizeof(value)-size*sizeof(WCHAR)))
                continue;

            value[size] = ';';
            RtlInitUnicodeString(&us_value, value);
            RtlSetEnvironmentVariable(env, &us_name, &us_value);
            continue;
        }

        if (!get_reg_value(*env, hkey, name, value, sizeof(value)))
            continue;

        if(!value[0])
            continue;

        RtlInitUnicodeString(&us_value, value);
        RtlSetEnvironmentVariable(env, &us_name, &us_value);
    }
}

static void set_wow64_environment(WCHAR **env)
{
    static const WCHAR versionW[] = {'S','o','f','t','w','a','r','e','\\',
        'M','i','c','r','o','s','o','f','t','\\',
        'W','i','n','d','o','w','s','\\',
        'C','u','r','r','e','n','t','V','e','r','s','i','o','n',0};
    static const WCHAR progdirW[]   = {'P','r','o','g','r','a','m','F','i','l','e','s','D','i','r',0};
    static const WCHAR progdir86W[] = {'P','r','o','g','r','a','m','F','i','l','e','s','D','i','r',' ','(','x','8','6',')',0};
    static const WCHAR progfilesW[] = {'P','r','o','g','r','a','m','F','i','l','e','s',0};
    static const WCHAR progw6432W[] = {'P','r','o','g','r','a','m','W','6','4','3','2',0};
    static const WCHAR commondirW[]   = {'C','o','m','m','o','n','F','i','l','e','s','D','i','r',0};
    static const WCHAR commondir86W[] = {'C','o','m','m','o','n','F','i','l','e','s','D','i','r',' ','(','x','8','6',')',0};
    static const WCHAR commonfilesW[] = {'C','o','m','m','o','n','P','r','o','g','r','a','m','F','i','l','e','s',0};
    static const WCHAR commonw6432W[] = {'C','o','m','m','o','n','P','r','o','g','r','a','m','W','6','4','3','2',0};

    UNICODE_STRING nameW, valueW;
    WCHAR buf[64];
    HKEY hkey;
    BOOL is_win64 = (sizeof(void *) > sizeof(int));
    BOOL is_wow64;

    IsWow64Process( GetCurrentProcess(), &is_wow64 );

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, versionW, 0,
                KEY_READ|KEY_WOW64_64KEY, &hkey))
        return;

    /* set the ProgramFiles variables */

    if (get_reg_value(*env, hkey, progdirW, buf, sizeof(buf)))
    {
        if (is_win64 || is_wow64)
        {
            RtlInitUnicodeString(&nameW, progw6432W);
            RtlInitUnicodeString(&valueW, buf);
            RtlSetEnvironmentVariable(env, &nameW, &valueW);
        }
        if (is_win64 || !is_wow64)
        {
            RtlInitUnicodeString(&nameW, progfilesW);
            RtlInitUnicodeString(&valueW, buf);
            RtlSetEnvironmentVariable(env, &nameW, &valueW);
        }
    }
    if (is_wow64 && get_reg_value(*env, hkey, progdir86W, buf, sizeof(buf)))
    {
        RtlInitUnicodeString(&nameW, progfilesW);
        RtlInitUnicodeString(&valueW, buf);
        RtlSetEnvironmentVariable(env, &nameW, &valueW);
    }

    /* set the CommonProgramFiles variables */

    if (get_reg_value(*env, hkey, commondirW, buf, sizeof(buf)))
    {
        if (is_win64 || is_wow64)
        {
            RtlInitUnicodeString(&nameW, commonw6432W);
            RtlInitUnicodeString(&valueW, buf);
            RtlSetEnvironmentVariable(env, &nameW, &valueW);
        }
        if (is_win64 || !is_wow64)
        {
            RtlInitUnicodeString(&nameW, commonfilesW);
            RtlInitUnicodeString(&valueW, buf);
            RtlSetEnvironmentVariable(env, &nameW, &valueW);
        }
    }
    if (is_wow64 && get_reg_value(*env, hkey, commondir86W, buf, sizeof(buf)))
    {
        RtlInitUnicodeString(&nameW, commonfilesW);
        RtlInitUnicodeString(&valueW, buf);
        RtlSetEnvironmentVariable(env, &nameW, &valueW);
    }

    RegCloseKey(hkey);
}

BOOL WINAPI CreateEnvironmentBlock( LPVOID* lpEnvironment,
                     HANDLE hToken, BOOL bInherit )
{
    static const WCHAR env_keyW[] = {'S','y','s','t','e','m','\\',
        'C','u','r','r','e','n','t','C','o','n','t','r','o','l','S','e','t','\\',
        'C','o','n','t','r','o','l','\\',
        'S','e','s','s','i','o','n',' ','M','a','n','a','g','e','r','\\',
        'E','n','v','i','r','o','n','m','e','n','t',0};
    static const WCHAR profile_keyW[] = {'S','o','f','t','w','a','r','e','\\',
        'M','i','c','r','o','s','o','f','t','\\',
        'W','i','n','d','o','w','s',' ','N','T','\\',
        'C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\',
        'P','r','o','f','i','l','e','L','i','s','t',0};
    static const WCHAR envW[] = {'E','n','v','i','r','o','n','m','e','n','t',0};
    static const WCHAR volatile_envW[] = {'V','o','l','a','t','i','l','e',' ','E','n','v','i','r','o','n','m','e','n','t',0};
    static const WCHAR ProfilesDirectoryW[] = {'P','r','o','f','i','l','e','s','D','i','r','e','c','t','o','r','y',0};

    static const WCHAR SystemRootW[] = {'S','y','s','t','e','m','R','o','o','t',0};
    static const WCHAR SystemDriveW[] = {'S','y','s','t','e','m','D','r','i','v','e',0};
    static const WCHAR AllUsersProfileW[] = {'A','l','l','U','s','e','r','s','P','r','o','f','i','l','e',0};
    static const WCHAR ALLUSERSPROFILEW[] = {'A','L','L','U','S','E','R','S','P','R','O','F','I','L','E',0};
    static const WCHAR USERNAMEW[] = {'U','S','E','R','N','A','M','E',0};
    static const WCHAR USERPROFILEW[] = {'U','S','E','R','P','R','O','F','I','L','E',0};
    static const WCHAR DefaultW[] = {'D','e','f','a','u','l','t',0};
    static const WCHAR COMPUTERNAMEW[] = {'C','O','M','P','U','T','E','R','N','A','M','E',0};

    WCHAR *env, buf[UNICODE_STRING_MAX_CHARS], profiles_dir[MAX_PATH];
    UNICODE_STRING us_name, us_val;
    DWORD len;
    HKEY hkey, hsubkey;

    TRACE("%p %p %d\n", lpEnvironment, hToken, bInherit );

    if (!lpEnvironment)
        return FALSE;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, env_keyW, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
        return FALSE;

    if (RtlCreateEnvironment(bInherit, &env) != STATUS_SUCCESS)
    {
        RegCloseKey(hkey);
        return FALSE;
    }

    if (!GetEnvironmentVariableW(SystemRootW, buf, UNICODE_STRING_MAX_CHARS))
    {
        if (!get_reg_value(env, hkey, SystemRootW, buf, UNICODE_STRING_MAX_CHARS))
        {
            buf[0] = 0;
            WARN("SystemRoot variable not set\n");
        }
    }
    RtlInitUnicodeString(&us_name, SystemRootW);
    RtlInitUnicodeString(&us_val, buf);
    RtlSetEnvironmentVariable(&env, &us_name, &us_val);

    if (!GetEnvironmentVariableW(SystemDriveW, buf, UNICODE_STRING_MAX_CHARS))
    {
        if (!get_reg_value(env, hkey, SystemRootW, buf, UNICODE_STRING_MAX_CHARS))
        {
            buf[0] = 0;
            WARN("SystemDrive variable not set\n");
        }
    }
    RtlInitUnicodeString(&us_name, SystemDriveW);
    RtlInitUnicodeString(&us_val, buf);
    RtlSetEnvironmentVariable(&env, &us_name, &us_val);

    set_registry_variables(&env, hkey, REG_SZ, !bInherit);
    set_registry_variables(&env, hkey, REG_EXPAND_SZ, !bInherit);

    if (RegOpenKeyExW(hkey, envW, 0, KEY_READ, &hsubkey) == ERROR_SUCCESS)
    {
        set_registry_variables(&env, hsubkey, REG_SZ, !bInherit);
        set_registry_variables(&env, hsubkey, REG_EXPAND_SZ, !bInherit);
        RegCloseKey(hsubkey);
    }

    if (RegOpenKeyExW(hkey, volatile_envW, 0, KEY_READ, &hsubkey) == ERROR_SUCCESS)
    {
        set_registry_variables(&env, hsubkey, REG_SZ, !bInherit);
        set_registry_variables(&env, hsubkey, REG_EXPAND_SZ, !bInherit);
        RegCloseKey(hsubkey);
    }
    RegCloseKey(hkey);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, profile_keyW, 0, KEY_READ, &hkey) == ERROR_SUCCESS)
    {
        if (get_reg_value(env, hkey, ProfilesDirectoryW, profiles_dir, MAX_PATH-sizeof(WCHAR)))
        {
            len = strlenW(profiles_dir);
            if (profiles_dir[len-1] != '\\')
            {
                profiles_dir[len++] = '\\';
                profiles_dir[len] = '\0';
            }

            memcpy(buf, profiles_dir, len*sizeof(WCHAR));
            if (get_reg_value(env, hkey, AllUsersProfileW, buf+len, UNICODE_STRING_MAX_CHARS-len))
            {
                RtlInitUnicodeString(&us_name, ALLUSERSPROFILEW);
                RtlInitUnicodeString(&us_val, buf);
                RtlSetEnvironmentVariable(&env, &us_name, &us_val);
            }
        }
        else
        {
            profiles_dir[0] = 0;
        }

        RegCloseKey(hkey);
    }

    len = sizeof(buf)/sizeof(WCHAR);
    if (GetComputerNameW(buf, &len))
    {
        RtlInitUnicodeString(&us_name, COMPUTERNAMEW);
        RtlInitUnicodeString(&us_val, buf);
        RtlSetEnvironmentVariable(&env, &us_name, &us_val);
    }

    set_wow64_environment(&env);

    if (!hToken)
    {
        if (profiles_dir[0])
        {
            len = strlenW(profiles_dir);
            if (len*sizeof(WCHAR)+sizeof(DefaultW) < sizeof(buf))
            {
                memcpy(buf, profiles_dir, len*sizeof(WCHAR));
                memcpy(buf+len, DefaultW, sizeof(DefaultW));
                RtlInitUnicodeString(&us_name, USERPROFILEW);
                RtlInitUnicodeString(&us_val, buf);
                RtlSetEnvironmentVariable(&env, &us_name, &us_val);
            }
        }

        buf[0] = '.';
        memcpy(buf+1, DefaultW, sizeof(DefaultW));
    }
    else
    {
        TOKEN_USER *token_user = NULL;
        SID_NAME_USE use;
        WCHAR *sidW;
        DWORD size, tmp=0;

        if (GetTokenInformation(hToken, TokenUser, NULL, 0, &len) ||
                GetLastError()!=ERROR_INSUFFICIENT_BUFFER ||
                !(token_user = HeapAlloc(GetProcessHeap(), 0, len)) ||
                !GetTokenInformation(hToken, TokenUser, token_user, len, &len) ||
                !ConvertSidToStringSidW(token_user->User.Sid, &sidW))
        {
            HeapFree(GetProcessHeap(), 0, token_user);
            RtlDestroyEnvironment(env);
            return FALSE;
        }

        len = strlenW(profiles_dir);
        memcpy(buf, profiles_dir, len*sizeof(WCHAR));

        size = UNICODE_STRING_MAX_CHARS-len;
        if (LookupAccountSidW(NULL, token_user->User.Sid,
                    buf+len, &size, NULL, &tmp, &use))
        {
            RtlInitUnicodeString(&us_name, USERNAMEW);
            RtlInitUnicodeString(&us_val, buf+len);
            RtlSetEnvironmentVariable(&env, &us_name, &us_val);

            if (len)
            {
                RtlInitUnicodeString(&us_name, USERPROFILEW);
                RtlInitUnicodeString(&us_val, buf);
                RtlSetEnvironmentVariable(&env, &us_name, &us_val);
            }
        }

        HeapFree(GetProcessHeap(), 0, token_user);
        strcpyW(buf, sidW);
        LocalFree(sidW);
    }

    if (RegOpenKeyExW(HKEY_USERS, buf, 0, KEY_READ, &hkey) == ERROR_SUCCESS)
    {
        if (RegOpenKeyExW(hkey, envW, 0, KEY_READ, &hsubkey) == ERROR_SUCCESS)
        {
            set_registry_variables(&env, hsubkey, REG_SZ, !bInherit);
            set_registry_variables(&env, hsubkey, REG_EXPAND_SZ, !bInherit);
            RegCloseKey(hsubkey);
        }

        if (RegOpenKeyExW(hkey, volatile_envW, 0, KEY_READ, &hsubkey) == ERROR_SUCCESS)
        {
            set_registry_variables(&env, hsubkey, REG_SZ, !bInherit);
            set_registry_variables(&env, hsubkey, REG_EXPAND_SZ, !bInherit);
            RegCloseKey(hsubkey);
        }
        RegCloseKey(hkey);
    }

    *lpEnvironment = env;
    return TRUE;
}

BOOL WINAPI DestroyEnvironmentBlock(LPVOID lpEnvironment)
{
    NTSTATUS r;

    TRACE("%p\n", lpEnvironment);
    r = RtlDestroyEnvironment(lpEnvironment);
    if (r == STATUS_SUCCESS)
        return TRUE;
    return FALSE;
}

BOOL WINAPI ExpandEnvironmentStringsForUserA( HANDLE hToken, LPCSTR lpSrc,
                     LPSTR lpDest, DWORD dwSize )
{
    BOOL ret;

    TRACE("%p %s %p %d\n", hToken, debugstr_a(lpSrc), lpDest, dwSize);

    ret = ExpandEnvironmentStringsA( lpSrc, lpDest, dwSize );
    TRACE("<- %s\n", debugstr_a(lpDest));
    return ret;
}

BOOL WINAPI ExpandEnvironmentStringsForUserW( HANDLE hToken, LPCWSTR lpSrc,
                     LPWSTR lpDest, DWORD dwSize )
{
    BOOL ret;

    TRACE("%p %s %p %d\n", hToken, debugstr_w(lpSrc), lpDest, dwSize);

    ret = ExpandEnvironmentStringsW( lpSrc, lpDest, dwSize );
    TRACE("<- %s\n", debugstr_w(lpDest));
    return ret;
}

BOOL WINAPI GetDefaultUserProfileDirectoryA( LPSTR lpProfileDir, LPDWORD lpcchSize )
{
    FIXME("%p %p\n", lpProfileDir, lpcchSize );
    return FALSE;
}

BOOL WINAPI GetDefaultUserProfileDirectoryW( LPWSTR lpProfileDir, LPDWORD lpcchSize )
{
    FIXME("%p %p\n", lpProfileDir, lpcchSize );
    return FALSE;
}

BOOL WINAPI GetUserProfileDirectoryA( HANDLE hToken, LPSTR lpProfileDir,
                     LPDWORD lpcchSize )
{
    BOOL ret;
    WCHAR *dirW = NULL;

    TRACE( "%p %p %p\n", hToken, lpProfileDir, lpcchSize );

    if (!lpProfileDir || !lpcchSize)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    if (!(dirW = HeapAlloc( GetProcessHeap(), 0, *lpcchSize * sizeof(WCHAR) )))
        return FALSE;

    if ((ret = GetUserProfileDirectoryW( hToken, dirW, lpcchSize )))
        WideCharToMultiByte( CP_ACP, 0, dirW, *lpcchSize, lpProfileDir, *lpcchSize, NULL, NULL );

    HeapFree( GetProcessHeap(), 0, dirW );
    return ret;
}

BOOL WINAPI GetUserProfileDirectoryW( HANDLE hToken, LPWSTR lpProfileDir,
                     LPDWORD lpcchSize )
{
    static const WCHAR slashW[] = {'\\',0};
    TOKEN_USER *t;
    WCHAR *userW = NULL, *dirW = NULL;
    DWORD len, dir_len, domain_len;
    SID_NAME_USE use;
    BOOL ret = FALSE;

    TRACE( "%p %p %p\n", hToken, lpProfileDir, lpcchSize );

    if (!lpcchSize)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }

    len = 0;
    GetTokenInformation( hToken, TokenUser, NULL, 0, &len );
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return FALSE;
    if (!(t = HeapAlloc( GetProcessHeap(), 0, len ))) return FALSE;
    if (!GetTokenInformation( hToken, TokenUser, t, len, &len )) goto done;

    len = domain_len = 0;
    LookupAccountSidW( NULL, t->User.Sid, NULL, &len, NULL, &domain_len, NULL );
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) goto done;
    if (!(userW = HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) ))) goto done;
    if (!LookupAccountSidW( NULL, t->User.Sid, userW, &len, NULL, &domain_len, &use )) goto done;

    dir_len = 0;
    GetProfilesDirectoryW( NULL, &dir_len );
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) goto done;
    if (!(dirW = HeapAlloc( GetProcessHeap(), 0, (dir_len + 1) * sizeof(WCHAR) ))) goto done;
    if (!GetProfilesDirectoryW( dirW, &dir_len )) goto done;

    len += dir_len + 2;
    if (*lpcchSize < len)
    {
        SetLastError( ERROR_INSUFFICIENT_BUFFER );
        *lpcchSize = len;
        goto done;
    }
    strcpyW( lpProfileDir, dirW );
    strcatW( lpProfileDir, slashW );
    strcatW( lpProfileDir, userW );
    *lpcchSize = len;
    ret = TRUE;

done:
    HeapFree( GetProcessHeap(), 0, t );
    HeapFree( GetProcessHeap(), 0, userW );
    HeapFree( GetProcessHeap(), 0, dirW );
    return ret;
}

static const char ProfileListA[] = "Software\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList";

BOOL WINAPI GetProfilesDirectoryA( LPSTR lpProfilesDir, LPDWORD lpcchSize )
{
    static const char ProfilesDirectory[] = "ProfilesDirectory";
    LONG l;
    HKEY key;
    BOOL ret = FALSE;
    DWORD len = 0, expanded_len;
    LPSTR unexpanded_profiles_dir = NULL;

    TRACE("%p %p\n", lpProfilesDir, lpcchSize );

    if (!lpProfilesDir || !lpcchSize)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    l = RegOpenKeyExA(HKEY_LOCAL_MACHINE, ProfileListA, 0, KEY_READ, &key);
    if (l)
    {
        SetLastError(l);
        return FALSE;
    }
    l = RegQueryValueExA(key, ProfilesDirectory, NULL, NULL, NULL, &len);
    if (l)
    {
        SetLastError(l);
        goto end;
    }
    unexpanded_profiles_dir = HeapAlloc(GetProcessHeap(), 0, len);
    if (!unexpanded_profiles_dir)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto end;
    }
    l = RegQueryValueExA(key, ProfilesDirectory, NULL, NULL,
                             (BYTE *)unexpanded_profiles_dir, &len);
    if (l)
    {
        SetLastError(l);
        goto end;
    }
    expanded_len = ExpandEnvironmentStringsA(unexpanded_profiles_dir, NULL, 0);
    /* The returned length doesn't include the NULL terminator. */
    if (*lpcchSize < expanded_len - 1)
    {
        *lpcchSize = expanded_len - 1;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto end;
    }
    *lpcchSize = expanded_len - 1;
    /* The return value is also the expected length. */
    ret = ExpandEnvironmentStringsA(unexpanded_profiles_dir, lpProfilesDir,
                                    expanded_len) - 1;
end:
    HeapFree(GetProcessHeap(), 0, unexpanded_profiles_dir);
    RegCloseKey(key);
    return ret;
}

static const WCHAR ProfileListW[] = {'S','o','f','t','w','a','r','e','\\','M','i','c','r','o','s','o','f','t','\\','W','i','n','d','o','w','s',' ','N','T','\\','C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\','P','r','o','f','i','l','e','L','i','s','t',0};

BOOL WINAPI GetProfilesDirectoryW( LPWSTR lpProfilesDir, LPDWORD lpcchSize )
{
    static const WCHAR ProfilesDirectory[] = {'P','r','o','f','i','l','e','s','D','i','r','e','c','t','o','r','y',0};
    LONG l;
    HKEY key;
    BOOL ret = FALSE;
    DWORD len = 0, expanded_len;
    LPWSTR unexpanded_profiles_dir = NULL;

    TRACE("%p %p\n", lpProfilesDir, lpcchSize );

    if (!lpcchSize)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    l = RegOpenKeyExW(HKEY_LOCAL_MACHINE, ProfileListW, 0, KEY_READ, &key);
    if (l)
    {
        SetLastError(l);
        return FALSE;
    }
    l = RegQueryValueExW(key, ProfilesDirectory, NULL, NULL, NULL, &len);
    if (l)
    {
        SetLastError(l);
        goto end;
    }
    unexpanded_profiles_dir = HeapAlloc(GetProcessHeap(), 0, len);
    if (!unexpanded_profiles_dir)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        goto end;
    }
    l = RegQueryValueExW(key, ProfilesDirectory, NULL, NULL,
                             (BYTE *)unexpanded_profiles_dir, &len);
    if (l)
    {
        SetLastError(l);
        goto end;
    }
    expanded_len = ExpandEnvironmentStringsW(unexpanded_profiles_dir, NULL, 0);
    /* The returned length doesn't include the NULL terminator. */
    if (*lpcchSize < expanded_len - 1 || !lpProfilesDir)
    {
        *lpcchSize = expanded_len - 1;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto end;
    }
    *lpcchSize = expanded_len - 1;
    /* The return value is also the expected length. */
    ret = ExpandEnvironmentStringsW(unexpanded_profiles_dir, lpProfilesDir,
                                    expanded_len) - 1;
end:
    HeapFree(GetProcessHeap(), 0, unexpanded_profiles_dir);
    RegCloseKey(key);
    return ret;
}

BOOL WINAPI GetAllUsersProfileDirectoryA( LPSTR lpProfileDir, LPDWORD lpcchSize )
{
    FIXME("%p %p\n", lpProfileDir, lpcchSize);
    return FALSE;
}

BOOL WINAPI GetAllUsersProfileDirectoryW( LPWSTR lpProfileDir, LPDWORD lpcchSize )
{
    FIXME("%p %p\n", lpProfileDir, lpcchSize);
    return FALSE;
}

BOOL WINAPI GetProfileType( DWORD *pdwFlags )
{
    FIXME("%p\n", pdwFlags );
    *pdwFlags = 0;
    return TRUE;
}

BOOL WINAPI LoadUserProfileA( HANDLE hToken, LPPROFILEINFOA lpProfileInfo )
{
    FIXME("%p %p\n", hToken, lpProfileInfo );
    lpProfileInfo->hProfile = HKEY_CURRENT_USER;
    return TRUE;
}

BOOL WINAPI LoadUserProfileW( HANDLE hToken, LPPROFILEINFOW lpProfileInfo )
{
    FIXME("%p %p\n", hToken, lpProfileInfo );
    lpProfileInfo->hProfile = HKEY_CURRENT_USER;
    return TRUE;
}

BOOL WINAPI RegisterGPNotification( HANDLE event, BOOL machine )
{
    FIXME("%p %d\n", event, machine );
    return TRUE;
}

BOOL WINAPI UnregisterGPNotification( HANDLE event )
{
    FIXME("%p\n", event );
    return TRUE;
}

BOOL WINAPI UnloadUserProfile( HANDLE hToken, HANDLE hProfile )
{
    FIXME("(%p, %p): stub\n", hToken, hProfile);
    return FALSE;
}

/******************************************************************************
 *              USERENV.138
 *
 * Create .lnk file
 *
 * PARAMETERS
 *   int     csidl               [in] well-known directory location to create link in
 *   LPCSTR lnk_dir              [in] directory (relative to directory specified by csidl) to create link in
 *   LPCSTR lnk_filename         [in] filename of the link file without .lnk extension
 *   LPCSTR lnk_target           [in] file/directory pointed to by link
 *   LPCSTR lnk_iconfile         [in] link icon resource filename
 *   DWORD   lnk_iconid          [in] link icon resource id in file referred by lnk_iconfile
 *   LPCSTR work_directory       [in] link target's work directory
 *   WORD    hotkey              [in] link hotkey (virtual key id)
 *   DWORD   win_state           [in] initial window size (SW_SHOWMAXIMIZED to start maximized,
 *                                     SW_SHOWMINNOACTIVE to start minimized, everything else is default state)
 *   LPCSTR comment              [in] comment - link's comment
 *   LPCSTR loc_filename_resfile [in] resource file which holds localized filename for this link file
 *   DWORD   loc_filename_resid  [in] resource id for this link file's localized filename
 *
 * RETURNS
 *    TRUE:  Link file was successfully created
 *    FALSE: Link file was not created
 */
BOOL WINAPI USERENV_138( int csidl, LPCSTR lnk_dir, LPCSTR lnk_filename,
            LPCSTR lnk_target, LPCSTR lnk_iconfile, DWORD lnk_iconid,
            LPCSTR work_directory, WORD hotkey, DWORD win_state, LPCSTR comment,
            LPCSTR loc_filename_resfile, DWORD loc_filename_resid)
{
    FIXME("(%d,%s,%s,%s,%s,%d,%s,0x%x,%d,%s,%s,%d) - stub\n", csidl, debugstr_a(lnk_dir),
            debugstr_a(lnk_filename), debugstr_a(lnk_target), debugstr_a(lnk_iconfile),
            lnk_iconid, debugstr_a(work_directory), hotkey, win_state,
            debugstr_a(comment), debugstr_a(loc_filename_resfile), loc_filename_resid );

    return FALSE;
}
