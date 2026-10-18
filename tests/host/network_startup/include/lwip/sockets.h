#ifndef TEST_NETWORK_SOCKETS_H
#define TEST_NETWORK_SOCKETS_H
#include <sys/socket.h>
int test_socket(int domain, int type, int protocol);
int test_bind(int socket_fd, const struct sockaddr *address, socklen_t length);
int test_close(int socket_fd);
int test_setsockopt(int socket_fd, int level, int option, const void *value, socklen_t length);
ssize_t test_recvfrom(int socket_fd, void *buffer, size_t length, int flags,
                      struct sockaddr *address, socklen_t *address_length);
ssize_t test_sendto(int socket_fd, const void *buffer, size_t length, int flags,
                    const struct sockaddr *address, socklen_t address_length);
#define socket test_socket
#define bind test_bind
#define close test_close
#define recvfrom test_recvfrom
#define sendto test_sendto
#define setsockopt test_setsockopt
#endif
