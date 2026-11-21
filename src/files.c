#include "files.h"
#include "http.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int decode_uri_path(const char *uri, char *path, size_t capacity)
{
    size_t source = 0;
    size_t target = 0;
    if (uri[0] != '/')
        return -1;
    while (uri[source] != '\0' && uri[source] != '?') {
        unsigned char c = (unsigned char)uri[source++];
        if (c == '#')
            return -1;
        if (c == '%') {
            int high;
            int low;
            if (uri[source] == '\0' || uri[source + 1] == '\0')
                return -1;
            high = hex_value(uri[source]);
            low = hex_value(uri[source + 1]);
            if (high < 0 || low < 0)
                return -1;
            c = (unsigned char)((high << 4) | low);
            source += 2;
        }
        if (c == '\0' || c == '\\' || c < 0x20 || c == 0x7f)
            return -1;
        if (target + 1 >= capacity)
            return -1;
        path[target++] = (char)c;
    }
    path[target] = '\0';
    return 0;
}

int is_within_root(const char *root, const char *path)
{
    size_t root_length = strlen(root);
    if (strncmp(root, path, root_length) != 0)
        return 0;
    if (root_length == 1 && root[0] == '/')
        return 1;
    return path[root_length] == '\0' || path[root_length] == '/';
}

int status_for_errno(int error)
{
    if (error == ENOENT || error == ENOTDIR)
        return 404;
    if (error == EACCES || error == ELOOP)
        return 403;
    if (error == ENAMETOOLONG)
        return 400;
    return 500;
}

int files_open(const char *document_root, const char *uri, off_t *size,int *http_status, char *resolved_path,size_t resolved_path_capacity)
{
    char canonical_root[PATH_MAX];
    char candidate[PATH_MAX];
    char decoded_path[HTTP_TARGET_MAX];
    char *canonical_file;
    size_t path_length;
    struct stat file_info;
    int fd;
    *http_status = 500;
    if (realpath(document_root, canonical_root) == NULL) {
        *http_status = status_for_errno(errno);
        return -1;
    }
    if (decode_uri_path(uri, decoded_path, sizeof(decoded_path)) < 0) {
        *http_status = 400;
        return -1;
    }
    path_length = strlen(decoded_path);
    if (path_length == 1 || decoded_path[path_length - 1] == '/') {
        int written;
        if (strcmp(canonical_root, "/") == 0)
            written = snprintf(candidate, sizeof(candidate), "/%s",
                               decoded_path + 1);
        else
            written = snprintf(candidate, sizeof(candidate), "%s%s",
                               canonical_root, decoded_path);
        if (written < 0 || (size_t)written >= sizeof(candidate)) {
            *http_status = 400;
            return -1;
        }
        if (candidate[written - 1] != '/') {
            if ((size_t)written + sizeof("/") > sizeof(candidate)) {
                *http_status = 400;
                return -1;
            }
            candidate[written++] = '/';
            candidate[written] = '\0';
        }
        if ((size_t)written + sizeof("index.html") > sizeof(candidate)) {
            *http_status = 400;
            return -1;
        }
        memcpy(candidate + written, "index.html", sizeof("index.html"));
    } else {
        int written;

        if (strcmp(canonical_root, "/") == 0)
            written = snprintf(candidate, sizeof(candidate), "%s",
                               decoded_path);
        else
            written = snprintf(candidate, sizeof(candidate), "%s%s",
                               canonical_root, decoded_path);
        if (written < 0 || (size_t)written >= sizeof(candidate)) {
            *http_status = 400;
            return -1;
        }
    }

    canonical_file = realpath(candidate, NULL);
    if (canonical_file == NULL) {
        *http_status = status_for_errno(errno);
        return -1;
    }
    if (!is_within_root(canonical_root, canonical_file)) {
        free(canonical_file);
        *http_status = 403;
        return -1;
    }
    path_length = strlen(canonical_file);
    if (path_length >= resolved_path_capacity) {
        free(canonical_file);
        *http_status = 500;
        return -1;
    }
    memcpy(resolved_path, canonical_file, path_length + 1);
    free(canonical_file);

    fd = open(resolved_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        *http_status = status_for_errno(errno);
        return -1;
    }
    if (fstat(fd, &file_info) < 0) {
        *http_status = status_for_errno(errno);
        close(fd);
        return -1;
    }
    if (!S_ISREG(file_info.st_mode)) {
        *http_status = 403;
        close(fd);
        return -1;
    }

    *size = file_info.st_size;
    *http_status = 200;
    return fd;
}
