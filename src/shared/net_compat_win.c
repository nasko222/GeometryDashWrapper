#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stddef.h>
#include <string.h>

#include "net_compat_win.h"

int gd_net_android_family_to_host(int family) {
    return family == GD_ANDROID_AF_INET6 ? AF_INET6 : family;
}

int gd_net_host_family_to_android(int family) {
    return family == AF_INET6 ? GD_ANDROID_AF_INET6 : family;
}

static int gd_net_wsa_error_to_android_impl(int error, int preserve_unknown) {
    switch (error) {
    case 0: return 0;
    case WSAEINTR: return 4;
    case WSAEBADF: return 9;
    case WSAEACCES: return 13;
    case WSAEFAULT: return 14;
    case WSAEINVAL: return 22;
    case WSAEMFILE: return 24;
    case WSAEWOULDBLOCK: return 11;
    case WSAEINPROGRESS: return 115;
    case WSAEALREADY: return 114;
    case WSAENOTSOCK: return 88;
    case WSAEDESTADDRREQ: return 89;
    case WSAEMSGSIZE: return 90;
    case WSAEPROTOTYPE: return 91;
    case WSAENOPROTOOPT: return 92;
    case WSAEPROTONOSUPPORT: return 93;
    case WSAESOCKTNOSUPPORT: return 94;
    case WSAEOPNOTSUPP: return 95;
#ifdef WSAEPFNOSUPPORT
    case WSAEPFNOSUPPORT: return 96;
#endif
    case WSAEAFNOSUPPORT: return 97;
    case WSAEADDRINUSE: return 98;
    case WSAEADDRNOTAVAIL: return 99;
    case WSAENETDOWN: return 100;
    case WSAENETUNREACH: return 101;
    case WSAENETRESET: return 102;
    case WSAECONNABORTED: return 103;
    case WSAECONNRESET: return 104;
    case WSAENOBUFS: return 105;
    case WSAEISCONN: return 106;
    case WSAENOTCONN: return 107;
#ifdef WSAESHUTDOWN
    case WSAESHUTDOWN: return 108;
#endif
#ifdef WSAETOOMANYREFS
    case WSAETOOMANYREFS: return 109;
#endif
    case WSAETIMEDOUT: return 110;
    case WSAECONNREFUSED: return 111;
#ifdef WSAEHOSTDOWN
    case WSAEHOSTDOWN: return 112;
#endif
    case WSAEHOSTUNREACH: return 113;
    default: return preserve_unknown && error ? error : 5;
    }
}

int gd_net_wsa_error_to_android(int error) {
    return gd_net_wsa_error_to_android_impl(error, 0);
}

int gd_net_wsa_error_to_android_passthrough(int error) {
    return gd_net_wsa_error_to_android_impl(error, 1);
}

int gd_net_android_ai_flags_to_host(int flags) {
    int result = 0;
    if (flags & 0x0001) result |= AI_PASSIVE;
    if (flags & 0x0002) result |= AI_CANONNAME;
    if (flags & 0x0004) result |= AI_NUMERICHOST;
    if (flags & 0x0008) result |= AI_NUMERICSERV;
#ifdef AI_V4MAPPED
    if (flags & 0x0800) result |= AI_V4MAPPED;
#endif
#ifdef AI_ALL
    if (flags & 0x0100) result |= AI_ALL;
#endif
#ifdef AI_ADDRCONFIG
    if (flags & 0x0400) result |= AI_ADDRCONFIG;
#endif
    return result;
}

int gd_net_set_nonblocking(SOCKET socket_value, int enabled) {
    u_long value = enabled ? 1u : 0u;
    return ioctlsocket(socket_value, FIONBIO, &value) == 0 ? 0 : -1;
}

int gd_net_android_message_flags_to_host(int flags) {
    int result = flags;
    result &= ~GD_ANDROID_MSG_NOSIGNAL;
#ifdef MSG_DONTWAIT
    result &= ~MSG_DONTWAIT;
#endif
    return result;
}

const struct sockaddr *gd_net_sockaddr_to_host(
        const struct sockaddr *address,
        int length,
        struct sockaddr_storage *storage,
        int *host_length) {
    size_t copy_length;
    if (!address || length <= 0 || !storage || !host_length) {
        return address;
    }
    copy_length = (size_t)length;
    if (copy_length > sizeof(*storage)) {
        copy_length = sizeof(*storage);
    }
    memset(storage, 0, sizeof(*storage));
    memcpy(storage, address, copy_length);
    ((struct sockaddr *)storage)->sa_family =
        (ADDRESS_FAMILY)gd_net_android_family_to_host(address->sa_family);
    *host_length = (int)copy_length;
    return (const struct sockaddr *)storage;
}

void gd_net_sockaddr_from_host(
        struct sockaddr *destination,
        int *destination_length,
        const struct sockaddr *source,
        int source_length) {
    int capacity;
    int copy_length;
    if (!destination_length) return;
    capacity = *destination_length;
    *destination_length = source_length;
    if (!destination || !source || capacity <= 0) return;
    copy_length = capacity < source_length ? capacity : source_length;
    memcpy(destination, source, (size_t)copy_length);
    if (copy_length >= (int)sizeof(destination->sa_family)) {
        destination->sa_family =
            (ADDRESS_FAMILY)gd_net_host_family_to_android(source->sa_family);
    }
}
