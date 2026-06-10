#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <mutex>
#include <cstdint>
#include "core/thread_pool.hpp"
#include "crypto/e2e.hpp"
#include "optimize/compression.hpp"

namespace cleainput {

// WebSocket connection state
enum class WSState {
    CONNECTING,
    OPEN,
    CLOSING,
    CLOSED
};

// WebSocket message type
enum class WSMessageType {
    TEXT,          // text prompt
    VOICE_CHUNK,   // audio chunk
    VOICE_END,     // end of voice
    AI_RESPONSE,   // AI text response
    AI_VOICE,      // AI voice response
    CODE_GEN,      // generated code
    STATUS,        // system status
    ERROR,         // error message
    PING,          // keepalive
    PONG,          // keepalive reply
    AUTH,          // authentication
    AUTH_OK,       // auth success
    DISCONNECT     // clean disconnect
};

// WebSocket message
struct WSMessage {
    WSMessageType type;
    std::string   conn_id;
    std::string   user_id;
    std::string   text;
    std::vector<uint8_t> binary;
    uint64_t      timestamp;
    uint32_t      sequence;
    bool          encrypted = false;
    bool          compressed = false;

    bool is_voice() const {
        return type == WSMessageType
            ::VOICE_CHUNK
            || type == WSMessageType
            ::VOICE_END;
    }

    bool is_text() const {
        return type == WSMessageType
            ::TEXT;
    }
};

// WebSocket client connection
struct WSClient {
    int         fd;
    std::string conn_id;
    std::string user_id;
    WSState     state;
    uint64_t    connected_at;
    uint64_t    last_ping;
    uint64_t    bytes_sent;
    uint64_t    bytes_received;
    uint32_t    message_count;
    bool        authenticated;
    bool        compression_enabled;
    bool        encryption_enabled;

    // Voice session state
    bool        in_voice_session = false;
    uint64_t    voice_session_id = 0;
    std::vector<uint8_t> voice_buffer;

    bool alive() const {
        return state == WSState::OPEN
            && fd > 0;
    }
};

// Voice session
struct VoiceSession {
    uint64_t    session_id;
    std::string conn_id;
    std::string user_id;
    uint64_t    started_at;
    uint32_t    chunks_received;
    std::vector<uint8_t> audio_buffer;

    // VAD state
    bool        speech_detected;
    uint32_t    silence_ms;
    bool        complete;

    // Max 30 seconds of audio
    static constexpr size_t
        MAX_BUFFER = 30 * 16000
                   * 2; // 30s PCM

    bool buffer_full() const {
        return audio_buffer.size()
            >= MAX_BUFFER;
    }
};

// WebSocket stats
struct WSStats {
    std::atomic<uint64_t>
        active_connections{0};
    std::atomic<uint64_t>
        total_connections{0};
    std::atomic<uint64_t>
        messages_sent{0};
    std::atomic<uint64_t>
        messages_received{0};
    std::atomic<uint64_t>
        bytes_sent{0};
    std::atomic<uint64_t>
        bytes_received{0};
    std::atomic<uint64_t>
        voice_sessions{0};
    std::atomic<uint64_t>
        active_voice_sessions{0};
};

// Message handler callback
using WSMessageHandler = std::function<
    void(const WSMessage&)>;

// Connection callback
using WSConnectHandler = std::function<
    void(const WSClient&)>;

// Disconnect callback
using WSDisconnectHandler = std::function<
    void(const WSClient&,
         const std::string& reason)>;

class WebSocket {
public:
    explicit WebSocket(
        uint16_t    port,
        ThreadPool* pool,
        Compression* compressor);

    ~WebSocket();

    // ─── Server Control ───────────────

    bool start();
    void stop();

    bool is_running() const {
        return running_.load();
    }

    // ─── Event Handlers ───────────────

    void on_message(
        WSMessageHandler handler);

    void on_connect(
        WSConnectHandler handler);

    void on_disconnect(
        WSDisconnectHandler handler);

    void on_voice_complete(
        std::function<void(
            const std::string& user_id,
            const std::vector<uint8_t>&
                audio)> handler);

    // ─── Send Messages ────────────────

    // Send text to client
    bool send_text(
        const std::string& conn_id,
        const std::string& text);

    // Send binary to client
    bool send_binary(
        const std::string& conn_id,
        const std::vector<uint8_t>& data);

    // Send typed message
    bool send_message(
        const std::string& conn_id,
        const WSMessage& msg);

    // Stream AI response token by token
    bool stream_response(
        const std::string& conn_id,
        const std::string& token,
        bool is_final = false);

    // Send AI voice response
    bool send_voice_response(
        const std::string& conn_id,
        const std::vector<uint8_t>& audio);

    // Broadcast to all
    void broadcast(
        const WSMessage& msg);

    // Broadcast to user's connections
    void broadcast_to_user(
        const std::string& user_id,
        const WSMessage& msg);

    // ─── Connection Management ────────

    // Disconnect client
    void disconnect(
        const std::string& conn_id,
        const std::string& reason = "");

    // Get client info
    WSClient* get_client(
        const std::string& conn_id);

    // All active connections
    size_t connection_count() const;

    // ─── Voice Handling ───────────────

    // Start voice session
    uint64_t start_voice_session(
        const std::string& conn_id);

    // Add audio chunk to session
    void add_voice_chunk(
        const std::string& conn_id,
        const std::vector<uint8_t>& chunk);

    // End voice session
    void end_voice_session(
        const std::string& conn_id);

    // Get active voice session
    VoiceSession* get_voice_session(
        const std::string& conn_id);

    // ─── Stats ────────────────────────

    const WSStats& stats() const {
        return stats_;
    }

private:
    uint16_t     port_;
    ThreadPool*  pool_;
    Compression* compressor_;
    WSStats      stats_;

    std::atomic<bool> running_{false};
    int server_fd_ = -1;

    // Active clients
    std::unordered_map<
        std::string,
        WSClient> clients_;
    mutable std::mutex clients_mutex_;

    // Voice sessions
    std::unordered_map<
        std::string,
        VoiceSession> voice_sessions_;
    std::mutex voice_mutex_;

    // Handlers
    WSMessageHandler    on_message_;
    WSConnectHandler    on_connect_;
    WSDisconnectHandler on_disconnect_;
    std::function<void(
        const std::string&,
        const std::vector<uint8_t>&)>
        on_voice_complete_;

    // Accept loop
    void accept_loop();

    // Handle client
    void handle_client(int fd);

    // WebSocket handshake
    bool do_handshake(
        int fd,
        const std::string& request);

    // Read frame from fd
    WSMessage read_frame(int fd);

    // Write frame to fd
    bool write_frame(
        int fd,
        const WSMessage& msg);

    // Parse raw WS frame
    bool parse_frame(
        const uint8_t* data,
        size_t size,
        WSMessage& out);

    // Build raw WS frame
    std::vector<uint8_t> build_frame(
        const WSMessage& msg);

    // WS accept key
    std::string accept_key(
        const std::string& key) const;

    // Mask/unmask payload
    void mask_payload(
        uint8_t* data,
        size_t size,
        uint32_t mask);

    // Ping/pong keepalive
    void start_ping_loop();
    void send_ping(int fd);
    bool check_alive(
        const WSClient& client);

    // Cleanup dead connections
    void cleanup_dead_connections();

    // Generate conn_id
    static std::string gen_conn_id();

    // Process voice buffer
    void process_voice_buffer(
        VoiceSession& session);
};

} // namespace cleainput
