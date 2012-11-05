
#include "winehostproxy.h"

#include <wine/debug.h>
#include <wine/unicode.h>

#include <stdlib.h>

WINE_DEFAULT_DEBUG_CHANNEL(winehostproxy);

int setup_wineprefix(void)
{
    char *prefix = NULL;

    if (getenv("WINEPREFIX"))
        return 0;

    prefix = NULL;//bundle_get_wineprefix();
    if (prefix)
    {
        setenv("WINEPREFIX", prefix, 1);
        HeapFree(GetProcessHeap(), 0, prefix);
    }

    return 0;
}

int launch_unix_file(const WCHAR *file, const WCHAR *progid)
{
    WINE_ERR("file = %s\n", wine_dbgstr_w(file));
    return 0;
}

int launch_url(const WCHAR *url)
{
    WINE_ERR("url = %s\n", wine_dbgstr_w(url));
    return 0;
}

int default_launch(void)
{
    int r;
   
    r = -1;//osx_desktop_run();
    if (r == 0)
        return 0;

    WINE_ERR("No action found.\n");
    return 1;
}

int osx_desktop_launch(void)
{
    int r;
    int nargs;
    WCHAR **args;
    int isurl;

    nargs = get_appleevent_launch_args(&args, &isurl);
    if (nargs < 0)
        return 1;

    if (nargs > 0)
    {
        WCHAR *progid = NULL;
        int lr;
        int i;

        if (!isurl)
            progid = NULL;//bundle_get_progid();
        for (i = 0 ; i < nargs ; i++)
        {
            if (isurl)
                lr = launch_url(args[i]);
            else
                lr = launch_unix_file(args[i], progid);
            HeapFree(GetProcessHeap(), 0, args[i]);
            if (lr)
            {
                WINE_ERR("Failed to launch %s: %d\n", wine_dbgstr_w(args[i]), lr);
                r = 1;
            }
        }
        HeapFree(GetProcessHeap(), 0, args);
        HeapFree(GetProcessHeap(), 0, progid);
        return r;
    }

    return default_launch();
}

int wmain(int argc, WCHAR *argv[])
{
    static const WCHAR dash_psnW[] = {'-','p','s','n'};

    if (argc >= 2)
    {
        if (strncmpW(argv[1], dash_psnW, sizeof(dash_psnW) / sizeof(dash_psnW[0])) == 0)
            return osx_desktop_launch();
    }

    return default_launch();
}
