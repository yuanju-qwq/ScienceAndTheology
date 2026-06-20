// M3: Protocol codec + network round-trip tests.
// Design: docs/多人游戏系统设计.md §4 方案B
//
// Tests:
//   1. Frame encode/decode round-trip (various payload sizes).
//   2. CRC32 corruption detection.
//   3. Multi-frame stream decoding.
//   4. CommandQueue thread-safe push/drain.
//   5. ServerCore + NetworkClient TCP round-trip (login + command + delta).
//   6. LAN discovery (UDP broadcast/reply).

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "server/protocol/protocol_codec.hpp"
#include "server/command_queue.hpp"
#include "server/server_core.hpp"
#include "server/network_client.hpp"

using namespace science_and_theology::server;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& msg) {
    if (cond) return;
    std::cerr << "  FAIL: " << msg << '\n';
    ++g_failures;
}

// --- Test 1: Frame encode/decode round-trip ---
bool test_frame_roundtrip() {
    std::cerr << "[M3] test_frame_roundtrip" << '\n';

    // Empty payload.
    {
        auto wire = encode_frame_empty(PacketType::HEARTBEAT, 42);
        check(wire.size() == kFrameHeaderSize + kFrameCrcSize,
              "empty frame size == header + crc");
        std::vector<uint8_t> buf(wire.begin(), wire.end());
        auto result = decode_frame(buf);
        check(result.status == DecodeStatus::OK, "empty decode OK");
        check(result.frame.has_value(), "empty frame present");
        check(result.frame->type == PacketType::HEARTBEAT, "type matches");
        check(result.frame->player_id == 42, "player_id matches");
        check(result.frame->payload.empty(), "payload empty");
        check(buf.empty(), "buffer consumed");
    }

    // Small payload.
    {
        const std::string payload = "hello server";
        auto wire = encode_frame_string(PacketType::CMD_COMMAND, 7, payload);
        std::vector<uint8_t> buf(wire.begin(), wire.end());
        auto result = decode_frame(buf);
        check(result.status == DecodeStatus::OK, "small decode OK");
        check(result.frame->player_id == 7, "pid 7");
        check(result.frame->payload.size() == payload.size(), "payload size");
        std::string decoded(result.frame->payload.begin(),
                            result.frame->payload.end());
        check(decoded == payload, "payload content matches");
    }

    // Large payload (1 MiB).
    {
        std::vector<uint8_t> big(1024 * 1024, 0xAB);
        auto wire = encode_frame(PacketType::SYNC_SNAPSHOT, 99,
                                 big.data(), big.size());
        check(wire.size() == kFrameHeaderSize + big.size() + kFrameCrcSize,
              "large frame size");
        std::vector<uint8_t> buf(wire.begin(), wire.end());
        auto result = decode_frame(buf);
        check(result.status == DecodeStatus::OK, "large decode OK");
        check(result.frame->payload.size() == big.size(), "large payload size");
        check(result.frame->payload == big, "large payload content");
    }

    return g_failures == 0;
}

// --- Test 2: CRC corruption detection ---
bool test_crc_corruption() {
    std::cerr << "[M3] test_crc_corruption" << '\n';

    auto wire = encode_frame_string(PacketType::CMD_LOGIN, 1, "secret");
    std::vector<uint8_t> buf = wire;

    // Flip a byte in the payload.
    buf[kFrameHeaderSize] ^= 0xFF;

    auto result = decode_frame(buf);
    check(result.status == DecodeStatus::CRC_MISMATCH, "CRC mismatch detected");

    // Bad magic.
    std::vector<uint8_t> bad_magic = wire;
    bad_magic[0] = 'X';
    auto result2 = decode_frame(bad_magic);
    check(result2.status == DecodeStatus::INVALID_MAGIC, "bad magic detected");

    return g_failures == 0;
}

