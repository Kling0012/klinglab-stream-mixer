#include "WebSocketServer.h"
#include <juce_events/juce_events.h>
#include <CommonCrypto/CommonDigest.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <deque>
#include <vector>

namespace ksm
{
namespace
{
constexpr size_t maximumPayload = 1024 * 1024;
juce::var object(std::initializer_list<std::pair<juce::Identifier, juce::var>> fields)
{
    auto* value = new juce::DynamicObject();
    for (const auto& [key, field] : fields) value->setProperty(key, field);
    return juce::var(value);
}
bool integer(const juce::var& value) { return value.isInt() || value.isInt64(); }
bool validId(const juce::var& value)
{
    if (!value.isString()) return false;
    const auto id = value.toString();
    return id.isNotEmpty() && id.length() <= 64 && id.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
}
void nonblocking(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
}
}
struct WebSocketServer::Impl
{
    struct Client
    {
        int fd = -1;
        bool upgraded = false, closing = false, dead = false, pong = true, fragmented = false;
        juce::var pendingId;
        std::vector<uint8_t> input, fragment;
        std::deque<std::vector<uint8_t>> output;
        size_t sent = 0, queued = 0;
        double opened = juce::Time::getMillisecondCounterHiRes(), rateStart = opened, heartbeat = opened, closeStarted = 0.0;
        int requests = 0;
        ~Client() { if (fd >= 0) ::close(fd); }
        void raw(std::vector<uint8_t> data)
        {
            if (queued + data.size() > 4 * maximumPayload) { dead = true; return; }
            queued += data.size(); output.push_back(std::move(data));
        }
        void frame(uint8_t opcode, const void* bytes, size_t length)
        {
            if (dead || closing) return;
            std::vector<uint8_t> data { static_cast<uint8_t>(0x80 | opcode) };
            if (length < 126) data.push_back(static_cast<uint8_t>(length));
            else if (length <= 65535) { data.push_back(126); data.push_back(static_cast<uint8_t>(length >> 8)); data.push_back(static_cast<uint8_t>(length)); }
            else { data.push_back(127); for (int b = 7; b >= 0; --b) data.push_back(static_cast<uint8_t>(static_cast<uint64_t>(length) >> (b * 8))); }
            if (length > 0) { const auto* p = static_cast<const uint8_t*>(bytes); data.insert(data.end(), p, p + length); }
            raw(std::move(data));
        }
        void send(const juce::var& message)
        {
            const auto text = juce::JSON::toString(message, true);
            frame(1, text.toRawUTF8(), text.getNumBytesAsUTF8());
        }
        void close(uint16_t code)
        {
            const uint8_t bytes[] { static_cast<uint8_t>(code >> 8), static_cast<uint8_t>(code) };
            frame(8, bytes, 2); closing = true; closeStarted = juce::Time::getMillisecondCounterHiRes();
        }
        void flush()
        {
            size_t budget = 256 * 1024;
            while (!output.empty() && budget > 0)
            {
                auto& data = output.front();
                const auto n = ::send(fd, data.data() + sent, std::min(data.size() - sent, budget), 0);
                if (n < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) dead = true; return; }
                if (n == 0) return;
                const auto count = static_cast<size_t>(n); sent += count; queued -= count; budget -= count;
                if (sent == data.size()) { output.pop_front(); sent = 0; }
            }
            if (closing && output.empty()) dead = true;
        }
    };
    explicit Impl(WebSocketServer& s) : owner(s) {}
    WebSocketServer& owner;
    int listener = -1;
    int64_t revision = 0;
    juce::var state;
    std::vector<std::unique_ptr<Client>> clients;
    void broadcast(const juce::var& message)
    {
        for (auto& c : clients) if (c->upgraded && !c->closing) c->send(message);
    }
    void result(Client& c, const juce::var& id, const juce::var& error = {})
    {
        auto message = object({ {"type", "result"}, {"version", 1}, {"id", id}, {"ok", error.isVoid()} });
        if (!error.isVoid()) message.getDynamicObject()->setProperty("error", error);
        c.send(message);
    }
    void request(Client& c, const std::vector<uint8_t>& bytes)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - c.rateStart >= 1000.0) { c.rateStart = now; c.requests = 0; }
        if (++c.requests > 30) { c.close(1008); return; }
        if (!juce::CharPointer_UTF8::isValidString(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()))) { c.close(1007); return; }
        const auto message = juce::JSON::parse(juce::String::fromUTF8(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size())));
        const auto id = validId(message["id"]) ? message["id"] : juce::var();
        if (!message.isObject() || !integer(message["version"])) { result(c, id, failure("INVALID_MESSAGE")); return; }
        if (static_cast<int64_t>(message["version"]) != 1) { result(c, id, failure("VERSION_MISMATCH")); c.close(1002); return; }
        if (message["type"].toString() != "request" || id.isVoid() || !message["command"].isString()) { result(c, id, failure("INVALID_MESSAGE")); return; }
        try
        {
            const auto outcome = owner.onRequest ? owner.onRequest(message) : failure("INTERNAL_ERROR");
            if (outcome["pending"].isBool() && static_cast<bool>(outcome["pending"])) c.pendingId = id;
            else result(c, id, outcome);
        }
        catch (...) { result(c, id, failure("INTERNAL_ERROR")); }
    }
    void handshake(Client& c)
    {
        static const uint8_t delimiter[] { 13, 10, 13, 10 };
        const auto ending = std::search(c.input.begin(), c.input.end(), std::begin(delimiter), std::end(delimiter));
        if (ending == c.input.end()) { if (c.input.size() > 16384) c.dead = true; return; }
        const auto end = static_cast<size_t>(ending - c.input.begin());
        if (end > 16384 || std::any_of(c.input.begin(), ending, [](uint8_t b) { return b == 0 || b >= 128; })) { c.dead = true; return; }
        const auto text = juce::String::fromUTF8(reinterpret_cast<const char*>(c.input.data()), static_cast<int>(end));
        juce::String key, upgrade, connection, version;
        bool origin = false;
        const auto lines = juce::StringArray::fromLines(text);
        for (int i = 1; i < lines.size(); ++i)
        {
            const auto name = lines[i].upToFirstOccurrenceOf(":", false, false).trim().toLowerCase();
            const auto value = lines[i].fromFirstOccurrenceOf(":", false, false).trim();
            if (name == "origin") origin = true;
            if (name == "sec-websocket-key") key = value;
            if (name == "upgrade") upgrade = value.toLowerCase();
            if (name == "connection") connection = value.toLowerCase();
            if (name == "sec-websocket-version") version = value;
        }
        juce::MemoryOutputStream decoded;
        const bool keyOK = juce::Base64::convertFromBase64(decoded, key) && decoded.getDataSize() == 16;
        const auto tokens = juce::StringArray::fromTokens(connection, ",", "");
        bool connectionOK = false;
        for (const auto& token : tokens) if (token.trim() == "upgrade") connectionOK = true;
        if (origin || lines.isEmpty() || lines[0] != "GET / HTTP/1.1" || upgrade != "websocket" || !connectionOK || version != "13" || !keyOK)
        {
            const juce::String response = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            c.raw(std::vector<uint8_t>(response.toRawUTF8(), response.toRawUTF8() + response.getNumBytesAsUTF8()));
            c.closing = true; c.closeStarted = juce::Time::getMillisecondCounterHiRes(); return;
        }
        const auto challenge = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        unsigned char digest[CC_SHA1_DIGEST_LENGTH];
        CC_SHA1(challenge.toRawUTF8(), static_cast<CC_LONG>(challenge.getNumBytesAsUTF8()), digest);
        const auto response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
            + juce::Base64::toBase64(digest, sizeof(digest)) + "\r\n\r\n";
        c.raw(std::vector<uint8_t>(response.toRawUTF8(), response.toRawUTF8() + response.getNumBytesAsUTF8()));
        c.upgraded = true;
        c.input.erase(c.input.begin(), c.input.begin() + static_cast<std::ptrdiff_t>(end + 4));
        c.send(object({ {"type", "state"}, {"version", 1}, {"revision", revision}, {"state", state} }));
    }
    void frames(Client& c)
    {
        int budget = 64;
        while (!c.closing && c.input.size() >= 2 && --budget >= 0)
        {
            const auto* p = c.input.data();
            const bool final = (p[0] & 0x80) != 0;
            const int opcode = p[0] & 15;
            if ((p[0] & 0x70) != 0 || (p[1] & 0x80) == 0) { c.close(1002); break; }
            uint64_t length = p[1] & 127; size_t header = 2;
            if (length == 126)
            {
                if (c.input.size() < 4) break;
                length = (static_cast<uint64_t>(p[2]) << 8) | p[3]; header = 4;
                if (length < 126) { c.close(1002); break; }
            }
            else if (length == 127)
            {
                if (c.input.size() < 10) break;
                length = 0; for (int i = 2; i < 10; ++i) length = (length << 8) | p[i]; header = 10;
                if (length <= 65535 || (p[2] & 0x80) != 0) { c.close(1002); break; }
            }
            if (length > maximumPayload || (opcode < 8 && c.fragment.size() + length > maximumPayload)) { c.close(1009); break; }
            if (opcode >= 8 && (!final || length > 125)) { c.close(1002); break; }
            if (c.input.size() < header + 4 + length) break;
            std::vector<uint8_t> payload(static_cast<size_t>(length));
            for (size_t i = 0; i < payload.size(); ++i) payload[i] = p[header + 4 + i] ^ p[header + i % 4];
            c.input.erase(c.input.begin(), c.input.begin() + static_cast<std::ptrdiff_t>(header + 4 + length));
            if (opcode == 8)
            {
                if (payload.size() == 1) { c.close(1002); break; }
                if (payload.size() >= 2)
                {
                    const auto code = (static_cast<unsigned>(payload[0]) << 8) | payload[1];
                    const bool valid = (code >= 1000 && code <= 1014 && code != 1004 && code != 1005 && code != 1006)
                        || (code >= 3000 && code <= 4999);
                    if (!valid) { c.close(1002); break; }
                    if (!juce::CharPointer_UTF8::isValidString(reinterpret_cast<const char*>(payload.data() + 2), static_cast<int>(payload.size() - 2)))
                    { c.close(1007); break; }
                }
                c.close(1000); break;
            }
            if (opcode == 9) { c.frame(10, payload.data(), payload.size()); continue; }
            if (opcode == 10) { c.pong = true; continue; }
            if (opcode == 2) { c.close(1003); break; }
            if ((opcode != 0 && opcode != 1) || (opcode == 0 && !c.fragmented) || (opcode == 1 && c.fragmented)) { c.close(1002); break; }
            if (!final || opcode == 0)
            {
                c.fragmented = true;
                c.fragment.insert(c.fragment.end(), payload.begin(), payload.end());
                if (final) { request(c, c.fragment); c.fragment.clear(); c.fragmented = false; }
            }
            else request(c, payload);
        }
    }
    void tick()
    {
        for (int i = 0; i < 8; ++i)
        {
            const auto fd = ::accept(listener, nullptr, nullptr);
            if (fd < 0) break;
            if (clients.size() >= 16) { ::close(fd); continue; }
            nonblocking(fd); auto c = std::make_unique<Client>(); c->fd = fd; clients.push_back(std::move(c));
        }
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (auto& client : clients)
        {
            auto& c = *client;
            if (c.dead) continue;
            uint8_t buffer[16384];
            for (int i = 0; !c.closing && i < 16; ++i)
            {
                const auto n = ::recv(c.fd, buffer, sizeof(buffer), 0);
                if (n == 0) { c.dead = true; break; }
                if (n < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) c.dead = true; break; }
                c.input.insert(c.input.end(), buffer, buffer + n);
                if (c.input.size() > maximumPayload + 32) { c.close(1009); break; }
            }
            if (c.dead) continue;
            if (!c.upgraded && !c.closing) handshake(c);
            if (c.upgraded && !c.closing) frames(c);
            if (!c.upgraded && now - c.opened > 5000.0) c.dead = true;
            if (c.upgraded && now - c.heartbeat >= 10000.0)
            {
                if (!c.pong) c.dead = true;
                else { c.pong = false; c.heartbeat = now; c.frame(9, nullptr, 0); }
            }
            if (c.closing && c.closeStarted > 0.0 && now - c.closeStarted > 2000.0) c.dead = true;
            c.flush();
        }
        std::erase_if(clients, [](const auto& c) { return c->dead; });
    }
};
WebSocketServer::WebSocketServer() : impl(std::make_unique<Impl>(*this)) {}
WebSocketServer::~WebSocketServer() { stop(); }
juce::Result WebSocketServer::start()
{
    stop();
    impl->listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (impl->listener < 0) return juce::Result::fail("WebSocket socket unavailable");
    nonblocking(impl->listener);
    const int yes = 1; setsockopt(impl->listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address {}; address.sin_family = AF_INET; address.sin_port = htons(47321); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(impl->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(impl->listener, 8) != 0)
    {
        const auto reason = juce::String::fromUTF8(strerror(errno)); stop();
        return juce::Result::fail("WebSocket 127.0.0.1:47321: " + reason);
    }
    startTimer(10); juce::Logger::writeToLog("REMOTE listening 127.0.0.1:47321 protocol=1");
    return juce::Result::ok();
}
void WebSocketServer::stop()
{
    stopTimer(); impl->clients.clear();
    if (impl->listener >= 0) ::close(impl->listener);
    impl->listener = -1;
}
bool WebSocketServer::isListening() const noexcept { return impl->listener >= 0; }
void WebSocketServer::timerCallback() { impl->tick(); }
void WebSocketServer::update(const juce::var& state)
{
    auto changes = object({});
    for (const auto& field : state.getDynamicObject()->getProperties())
        if (juce::JSON::toString(impl->state[field.name], true) != juce::JSON::toString(field.value, true))
            changes.getDynamicObject()->setProperty(field.name, field.value);
    const bool initial = impl->state.isVoid();
    impl->state = state.clone();
    if (!initial && changes.getDynamicObject()->getProperties().size() > 0)
        impl->broadcast(object({ {"type", "event"}, {"version", 1}, {"revision", ++impl->revision}, {"changes", changes} }));
}
void WebSocketServer::completePending(const juce::var& error)
{
    for (auto& c : impl->clients)
        if (!c->pendingId.isVoid()) { impl->result(*c, c->pendingId, error); c->pendingId = juce::var(); }
}
void WebSocketServer::error(const juce::String& code, const juce::String& message)
{
    impl->broadcast(object({ {"type", "error"}, {"version", 1}, {"error", object({ {"code", code}, {"message", message} })} }));
}
juce::var WebSocketServer::failure(const juce::String& code)
{
    juce::String message = juce::String::fromUTF8("操作を処理できませんでした。");
    if (code == "INVALID_MESSAGE") message = juce::String::fromUTF8("要求の形式が正しくありません。");
    else if (code == "VERSION_MISMATCH") message = juce::String::fromUTF8("通信バージョンが一致しません。");
    else if (code == "INVALID_ARGUMENT") message = juce::String::fromUTF8("引数が正しくありません。");
    else if (code == "UNKNOWN_COMMAND") message = juce::String::fromUTF8("対応していない操作です。");
    else if (code == "EMPTY_SLOT") message = juce::String::fromUTF8("この番号には登録がありません。");
    else if (code == "BUSY") message = juce::String::fromUTF8("切り替え中です。少し待ってください。");
    else if (code == "LIMIT_REACHED") message = juce::String::fromUTF8("同時に再生できる効果音は16個までです。");
    else if (code == "TOO_QUIET") message = juce::String::fromUTF8("音が小さすぎるため学習できませんでした。");
    else if (code == "LOAD_FAILED") message = juce::String::fromUTF8("プロジェクトを読み込めませんでした。");
    return object({ {"code", code}, {"message", message} });
}
}
