#include "files.h"
#include "http.h"
#include "mime.h"
#include "server.h"
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>
#define MAX_EVENTS 32
#define ERROR_BODY_MAX 128

struct client {
    int fd;
    int file_fd;
    int peer_eof;
    int response_pending;
    int close_after_response;
    char input[HTTP_REQUEST_MAX];
    size_t input_length;
    char response_header[HTTP_RESPONSE_HEADER_MAX];
    size_t response_header_length;
    size_t response_header_sent;
    char error_body[ERROR_BODY_MAX];
    size_t error_body_length;
    size_t error_body_sent;
    off_t file_size;
    off_t file_offset;
};

void close_client(int epoll_fd, struct client *client)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL) < 0 &&
        errno != ENOENT && errno != EBADF)
        perror("epoll_ctl DEL");
    if (client->file_fd >= 0 && close(client->file_fd) < 0)
        perror("close file");
    if (close(client->fd) < 0)
        perror("close client");
    free(client);
}

int set_client_events(int epoll_fd, struct client *client, uint32_t events)
{
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events | EPOLLRDHUP;
    event.data.ptr = client;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &event) < 0) {
        perror("epoll_ctl MOD client");
        return -1;
    }
    return 0;
}

int start_response(struct client *client, int status, int keep_alive,
                   const char *content_type, off_t content_length)
{
    int header_length;
    if (status != 200) {
        int body_length = snprintf(client->error_body,sizeof(client->error_body), "%s\n",http_reason_phrase(status));
        if (body_length < 0 || (size_t)body_length >= sizeof(client->error_body))
            return -1;
        client->error_body_length = (size_t)body_length;
        client->error_body_sent = 0;
        content_type = "text/plain; charset=utf-8";
        content_length = body_length;
    } else {
        client->error_body_length = 0;
        client->error_body_sent = 0;
    }

    header_length = http_build_response_header(client->response_header,sizeof(client->response_header),status, content_type,content_length, keep_alive);
    if (header_length < 0)
        return -1;
    client->response_header_length = (size_t)header_length;
    client->response_header_sent = 0;
    client->close_after_response = !keep_alive;
    client->response_pending = 1;
    return 0;
}

int start_error_response(struct client *client, int status)
{
    if (client->file_fd >= 0) {
        close(client->file_fd);
        client->file_fd = -1;
    }
    return start_response(client, status, 0, "text/plain; charset=utf-8", 0);
}

int process_request(struct client *client, const char *document_root)
{
    struct http_request request;
    int parse_result;
    int status;
    int file_fd;
    off_t file_size = 0;
    char resolved_path[PATH_MAX];
    const char *content_type;
    parse_result = http_parse_request(client->input, client->input_length,&request);
    if (parse_result == HTTP_PARSE_INCOMPLETE) {
        if (client->peer_eof)
            return start_error_response(client, 400) < 0 ? -1 : 1;
        return 0;
    }
    if (parse_result != HTTP_PARSE_COMPLETE)
        return start_error_response(client, 400) < 0 ? -1 : 1;
    memmove(client->input, client->input + request.consumed,client->input_length - request.consumed);
    client->input_length -= request.consumed;
    if (client->peer_eof)
        request.keep_alive = 0;
    if (strcmp(request.method, "GET") != 0)
        return start_response(client, 405, 0,"text/plain; charset=utf-8", 0) < 0 ? -1 : 1;
    if (request.has_body)
        return start_error_response(client, 400) < 0 ? -1 : 1;
    file_fd = files_open(document_root, request.target, &file_size, &status,
                         resolved_path, sizeof(resolved_path));
    if (file_fd < 0)
        return start_response(client, status, request.keep_alive,"text/plain; charset=utf-8", 0) < 0 ? -1 : 1;
    client->file_fd = file_fd;
    client->file_size = file_size;
    client->file_offset = 0;
    content_type = mime_type_for_path(resolved_path);
    if (start_response(client, 200, request.keep_alive, content_type,file_size) < 0) {
        close(client->file_fd);
        client->file_fd = -1;
        return -1;
    }
    return 1;
}

