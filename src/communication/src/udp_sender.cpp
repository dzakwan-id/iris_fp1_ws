#include <arpa/inet.h>
#include <endian.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

constexpr char kHeader[4] = {'I', 'R', 'I', 'S'};
constexpr size_t kPacketSize = 12;
constexpr auto kPeriod = std::chrono::milliseconds(50);  // 20 Hz

volatile std::sig_atomic_t g_running = 1;

void on_sigint(int) { g_running = 0; }

void build_packet(uint8_t (&buf)[kPacketSize], int32_t pwm, int32_t steer) {
    pwm = std::clamp<int32_t>(pwm, -255, 255);
    // htole32 keeps the wire format little-endian on any host
    uint32_t pwm_le = htole32(static_cast<uint32_t>(pwm));
    uint32_t steer_le = htole32(static_cast<uint32_t>(steer));

    std::memcpy(buf, kHeader, sizeof(kHeader));
    std::memcpy(buf + 4, &pwm_le, sizeof(pwm_le));
    std::memcpy(buf + 8, &steer_le, sizeof(steer_le));
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <pwm> [steer_deg] [host] [port]\n", argv[0]);
        return 1;
    }
    int32_t pwm = std::atoi(argv[1]);
    int32_t steer = argc > 2 ? std::atoi(argv[2]) : 0;
    const char *host = argc > 3 ? argv[3] : "127.0.0.1";
    int port = argc > 4 ? std::atoi(argv[4]) : 12151;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        std::fprintf(stderr, "Invalid host: %s\n", host);
        close(sock);
        return 1;
    }

    std::signal(SIGINT, on_sigint);

    uint8_t packet[kPacketSize];
    build_packet(packet, pwm, steer);
    std::printf("Sending pwm=%d steer=%d to %s:%d at 20 Hz (Ctrl-C to stop)\n",
                std::clamp<int32_t>(pwm, -255, 255), steer, host, port);

    while (g_running) {
        if (sendto(sock, packet, sizeof(packet), 0,
                   reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            std::perror("sendto");
        }
        std::this_thread::sleep_for(kPeriod);
    }

    // keep the current steering angle, like the receiver's own timeout stop
    build_packet(packet, 0, steer);
    sendto(sock, packet, sizeof(packet), 0, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    std::printf("Stopped\n");

    close(sock);
    return 0;
}
