#include <rstd/test/gtest.hpp>
import rstd;
import dbusto;
import dbusto.test_counter;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace async = rstd::async;
auto bus_roundtrip(String address) -> async::coro<dbusto::Result<empty>> {
    dbusto::ConnectionOptions options;
    options.pending_calls = usize(2);
    auto opened           = co_await dbusto::connect(address.as_str(), options);
    if (opened.is_err()) co_return Err(rstd::move(opened).unwrap_err());
    auto connection = rstd::move(opened->connection);
    auto driver     = async::spawn(rstd::move(opened->driver).run());
    auto named      = co_await connection.request_name("org.example.Dbusto"_str);
    if (named.is_err()) {
        (void)co_await connection.close();
        (void)co_await rstd::move(driver);
        co_return Err(rstd::move(named).unwrap_err());
    }
    auto uid = co_await connection.peer_uid(connection.unique_name().as_str());
    EXPECT_TRUE(uid.is_ok());
    if (uid.is_ok()) EXPECT_EQ(*uid, rstd::os::unix::process::current_uid());
    auto call     = dbusto::method_call("org.freedesktop.DBus"_str,
                                        "/org/freedesktop/DBus"_str,
                                        "org.freedesktop.DBus"_str,
                                        "ListNames"_str);
    auto response = co_await connection.call(rstd::move(call));
    EXPECT_TRUE(response.is_ok());
    if (response.is_ok()) EXPECT_EQ(dbusto::body_signature(response->body).as_str(), "as"_str);
    auto self = connection.unique_name();
    auto one  = async::spawn(connection.call(
        dbusto::method_call(self.as_str(), "/"_str, "org.example.Dbusto"_str, "One"_str)));
    auto two  = async::spawn(connection.call(
        dbusto::method_call(self.as_str(), "/"_str, "org.example.Dbusto"_str, "Two"_str)));
    co_await async::yield_now();
    auto excess = co_await connection.call(
        dbusto::method_call(self.as_str(), "/"_str, "org.example.Dbusto"_str, "Excess"_str));
    EXPECT_TRUE(excess.is_err());
    if (excess.is_err()) EXPECT_TRUE(excess.unwrap_err().is_Busy());
    auto calls = Vec<dbusto::Message>::make();
    while (calls.len() < usize(2)) {
        auto incoming = co_await connection.next();
        if (incoming.is_err()) co_return Err(rstd::move(incoming).unwrap_err());
        if (incoming->type == dbusto::MessageType::MethodCall) calls.push(rstd::move(*incoming));
    }
    (void)co_await connection.send(
        dbusto::method_return(calls[usize(1)], dbusto::pack(calls[usize(1)].member.clone())));
    (void)co_await connection.send(
        dbusto::method_return(calls[usize()], dbusto::pack(calls[usize()].member.clone())));
    auto a = co_await rstd::move(one);
    auto b = co_await rstd::move(two);
    EXPECT_TRUE(a.is_ok() && a->is_ok());
    EXPECT_TRUE(b.is_ok() && b->is_ok());
    if (a.is_ok() && a->is_ok())
        EXPECT_EQ(a->unwrap().body[usize()].as_Text().value.as_str(), "One"_str);
    if (b.is_ok() && b->is_ok())
        EXPECT_EQ(b->unwrap().body[usize()].as_Text().value.as_str(), "Two"_str);
    auto canceled =
        co_await async::timeout(async::into_future(connection.call(dbusto::method_call(
                                    self.as_str(), "/"_str, "org.example.Dbusto"_str, "Late"_str))),
                                rstd::time::Duration::from_millis(u64(10)));
    EXPECT_TRUE(canceled.is_err());
    for (;;) {
        auto incoming = co_await connection.next();
        if (incoming.is_err()) co_return Err(rstd::move(incoming).unwrap_err());
        if (incoming->type == dbusto::MessageType::MethodCall) {
            (void)co_await connection.send(dbusto::method_return(*incoming));
            break;
        }
    }
    Counter::Proxy proxy { connection.clone(), self.clone(), String::make("/"_str) };
    auto echo = async::spawn(proxy.echo(dbusto::pack(i32(42), String::make("macro"_str))));
    for (;;) {
        auto incoming = co_await connection.next();
        if (incoming.is_err()) co_return Err(rstd::move(incoming).unwrap_err());
        if (incoming->type != dbusto::MessageType::MethodCall) continue;
        Counter counter;
        auto    body = co_await counter.dbus_dispatch(*incoming);
        EXPECT_TRUE(body.is_ok());
        if (body.is_ok())
            (void)co_await connection.send(dbusto::method_return(*incoming, rstd::move(*body)));
        break;
    }
    auto echoed = co_await rstd::move(echo);
    EXPECT_TRUE(echoed.is_ok() && echoed->is_ok());
    auto remaining = co_await connection.call(dbusto::method_call("org.freedesktop.DBus"_str,
                                                                  "/org/freedesktop/DBus"_str,
                                                                  "org.freedesktop.DBus"_str,
                                                                  "ListNames"_str));
    EXPECT_TRUE(remaining.is_ok());
    auto closed = co_await connection.close();
    (void)co_await rstd::move(driver);
    auto again = co_await connection.close();
    EXPECT_TRUE(again.is_ok());
    co_return closed;
}
TEST(DBusConnection, PrivateBusRoundtrip) {
    auto address = rstd::env::var("DBUSTO_TEST_BUS"_str);
    if (address.is_err()) GTEST_SKIP();
    auto result = async::block_on(bus_roundtrip(rstd::move(address).unwrap()));
    if (result.is_err()) rstd::io::eprintln("{}", result.unwrap_err());
    EXPECT_TRUE(result.is_ok());
}
auto binding_bus_checks(dbusto::Connection connection) -> async::coro<dbusto::Result<empty>> {
    auto object = dbusto::Object::make("/test"_str).unwrap();
    auto state  = rstd::sync::Arc<TestPlayerState>::make();
    EXPECT_TRUE(object.add(TestPlayer { state.clone() }).is_ok());
    EXPECT_TRUE(object.add(ReadOnly {}).is_ok());
    auto name    = connection.unique_name();
    auto request = [&](ref<str> member, dbusto::Body body) {
        return dbusto::method_call(name.as_str(),
                                   "/test"_str,
                                   "org.freedesktop.DBus.Properties"_str,
                                   member,
                                   rstd::move(body));
    };
    for (unsigned i = 0; i < 4; ++i) {
        auto call    = i == 0   ? request("Get"_str,
                                          dbusto::pack(String::make(TestPlayer::dbus_interface()),
                                                       String::make("Volume"_str)))
                       : i == 1 ? request("Set"_str,
                                          dbusto::pack(String::make(TestPlayer::dbus_interface()),
                                                       String::make("Volume"_str),
                                                       dbusto::Value::Floating(0.9)))
                       : i == 2 ? request("Set"_str,
                                          dbusto::pack(String::make(TestPlayer::dbus_interface()),
                                                       String::make("Position"_str),
                                                       dbusto::Value::Int64(i64(3))))
                                : request("GetAll"_str,
                                          dbusto::pack(String::make(TestPlayer::dbus_interface())));
        auto pending = async::spawn(connection.call(rstd::move(call)));
        for (;;) {
            auto incoming = co_await connection.next();
            if (incoming.is_err()) co_return Err(rstd::move(incoming).unwrap_err());
            if (incoming->type != dbusto::MessageType::MethodCall) continue;
            auto handled = co_await object.handle(connection.clone(), rstd::move(*incoming));
            if (handled.is_err()) co_return handled;
            break;
        }
        auto joined = co_await rstd::move(pending);
        if (joined.is_err()) co_return Err(dbusto::Error::Closed());
        auto response = rstd::move(*joined);
        if (i == 2) {
            EXPECT_TRUE(response.is_err());
            if (response.is_err()) {
                auto error = rstd::move(response).unwrap_err();
                EXPECT_TRUE(error.is_Remote());
                if (error.is_Remote())
                    EXPECT_EQ(error.as_Remote().name.as_str(),
                              "org.freedesktop.DBus.Error.PropertyReadOnly"_str);
            }
        } else {
            if (response.is_err()) co_return Err(rstd::move(response).unwrap_err());
            EXPECT_EQ(dbusto::body_signature(response->body).as_str(),
                      i == 0   ? "v"_str
                      : i == 1 ? ""_str
                               : "a{sv}"_str);
        }
    }
    EXPECT_EQ(state->volume, 0.9);
    EXPECT_EQ(state->writes, 1);
    auto matched = co_await connection.add_match("type='signal',path='/test'"_str);
    if (matched.is_err()) co_return matched;
    auto changed = dbusto::PropertyMap::make();
    changed.insert(String::make("Volume"_str), dbusto::Value::Floating(state->volume));
    auto notification =
        object.properties_changed(TestPlayer::dbus_interface(), rstd::move(changed));
    if (notification.is_err()) co_return Err(rstd::move(notification).unwrap_err());
    auto sent = co_await connection.send(rstd::move(*notification));
    if (sent.is_err()) co_return sent;
    for (;;) {
        auto incoming = co_await connection.next();
        if (incoming.is_err()) co_return Err(rstd::move(incoming).unwrap_err());
        if (incoming->member.as_str() != "PropertiesChanged"_str) continue;
        EXPECT_EQ(dbusto::body_signature(incoming->body).as_str(), "sa{sv}as"_str);
        EXPECT_EQ(incoming->body[usize()].as_Text().value.as_str(), TestPlayer::dbus_interface());
        break;
    }
    auto emitter = dbusto::SignalEmitter::make(connection.clone(), "/test"_str).unwrap();
    sent         = co_await emitter.emit(
        TestPlayer::dbus_interface(), TestPlayer::seeked(), dbusto::pack(i64(1234)));
    if (sent.is_err()) co_return sent;
    for (;;) {
        auto incoming = co_await connection.next();
        if (incoming.is_err()) co_return Err(rstd::move(incoming).unwrap_err());
        if (incoming->member.as_str() != "Seeked"_str) continue;
        EXPECT_EQ(dbusto::body_signature(incoming->body).as_str(), "x"_str);
        EXPECT_EQ(incoming->body[usize()].as_Int64().value, i64(1234));
        break;
    }
    co_return Ok(empty {});
}
auto registered_bus_roundtrip(String address) -> async::coro<dbusto::Result<empty>> {
    auto opened = co_await dbusto::connect(address.as_str());
    if (opened.is_err()) co_return Err(rstd::move(opened).unwrap_err());
    auto connection = rstd::move(opened->connection);
    auto driver     = async::spawn(rstd::move(opened->driver).run());
    auto checked =
        co_await async::timeout(async::into_future(binding_bus_checks(connection.clone())),
                                rstd::time::Duration::from_secs(u64(5)));
    auto closed = co_await connection.close();
    (void)co_await rstd::move(driver);
    if (closed.is_err()) co_return closed;
    if (checked.is_err()) co_return Err(dbusto::Error::TimedOut());
    co_return rstd::move(*checked);
}
TEST(DBusConnection, RegisteredPropertiesAndSignals) {
    auto address = rstd::env::var("DBUSTO_TEST_BUS"_str);
    if (address.is_err()) GTEST_SKIP();
    auto result = async::block_on(registered_bus_roundtrip(rstd::move(address).unwrap()));
    if (result.is_err()) rstd::io::eprintln("{}", result.as_ref().unwrap_err());
    EXPECT_TRUE(result.is_ok());
}
TEST(DBusAddress, UnixEscapesAndAlternatives) {
    auto parsed = dbusto::parse_addresses(
        "unix:path=/tmp/my%20bus,guid=0123456789abcdef0123456789abcdef;unix:abstract=dbusto"_str);
    ASSERT_TRUE(parsed.is_ok());
    EXPECT_EQ(parsed->len(), usize(2));
    EXPECT_EQ((*parsed)[usize()].name.as_slice(), "/tmp/my bus"_bytes);
    EXPECT_TRUE((*parsed)[usize(1)].abstract);
    EXPECT_TRUE(dbusto::parse_addresses("unix:path=a,path=b"_str).is_err());
    EXPECT_TRUE(dbusto::parse_addresses("unix:path=%xx"_str).is_err());
    EXPECT_TRUE(dbusto::parse_addresses("unix:path=a%00b"_str).is_err());
    EXPECT_TRUE(dbusto::parse_addresses("tcp:host=localhost,port=1234"_str).is_err());
}
