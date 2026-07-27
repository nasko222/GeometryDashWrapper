#ifndef GD_NET_COMPAT_WIN_H
#define GD_NET_COMPAT_WIN_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
typedef int SOCKET;
struct sockaddr;
struct sockaddr_storage;
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GD_ANDROID_AF_INET6 = 10,
    GD_ANDROID_SOL_SOCKET = 1,
    GD_ANDROID_F_GETFL = 3,
    GD_ANDROID_F_SETFL = 4,
    GD_ANDROID_O_NONBLOCK = 0x800,
    GD_ANDROID_FIONREAD = 0x541b,
    GD_ANDROID_FIONBIO = 0x5421,
    GD_ANDROID_MSG_NOSIGNAL = 0x4000,
    GD_ANDROID_SOCK_NONBLOCK = 0x800,
    GD_ANDROID_SOCK_CLOEXEC = 0x80000
};

int gd_net_android_family_to_host(int family);
int gd_net_host_family_to_android(int family);
int gd_net_wsa_error_to_android(int error);
int gd_net_wsa_error_to_android_passthrough(int error);
int gd_net_android_ai_flags_to_host(int flags);
int gd_net_set_nonblocking(SOCKET socket_value, int enabled);
int gd_net_android_message_flags_to_host(int flags);

const struct sockaddr *gd_net_sockaddr_to_host(
    const struct sockaddr *address,
    int length,
    struct sockaddr_storage *storage,
    int *host_length);

void gd_net_sockaddr_from_host(
    struct sockaddr *destination,
    int *destination_length,
    const struct sockaddr *source,
    int source_length);

#ifdef __cplusplus
}
#endif

#endif
