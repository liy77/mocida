#include <uikit/extra.h>

int MOCIDA_IsValidURL(const char* url) {
    if (url == NULL) return 0;
    
    // Check for common URL schemes
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0 || strncmp(url, "file://", 7) == 0) {
        return 1;
    }
    
    return 0;
}