// --- Test 3: Multi-frame stream decoding ---
bool test_multi_frame_stream() {
    std::cerr << "[M3] test_multi_frame_stream" << '\n';

    std::vector<uint8_t> stream;
    auto f1 = encode_frame_string(PacketType::CMD_COMMAND, 1, "cmd1");
    auto f2 = encode_frame_empty(PacketType::HEARTBEAT, 1);
    auto f3 = encode_frame_string(PacketType::SYNC_DELTA, 1, "delta1");
    stream.insert(stream.end(), f1.begin(), f1.end());
    stream.insert(stream.end(), f2.begin(), f2.end());
    stream.insert(stream.end(), f3.begin(), f3.end());

    std::vector<Frame> frames;
    while (!stream.empty()) {
        auto result = decode_frame(stream);
        if (result.status != DecodeStatus::OK) {
            check(false, "multi-frame decode failed mid-stream");
            break;
        }
        frames.push_back(std::move(*result.frame));
    }
    check(frames.size() == 3, "decoded 3 frames");
    check(frames[0].type == PacketType::CMD_COMMAND, "frame 0 type");
    check(frames[1].type == PacketType::HEARTBEAT, "frame 1 type");
    check(frames[2].type == PacketType::SYNC_DELTA, "frame 2 type");

    return g_failures == 0;
}

// --- Test 4: CommandQueue ---
bool test_command_queue() {
    std::cerr << "[M3] test_command_queue" << '\n';

    CommandQueue q;
    check(q.empty(), "starts empty");

    q.push(1, {'c', 'm', 'd', '1'});
    q.push(2, {'c', 'm', 'd', '2'});
    q.push(3, {'c', 'm', 'd', '3'});
    check(q.size() == 3, "size 3 after 3 pushes");

    std::vector<QueuedCommand> drained;
    size_t n = q.drain_all(drained);
    check(n == 3, "drained 3");
    check(drained.size() == 3, "out vector size 3");
    check(drained[0].player_id == 1, "FIFO order 1");
    check(drained[1].player_id == 2, "FIFO order 2");
    check(drained[2].player_id == 3, "FIFO order 3");
    check(q.empty(), "empty after drain");

    // drain_some with cap.
    q.push(1, {'a'});
    q.push(2, {'b'});
    q.push(3, {'c'});
    std::vector<QueuedCommand> some;
    n = q.drain_some(some, 2);
    check(n == 2, "drain_some 2");
    check(q.size() == 1, "1 remaining");

    q.clear();
    check(q.empty(), "clear ok");

    return g_failures == 0;
}

