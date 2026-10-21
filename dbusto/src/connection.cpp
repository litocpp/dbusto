module;
#include <rstd/macro.hpp>
module dbusto;
import :connection;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace dbusto
{
namespace async = rstd::async;
using rstd::sync::Arc;
auto hex_digit(unsigned char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
auto unescape(ref<str> text) -> Result<Bytes> {
    auto out = Vec<u8>::make();
    for (usize i; i < text.len(); ++i) {
        auto c = text.as_bytes()[i].to_primitive();
        if (c == '%') {
            if (i + usize(2) >= text.len())
                return Err(Error::InvalidMessage(i, String::make("invalid address escape"_str)));
            auto a = hex_digit(text.as_bytes()[++i].to_primitive());
            auto b = hex_digit(text.as_bytes()[++i].to_primitive());
            if (a < 0 || b < 0)
                return Err(Error::InvalidMessage(i, String::make("invalid address escape"_str)));
            c = static_cast<unsigned char>((a << 4) | b);
        }
        out.push(u8(c));
    }
    return Ok(Bytes::copy_from_slice(out.as_slice()));
}
auto valid_guid(ref<str> guid) -> bool {
    if (guid.len() != usize(32)) return false;
    for (auto c : guid.as_bytes())
        if (hex_digit(c.to_primitive()) < 0) return false;
    return true;
}
auto text_part(ref<str> text, usize start, Option<usize> length = {}) -> ref<str> {
    auto rest = text.split_at(start).get<1>();
    return length.is_some() ? rest.split_at(*length).get<0>() : rest;
}
auto text_split(ref<str> text, ref<str> separator) -> Vec<ref<str>> {
    auto out = Vec<ref<str>>::make();
    for (;;) {
        auto pair = text.split_once(separator);
        if (pair.is_none()) {
            out.push(ref<str>(text));
            return out;
        }
        out.push(ref<str>(pair->get<0>()));
        text = pair->get<1>();
    }
}
auto parse_addresses(ref<str> text) -> Result<Vec<Address>> {
    auto out = Vec<Address>::make();
    if (text.len() > usize(16384)) return Err(Error::LimitExceeded());
    for (auto alternative : text_split(text, ";"_str)) {
        if (! alternative.starts_with("unix:"_str)) continue;
        Address address;
        bool    found = false;
        for (auto pair : text_split(text_part(alternative, usize(5)), ","_str)) {
            auto equal = pair.find("="_str);
            if (equal.is_none())
                return Err(Error::InvalidMessage(usize(), String::make("invalid bus address"_str)));
            auto key   = text_part(pair, usize(), Some(*equal));
            auto value = text_part(pair, *equal + usize(1));
            if (key == "path"_str || key == "abstract"_str) {
                if (found)
                    return Err(
                        Error::InvalidMessage(usize(), String::make("duplicate Unix address"_str)));
                auto decoded = unescape(value);
                if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err());
                address.name     = rstd::move(decoded).unwrap();
                address.abstract = key == "abstract"_str;
                found            = true;
            } else if (key == "guid"_str) {
                if (! valid_guid(value) || ! address.guid.is_empty())
                    return Err(Error::Authentication());
                address.guid = String::make(value);
            } else
                return Err(Error::Unsupported(String::make(key)));
        }
        if (! found || address.name.is_empty())
            return Err(Error::InvalidMessage(usize(), String::make("missing Unix address"_str)));
        if (! address.abstract && address.name.as_slice()[usize()] != u8('/'))
            return Err(
                Error::InvalidMessage(usize(), String::make("Unix path must be absolute"_str)));
        if (! address.abstract)
            for (auto c : address.name.as_slice())
                if (c == u8())
                    return Err(
                        Error::InvalidMessage(usize(), String::make("NUL in Unix path"_str)));
        out.push(rstd::move(address));
        if (out.len() > usize(16)) return Err(Error::LimitExceeded());
    }
    if (out.is_empty()) return Err(Error::Unsupported(String::make("bus address transport"_str)));
    return Ok(rstd::move(out));
}
struct Pending {
    u32                                     serial;
    String                                  expected_sender;
    async::oneshot::Sender<Result<Message>> sender;
};
struct Outbound {
    Bytes                                 wire;
    async::oneshot::Sender<Result<empty>> sender;
};
struct ConnectionFields {
    Option<rstd::net::UnixStream> socket;
    String                        unique_name = String::make();
    Vec<Pending>                  pending     = Vec<Pending>::make();
    Vec<Message>                  incoming    = Vec<Message>::make();
    Vec<Outbound>                 outgoing    = Vec<Outbound>::make();
    u32                           serial { 1 };
    bool                          closed {};
    bool                          driving {};
};
struct ConnectionState {
    ConnectionOptions                   options;
    rstd::sync::Mutex<ConnectionFields> fields;
    async::Mutex<empty>                 writer { empty {} };
    async::Notify                       incoming;
    async::Notify                       outgoing;
    using Closing =
        decltype(async::into_future(rstd::move(rstd::declval<rstd::net::UnixStream&>()).close()));
    struct CloseState {
        Option<Closing>         future;
        bool                    done {};
        Option<rstd::io::Error> error;
    };
    async::Mutex<CloseState> closing { CloseState {} };
    ConnectionState(ConnectionOptions     options,
                    rstd::net::UnixStream socket,
                    String                name,
                    async::Notify         notify,
                    async::Notify         outgoing)
        : options(options),
          fields(ConnectionFields { Some(rstd::move(socket)), rstd::move(name) }),
          incoming(rstd::move(notify)),
          outgoing(rstd::move(outgoing)) {}
    void fail(const Error& error) {
        auto pending  = Vec<Pending>::make();
        auto outgoing = Vec<Outbound>::make();
        {
            auto fields      = this->fields.lock().unwrap();
            fields->closed   = true;
            pending          = rstd::move(fields->pending);
            fields->pending  = Vec<Pending>::make();
            outgoing         = rstd::move(fields->outgoing);
            fields->outgoing = Vec<Outbound>::make();
        }
        for (auto& entry : pending) (void)entry.sender.send(Err(error.clone()));
        for (auto& entry : outgoing) (void)entry.sender.send(Err(error.clone()));
        this->outgoing.notifier().notify();
        incoming.notifier().notify();
    }
};
auto closed_io() -> async::coro<rstd::io::Result<Bytes>> {
    co_return Err(
        rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::NotConnected }));
}
auto receive(const Arc<ConnectionState>& state, usize size)
    -> async::coro<rstd::io::Result<Bytes>> {
    auto fields = state->fields.lock().unwrap();
    if (fields->closed || fields->socket.is_none()) return closed_io();
    return fields->socket->recv(size);
}
auto read_exact(Arc<ConnectionState> state, usize size) -> async::coro<Result<Bytes>> {
    auto bytes = Vec<u8>::make();
    while (bytes.len() < size) {
        auto part = co_await receive(state, size - bytes.len());
        if (part.is_err()) co_return Err(Error::Io(rstd::move(part).unwrap_err()));
        if (part->is_empty()) co_return Err(Error::Closed());
        bytes.extend_from_slice(part->as_slice());
    }
    co_return Ok(Bytes::copy_from_slice(bytes.as_slice()));
}
auto read_message(Arc<ConnectionState> state) -> async::coro<Result<Message>> {
    auto prefix = co_await read_exact(state.clone(), usize(16));
    if (prefix.is_err()) co_return Err(rstd::move(prefix).unwrap_err());
    auto size = frame_size(prefix->as_slice(), state->options.limits);
    if (size.is_err()) co_return Err(rstd::move(size).unwrap_err());
    auto rest = co_await read_exact(state.clone(), **size - usize(16));
    if (rest.is_err()) co_return Err(rstd::move(rest).unwrap_err());
    auto wire = Vec<u8>::make();
    wire.extend_from_slice(prefix->as_slice());
    wire.extend_from_slice(rest->as_slice());
    co_return decode(Bytes::copy_from_slice(wire.as_slice()), state->options.limits);
}
auto close_state(Arc<ConnectionState> state) -> async::coro<Result<empty>> {
    state->fail(Error::Closed());
    auto closing = co_await state->closing.lock();
    if (! closing->done && closing->future.is_none()) {
        auto fields = state->fields.lock().unwrap();
        auto socket = fields->socket.take();
        if (socket.is_some()) {
            (void)socket->shutdown_both();
            closing->future = Some(async::into_future(rstd::move(*socket).close()));
        } else
            closing->done = true;
    }
    if (closing->future.is_some()) {
        auto result = co_await rstd::future::by_ref(*closing->future);
        if (result.is_err()) closing->error = Some(rstd::move(result).unwrap_err());
        closing->future = None();
        closing->done   = true;
    }
    if (closing->error.is_some()) co_return Err(Error::Io(*closing->error));
    co_return Ok(empty {});
}

