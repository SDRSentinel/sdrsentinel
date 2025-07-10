#pragma once
// Minimal blocking TCP helpers (POSIX sockets with a small Winsock shim) shared by the Heimdall clients.
// SDR++'s core net.h was not used: it has no exact-length blocking read, no shutdown() to unblock a reader
// from another thread, and no connect/receive timeouts.
#include <cstddef>
#include <cstdint>
#include <string>

namespace kraken_doa::heimdall::tcp {

#ifdef _WIN32
    using Socket = uintptr_t;
    constexpr Socket INVALID = ~(Socket)0;
#else
    using Socket = int;
    constexpr Socket INVALID = -1;
#endif

    // Connect with timeout (non-blocking connect + select), sets TCP_NODELAY and SO_RCVBUF.
    Socket connectTo(const std::string& host, int port, int timeoutMs, int rcvBufBytes, std::string& err);
    // Receive timeout for blocking reads, 0 disables.
    void setRecvTimeout(Socket s, int timeoutMs);
    // Read exactly n bytes. Returns false on EOF, error or timeout (timedOut set for the latter).
    bool recvExact(Socket s, void* buf, size_t n, bool* timedOut = nullptr);
    bool sendAll(Socket s, const void* buf, size_t n);
    void shutdownBoth(Socket s);
    void closeSocket(Socket s);
}