int read_client(int epoll_fd, struct client *client,const char *document_root)
{
    while (!client->response_pending) {
        ssize_t bytes;
        int request_result;
        if (client->input_length == sizeof(client->input)) {
            if (start_error_response(client, 400) < 0)
                return -1;
            break;
        }

        bytes = recv(client->fd, client->input + client->input_length,sizeof(client->input) - client->input_length, 0);
        if (bytes > 0) {
            client->input_length += (size_t)bytes;
            request_result = process_request(client, document_root);
            if (request_result < 0)
                return -1;
            if (client->response_pending)
                break;
            continue;
        }
        if (bytes == 0) {
            client->peer_eof = 1;
            if (client->input_length == 0)
                return 1;
            request_result = process_request(client, document_root);
            if (request_result < 0)
                return -1;
            if (!client->response_pending) {
                if (start_error_response(client, 400) < 0)
                    return -1;
            }
            break;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        perror("recv");
        return -1;
    }

    if (client->response_pending && set_client_events(epoll_fd, client, EPOLLOUT) < 0)
        return -1;
    return 0;
}

int flush_response(struct client *client)
{
    while (client->response_header_sent < client->response_header_length) {
        ssize_t bytes = send(client->fd,
                             client->response_header +
                                 client->response_header_sent,
                             client->response_header_length -
                                 client->response_header_sent,
                             MSG_NOSIGNAL);

        if (bytes > 0) {
            client->response_header_sent += (size_t)bytes;
            continue;
        }
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        return -1;
    }

    if (client->file_fd >= 0) {
        while (client->file_offset < client->file_size) {
            size_t remaining =
                (size_t)(client->file_size - client->file_offset);
            ssize_t bytes = sendfile(client->fd, client->file_fd,&client->file_offset, remaining);
            if (bytes > 0)
                continue;
            if (bytes == 0) {
                errno = EIO;
                return -1;
            }
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            return -1;
        }
    } else {
        while (client->error_body_sent < client->error_body_length) {
            ssize_t bytes = send(client->fd,
                                 client->error_body +
                                     client->error_body_sent,
                                 client->error_body_length -
                                     client->error_body_sent,
                                 MSG_NOSIGNAL);

            if (bytes > 0) {
                client->error_body_sent += (size_t)bytes;
                continue;
            }
            if (bytes < 0 && errno == EINTR)
                continue;
            if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return 0;
            return -1;
        }
    }
    return 1;
}

int finish_response(int epoll_fd, struct client *client,const char *document_root)
{
    int request_result;
    if (client->file_fd >= 0) {
        close(client->file_fd);
        client->file_fd = -1;
    }
    client->response_pending = 0;
    if (client->close_after_response || client->peer_eof)
        return 1;
    request_result = process_request(client, document_root);
    if (request_result < 0)
        return -1;
    if (client->response_pending) {
        if (set_client_events(epoll_fd, client, EPOLLOUT) < 0)
            return -1;
        return 0;
    }
    if (set_client_events(epoll_fd, client, EPOLLIN) < 0)
        return -1;
    return 0;
}

int service_client(int epoll_fd, struct client *client,const char *document_root, uint32_t event_flags)
{
    int read_result;
    if (event_flags & EPOLLERR)
        return 1;
    if (event_flags & EPOLLRDHUP)
        client->peer_eof = 1;
    if (!client->response_pending &&
        (event_flags & (EPOLLIN | EPOLLHUP | EPOLLRDHUP))) {
        read_result = read_client(epoll_fd, client, document_root);
        if (read_result != 0)
            return 1;
    }

    while (client->response_pending) {
        int write_result = flush_response(client);
        int finish_result;

        if (write_result < 0) {
            if (errno != EPIPE && errno != ECONNRESET)
                perror("send response");
            return 1;
        }
        if (write_result == 0) {
            if (set_client_events(epoll_fd, client, EPOLLOUT) < 0)
                return 1;
            return 0;
        }

        finish_result = finish_response(epoll_fd, client, document_root);
        if (finish_result != 0)
            return finish_result > 0;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int server_fd;
    int epoll_fd;
    char document_root[PATH_MAX];
    struct epoll_event event;
    struct epoll_event events[MAX_EVENTS];

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (realpath("www", document_root) == NULL) {
        perror("realpath www");
        return EXIT_FAILURE;
    }

    server_fd = server_create_listener(argv[1]);
    if (server_fd < 0)
        return EXIT_FAILURE;
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal SIGPIPE");
        close(server_fd);
        return EXIT_FAILURE;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(server_fd);
        return EXIT_FAILURE;
    }

    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.ptr = NULL;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) < 0) {
        perror("epoll_ctl ADD listener");
        close(epoll_fd);
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Servidor HTTP escuchando en el puerto %s (raiz: %s)\n",argv[1], document_root);
    for (;;) {
        int ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        int i;

        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            close(epoll_fd);
            close(server_fd);
            return EXIT_FAILURE;
        }

        for (i = 0; i < ready; i++) {
            struct client *client = events[i].data.ptr;
            if (client == NULL) {
                for (;;) {
                    int client_fd = accept(server_fd, NULL, NULL);
                    struct epoll_event client_event;
                    if (client_fd < 0) {
                        if (errno == EINTR)
                            continue;
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept");
                        break;
                    }
                    if (server_set_nonblocking(client_fd) < 0) {
                        close(client_fd);
                        continue;
                    }
                    client = calloc(1, sizeof(*client));
                    if (client == NULL) {
                        perror("calloc client");
                        close(client_fd);
                        continue;
                    }
                    client->fd = client_fd;
                    client->file_fd = -1;
                    memset(&client_event, 0, sizeof(client_event));
                    client_event.events = EPOLLIN | EPOLLRDHUP;
                    client_event.data.ptr = client;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd,
                                  &client_event) < 0) {
                        perror("epoll_ctl ADD client");
                        close(client_fd);
                        free(client);
                        continue;
                    }
                    puts("Cliente conectado");
                }
            } else if (service_client(epoll_fd, client, document_root,events[i].events)) {
                close_client(epoll_fd, client);
            }
        }
    }
}
