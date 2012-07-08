
DWORD delete_key( HKEY hkey );
BOOL remove_recursive(char *path);
BOOL winemenubuilder_available(void);
BOOL run_winemenubuilder(const char *args);
void wait_for_boot(void);
void verify_file_present(const char *path);
void verify_file_not_present(const char *path);
void create_link(const WCHAR *link, const char *cmd, const char *args, const char *workdir,
        const char *desc, const char *iconPath, int iconId);
void create_url(const WCHAR *link, const char *url, WCHAR *iconPath, int iconId);
void wait_for_menubuilder(void);
WCHAR *get_common_desktop_directory(WCHAR *buffer, size_t buflen);
WCHAR *get_common_start_menu_directory(WCHAR *buffer, size_t buflen);
char *get_private_desktop_directoryA(char *buffer, size_t buflen);
void setup_association_keys(void);
void remove_association_keys(void);