struct WriteTicket {
    Arc<ConnectionState> state;
    bool                 complete {};
    ~WriteTicket() {
        if (! complete) state->fail(Error::Closed());
    }
};
auto write_bytes(Arc<ConnectionState> state, Bytes wire) -> async::coro<Result<empty>> {
    auto lock      = co_await state->writer.lock();
    auto operation = [&]() -> Option<async::coro<rstd::io::Result<usize>>> {
        auto fields = state->fields.lock().unwrap();
        if (fields->closed || fields->socket.is_none()) return None();
        return Some(fields->socket->send_all(rstd::move(wire)));
    }();
    if (operation.is_none()) co_return Err(Error::Closed());
    WriteTicket ticket { state.clone() };
    auto        result = co_await async::timeout(async::into_future(rstd::move(*operation)),
                                                 state->options.call_timeout);
    if (result.is_err()) co_return Err(Error::TimedOut());
    if (result->is_err()) co_return Err(Error::Io(rstd::move(*result).unwrap_err()));
    ticket.complete = true;
    co_return Ok(empty {});
}
auto write_message(Arc<ConnectionState> state, Message message) -> async::coro<Result<empty>> {
    auto wire = encode(message, state->options.limits);
    if (wire.is_err()) co_return Err(rstd::move(wire).unwrap_err());
    co_return co_await write_bytes(state.clone(), rstd::move(*wire));
}
auto send_owned(Arc<ConnectionState> state, Message message) -> async::coro<Result<empty>> {
    auto wire = encode(message, state->options.limits);
    if (wire.is_err()) co_return Err(rstd::move(wire).unwrap_err());
    auto channel = async::oneshot::channel<Result<empty>>();
    {
        auto fields = state->fields.lock().unwrap();
        if (fields->closed) co_return Err(Error::Closed());
        if (fields->outgoing.len() >= state->options.pending_calls) co_return Err(Error::Busy());
        fields->outgoing.push(Outbound { rstd::move(*wire), rstd::move(channel.get<0>()) });
    }
    state->outgoing.notifier().notify();
    auto result = co_await rstd::move(channel.get<1>());
    if (result.is_err()) co_return Err(Error::Closed());
    co_return rstd::move(*result);
}
auto write_loop(Arc<ConnectionState> state) -> async::coro<empty> {
    for (;;) {
        Option<Outbound> work;
        {
            auto fields = state->fields.lock().unwrap();
            if (fields->closed) break;
            if (! fields->outgoing.is_empty()) work = Some(fields->outgoing.remove(usize()));
        }
        if (work.is_none()) {
            co_await state->outgoing.notified();
            continue;
        }
        auto sent   = co_await write_bytes(state.clone(), rstd::move(work->wire));
        bool failed = sent.is_err();
        (void)work->sender.send(rstd::move(sent));
        if (failed) {
            (void)co_await close_state(state.clone());
            break;
        }
    }
    co_return empty {};
}
struct PendingTicket {
    Arc<ConnectionState> state;
    u32                  serial;
    ~PendingTicket() {
        auto fields = state->fields.lock().unwrap();
        for (usize i; i < fields->pending.len(); ++i)
            if (fields->pending[i].serial == serial) {
                fields->pending.remove(i);
                break;
            }
    }
};
auto next_serial(ConnectionFields& fields) -> u32 {
    for (;;) {
        fields.serial += u32(1);
        if (fields.serial == u32()) fields.serial = u32(1);
        bool used = false;
        for (const auto& entry : fields.pending)
            if (entry.serial == fields.serial) used = true;
        if (! used) return fields.serial;
    }
}
auto call_owned(Arc<ConnectionState> state, Message message) -> async::coro<Result<Message>> {
    auto channel = async::oneshot::channel<Result<Message>>();
    {
        auto fields = state->fields.lock().unwrap();
        if (fields->closed) co_return Err(Error::Closed());
        if (fields->pending.len() >= state->options.pending_calls) co_return Err(Error::Busy());
        message.type   = MessageType::MethodCall;
        message.flags  = u8();
        message.serial = next_serial(*fields);
        fields->pending.push(
            Pending { message.serial,
                      message.destination.as_str().starts_with(":"_str) ||
                              message.destination.as_str() == "org.freedesktop.DBus"_str
                          ? message.destination.clone()
                          : String::make(),
                      rstd::move(channel.get<0>()) });
    }
    PendingTicket ticket { state.clone(), message.serial };
    auto          sent = co_await send_owned(state.clone(), rstd::move(message));
    if (sent.is_err()) co_return Err(rstd::move(sent).unwrap_err());
    auto received =
        co_await async::timeout(rstd::move(channel.get<1>()), state->options.call_timeout);
    if (received.is_err()) co_return Err(Error::TimedOut());
    if (received->is_err()) co_return Err(Error::Closed());
    auto response = rstd::move(*received).unwrap();
    if (response.is_err()) co_return Err(rstd::move(response).unwrap_err());
    if (response->type == MessageType::Error) {
        auto text = String::make();
        if (! response->body.is_empty() && response->body[usize()].is_Text())
            text = response->body[usize()].as_Text().value.clone();
        co_return Err(Error::Remote(response->error_name.clone(), rstd::move(text)));
    }
    co_return response;
}
auto next_owned(Arc<ConnectionState> state) -> async::coro<Result<Message>> {
    for (;;) {
        {
            auto fields = state->fields.lock().unwrap();
            if (! fields->incoming.is_empty()) co_return Ok(fields->incoming.remove(usize()));
            if (fields->closed) co_return Err(Error::Closed());
        }
        co_await state->incoming.notified();
    }
}
struct DriveTicket {
    Arc<ConnectionState> state;
    ~DriveTicket() { state->fail(Error::Closed()); }
};
auto drive(Arc<ConnectionState> state) -> async::coro<Result<empty>> {
    {
        auto fields = state->fields.lock().unwrap();
        if (fields->driving || fields->closed) co_return Err(Error::Closed());
        fields->driving = true;
    }
    DriveTicket   ticket { state.clone() };
    auto          writer = async::AbortOnDropHandle(async::spawn(write_loop(state.clone())));
    Option<Error> failure;
    for (;;) {
        auto message = co_await read_message(state.clone());
        if (message.is_err()) {
            failure = Some(rstd::move(message).unwrap_err());
            break;
        }
        Option<async::oneshot::Sender<Result<Message>>> reply;
        bool                                            overflow = false;
        {
            auto fields = state->fields.lock().unwrap();
            if (message->type == MessageType::MethodReturn || message->type == MessageType::Error) {
                for (usize i; i < fields->pending.len(); ++i)
                    if (fields->pending[i].serial == message->reply_serial &&
                        (fields->pending[i].expected_sender.is_empty() ||
                         fields->pending[i].expected_sender == message->sender)) {
                        reply = Some(rstd::move(fields->pending.remove(i).sender));
                        break;
                    }
            } else if (fields->incoming.len() >= state->options.incoming_messages)
                overflow = true;
            else
                fields->incoming.push(rstd::move(*message));
        }
        if (overflow) {
            failure = Some(Error::Busy());
            break;
        }
        if (reply.is_some())
            (void)reply->send(Ok(rstd::move(*message)));
        else
            state->incoming.notifier().notify();
    }
    state->fail(*failure);
    auto closed = co_await close_state(state.clone());
    (void)closed;
    (void)co_await rstd::move(writer);
    co_return Err(rstd::move(*failure));
}
Connection::Connection(Arc<ConnectionState> state): state_(rstd::move(state)) {
}
Connection::Connection(Connection&&) noexcept                    = default;
auto Connection::operator=(Connection&&) noexcept -> Connection& = default;
Connection::~Connection()                                        = default;
auto Connection::clone() const -> Connection {
    return Connection(state_.clone());
}
auto Connection::unique_name() const -> String {
    auto fields = state_->fields.lock().unwrap();
    return fields->unique_name.clone();
}
auto Connection::call(Message message) const -> async::coro<Result<Message>> {
    return call_owned(state_.clone(), rstd::move(message));
}
auto Connection::send(Message message) const -> async::coro<Result<empty>> {
    {
        auto fields    = state_->fields.lock().unwrap();
        message.serial = next_serial(*fields);
    }
    return send_owned(state_.clone(), rstd::move(message));
}
auto Connection::next() const -> async::coro<Result<Message>> {
    return next_owned(state_.clone());
}
auto Connection::close() const -> async::coro<Result<empty>> {
    return close_state(state_.clone());
}
Driver::Driver(Arc<ConnectionState> state): state_(rstd::move(state)) {
}
Driver::Driver(Driver&&) noexcept                    = default;
auto Driver::operator=(Driver&&) noexcept -> Driver& = default;
Driver::~Driver() {
    if (state_) state_->fail(Error::Closed());
}
auto Driver::run() && -> async::coro<Result<empty>> {
    return drive(rstd::move(state_));
}
auto method_call(ref<str> destination,
                 ref<str> path,
                 ref<str> interface,
                 ref<str> member,
                 Body     body) -> Message {
    Message out;
    out.destination = String::make(destination);
    out.path        = String::make(path);
    out.interface   = String::make(interface);
    out.member      = String::make(member);
    out.body        = rstd::move(body);
    return out;
}
auto method_return(const Message& call, Body body) -> Message {
    Message out;
    out.type         = MessageType::MethodReturn;
    out.reply_serial = call.serial;
    out.destination  = call.sender.clone();
    out.body         = rstd::move(body);
    return out;
}
auto method_error(const Message& call, ref<str> name, ref<str> text) -> Message {
    auto out       = method_return(call);
    out.type       = MessageType::Error;
    out.error_name = String::make(name);
    out.body.push(Value::Text(String::make(text)));
    return out;
}
auto bus_call(ref<str> method, Body body = Body::make()) -> Message {
    return method_call("org.freedesktop.DBus"_str,
                       "/org/freedesktop/DBus"_str,
                       "org.freedesktop.DBus"_str,
                       method,
                       rstd::move(body));
}
auto name_owned(Connection connection, String name) -> async::coro<Result<empty>> {
    if (! valid_bus_name(name.as_str()))
        co_return Err(
            Error::InvalidMessage(usize(), String::make("invalid requested bus name"_str)));
    auto body = Body::make();
    body.push(Value::Text(rstd::move(name)));
    body.push(Value::UInt32(u32(4)));
    auto result = co_await connection.call(bus_call("RequestName"_str, rstd::move(body)));
    if (result.is_err()) co_return Err(rstd::move(result).unwrap_err());
    if (body_signature(result->body).as_str() != "u"_str)
        co_return Err(
            Error::InvalidMessage(usize(), String::make("invalid RequestName reply"_str)));
    auto status = result->body[usize()].as_UInt32().value;
    if (status != u32(1) && status != u32(4)) co_return Err(Error::Busy());
    co_return Ok(empty {});
}
auto Connection::request_name(ref<str> name) const -> async::coro<Result<empty>> {
    return name_owned(clone(), String::make(name));
}
auto match_owned(Connection connection, String rule) -> async::coro<Result<empty>> {
    auto body = Body::make();
    body.push(Value::Text(rstd::move(rule)));
    auto result = co_await connection.call(bus_call("AddMatch"_str, rstd::move(body)));
    if (result.is_err()) co_return Err(rstd::move(result).unwrap_err());
    co_return Ok(empty {});
}
auto Connection::add_match(ref<str> rule) const -> async::coro<Result<empty>> {
    return match_owned(clone(), String::make(rule));
}
auto uid_owned(Connection connection, String name) -> async::coro<Result<u32>> {
    if (! valid_bus_name(name.as_str())) co_return Err(Error::Authentication());
    auto body = Body::make();
    body.push(Value::Text(rstd::move(name)));
    auto result = co_await connection.call(bus_call("GetConnectionUnixUser"_str, rstd::move(body)));
    if (result.is_err()) co_return Err(rstd::move(result).unwrap_err());
    if (body_signature(result->body).as_str() != "u"_str) co_return Err(Error::Authentication());
    co_return Ok(result->body[usize()].as_UInt32().value);
}
auto Connection::peer_uid(ref<str> name) const -> async::coro<Result<u32>> {
    return uid_owned(clone(), String::make(name));
}
auto authenticate(rstd::net::UnixStream& socket, ref<str> expected_guid)
    -> async::coro<Result<empty>> {
    auto        uid    = rstd::format("{}", rstd::os::unix::process::current_uid());
    auto        auth   = String::make("\0AUTH EXTERNAL "_str);
    const char* digits = "0123456789abcdef";
    for (auto c : uid.as_str().as_bytes()) {
        auto byte = c.to_primitive();
        auth.push(digits[byte >> 4]);
        auth.push(digits[byte & 15]);
    }
    auth.push_str("\r\n"_str);
    auto sent = co_await socket.send_all(Bytes::copy_from_slice(auth.as_str().as_bytes()));
    if (sent.is_err()) co_return Err(Error::Io(rstd::move(sent).unwrap_err()));
    auto line = String::make();
    for (;;) {
        auto received = co_await socket.recv(usize(1));
        if (received.is_err()) co_return Err(Error::Io(rstd::move(received).unwrap_err()));
        if (received->is_empty()) co_return Err(Error::Closed());
        line.push(char(received->as_slice()[usize()].to_primitive()));
        if (line.len() > usize(4096)) co_return Err(Error::LimitExceeded());
        if (line.as_str().ends_with("\r\n"_str)) break;
    }
    if (! line.as_str().starts_with("OK "_str)) co_return Err(Error::Authentication());
    auto guid = text_part(line.as_str(), usize(3), Some(line.len() - usize(5)));
    if (! valid_guid(guid) || (! expected_guid.is_empty() && expected_guid != guid))
        co_return Err(Error::Authentication());
    sent = co_await socket.send_all(Bytes::copy_from_slice("BEGIN\r\n"_bytes));
    if (sent.is_err()) co_return Err(Error::Io(rstd::move(sent).unwrap_err()));
    co_return Ok(empty {});
}
auto connect_one(Address address, ConnectionOptions options) -> async::coro<Result<Connected>> {
    auto socket =
        co_await rstd::net::UnixStream::connect(rstd::move(address.name), address.abstract);
    if (socket.is_err()) co_return Err(Error::Io(rstd::move(socket).unwrap_err()));
    auto authenticated = co_await authenticate(*socket, address.guid.as_str());
    if (authenticated.is_err()) co_return Err(rstd::move(authenticated).unwrap_err());
    auto notify = async::Notify::make();
    if (notify.is_err()) co_return Err(Error::Io(rstd::move(notify).unwrap_err()));
    auto outgoing = async::Notify::make();
    if (outgoing.is_err()) co_return Err(Error::Io(rstd::move(outgoing).unwrap_err()));
    auto state = Arc<ConnectionState>::make(
        options, rstd::move(*socket), String::make(), rstd::move(*notify), rstd::move(*outgoing));
    auto hello   = bus_call("Hello"_str);
    hello.serial = u32(1);
    auto sent    = co_await write_message(state.clone(), rstd::move(hello));
    if (sent.is_err()) co_return Err(rstd::move(sent).unwrap_err());
    for (;;) {
        auto response = co_await read_message(state.clone());
        if (response.is_err()) co_return Err(rstd::move(response).unwrap_err());
        if (response->type == MessageType::Signal) {
            auto fields = state->fields.lock().unwrap();
            if (fields->incoming.len() >= options.incoming_messages) co_return Err(Error::Busy());
            fields->incoming.push(rstd::move(*response));
            continue;
        }
        if (response->type != MessageType::MethodReturn || response->reply_serial != u32(1) ||
            body_signature(response->body).as_str() != "s"_str)
            co_return Err(Error::Authentication());
        auto name = response->body[usize()].as_Text().value.clone();
        if (! valid_bus_name(name.as_str()) || ! name.as_str().starts_with(":"_str))
            co_return Err(Error::Authentication());
        {
            auto fields         = state->fields.lock().unwrap();
            fields->unique_name = rstd::move(name);
        }
        co_return Ok(Connected { Connection(state.clone()), Driver(rstd::move(state)) });
    }
}
auto connect_owned(String address, ConnectionOptions options) -> async::coro<Result<Connected>> {
    auto addresses = parse_addresses(address.as_str());
    if (addresses.is_err()) co_return Err(rstd::move(addresses).unwrap_err());
    auto last = Error::Closed();
    for (auto& item : *addresses) {
        auto result = co_await async::timeout(
            async::into_future(connect_one(rstd::move(item), options)), options.setup_timeout);
        if (result.is_err()) {
            last = Error::TimedOut();
            continue;
        }
        if (result->is_ok()) co_return rstd::move(*result);
        last = rstd::move(*result).unwrap_err();
    }
    co_return Err(rstd::move(last));
}
auto connect(ref<str> address, ConnectionOptions options) -> async::coro<Result<Connected>> {
    return connect_owned(String::make(address), options);
}
} // namespace dbusto
