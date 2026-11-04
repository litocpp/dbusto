#include <initializer_list>
#include <rstd/test/gtest.hpp>
import rstd;
import dbusto;
using namespace rstd::prelude;
using namespace rstd::literals;
using namespace dbusto;
TEST(DBusError, ClonesKeepTheOwnedIoPayloadAlive) {
    Option<dbusto::Error> retained;
    {
        auto source = rstd::io::Error::with_source(
            rstd::io::ErrorKind { rstd::io::ErrorKind::Other },
            Box<rstd::dyn<rstd::error::Error>>::make(rstd::io::Error::from_raw_os_error(i32(111))));
        auto error = dbusto::Error::Io(rstd::move(source));
        retained   = Some(error.clone());
    }
    ASSERT_TRUE(retained->is_Io());
    const auto& source = *retained->as_Io().source;
    EXPECT_EQ(source.kind(), rstd::io::ErrorKind { rstd::io::ErrorKind::Other });
    ASSERT_TRUE(source.get_ref().is_some());
    auto native = rstd::error::downcast_ref<rstd::io::Error>(*source.get_ref());
    ASSERT_TRUE(native.is_some());
    EXPECT_EQ((*native)->raw_os_error(), Some(i32(111)));
}
TEST(DBusSignature, NestedTypesAndInvalidGrammar) {
    EXPECT_TRUE(Signature::parse("ia(iayqs)a{sv}(st)v"_str).is_ok());
    EXPECT_TRUE(Signature::parse(""_str).is_ok());
    for (auto text : { "a"_str,
                       "()"_str,
                       "{ss}"_str,
                       "a{vs}"_str,
                       "a{sss}"_str,
                       "a()"_str,
                       "(i"_str,
                       "i)"_str })
        EXPECT_TRUE(Signature::parse(text).is_err());
}
TEST(DBusWire, KnownEndianEncoding) {
    auto body = Body::make();
    body.push(Value::Byte(u8(0x42)));
    body.push(Value::UInt32(u32(0x01020304)));
    body.push(Value::Text(String::make("hi"_str)));
    auto little = encode_body(body).unwrap();
    auto big    = encode_body(body, Endian::Big).unwrap();
    EXPECT_EQ(little.as_slice(), "\x42\0\0\0\x04\x03\x02\x01\x02\0\0\0hi\0"_bytes);
    EXPECT_EQ(big.as_slice(), "\x42\0\0\0\x01\x02\x03\x04\0\0\0\x02hi\0"_bytes);
}
TEST(DBusWire, ManagerMessageAndNestedContainers) {
    for (auto endian : { Endian::Little, Endian::Big }) {
        Message message;
        message.endian      = endian;
        message.serial      = u32(17);
        message.path        = String::make("/org/freedesktop/resolve1"_str);
        message.interface   = String::make("org.freedesktop.resolve1.Manager"_str);
        message.member      = String::make("SetLinkDNS"_str);
        message.destination = String::make("org.freedesktop.resolve1"_str);
        message.body.push(Value::Int32(i32(3)));
        auto server = Body::make();
        server.push(Value::Int32(i32(2)));
        server.push(Value::Octets(Bytes::copy_from_slice("\x01\x01\x01\x01"_bytes)));
        auto servers = Body::make();
        servers.push(Value::Structure(rstd::move(server)));
        message.body.push(Value::Array(String::make("(iay)"_str), rstd::move(servers)));
        auto wire = encode(message).unwrap();
        for (usize i; i < usize(16); ++i) {
            auto partial = wire.clone();
            partial.truncate(i);
            EXPECT_TRUE(frame_size(partial.as_slice()).unwrap().is_none());
        }
        auto decoded = decode(wire.clone());
        ASSERT_TRUE(decoded.is_ok());
        EXPECT_EQ(decoded->serial, u32(17));
        EXPECT_EQ(decoded->member, message.member);
        EXPECT_EQ(body_signature(decoded->body), String::make("ia(iay)"_str));
        EXPECT_EQ(encode(*decoded).unwrap().as_slice(), wire.as_slice());
        auto dict  = Body::make();
        auto entry = Body::make();
        entry.push(Value::Text(String::make("DNS"_str)));
        entry.push(Value::Variant(Box<Value>::make(message.body[usize(1)].clone())));
        dict.push(Value::Entry(rstd::move(entry)));
        auto container = Body::make();
        container.push(Value::Array(String::make("{sv}"_str), rstd::move(dict)));
        auto bytes = encode_body(container, endian).unwrap();
        EXPECT_TRUE(decode_body("a{sv}"_str, rstd::move(bytes), endian).is_ok());
    }
}
TEST(DBusWire, RejectsMalformedAndBoundedInput) {
    EXPECT_TRUE(decode_body("b"_str, Bytes::copy_from_slice("\x02\0\0\0"_bytes)).is_err());
    EXPECT_TRUE(decode_body("s"_str, Bytes::copy_from_slice("\x01\0\0\0xZ"_bytes)).is_err());
    EXPECT_TRUE(decode_body("v"_str, Bytes::copy_from_slice("\0\0"_bytes)).is_err());
    EXPECT_TRUE(decode_body("ai"_str, Bytes::copy_from_slice("\x03\0\0\0abc"_bytes)).is_err());
    EXPECT_TRUE(decode_body("y"_str, Bytes::copy_from_slice("ab"_bytes)).is_err());
    auto body = Body::make();
    body.push(Value::Text(String::make("long"_str)));
    Limits limits;
    limits.message_bytes = usize(4);
    EXPECT_TRUE(encode_body(body, Endian::Little, limits).is_err());
    Message message;
    message.serial = u32(1);
    message.path   = String::make("/"_str);
    message.member = String::make("Ping"_str);
    auto wire      = encode(message).unwrap();
    auto mutation  = Vec<u8>::make();
    mutation.extend_from_slice(wire.as_slice());
    mutation[usize(3)] = u8(2);
    EXPECT_TRUE(decode(Bytes::copy_from_slice(mutation.as_slice())).is_err());
    for (usize i; i < wire.len(); ++i) {
        auto truncated = wire.clone();
        truncated.truncate(i);
        EXPECT_TRUE(decode(rstd::move(truncated)).is_err());
    }
}
