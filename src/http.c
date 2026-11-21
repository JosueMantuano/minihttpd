#include "http.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

size_t find_crlf(const char *data, size_t start, size_t end)
{
    size_t i;
    for (i = start; i + 1 < end; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n')
            return i;
    }
    return end;
}

size_t find_header_end(const char *data, size_t length)
{
    size_t i;
    for (i = 0; i + 3 < length; i++) {
        if (memcmp(data + i, "\r\n\r\n", 4) == 0)
            return i;
    }
    return length;
}

int is_token_char(unsigned char c)
{
    if (c == '\0')
        return 0;
    if (isalnum(c))
        return 1;
    return strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

int is_token(const char *value, size_t length)
{
    size_t i;
    if (length == 0)
        return 0;
    for (i = 0; i < length; i++) {
        if (!is_token_char((unsigned char)value[i]))
            return 0;
    }
    return 1;
}

int value_has_token(const char *value, size_t length, const char *wanted)
{
    size_t start = 0;
    size_t i;
    size_t wanted_length = strlen(wanted);
    while (start < length) {
        size_t end;
        while (start < length &&
               (value[start] == ' ' || value[start] == '\t' ||
                value[start] == ','))
            start++;
        end = start;
        while (end < length && value[end] != ',')
            end++;
        while (end > start &&
               (value[end - 1] == ' ' || value[end - 1] == '\t'))
            end--;
        if (end - start == wanted_length &&
            strncasecmp(value + start, wanted, wanted_length) == 0)
            return 1;
        i = end;
        while (i < length && value[i] != ',')
            i++;
        start = i + (i < length);
    }
    return 0;
}

int http_parse_request(const char *data, size_t length,struct http_request *request)
{
    size_t header_end;
    size_t line_end;
    size_t offset;
    size_t method_end;
    size_t target_end;
    int http_minor;
    int host_seen = 0;
    int connection_close = 0;
    int connection_keep_alive = 0;
    int content_length_seen = 0;
    unsigned int header_count = 0;
    memset(request, 0, sizeof(*request));
    if (length > HTTP_REQUEST_MAX)
        return HTTP_PARSE_TOO_LARGE;
    header_end = find_header_end(data, length);
    if (header_end == length)
        return length == HTTP_REQUEST_MAX ?
            HTTP_PARSE_TOO_LARGE : HTTP_PARSE_INCOMPLETE;
    line_end = find_crlf(data, 0, header_end);
    if (line_end == header_end)
        return HTTP_PARSE_BAD_REQUEST;
    method_end = 0;
    while (method_end < line_end && data[method_end] != ' ')
        method_end++;
    if (method_end == line_end || !is_token(data, method_end) ||
        method_end >= sizeof(request->method))
        return HTTP_PARSE_BAD_REQUEST;
    target_end = method_end + 1;
    while (target_end < line_end && data[target_end] != ' ')
        target_end++;
    if (target_end == method_end + 1 || target_end == line_end ||
        target_end - method_end - 1 >= sizeof(request->target))
        return HTTP_PARSE_BAD_REQUEST;
    if (data[method_end + 1] != '/' ||
        memchr(data + method_end + 1, '#',
               target_end - method_end - 1) != NULL)
        return HTTP_PARSE_BAD_REQUEST;
    for (offset = method_end + 1; offset < target_end; offset++) {
        unsigned char c = (unsigned char)data[offset];
        if (c <= 0x20 || c == 0x7f)
            return HTTP_PARSE_BAD_REQUEST;
    }
    if (line_end - target_end - 1 == 8 &&
        memcmp(data + target_end + 1, "HTTP/1.1", 8) == 0)
        http_minor = 1;
    else if (line_end - target_end - 1 == 8 &&
             memcmp(data + target_end + 1, "HTTP/1.0", 8) == 0)
        http_minor = 0;
    else
        return HTTP_PARSE_BAD_REQUEST;
    memcpy(request->method, data, method_end);
    request->method[method_end] = '\0';
    memcpy(request->target, data + method_end + 1,
           target_end - method_end - 1);
    request->target[target_end - method_end - 1] = '\0';
    offset = line_end + 2;
    while (offset < header_end) {
        size_t current_end = find_crlf(data, offset, header_end + 2);
        size_t colon;
        size_t value_start;
        size_t value_end;
        size_t i;
        if (current_end > header_end)
            return HTTP_PARSE_BAD_REQUEST;
        if (++header_count > 100)
            return HTTP_PARSE_TOO_LARGE;
        if (data[offset] == ' ' || data[offset] == '\t')
            return HTTP_PARSE_BAD_REQUEST;
        colon = offset;
        while (colon < current_end && data[colon] != ':')
            colon++;
        if (colon == current_end ||
            !is_token(data + offset, colon - offset))
            return HTTP_PARSE_BAD_REQUEST;
        value_start = colon + 1;
        while (value_start < current_end &&
               (data[value_start] == ' ' || data[value_start] == '\t'))
            value_start++;
        value_end = current_end;
        while (value_end > value_start &&
               (data[value_end - 1] == ' ' ||
                data[value_end - 1] == '\t'))
            value_end--;
        for (i = value_start; i < value_end; i++) {
            unsigned char c = (unsigned char)data[i];
            if ((c < 0x20 && c != '\t') || c == 0x7f)
                return HTTP_PARSE_BAD_REQUEST;
        }
        if (colon - offset == 4 &&
            strncasecmp(data + offset, "Host", 4) == 0) {
            if (host_seen || value_start == value_end)
                return HTTP_PARSE_BAD_REQUEST;
            host_seen = 1;
        } else if (colon - offset == 10 &&
                   strncasecmp(data + offset, "Connection", 10) == 0) {
            connection_close |= value_has_token(data + value_start,value_end - value_start,"close");
            connection_keep_alive |=
                value_has_token(data + value_start,value_end - value_start, "keep-alive");
        } else if (colon - offset == 10 && strncasecmp(data + offset, "User-Agent", 10) == 0) {
            request->has_user_agent = 1;
        } else if (colon - offset == 14 &&
                   strncasecmp(data + offset, "Content-Length", 14) == 0) {
            if (content_length_seen || value_start == value_end)
                return HTTP_PARSE_BAD_REQUEST;
            content_length_seen = 1;
            for (i = value_start; i < value_end; i++) {
                if (data[i] < '0' || data[i] > '9')
                    return HTTP_PARSE_BAD_REQUEST;
                if (data[i] != '0')
                    request->has_body = 1;
            }
        } else if (colon - offset == 17 && strncasecmp(data + offset, "Transfer-Encoding", 17) == 0) {
            if (value_start == value_end)
                return HTTP_PARSE_BAD_REQUEST;
            request->has_body = 1;
        }
        offset = current_end + 2;
    }

    if (http_minor == 1 && !host_seen)
        return HTTP_PARSE_BAD_REQUEST;
    request->keep_alive = http_minor == 1;
    if (connection_close)
        request->keep_alive = 0;
    else if (connection_keep_alive)
        request->keep_alive = 1;
    request->consumed = header_end + 4;
    return HTTP_PARSE_COMPLETE;
}

const char *http_reason_phrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    default: return "Internal Server Error";
    }
}

int http_build_response_header(char *buffer, size_t capacity, int status,const char *content_type, off_t content_length,int keep_alive)
{
    int length;
    if (status == 405) {
        length = snprintf(buffer, capacity,
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %lld\r\n"
                          "Allow: GET\r\n"
                          "Connection: %s\r\n\r\n",
                          status, http_reason_phrase(status), content_type,
                          (long long)content_length,
                          keep_alive ? "keep-alive" : "close");
    } else {
        length = snprintf(buffer, capacity,
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %lld\r\n"
                          "Connection: %s\r\n\r\n",
                          status, http_reason_phrase(status), content_type,
                          (long long)content_length,
                          keep_alive ? "keep-alive" : "close");
    }

    if (length < 0 || (size_t)length >= capacity)
        return -1;
    return length;
}
