#include <rstd/test/gtest.hpp>
import rstd;
import dbusto;
using namespace rstd::prelude;
using namespace rstd::literals;
import dbusto.test_counter;
TEST(DBusBinding, GeneratedDispatchAndIntrospection) {
    Counter counter;
    auto    call     = dbusto::method_call("org.example.Counter"_str,
                                           "/"_str,
                                           Counter::dbus_interface(),
                                           "Echo"_str,
                                           dbusto::pack(i32(42), String::make("hello"_str)));
    auto    response = rstd::async::block_on(counter.dbus_dispatch(call));
    ASSERT_TRUE(response.is_ok());
    EXPECT_EQ(dbusto::body_signature(*response).as_str(), "is"_str);
    auto xml = dbusto::introspection(Counter::dbus_interface(), Counter::dbus_methods()).unwrap();
    EXPECT_TRUE(xml.as_str().contains("name=\"Echo\""_str));
    call.body.clear();
    EXPECT_TRUE(rstd::async::block_on(counter.dbus_dispatch(call)).is_err());
    auto vector = Vec<rstd::tuple<i32, String>>::make();
    vector.push({ i32(1), String::make("one"_str) });
    auto value = dbusto::Codec<decltype(vector)>::encode(rstd::move(vector));
    EXPECT_EQ(value.signature().as_str(), "a(is)"_str);
    EXPECT_TRUE((dbusto::Codec<Vec<rstd::tuple<i32, String>>>::decode(value).is_ok()));
}
