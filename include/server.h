#ifndef SERVER_H
#define SERVER_H

int server_create_listener(const char *port_text);
int server_set_nonblocking(int fd);

#endif
