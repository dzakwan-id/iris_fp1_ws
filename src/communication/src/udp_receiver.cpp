#include <arpa/inet.h>
#include <endian.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr char kHeader[4] = {'I', 'R', 'I', 'S'};
constexpr size_t kPacketSize = 20;

volatile std::sig_atomic_t g_running = 1;

void on_sigint(int) { g_running = 0; }

struct Feedback {
    int32_t left_ticks;
    int32_t right_ticks;
    int32_t vel_x_cms;
    int32_t vel_y_cms;
};

int32_t read_i32_le(const uint8_t *src) {
    uint32_t raw;
    std::memcpy(&raw, src, sizeof(raw));
    // le32toh keeps the wire format little-endian on any host
    return static_cast<int32_t>(le32toh(raw));
}

bool parse_packet(const uint8_t *buf, size_t len, Feedback &out) {
    if (len < kPacketSize || std::memcmp(buf, kHeader, sizeof(kHeader)) != 0) {
        return false;
    }
    out.left_ticks = read_i32_le(buf + 4);
    out.right_ticks = read_i32_le(buf + 8);
    out.vel_x_cms = read_i32_le(buf + 12);
    out.vel_y_cms = read_i32_le(buf + 16);
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 12152;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        close(sock);
        return 1;
    }

    // wake up once a second so Ctrl-C exits even when no packets arrive
    timeval tv{1, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // no SA_RESTART, so Ctrl-C interrupts a blocking recvfrom right away
    struct sigaction sa{};
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, nullptr);

    std::printf("Listening for feedback on UDP port %d (Ctrl-C to stop)\n", port);

    uint8_t buf[64];
    while (g_running) {
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                std::perror("recvfrom");
            }
            continue;
        }

        Feedback fb;
        if (!parse_packet(buf, static_cast<size_t>(n), fb)) {
            std::fprintf(stderr, "Ignoring malformed packet (%zd bytes)\n", n);
            continue;
        }
        std::printf("enc L=%d R=%d  vel x=%d y=%d cm/s\n",
                    fb.left_ticks, fb.right_ticks, fb.vel_x_cms, fb.vel_y_cms);
    }

    std::printf("Stopped\n");
    close(sock);
    return 0;
}

