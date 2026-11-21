#include "mime.h"
#include <string.h>
#include <strings.h>

const char *mime_type_for_path(const char *path)
{
    const char *extension = strrchr(path, '.');
    if (extension == NULL)
        return "application/octet-stream";
    if (strcasecmp(extension, ".html") == 0)
        return "text/html";
    if (strcasecmp(extension, ".css") == 0)
        return "text/css";
    if (strcasecmp(extension, ".js") == 0)
        return "application/javascript";
    if (strcasecmp(extension, ".png") == 0)
        return "image/png";
    if (strcasecmp(extension, ".jpg") == 0)
        return "image/jpeg";
    return "application/octet-stream";
}
