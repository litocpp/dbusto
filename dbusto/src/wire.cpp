module;
#include <rstd/enum.hpp>
module dbusto;
import :wire;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace dbusto
{
auto Error::message() const -> ref<str> {
    RSTD_MATCH(*this) {
        RSTD_CASE(InvalidSignature) {
            return "invalid D-Bus signature"_str;
        }
        RSTD_CASE(InvalidMessage, offset, reason) {
            return reason.is_empty() ? "invalid D-Bus message"_str : reason.as_str();
        }
        RSTD_CASE(LimitExceeded) {
            return "D-Bus resource limit exceeded"_str;
        }
        RSTD_CASE(Unsupported) {
            return "unsupported D-Bus capability"_str;
        }
        RSTD_CASE(Io) {
            return "D-Bus I/O failed"_str;
        }
        RSTD_CASE(Authentication) {
            return "D-Bus authentication failed"_str;
        }
        RSTD_CASE(Closed) {
            return "D-Bus connection closed"_str;
        }
        RSTD_CASE(Busy) {
            return "D-Bus queue full"_str;
        }
        RSTD_CASE(TimedOut) {
            return "D-Bus call timed out"_str;
        }
        RSTD_CASE(Remote, name, text) {
            return text.as_str();
        }
    }
    rstd::unreachable();
}
auto Error::clone() const -> Error {
    RSTD_MATCH(*this) {
        RSTD_CASE(InvalidSignature, offset) {
            return InvalidSignature(offset);
        }
        RSTD_CASE(InvalidMessage, offset, reason) {
            return InvalidMessage(offset, reason.clone());
        }
        RSTD_CASE(LimitExceeded) {
            return LimitExceeded();
        }
        RSTD_CASE(Unsupported, feature) {
            return Unsupported(feature.clone());
        }
        RSTD_CASE(Io, source) {
            return Io(source);
        }
        RSTD_CASE(Authentication) {
            return Authentication();
        }
        RSTD_CASE(Closed) {
            return Closed();
        }
        RSTD_CASE(Busy) {
            return Busy();
        }
        RSTD_CASE(TimedOut) {
            return TimedOut();
        }
        RSTD_CASE(Remote, name, text) {
            return Remote(name.clone(), text.clone());
        }
    }
    rstd::unreachable();
}
auto bad(usize offset, ref<str> reason) -> Error {
    return Error::InvalidMessage(offset, String::make(reason));
}
auto clone_body(const Body& values) -> Body {
    auto result = Body::make();
    for (const auto& value : values) result.push(value.clone());
    return result;
}
auto Value::clone() const -> Value {
    RSTD_MATCH(*this) {
        RSTD_CASE(Byte, x) {
            return Byte(x);
        }
        RSTD_CASE(Boolean, x) {
            return Boolean(x);
        }
        RSTD_CASE(Int16, x) {
            return Int16(x);
        }
        RSTD_CASE(UInt16, x) {
            return UInt16(x);
        }
        RSTD_CASE(Int32, x) {
            return Int32(x);
        }
        RSTD_CASE(UInt32, x) {
            return UInt32(x);
        }
        RSTD_CASE(Int64, x) {
            return Int64(x);
        }
        RSTD_CASE(UInt64, x) {
            return UInt64(x);
        }
        RSTD_CASE(Floating, x) {
            return Floating(x);
        }
        RSTD_CASE(Text, x) {
            return Text(x.clone());
        }
        RSTD_CASE(Path, x) {
            return Path(x.clone());
        }
        RSTD_CASE(TypeString, x) {
            return TypeString(x.clone());
        }
        RSTD_CASE(Octets, x) {
            return Octets(x.clone());
        }
        RSTD_CASE(Array, element, values) {
            return Array(element.clone(), clone_body(values));
        }
        RSTD_CASE(Structure, values) {
            return Structure(clone_body(values));
        }
        RSTD_CASE(Entry, values) {
            return Entry(clone_body(values));
        }
        RSTD_CASE(Variant, x) {
            return Variant(Box<Value>::make(x->clone()));
        }
    }
    rstd::unreachable();
}
auto body_signature(const Body& body) -> String {
    auto out = String::make();
    for (const auto& value : body) out.push_str(value.signature().as_str());
    return out;
}
auto Value::signature() const -> String {
    RSTD_MATCH(*this) {
        RSTD_CASE(Byte) {
            return String::make("y"_str);
        }
        RSTD_CASE(Boolean) {
            return String::make("b"_str);
        }
        RSTD_CASE(Int16) {
            return String::make("n"_str);
        }
        RSTD_CASE(UInt16) {
            return String::make("q"_str);
        }
        RSTD_CASE(Int32) {
            return String::make("i"_str);
        }
        RSTD_CASE(UInt32) {
            return String::make("u"_str);
        }
        RSTD_CASE(Int64) {
            return String::make("x"_str);
        }
        RSTD_CASE(UInt64) {
            return String::make("t"_str);
        }
        RSTD_CASE(Floating) {
            return String::make("d"_str);
        }
        RSTD_CASE(Text) {
            return String::make("s"_str);
        }
        RSTD_CASE(Path) {
            return String::make("o"_str);
        }
        RSTD_CASE(TypeString) {
            return String::make("g"_str);
        }
        RSTD_CASE(Octets) {
            return String::make("ay"_str);
        }
        RSTD_CASE(Array, element, _values) {
            return rstd::format("a{}", element);
        }
        RSTD_CASE(Structure, values) {
            return rstd::format("({})", body_signature(values));
        }
        RSTD_CASE(Entry, values) {
            return rstd::format("{{{}}}", body_signature(values));
        }
        RSTD_CASE(Variant) {
            return String::make("v"_str);
        }
    }
    rstd::unreachable();
}
auto Type::alignment() const -> usize {
    switch (code) {
    case 'y':
    case 'g':
    case 'v': return usize(1);
    case 'n':
    case 'q': return usize(2);
    case 'b':
    case 'i':
    case 'u':
    case 's':
    case 'o':
    case 'a':
    case 'h': return usize(4);
    default: return usize(8);
    }
}
auto basic(char c) -> bool {
    return c == 'y' || c == 'b' || c == 'n' || c == 'q' || c == 'i' || c == 'u' || c == 'x' ||
           c == 't' || c == 'd' || c == 's' || c == 'o' || c == 'g' || c == 'h';
}
class SignatureReader {
    ref<str> input_;
    usize    cursor_ {};

public:
    explicit SignatureReader(ref<str> input): input_(input) {}
    auto cursor() const -> usize { return cursor_; }
    auto type(usize depth, bool array_element = false) -> Result<Type> {
        auto start = cursor_;
        if (cursor_ == input_.len() || depth > usize(32))
            return Err(Error::InvalidSignature(cursor_));
        Type out;
        out.code = char(input_.as_bytes()[cursor_++].to_primitive());
        if (out.code == 'a') {
            auto child = type(depth + usize(1), true);
            if (child.is_err()) return Err(rstd::move(child).unwrap_err());
            out.children.push(rstd::move(child).unwrap());
        } else if (out.code == '(' || out.code == '{') {
            if (out.code == '{' && ! array_element) return Err(Error::InvalidSignature(start));
            auto end = out.code == '(' ? ')' : '}';
            while (cursor_ < input_.len() &&
                   char(input_.as_bytes()[cursor_].to_primitive()) != end) {
                auto child = type(depth + usize(1));
                if (child.is_err()) return Err(rstd::move(child).unwrap_err());
                out.children.push(rstd::move(child).unwrap());
            }
            if (cursor_ == input_.len() || out.children.is_empty())
                return Err(Error::InvalidSignature(cursor_));
            ++cursor_;
            if (out.code == '{' &&
                (out.children.len() != usize(2) || ! basic(out.children[usize()].code)))
                return Err(Error::InvalidSignature(start));
        } else if (! basic(out.code) && out.code != 'v')
            return Err(Error::InvalidSignature(start));
        auto text = String::make();
        for (auto i = start; i < cursor_; ++i) text.push(char(input_.as_bytes()[i].to_primitive()));
        out.text = rstd::move(text);
        return Ok(rstd::move(out));
    }
};
Signature::Signature(String text, Vec<Type> types)
    : text_(rstd::move(text)), types_(rstd::move(types)) {
}
auto Signature::parse(ref<str> text) -> Result<Signature> {
    if (text.len() > usize(255)) return Err(Error::InvalidSignature(usize(255)));
    SignatureReader reader(text);
    auto            types = Vec<Type>::make();
    while (reader.cursor() < text.len()) {
        auto parsed = reader.type(usize());
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
        types.push(rstd::move(parsed).unwrap());
    }
    return Ok(Signature(String::make(text), rstd::move(types)));
}
auto Signature::text() const -> ref<str> {
    return text_.as_str();
}
auto Signature::types() const -> slice<Type> {
    return types_.as_slice();
}

auto ident_char(unsigned char c, bool digit = true, bool dash = false) -> bool {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (digit && c >= '0' && c <= '9') || (dash && c == '-');
}
auto valid_path(ref<str> text) -> bool {
    if (text.is_empty() || text.as_bytes()[usize()] != u8('/')) return false;
    if (text.len() == usize(1)) return true;
    bool empty = true;
    for (usize i(1); i < text.len(); ++i) {
        auto c = text.as_bytes()[i].to_primitive();
        if (c == '/') {
            if (empty) return false;
            empty = true;
        } else {
            if (! ident_char(c)) return false;
            empty = false;
        }
    }
    return ! empty;
}
auto valid_member(ref<str> text) -> bool {
    if (text.is_empty() || text.len() > usize(255)) return false;
    for (usize i; i < text.len(); ++i)
        if (! ident_char(text.as_bytes()[i].to_primitive(), i != usize())) return false;
    return true;
}
auto dotted_name(ref<str> text, bool bus) -> bool {
    if (text.is_empty() || text.len() > usize(255)) return false;
    bool unique = bus && text.as_bytes()[usize()] == u8(':');
    bool first = true, dot = false;
    for (usize i(unique ? 1 : 0); i < text.len(); ++i) {
        auto c = text.as_bytes()[i].to_primitive();
        if (c == '.') {
            if (first) return false;
            first = true;
            dot   = true;
        } else {
            if (! ident_char(c, ! first || unique, bus)) return false;
            first = false;
        }
    }
    return dot && ! first;
}
auto valid_interface(ref<str> text) -> bool {
    return dotted_name(text, false);
}
auto valid_bus_name(ref<str> text) -> bool {
    return dotted_name(text, true);
}

class Encoder {
    Vec<u8>       data_ = Vec<u8>::make();
    Endian        endian_;
    Limits        limits_;
    usize         nodes_ {};
    Option<Error> error_;

public:
    Encoder(Endian endian, Limits limits): endian_(endian), limits_(limits) {}
    auto size() const -> usize { return data_.len(); }
    void fail(ref<str> reason) {
        if (error_.is_none()) error_ = Some(bad(size(), reason));
    }
    void byte(unsigned char value) {
        if (data_.len() >= limits_.message_bytes) {
            if (error_.is_none()) error_ = Some(Error::LimitExceeded());
            return;
        }
        data_.push(u8(value));
    }
    void align(usize alignment) {
        while (error_.is_none() && size() % alignment != usize()) byte(0);
    }
    void number(rstd::uint64_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            byte(static_cast<unsigned char>(value >>
                                            (8 * (endian_ == Endian::Little ? i : count - i - 1))));
    }
    void patch(usize offset, usize value) {
        if (offset + usize(4) > size()) {
            fail("invalid length patch"_str);
            return;
        }
        for (unsigned i = 0; i < 4; ++i)
            data_[offset + usize(i)] = u8(static_cast<unsigned char>(
                value.to_primitive() >> (8 * (endian_ == Endian::Little ? i : 3 - i))));
    }
    void raw(slice<u8> data) {
        for (auto c : data) byte(c.to_primitive());
    }
    void text(ref<str> text, bool signature) {
        for (auto c : text.as_bytes())
            if (c == u8()) {
                fail("embedded NUL in string"_str);
                return;
            }
        if (signature && text.len() > usize(255)) {
            fail("signature too long"_str);
            return;
        }
        if (! signature) align(usize(4));
        number(text.len().to_primitive(), signature ? 1 : 4);
        raw(text.as_bytes());
        byte(0);
    }
    void value(const Value& value, usize depth = {}) {
        if (error_.is_some()) return;
        if (++nodes_ > limits_.values || depth > limits_.depth) {
            error_ = Some(Error::LimitExceeded());
            return;
        }
        RSTD_MATCH(value) {
            RSTD_CASE(Byte, x) {
                byte(x.to_primitive());
            }
            RSTD_CASE(Boolean, x) {
                align(usize(4));
                number(x ? 1 : 0, 4);
            }
            RSTD_CASE(Int16, x) {
                align(usize(2));
                number(static_cast<rstd::uint16_t>(x.to_primitive()), 2);
            }
            RSTD_CASE(UInt16, x) {
                align(usize(2));
                number(x.to_primitive(), 2);
            }
            RSTD_CASE(Int32, x) {
                align(usize(4));
                number(static_cast<rstd::uint32_t>(x.to_primitive()), 4);
            }
            RSTD_CASE(UInt32, x) {
                align(usize(4));
                number(x.to_primitive(), 4);
            }
            RSTD_CASE(Int64, x) {
                align(usize(8));
                number(static_cast<rstd::uint64_t>(x.to_primitive()), 8);
            }
            RSTD_CASE(UInt64, x) {
                align(usize(8));
                number(x.to_primitive(), 8);
            }
            RSTD_CASE(Floating, x) {
                align(usize(8));
                number(__builtin_bit_cast(rstd::uint64_t, x), 8);
            }
            RSTD_CASE(Text, x) {
                text(x.as_str(), false);
            }
            RSTD_CASE(Path, x) {
                if (! valid_path(x.as_str())) fail("invalid object path"_str);
                text(x.as_str(), false);
            }
            RSTD_CASE(TypeString, x) {
                if (Signature::parse(x.as_str()).is_err()) fail("invalid signature value"_str);
                text(x.as_str(), true);
            }
            RSTD_CASE(Octets, x) {
                align(usize(4));
                number(x.len().to_primitive(), 4);
                raw(x.as_slice());
            }
            RSTD_CASE(Array, element, values) {
                auto parsed = Signature::parse(rstd::format("a{}", element).as_str());
                if (parsed.is_err() || parsed->types().len() != usize(1)) {
                    fail("invalid array element signature"_str);
                    return;
                }
                align(usize(4));
                auto length = size();
                number(0, 4);
                align(parsed->types()[usize()].children[usize()].alignment());
                auto begin = size();
                for (const auto& child : values) {
                    if (child.signature() != element) {
                        fail("array element type mismatch"_str);
                        return;
                    }
                    this->value(child, depth + usize(1));
                }
                patch(length, size() - begin);
            }
            RSTD_CASE(Structure, values) {
                align(usize(8));
                for (const auto& child : values) this->value(child, depth + usize(1));
            }
            RSTD_CASE(Entry, values) {
                if (values.len() != usize(2)) fail("invalid dictionary entry"_str);
                align(usize(8));
                for (const auto& child : values) this->value(child, depth + usize(1));
            }
            RSTD_CASE(Variant, x) {
                auto sig    = x->signature();
                auto parsed = Signature::parse(sig.as_str());
                if (parsed.is_err() || parsed->types().len() != usize(1)) {
                    fail("invalid variant signature"_str);
                    return;
                }
                text(sig.as_str(), true);
                this->value(*x, depth + usize(1));
            }
        }
    }
    auto finish() -> Result<Bytes> {
        if (error_.is_some()) return Err(error_.take().unwrap());
        return Ok(Bytes::copy_from_slice(data_.as_slice()));
    }
};

class Decoder {
    Bytes         bytes_;
    Endian        endian_;
    Limits        limits_;
    usize         cursor_ {};
    usize         end_;
    usize         nodes_ {};
    Option<Error> error_;

public:
    Decoder(Bytes bytes, Endian endian, Limits limits, usize start = {})
        : bytes_(rstd::move(bytes)),
          endian_(endian),
          limits_(limits),
          cursor_(start),
          end_(bytes_.len()) {}
    auto cursor() const -> usize { return cursor_; }
    void fail(ref<str> reason) {
        if (error_.is_none()) error_ = Some(bad(cursor_, reason));
    }
    auto remaining() const -> usize { return end_ - cursor_; }
    auto byte() -> unsigned char {
        if (cursor_ == end_) {
            fail("truncated value"_str);
            return 0;
        }
        return bytes_.as_slice()[cursor_++].to_primitive();
    }
    void align(usize alignment) {
        while (error_.is_none() && cursor_ % alignment != usize())
            if (byte() != 0) fail("nonzero alignment padding"_str);
    }
    auto number(unsigned count) -> rstd::uint64_t {
        rstd::uint64_t out {};
        for (unsigned i = 0; i < count; ++i)
            out |= static_cast<rstd::uint64_t>(byte())
                   << (8 * (endian_ == Endian::Little ? i : count - i - 1));
        return out;
    }
    auto text(bool signature) -> String {
        if (! signature) align(usize(4));
        auto length = usize(number(signature ? 1 : 4));
        if (error_.is_some() || length >= remaining()) {
            fail("truncated string"_str);
            return String::make();
        }
        auto raw = Vec<u8>::make();
        for (usize i; i < length; ++i) {
            auto c = byte();
            if (! c) fail("embedded NUL in string"_str);
            raw.push(u8(c));
        }
        if (byte() != 0) fail("missing string terminator"_str);
        auto converted = String::from_utf8(rstd::move(raw));
        if (converted.is_err()) {
            fail("invalid UTF-8"_str);
            return String::make();
        }
        return rstd::move(converted).unwrap();
    }
    auto value(const Type& type, usize depth = {}) -> Value {
        if (error_.is_some()) return Value::Byte(u8());
        if (++nodes_ > limits_.values || depth > limits_.depth) {
            error_ = Some(Error::LimitExceeded());
            return Value::Byte(u8());
        }
        align(type.alignment());
        switch (type.code) {
        case 'y': return Value::Byte(u8(byte()));
        case 'b': {
            auto n = number(4);
            if (n > 1) fail("invalid boolean"_str);
            return Value::Boolean(n != 0);
        }
        case 'n': return Value::Int16(i16(static_cast<rstd::int16_t>(number(2))));
        case 'q': return Value::UInt16(u16(static_cast<rstd::uint16_t>(number(2))));
        case 'i': return Value::Int32(i32(static_cast<rstd::int32_t>(number(4))));
        case 'u': return Value::UInt32(u32(static_cast<rstd::uint32_t>(number(4))));
        case 'x': return Value::Int64(i64(static_cast<rstd::int64_t>(number(8))));
        case 't': return Value::UInt64(u64(number(8)));
        case 'd': {
            auto n = number(8);
            return Value::Floating(__builtin_bit_cast(double, n));
        }
        case 's': return Value::Text(text(false));
        case 'o': {
            auto x = text(false);
            if (! valid_path(x.as_str())) fail("invalid object path"_str);
            return Value::Path(rstd::move(x));
        }
        case 'g': {
            auto x = text(true);
            if (Signature::parse(x.as_str()).is_err()) fail("invalid signature value"_str);
            return Value::TypeString(rstd::move(x));
        }
        case 'a': {
            auto        length  = usize(number(4));
            const auto& element = type.children[usize()];
            align(element.alignment());
            if (error_.is_some() || length > remaining()) {
                fail("truncated array"_str);
                return Value::Byte(u8());
            }
            auto outer_end = end_;
            end_           = cursor_ + length;
            if (element.code == 'y') {
                auto bytes = Vec<u8>::make();
                while (cursor_ < end_) bytes.push(u8(byte()));
                end_ = outer_end;
                return Value::Octets(Bytes::copy_from_slice(bytes.as_slice()));
            }
            auto children = Body::make();
            while (error_.is_none() && cursor_ < end_) {
                auto before = cursor_;
                children.push(value(element, depth + usize(1)));
                if (cursor_ == before) fail("array element made no progress"_str);
            }
            if (cursor_ != end_) fail("array length mismatch"_str);
            end_ = outer_end;
            return Value::Array(element.text.clone(), rstd::move(children));
        }
        case '(':
        case '{': {
            auto children = Body::make();
            for (const auto& element : type.children)
                children.push(value(element, depth + usize(1)));
            return type.code == '(' ? Value::Structure(rstd::move(children))
                                    : Value::Entry(rstd::move(children));
        }
        case 'v': {
            auto signature = text(true);
            auto parsed    = Signature::parse(signature.as_str());
            if (parsed.is_err() || parsed->types().len() != usize(1)) {
                fail("invalid variant signature"_str);
                return Value::Byte(u8());
            }
            return Value::Variant(
                Box<Value>::make(value(parsed->types()[usize()], depth + usize(1))));
        }
        default:
            error_ = Some(Error::Unsupported(String::make("Unix FD transfer"_str)));
            return Value::Byte(u8());
        }
    }
    auto result() -> Result<empty> {
        if (error_.is_some()) return Err(error_.take().unwrap());
        return Ok(empty {});
    }
};
auto encode_body(const Body& body, Endian endian, Limits limits) -> Result<Bytes> {
    auto parsed = Signature::parse(body_signature(body).as_str());
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    Encoder encoder(endian, limits);
    for (const auto& value : body) encoder.value(value);
    return encoder.finish();
}
auto decode_body(ref<str> signature, Bytes bytes, Endian endian, Limits limits) -> Result<Body> {
    if (bytes.len() > limits.message_bytes) return Err(Error::LimitExceeded());
    auto parsed = Signature::parse(signature);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    Decoder decoder(rstd::move(bytes), endian, limits);
    auto    values = Body::make();
    for (const auto& type : parsed->types()) values.push(decoder.value(type));
    auto checked = decoder.result();
    if (checked.is_err()) return Err(rstd::move(checked).unwrap_err());
    if (decoder.remaining() != usize())
        return Err(bad(decoder.cursor(), "trailing body bytes"_str));
    return Ok(rstd::move(values));
}
auto Message::clone() const -> Message {
    return { type,
             endian,
             flags,
             serial,
             reply_serial,
             path.clone(),
             interface.clone(),
             member.clone(),
             error_name.clone(),
             destination.clone(),
             sender.clone(),
             clone_body(body) };
}
auto Message::expects_reply() const -> bool {
    return type == MessageType::MethodCall && (flags & u8(1)) == u8();
}
auto message_valid(const Message& m) -> bool {
    auto t = static_cast<unsigned>(m.type);
    if (t < 1 || t > 4 || m.serial == u32()) return false;
    if (! m.path.is_empty() && ! valid_path(m.path.as_str())) return false;
    if (! m.interface.is_empty() && ! valid_interface(m.interface.as_str())) return false;
    if (! m.member.is_empty() && ! valid_member(m.member.as_str())) return false;
    if (! m.error_name.is_empty() && ! valid_interface(m.error_name.as_str())) return false;
    if (! m.destination.is_empty() && ! valid_bus_name(m.destination.as_str())) return false;
    if (! m.sender.is_empty() && ! valid_bus_name(m.sender.as_str())) return false;
    if ((m.type == MessageType::MethodCall || m.type == MessageType::Signal) &&
        (m.path.is_empty() || m.member.is_empty()))
        return false;
    if (m.type == MessageType::Signal && m.interface.is_empty()) return false;
    if ((m.type == MessageType::MethodReturn || m.type == MessageType::Error) &&
        m.reply_serial == u32())
        return false;
    return m.type != MessageType::Error || ! m.error_name.is_empty();
}
auto load_u32(slice<u8> bytes, usize offset, Endian endian) -> usize {
    rstd::uint32_t value {};
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<rstd::uint32_t>(bytes[offset + usize(i)].to_primitive())
                 << (8 * (endian == Endian::Little ? i : 3 - i));
    return usize(value);
}
auto frame_size(slice<u8> prefix, Limits limits) -> Result<Option<usize>> {
    if (prefix.len() < usize(16)) return Ok(None());
    if ((prefix[usize()] != u8('l') && prefix[usize()] != u8('B')) || prefix[usize(3)] != u8(1) ||
        prefix[usize(1)] < u8(1) || prefix[usize(1)] > u8(4))
        return Err(bad(usize(), "invalid fixed header"_str));
    auto endian = prefix[usize()] == u8('l') ? Endian::Little : Endian::Big;
    auto body = load_u32(prefix, usize(4), endian), fields = load_u32(prefix, usize(12), endian);
    if (fields > limits.header_bytes || body > limits.message_bytes)
        return Err(Error::LimitExceeded());
    auto header = (usize(16) + fields + usize(7)) & ~usize(7);
    if (header > limits.message_bytes || body > limits.message_bytes - header)
        return Err(Error::LimitExceeded());
    if (load_u32(prefix, usize(8), endian) == usize())
        return Err(bad(usize(8), "zero message serial"_str));
    return Ok(Some(header + body));
}
auto encode(const Message& message, Limits limits) -> Result<Bytes> {
    if (! message_valid(message)) return Err(bad(usize(), "invalid message fields"_str));
    auto body = encode_body(message.body, message.endian, limits);
    if (body.is_err()) return Err(rstd::move(body).unwrap_err());
    Encoder out(message.endian, limits);
    out.byte(message.endian == Endian::Little ? 'l' : 'B');
    out.byte(static_cast<unsigned char>(message.type));
    out.byte(message.flags.to_primitive());
    out.byte(1);
    out.number(body->len().to_primitive(), 4);
    out.number(message.serial.to_primitive(), 4);
    out.number(0, 4);
    auto field = [&](unsigned char code, Value value) {
        out.align(usize(8));
        out.byte(code);
        out.value(Value::Variant(Box<Value>::make(rstd::move(value))));
    };
    if (! message.path.is_empty()) field(1, Value::Path(message.path.clone()));
    if (! message.interface.is_empty()) field(2, Value::Text(message.interface.clone()));
    if (! message.member.is_empty()) field(3, Value::Text(message.member.clone()));
    if (! message.error_name.is_empty()) field(4, Value::Text(message.error_name.clone()));
    if (message.reply_serial != u32()) field(5, Value::UInt32(message.reply_serial));
    if (! message.destination.is_empty()) field(6, Value::Text(message.destination.clone()));
    if (! message.sender.is_empty()) field(7, Value::Text(message.sender.clone()));
    auto signature = body_signature(message.body);
    if (! signature.is_empty()) field(8, Value::TypeString(rstd::move(signature)));
    auto header_length = out.size() - usize(16);
    if (header_length > limits.header_bytes) return Err(Error::LimitExceeded());
    out.patch(usize(12), header_length);
    out.align(usize(8));
    out.raw(body->as_slice());
    return out.finish();
}
auto decode(Bytes wire, Limits limits) -> Result<Message> {
    auto expected = frame_size(wire.as_slice(), limits);
    if (expected.is_err()) return Err(rstd::move(expected).unwrap_err());
    if (expected->is_none() || **expected != wire.len())
        return Err(bad(usize(), "incomplete frame"_str));
    Message message;
    message.endian      = wire.as_slice()[usize()] == u8('l') ? Endian::Little : Endian::Big;
    message.type        = static_cast<MessageType>(wire.as_slice()[usize(1)].to_primitive());
    message.flags       = wire.as_slice()[usize(2)];
    message.serial      = u32(static_cast<rstd::uint32_t>(
        load_u32(wire.as_slice(), usize(8), message.endian).to_primitive()));
    auto     header_end = usize(16) + load_u32(wire.as_slice(), usize(12), message.endian);
    Decoder  header(wire.clone(), message.endian, limits, usize(16));
    auto     signature = String::make();
    unsigned seen {};
    auto     variant_type = Signature::parse("v"_str).unwrap();
    while (header.cursor() < header_end) {
        header.align(usize(8));
        auto code = header.byte();
        if (code == 0) return Err(bad(header.cursor(), "invalid header field code"_str));
        auto wrapped = header.value(variant_type.types()[usize()]);
        auto checked = header.result();
        if (checked.is_err()) return Err(rstd::move(checked).unwrap_err());
        if (header.cursor() > header_end)
            return Err(bad(header.cursor(), "header field overrun"_str));
        if (code > 9) continue;
        if (seen & (1u << code)) return Err(bad(header.cursor(), "duplicate header field"_str));
        seen |= 1u << code;
        auto& value = *wrapped.as_Variant().value;
        if (code == 1) {
            if (! value.is_Path()) return Err(bad(header.cursor(), "wrong path type"_str));
            message.path = value.as_Path().value.clone();
        } else if (code == 5 || code == 9) {
            if (! value.is_UInt32())
                return Err(bad(header.cursor(), "wrong integer header type"_str));
            if (code == 5)
                message.reply_serial = value.as_UInt32().value;
            else if (value.as_UInt32().value != u32())
                return Err(Error::Unsupported(String::make("Unix FD transfer"_str)));
        } else if (code == 8) {
            if (! value.is_TypeString())
                return Err(bad(header.cursor(), "wrong signature header type"_str));
            signature = value.as_TypeString().value.clone();
        } else {
            if (! value.is_Text()) return Err(bad(header.cursor(), "wrong string header type"_str));
            auto text = value.as_Text().value.clone();
            switch (code) {
            case 2: message.interface = rstd::move(text); break;
            case 3: message.member = rstd::move(text); break;
            case 4: message.error_name = rstd::move(text); break;
            case 6: message.destination = rstd::move(text); break;
            case 7: message.sender = rstd::move(text); break;
            }
        }
    }
    if (header.cursor() != header_end)
        return Err(bad(header.cursor(), "invalid header length"_str));
    header.align(usize(8));
    auto checked = header.result();
    if (checked.is_err()) return Err(rstd::move(checked).unwrap_err());
    auto body_bytes = Vec<u8>::make();
    for (auto i = header.cursor(); i < wire.len(); ++i) body_bytes.push(wire.as_slice()[i]);
    auto body = decode_body(
        signature.as_str(), Bytes::copy_from_slice(body_bytes.as_slice()), message.endian, limits);
    if (body.is_err()) return Err(rstd::move(body).unwrap_err());
    message.body = rstd::move(body).unwrap();
    if (! message_valid(message)) return Err(bad(usize(), "invalid required message fields"_str));
    return Ok(rstd::move(message));
}
} // namespace dbusto
