typedef void *HANDLE;
typedef struct { unsigned dwFileAttributes; char cFileName[260]; } WIN32_FIND_DATAA;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define FILE_ATTRIBUTE_DIRECTORY 16
static HANDLE FindFirstFileA(const char *p,WIN32_FIND_DATAA *f){(void)p;(void)f;return INVALID_HANDLE_VALUE;}
static int FindNextFileA(HANDLE h,WIN32_FIND_DATAA *f){(void)h;(void)f;return 0;}
static int FindClose(HANDLE h){(void)h;return 0;}
