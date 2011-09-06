
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

char* heap_printf(const char *format, ...);
WCHAR* assoc_query(ASSOCSTR assocStr, LPCWSTR name, LPCWSTR extra);
BOOL create_directories(char *directory);
DWORD register_menus_entry(const char *unix_file, const char *windows_file);
HRESULT read_ico_direntries(IStream *icoStream, ICONDIRENTRY **ppIconDirEntries, int *numEntries);

char* wchars_to_utf8_chars(LPCWSTR string);

HRESULT convert_to_native_icon(IStream *icoFile, int *indeces, int numIndeces,
                                      const CLSID *outputFormat, const char *outputFileName, LPCWSTR commentW);

HRESULT platform_write_icon(IStream *icoStream, int exeIndex, LPCWSTR icoPathW,
                                   const char *destFilename, char **nativeIdentifier);
void extract_icon(LPCWSTR icoPathW, int index, const char *destFilename, BOOL bWait, char **nativeIdentifier);

LPSTR escape(LPCWSTR arg);
WCHAR* utf8_chars_to_wchars(LPCSTR string);
BOOL remove_unix_link(const char *unix_link);

int platform_build_desktop_link(const char *unix_link, const char *link, const char *link_name, const char *path,
                                const char *args, const char *descr, const char *workdir, const char *icon);
int platform_build_menu_link(const char *unix_link, const char *link, const char *link_name, const char *path,
                             const char *args, const char *descr, const char *workdir, const char *icon);

void platform_refresh_file_type_associations(void);

BOOL platform_init(void);
