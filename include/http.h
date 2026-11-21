#ifndef HTTP_H
#define HTTP_H
#include <stddef.h>
#include <sys/types.h>
#define HTTP_REQUEST_MAX 8192
#define HTTP_METHOD_MAX 16
#define HTTP_TARGET_MAX 2048
#define HTTP_RESPONSE_HEADER_MAX 1024

struct http_request {
    char method[HTTP_METHOD_MAX];
    char target[HTTP_TARGET_MAX];
    int keep_alive;
    int has_body;
    int has_user_agent;
    size_t consumed;
};

enum http_parse_result {
    HTTP_PARSE_TOO_LARGE = -2,
    HTTP_PARSE_BAD_REQUEST = -1,
    HTTP_PARSE_INCOMPLETE = 0,
    HTTP_PARSE_COMPLETE = 1
};

int http_parse_request(const char *data, size_t length,struct http_request *request);
const char *http_reason_phrase(int status);
int http_build_response_header(char *buffer, size_t capacity, int status,const char *content_type, off_t content_length,int keep_alive);

#endif
