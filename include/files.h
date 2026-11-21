#ifndef FILES_H
#define FILES_H
#include <stddef.h>
#include <sys/types.h>

int files_open(const char *document_root, const char *uri, off_t *size,int *http_status, char *resolved_path,size_t resolved_path_capacity);

#endif
