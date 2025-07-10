#include "tcp_util.h"
#include <cstring>
#include <cerrno>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace kraken_doa::heimdall::tcp {

    namespace {
#ifdef _WIN32
        void ensureInit() {
            static bool done = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
            (void)done;
        }
        int lastErr() { return WSAGetLastError(); }
        bool wouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
        bool isTimeout(int e) { return e == WSAETIMEDOUT || e == WSAEWOULDBLOCK; }
        bool interrupted(int) { return false; }
        void setNonBlocking(Socket s, bool nb) { u_long m = nb ? 1 : 0; ioctlsocket(s, FIONBIO, &m); }
#else
        void ensureInit() {}
        int lastErr() { return errno; }
        bool wouldBlock(int e) { return e == EINPROGRESS || e == EWOULDBLOCK || e == EAGAIN; }
        bool isTimeout(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
        bool interrupted(int e) { return e == EINTR; }
        void setNonBlocking(Socket s, bool nb) {
            int f = fcntl(s, F_GETFL, 0);
            fcntl(s, F_SETFL, nb ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK));
        }
#endif
        constexpr int SEND_FLAGS =
#ifdef MSG_NOSIGNAL
            MSG_NOSIGNAL;
#else
            0;
#endif
    }

    Socket connectTo(const std::string& host, int port, int timeoutMs, int rcvBufBytes, std::string& err) {
        ensureInit();
        addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        std::string portStr = std::to_string(port);
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
            err = "cannot resolve " + host;
            return INVALID;
        }
        Socket result = INVALID;
        err = "connect failed";
        for (addrinfo* a = res; a && result == INVALID; a = a->ai_next) {
            Socket s = (Socket)socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s == INVALID) { continue; }
            setNonBlocking(s, true);
            int r = connect(s, a->ai_addr, (int)a->ai_addrlen);
            bool ok = (r == 0);
            if (!ok && wouldBlock(lastErr())) {
                fd_set wf;
                FD_ZERO(&wf);
                FD_SET(s, &wf);
                timeval tv;
                tv.tv_sec = timeoutMs / 1000;
                tv.tv_usec = (timeoutMs % 1000) * 1000;
                int n = select((int)s + 1, nullptr, &wf, nullptr, &tv);
                if (n > 0) {
                    int soerr = 0;
                    socklen_t l = sizeof(soerr);
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &l);
                    ok = (soerr == 0);
                    if (!ok) { err = std::string("connect failed: ") + std::strerror(soerr); }
                }
                else if (n == 0) { err = "connect timed out"; }
            }
            if (!ok) { closeSocket(s); continue; }
            setNonBlocking(s, false);
            int one = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
#ifdef SO_NOSIGPIPE
            setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&one, sizeof(one));
#endif
            if (rcvBufBytes > 0) { setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvBufBytes, sizeof(rcvBufBytes)); }
            result = s;
        }
        freeaddrinfo(res);
        return result;
    }

    void setRecvTimeout(Socket s, int timeoutMs) {
#ifdef _WIN32
        DWORD t = (DWORD)timeoutMs;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
#else
        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    }

    bool recvExact(Socket s, void* buf, size_t n, bool* timedOut) {
        if (timedOut) { *timedOut = false; }
        uint8_t* p = (uint8_t*)buf;
        size_t got = 0;
        while (got < n) {
            size_t chunk = n - got;
            if (chunk > (1u << 30)) { chunk = 1u << 30; }
            auto r = recv(s, (char*)p + got, (int)chunk, 0);
            if (r > 0) { got += (size_t)r; continue; }
            if (r == 0) { return false; }
            int e = lastErr();
            if (interrupted(e)) { continue; }
            if (timedOut && isTimeout(e)) { *timedOut = true; }
            return false;
        }
        return true;
    }

    bool sendAll(Socket s, const void* buf, size_t n) {
        const uint8_t* p = (const uint8_t*)buf;
        size_t sent = 0;
        while (sent < n) {
            auto r = send(s, (const char*)p + sent, (int)(n - sent), SEND_FLAGS);
            if (r > 0) { sent += (size_t)r; continue; }
            if (r < 0 && interrupted(lastErr())) { continue; }
            return false;
        }
        return true;
    }

    void shutdownBoth(Socket s) {
        if (s == INVALID) { return; }
#ifdef _WIN32
        shutdown(s, SD_BOTH);
#else
        shutdown(s, SHUT_RDWR);
#endif
    }

    void closeSocket(Socket s) {
        if (s == INVALID) { return; }
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
    }
}
