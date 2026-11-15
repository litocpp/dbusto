export module dbusto:connection;
export import :wire;
using namespace rstd::prelude;
export namespace dbusto
{
struct Address {
    Bytes  name;
    bool   abstract {};
    String guid = String::make();
};
auto parse_addresses(ref<str> text) -> Result<Vec<Address>>;
struct ConnectionOptions {
    Limits               limits;
    usize                pending_calls { 128 };
    usize                incoming_messages { 256 };
    rstd::time::Duration setup_timeout = rstd::time::Duration::from_secs(u64(5));
    rstd::time::Duration call_timeout  = rstd::time::Duration::from_secs(u64(25));
};
struct ConnectionState;
enum class NameClaim
{
    PrimaryOwner,
    AlreadyOwner,
    Exists
};
class Connection {
    rstd::sync::Arc<ConnectionState> state_;

public:
    explicit Connection(rstd::sync::Arc<ConnectionState> state);
    Connection(Connection&&) noexcept;
    auto operator=(Connection&&) noexcept -> Connection&;
    Connection(const Connection&) = delete;
    ~Connection();
    auto clone() const -> Connection;
    auto unique_name() const -> String;
    auto send(Message message) const -> rstd::async::coro<Result<empty>>;
    auto call(Message message) const -> rstd::async::coro<Result<Message>>;
    /// One consumer receives incoming method calls and signals.
    auto next() const -> rstd::async::coro<Result<Message>>;
    auto close() const -> rstd::async::coro<Result<empty>>;
    // Atomically claim with DO_NOT_QUEUE, without allowing or requesting replacement.
    auto claim_name(ref<str> name) const -> rstd::async::coro<Result<NameClaim>>;
    auto request_name(ref<str> name) const -> rstd::async::coro<Result<empty>>;
    auto add_match(ref<str> rule) const -> rstd::async::coro<Result<empty>>;
    auto peer_uid(ref<str> unique_name) const -> rstd::async::coro<Result<u32>>;
};
class Driver {
    rstd::sync::Arc<ConnectionState> state_;

public:
    explicit Driver(rstd::sync::Arc<ConnectionState> state);
    Driver(Driver&&) noexcept;
    auto operator=(Driver&&) noexcept -> Driver&;
    ~Driver();
    Driver(const Driver&) = delete;
    auto run() && -> rstd::async::coro<Result<empty>>;
};
struct Connected {
    Connection connection;
    Driver     driver;
};
auto connect(ref<str> address, ConnectionOptions options = {})
    -> rstd::async::coro<Result<Connected>>;
auto method_call(ref<str> destination,
                 ref<str> path,
                 ref<str> interface,
                 ref<str> member,
                 Body     body = Body::make()) -> Message;
auto method_return(const Message& call, Body body = Body::make()) -> Message;
auto method_error(const Message& call, ref<str> name, ref<str> text) -> Message;
} // namespace dbusto
