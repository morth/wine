
WCHAR *bundle_get_default_cmdline(void)
{
    CFBundleRef bundle;
    CFDictionaryRef infodict = NULL;
    WCHAR *cmdlineW = NULL;
    CFStringRef cmdlineStr = NULL;
    int cmdlineLen;

    bundle = CFBundleGetMainBundle();
    if (!bundle)
        goto out;

    infodict = CFBundleGetInfoDictionary(bundle);
    if (!infodict)
        goto out;

    cmdlineStr = CFDictionaryGetValue(infodict, CFSTR("org.winehq.cmdline"));
    if (!cmdlineStr)
        goto out;

    cmdlineLen = CFStringLength(cmdlineStr);
    cmdlineW = HeapAlloc(GetProcessHeap(), 0, (cmdlineLen + 1) * sizeof(WCHAR));
    if (!cmdlineW)
    {
        WINE_ERR("out of memory\n");
        goto out;
    }
    if (CFStringGetBytes(cmdlineStr, , kCFStringEncodingUTF16LE, 
    if (!CFStringGetCString(cmdlineStr, , kCFStringEncodingUTF16LE))
    {
        WINE_ERR("Failed to convert Info.plist cmdline\n");
        HeapFree(GetProcessHeap(), 0, cmdlineW);
        cmdlineW = NULL;
        goto out;
    }

out:

    if (infoDict)
        CFRelease(infoDict);
    if (bundle)
        CFRelease(bundle);
    return cmdlineW;
}
