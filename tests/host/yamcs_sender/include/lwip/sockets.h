#ifndef TEST_YAMCS_SOCKETS_H
#define TEST_YAMCS_SOCKETS_H
#include <sys/socket.h>
int test_socket(int domain, int type, int protocol);
int test_close(int socket_fd);
int test_setsockopt(int socket_fd, int level, int option, const void *value, socklen_t length);
ssize_t test_sendto(int socket_fd, const void *buffer, size_t length, int flags,
                    const struct sockaddr *address, socklen_t address_length);
#define socket test_socket
#define sendto test_sendto
#define close test_close
#define setsockopt test_setsockopt
#endif
