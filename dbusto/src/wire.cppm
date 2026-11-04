module;
#include <rstd/enum.hpp>
export module dbusto:wire;
export import rstd;

using namespace rstd::prelude;
export namespace dbusto
{
using Bytes = rstd::bytes::Bytes;
class Error {
    RSTD_ENUM(Error,
              (InvalidSignature, (usize offset {};)),
              (InvalidMessage, (usize offset {}; String reason = String::make();)),
              (LimitExceeded),
              (Unsupported, (String feature = String::make();)),
              (Io, (rstd::sync::Arc<rstd::io::Error> source;)),
              (Authentication),
              (Closed),
              (Busy),
              (TimedOut),
              (Remote, (String name; String text;)))
public:
    static auto Io(rstd::io::Error source) -> Error {
        return Io(rstd::sync::Arc<rstd::io::Error>::make(rstd::move(source)));
    }
    auto message() const -> ref<str>;
    auto clone() const -> Error;
};
template<class T>
using Result = rstd::Result<T, Error>;

struct Limits {
    usize message_bytes { 8 * 1024 * 1024 };
    usize header_bytes { 65536 };
    usize values { 65536 };
    usize depth { 32 };
};
enum class Endian
{
    Little,
    Big
};
struct Type {
    char      code {};
    String    text     = String::make();
    Vec<Type> children = Vec<Type>::make();
    auto      alignment() const -> usize;
};
class Signature {
    String    text_;
    Vec<Type> types_;
    Signature(String text, Vec<Type> types);

public:
    static auto parse(ref<str> text) -> Result<Signature>;
    auto        text() const -> ref<str>;
    auto        types() const -> slice<Type>;
};
class Value {
    RSTD_ENUM(Value,
              (Byte, (u8 value;)),
              (Boolean, (bool value;)),
              (Int16, (i16 value;)),
              (UInt16, (u16 value;)),
              (Int32, (i32 value;)),
              (UInt32, (u32 value;)),
              (Int64, (i64 value;)),
              (UInt64, (u64 value;)),
              (Floating, (double value;)),
              (Text, (String value;)),
              (Path, (String value;)),
              (TypeString, (String value;)),
              (Octets, (Bytes value;)),
              (Array, (String element; Vec<Value> values;)),
              (Structure, (Vec<Value> values;)),
              (Entry, (Vec<Value> values;)),
              (Variant, (Box<Value> value;)))
public:
    auto clone() const -> Value;
    auto signature() const -> String;
};
using Body = Vec<Value>;
auto body_signature(const Body& body) -> String;
auto encode_body(const Body& body, Endian endian = Endian::Little, Limits limits = {})
    -> Result<Bytes>;
auto decode_body(ref<str> signature,
                 Bytes    bytes,
                 Endian   endian = Endian::Little,
                 Limits   limits = {}) -> Result<Body>;
auto valid_path(ref<str> text) -> bool;
auto valid_interface(ref<str> text) -> bool;
auto valid_member(ref<str> text) -> bool;
auto valid_bus_name(ref<str> text) -> bool;

enum class MessageType : unsigned char
{
    MethodCall   = 1,
    MethodReturn = 2,
    Error        = 3,
    Signal       = 4
};
struct Message {
    MessageType type { MessageType::MethodCall };
    Endian      endian { Endian::Little };
    u8          flags {};
    u32         serial {};
    u32         reply_serial {};
    String      path        = String::make();
    String      interface   = String::make();
    String      member      = String::make();
    String      error_name  = String::make();
    String      destination = String::make();
    String      sender      = String::make();
    Body        body        = Body::make();
    auto        clone() const -> Message;
    auto        expects_reply() const -> bool;
};
auto frame_size(slice<u8> prefix, Limits limits = {}) -> Result<Option<usize>>;
auto encode(const Message& message, Limits limits = {}) -> Result<Bytes>;
auto decode(Bytes wire, Limits limits = {}) -> Result<Message>;
} // namespace dbusto
export namespace rstd
{
template<>
struct Impl<fmt::Display, dbusto::Error> : ImplBase<dbusto::Error> {
    auto fmt(fmt::Formatter& f) const -> bool { return f.write_str(this->self().message()); }
};
} // namespace rstd
