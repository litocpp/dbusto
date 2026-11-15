#include <rstd/test/gtest.hpp>
import rstd;
import dbusto;
import dbusto.test_counter;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace async = rstd::async;
using rstd::sync::Arc;

auto property_call(ref<str>              method,
                   ref<str>              interface,
                   ref<str>              property = ""_str,
                   Option<dbusto::Value> value    = {}) -> dbusto::Message {
    auto body = dbusto::pack(String::make(interface));
    if (method != "GetAll"_str) body.push(dbusto::Value::Text(String::make(property)));
    if (value.is_some()) body.push(dbusto::Codec<dbusto::Value>::encode(rstd::move(*value)));
    return dbusto::method_call("org.example.Test"_str,
                               "/test"_str,
                               "org.freedesktop.DBus.Properties"_str,
                               method,
                               rstd::move(body));
}
auto expect_error(dbusto::Result<dbusto::Body> result, ref<str> name) -> void {
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    ASSERT_TRUE(error.is_Remote());
    EXPECT_EQ(error.as_Remote().name.as_str(), name);
}
TEST(DBusObject, RegistersMultipleInterfacesAndIntrospectsOneDescription) {
    auto object = dbusto::Object::make("/test"_str).unwrap();
    EXPECT_TRUE(object.add(TestPlayer { Arc<TestPlayerState>::make() }).is_ok());
    EXPECT_TRUE(object.add(ReadOnly {}).is_ok());
    EXPECT_TRUE(object.add(ReadOnly {}).is_err());
    auto xml = object.introspection().unwrap();
    EXPECT_TRUE(xml.as_str().contains("name=\"org.example.Player\""_str));
    EXPECT_TRUE(xml.as_str().contains("name=\"org.example.ReadOnly\""_str));
    EXPECT_TRUE(xml.as_str().contains("name=\"Volume\" type=\"d\" access=\"readwrite\""_str));
    EXPECT_TRUE(xml.as_str().contains(
        "name=\"Position\" type=\"x\" access=\"read\"><annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"false\""_str));
    EXPECT_TRUE(xml.as_str().contains("<signal name=\"Seeked\"><arg type=\"x\"/>"_str));
    auto standard = "<interface name=\"org.freedesktop.DBus.Properties\">"_str;
    auto split    = xml.as_str().split_once(standard);
    ASSERT_TRUE(split.is_some());
    EXPECT_FALSE(split->get<1>().contains(standard));
    EXPECT_TRUE(xml.as_str().contains("<method name=\"Set\">"_str));
    auto result = async::block_on(
        object.dispatch(dbusto::method_call("org.example.Test"_str,
                                            "/test"_str,
                                            "org.freedesktop.DBus.Introspectable"_str,
                                            "Introspect"_str)));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ((*result)[usize()].as_Text().value, xml);
    EXPECT_FALSE(dbusto::Object::make("not/a/path"_str).is_ok());
}
TEST(DBusObject, PropertiesGetGetAllAndSetOwnTheResults) {
    auto object = dbusto::Object::make("/test"_str).unwrap();
    auto state  = Arc<TestPlayerState>::make();
    ASSERT_TRUE(object.add(TestPlayer { state.clone() }).is_ok());
    auto volume = async::block_on(
        object.dispatch(property_call("Get"_str, TestPlayer::dbus_interface(), "Volume"_str)));
    ASSERT_TRUE(volume.is_ok());
    EXPECT_EQ(dbusto::body_signature(*volume).as_str(), "v"_str);
    EXPECT_EQ((*volume)[usize()].as_Variant().value->as_Floating().value, 0.5);
    auto set = async::block_on(object.dispatch(property_call("Set"_str,
                                                             TestPlayer::dbus_interface(),
                                                             "Volume"_str,
                                                             Some(dbusto::Value::Floating(0.75)))));
    ASSERT_TRUE(set.is_ok());
    EXPECT_TRUE(set->is_empty());
    EXPECT_EQ(state->volume, 0.75);
    EXPECT_EQ(state->writes, 1);
    auto all =
        async::block_on(object.dispatch(property_call("GetAll"_str, TestPlayer::dbus_interface())));
    ASSERT_TRUE(all.is_ok());
    EXPECT_EQ(dbusto::body_signature(*all).as_str(), "a{sv}"_str);
    auto decoded = dbusto::Codec<dbusto::PropertyMap>::decode((*all)[usize()]);
    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded->len(), usize(4));
    auto metadata = decoded->get(String::make("Metadata"_str));
    ASSERT_TRUE(metadata.is_some());
    auto tags = dbusto::Codec<dbusto::PropertyMap>::decode(**metadata);
    ASSERT_TRUE(tags.is_ok());
    EXPECT_EQ(tags->len(), usize(2));
    EXPECT_TRUE(object.remove(TestPlayer::dbus_interface()));
    EXPECT_EQ((*volume)[usize()].as_Variant().value->as_Floating().value, 0.5);
    EXPECT_TRUE(dbusto::Codec<dbusto::PropertyMap>::decode((*all)[usize()]).is_ok());
}
TEST(DBusObject, RejectsUnknownNamesReadOnlyWritesAndInvalidSignatures) {
    auto object = dbusto::Object::make("/test"_str).unwrap();
    auto state  = Arc<TestPlayerState>::make();
    ASSERT_TRUE(object.add(TestPlayer { state.clone() }).is_ok());
    expect_error(async::block_on(object.dispatch(
                     property_call("Get"_str, TestPlayer::dbus_interface(), "Unknown"_str))),
                 "org.freedesktop.DBus.Error.UnknownProperty"_str);
    expect_error(
        async::block_on(object.dispatch(property_call("Set"_str,
                                                      TestPlayer::dbus_interface(),
                                                      "Position"_str,
                                                      Some(dbusto::Value::Int64(i64(9)))))),
        "org.freedesktop.DBus.Error.PropertyReadOnly"_str);
    expect_error(
        async::block_on(object.dispatch(property_call("Set"_str,
                                                      TestPlayer::dbus_interface(),
                                                      "Volume"_str,
                                                      Some(dbusto::Value::Boolean(true))))),
        "org.freedesktop.DBus.Error.InvalidArgs"_str);
    expect_error(
        async::block_on(object.dispatch(property_call("GetAll"_str, "org.example.Unknown"_str))),
        "org.freedesktop.DBus.Error.UnknownInterface"_str);
    auto malformed = property_call("Set"_str, TestPlayer::dbus_interface(), "Volume"_str);
    expect_error(async::block_on(object.dispatch(rstd::move(malformed))),
                 "org.freedesktop.DBus.Error.InvalidArgs"_str);
    auto wrong_path = property_call("GetAll"_str, TestPlayer::dbus_interface());
    wrong_path.path = String::make("/missing"_str);
    expect_error(async::block_on(object.dispatch(rstd::move(wrong_path))),
                 "org.freedesktop.DBus.Error.UnknownObject"_str);
    EXPECT_EQ(state->writes, 0);
    ASSERT_TRUE(object.add(Counter {}).is_ok());
    auto empty =
        async::block_on(object.dispatch(property_call("GetAll"_str, Counter::dbus_interface())));
    ASSERT_TRUE(empty.is_ok());
    EXPECT_EQ(dbusto::body_signature(*empty).as_str(), "a{sv}"_str);
    EXPECT_TRUE(dbusto::Codec<dbusto::PropertyMap>::decode((*empty)[usize()])->is_empty());
    ASSERT_TRUE(object.add(BadGetter {}).is_ok());
    EXPECT_TRUE(async::block_on(object.dispatch(property_call(
                                    "Get"_str, BadGetter::dbus_interface(), "Broken"_str)))
                    .is_err());
    EXPECT_TRUE(
        async::block_on(object.dispatch(property_call("GetAll"_str, BadGetter::dbus_interface())))
            .is_err());
}
TEST(DBusObject, SignalsAndPropertyNotificationsFollowTheDescriptors) {
    auto object = dbusto::Object::make("/test"_str).unwrap();
    ASSERT_TRUE(object.add(TestPlayer { Arc<TestPlayerState>::make() }).is_ok());
    auto changed = dbusto::PropertyMap::make();
    changed.insert(String::make("Volume"_str), dbusto::Value::Floating(0.25));
    auto message = object.properties_changed(TestPlayer::dbus_interface(), rstd::move(changed));
    ASSERT_TRUE(message.is_ok());
    EXPECT_EQ(message->type, dbusto::MessageType::Signal);
    EXPECT_EQ(message->path.as_str(), "/test"_str);
    EXPECT_EQ(dbusto::body_signature(message->body).as_str(), "sa{sv}as"_str);
    message->serial = u32(1);
    EXPECT_TRUE(dbusto::decode(dbusto::encode(*message).unwrap()).is_ok());
    auto position = dbusto::PropertyMap::make();
    position.insert(String::make("Position"_str), dbusto::Value::Int64(i64(3)));
    EXPECT_TRUE(
        object.properties_changed(TestPlayer::dbus_interface(), rstd::move(position)).is_err());
    auto invalidated = Vec<String>::make();
    invalidated.push(String::make("Cached"_str));
    EXPECT_TRUE(object
                    .properties_changed(TestPlayer::dbus_interface(),
                                        dbusto::PropertyMap::make(),
                                        rstd::move(invalidated))
                    .is_ok());
    auto unknown = dbusto::PropertyMap::make();
    unknown.insert(String::make("Unknown"_str), dbusto::Value::Boolean(true));
    EXPECT_TRUE(
        object.properties_changed(TestPlayer::dbus_interface(), rstd::move(unknown)).is_err());
    auto seeked = dbusto::signal(
        "/test"_str, TestPlayer::dbus_interface(), TestPlayer::seeked(), dbusto::pack(i64(123)));
    ASSERT_TRUE(seeked.is_ok());
    EXPECT_EQ(seeked->member.as_str(), "Seeked"_str);
    EXPECT_EQ(dbusto::body_signature(seeked->body).as_str(), "x"_str);
    EXPECT_TRUE(
        dbusto::signal(
            "/test"_str, TestPlayer::dbus_interface(), TestPlayer::seeked(), dbusto::pack(u64(123)))
            .is_err());
}
auto lifetime_check() -> async::coro<empty> {
    auto state  = Arc<rstd::sync::Mutex<LifetimeState>>::make(LifetimeState {});
    auto object = dbusto::Object::make("/test"_str).unwrap();
    EXPECT_TRUE(object.add(LifetimeInterface { Box<LifetimeProbe>::make(state.clone()) }).is_ok());
    auto pending = async::spawn(
        object.dispatch(dbusto::method_call("org.example.Test"_str,
                                            "/test"_str,
                                            LifetimeInterface::dbus_interface(),
                                            "Wait"_str,
                                            dbusto::pack(String::make("retained"_str)))));
    while (! state->lock().unwrap()->started) co_await async::yield_now();
    EXPECT_TRUE(object.remove(LifetimeInterface::dbus_interface()));
    EXPECT_EQ(state->lock().unwrap()->drops, 0);
    state->lock().unwrap()->released = true;
    auto result                      = co_await rstd::move(pending);
    EXPECT_TRUE(result.is_ok());
    if (result.is_ok()) {
        EXPECT_TRUE(result->is_ok());
        if (result->is_ok())
            EXPECT_EQ(result->unwrap()[usize()].as_Text().value.as_str(), "retained"_str);
    }
    EXPECT_EQ(state->lock().unwrap()->drops, 1);
    co_return empty {};
}
TEST(DBusObject, RemovalKeepsSuspendedHandlerAndMessageAlive) {
    async::block_on(lifetime_check());
}