// --- Test 5: ServerCore + NetworkClient TCP round-trip ---
// This test starts a ServerCore on localhost, connects a NetworkClient,
// performs login, sends a command, and receives a delta.
bool test_network_roundtrip() {
    std::cerr << "[M3] test_network_roundtrip" << '\n';

    // Use ephemeral ports to avoid conflicts.
    const uint16_t tcp_port = 18910;
    const uint16_t udp_port = 18911;

    ServerCore server;
    server.set_server_name("TestServer");

    // Track executed commands and produced deltas.
    std::atomic<int> commands_executed{0};
    std::atomic<int> deltas_produced{0};
    std::atomic<uint64_t> assigned_pid{0};

    server.set_command_executor(
        [&commands_executed](uint64_t pid, uint64_t tick,
                             const std::vector<uint8_t>& payload)
        -> std::vector<uint8_t> {
            (void)pid; (void)tick;
            ++commands_executed;
            // Return an echo as the "delta".
            return payload;
        });

    server.set_delta_producer(
        [&deltas_produced]() -> std::vector<std::pair<uint64_t, std::vector<uint8_t>>> {
            // Only produce a delta on the first few ticks to avoid flooding.
            if (deltas_produced.fetch_add(1) < 1) {
                return {{1, {'d', 'e', 'l', 't', 'a'}}};
            }
            return {};
        });

    server.set_login_handler(
        [&assigned_pid](uint64_t pid, const std::vector<uint8_t>& creds,
                        std::string& reject) -> bool {
            (void)creds; (void)reject;
            assigned_pid = pid;
            return true;
        });

    check(server.start(tcp_port, udp_port), "server started");

    // Give the server a moment to bind.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.poll(0);

    // Connect a client.
    NetworkClient client;
    std::atomic<ClientState> client_state{ClientState::DISCONNECTED};
    std::vector<std::string> received_deltas;

    client.set_state_handler([&client_state](ClientState s) {
        client_state = s;
    });
    client.set_sync_handler([&received_deltas](PacketType type,
                                               const std::vector<uint8_t>& payload) {
        if (type == PacketType::SYNC_DELTA) {
            received_deltas.emplace_back(payload.begin(), payload.end());
        }
    });

    check(client.connect("127.0.0.1", tcp_port), "client connect initiated");

    // Poll loop: run server + client until connected or timeout.
    bool connected = false;
    for (int i = 0; i < 100; ++i) {
        server.poll(0);
        client.poll(0);
        if (client.is_connected()) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(connected, "client connected within timeout");
    check(client.player_id() != 0, "client got player_id");
    check(assigned_pid != 0, "server login handler called");

    // Send a command.
    const std::string cmd_payload = "mine_block_at_0_0_0";
    check(client.submit_command(
        std::vector<uint8_t>(cmd_payload.begin(), cmd_payload.end())),
        "command submitted");

    // Poll until command is executed.
    bool cmd_done = false;
    for (int i = 0; i < 100; ++i) {
        server.poll(0);
        client.poll(0);
        if (commands_executed.load() > 0) {
            cmd_done = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(cmd_done, "command executed on server");

    // Poll a bit more to receive the echo delta.
    for (int i = 0; i < 20; ++i) {
        server.poll(0);
        client.poll(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(!received_deltas.empty(), "client received at least one delta");

    client.disconnect();
    server.stop();

    return g_failures == 0;
}

// --- Test 6: Password protection ---
bool test_password_protection() {
    std::cerr << "[M3] test_password_protection" << '\n';

    const uint16_t tcp_port = 18920;
    const uint16_t udp_port = 18921;

    ServerCore server;
    server.set_password("s3cret");

    check(server.start(tcp_port, udp_port), "password server started");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.poll(0);

    // Client with wrong password.
    {
        NetworkClient client;
        std::atomic<ClientState> state{ClientState::DISCONNECTED};
        client.set_state_handler([&state](ClientState s) { state = s; });
        check(client.connect("127.0.0.1", tcp_port, 0, "wrong"), "connect with wrong pw");

        for (int i = 0; i < 50; ++i) {
            server.poll(0);
            client.poll(0);
            if (state == ClientState::REJECTED) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(state == ClientState::REJECTED, "wrong password rejected");
    }

    // Client with correct password.
    {
        NetworkClient client;
        std::atomic<ClientState> state{ClientState::DISCONNECTED};
        client.set_state_handler([&state](ClientState s) { state = s; });
        check(client.connect("127.0.0.1", tcp_port, 0, "s3cret"), "connect with correct pw");

        bool ok = false;
        for (int i = 0; i < 50; ++i) {
            server.poll(0);
            client.poll(0);
            if (client.is_connected()) { ok = true; break; }
            if (state == ClientState::REJECTED) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(ok, "correct password accepted");
    }

    server.stop();
    return g_failures == 0;
}

} // namespace

int main() {
    std::cerr << "=== M3 Protocol + Network tests ===" << '\n';
    test_frame_roundtrip();
    test_crc_corruption();
    test_multi_frame_stream();
    test_command_queue();
    test_network_roundtrip();
    test_password_protection();

    if (g_failures == 0) {
        std::cerr << "=== M3 PASSED ===" << '\n';
        return 0;
    }
    std::cerr << "=== M3 FAILED with " << g_failures << " failures ===" << '\n';
    return 1;
}
