export module dbusto.test_counter;
import rstd;
import dbusto;
using namespace rstd::prelude;
using namespace rstd::literals;
export struct [[pmacro::attr("dbusto-macros::interface", "org.example.Counter")]] Counter {
    [[pmacro::helper("dbusto-macros::interface", "method", "Echo", "is", "is")]]
    auto echo(const dbusto::Message& call) -> rstd::async::coro<dbusto::Result<dbusto::Body>> {
        auto args = dbusto::unpack<i32, String>(call.body);
        if (args.is_err()) co_return Err(rstd::move(args).unwrap_err());
        co_return Ok(dbusto::pack(args->get<0>(), rstd::move(args->get<1>())));
    }
};
export struct TestPlayerState {
    double volume { 0.5 };
    i64    position { 17 };
    int    writes {};
};
export struct [[pmacro::attr("dbusto-macros::interface", "org.example.Player")]] TestPlayer {
    rstd::sync::Arc<TestPlayerState> state;

    [[pmacro::helper("dbusto-macros::interface", "method", "Echo", "s", "s")]]
    auto echo(const dbusto::Message& call) -> rstd::async::coro<dbusto::Result<dbusto::Body>> {
        co_await rstd::async::yield_now();
        co_return Ok(dbusto::pack(call.body[usize()].as_Text().value.clone()));
    }
    [[pmacro::helper("dbusto-macros::interface",
                     "property",
                     "Volume",
                     "d",
                     "readwrite",
                     "true",
                     "set_volume")]]
    auto volume() -> rstd::async::coro<dbusto::Result<dbusto::Value>> {
        co_return Ok(dbusto::Value::Floating(state->volume));
    }
    auto set_volume(const dbusto::Value& value) -> rstd::async::coro<dbusto::Result<empty>> {
        co_await rstd::async::yield_now();
        state->volume = value.as_Floating().value;
        ++state->writes;
        co_return Ok(empty {});
    }
    [[pmacro::helper("dbusto-macros::interface", "property", "Position", "x", "read", "false")]]
    auto position() -> rstd::async::coro<dbusto::Result<dbusto::Value>> {
        co_return Ok(dbusto::Value::Int64(state->position));
    }
    [[pmacro::helper("dbusto-macros::interface", "property", "Metadata", "a{sv}", "read", "true")]]
    auto metadata() -> rstd::async::coro<dbusto::Result<dbusto::Value>> {
        auto values = dbusto::PropertyMap::make();
        values.insert(String::make("mpris:trackid"_str),
                      dbusto::Value::Path(String::make("/track/1"_str)));
        values.insert(String::make("xesam:title"_str),
                      dbusto::Value::Text(String::make("hello"_str)));
        co_return Ok(dbusto::Codec<dbusto::PropertyMap>::encode(rstd::move(values)));
    }
    [[pmacro::helper("dbusto-macros::interface", "property", "Cached", "b", "read", "invalidates")]]
    auto cached() -> rstd::async::coro<dbusto::Result<dbusto::Value>> {
        co_return Ok(dbusto::Value::Boolean(true));
    }
    [[pmacro::helper("dbusto-macros::interface", "signal", "Seeked", "x")]]
    static auto seeked() -> dbusto::SignalSpec;
};
export struct [[pmacro::attr("dbusto-macros::interface", "org.example.ReadOnly")]] ReadOnly {
    [[pmacro::helper("dbusto-macros::interface", "property", "Identity", "s", "read", "const")]]
    auto identity() -> rstd::async::coro<dbusto::Result<dbusto::Value>> {
        co_return Ok(dbusto::Value::Text(String::make("test"_str)));
    }
};
export struct [[pmacro::attr("dbusto-macros::interface", "org.example.BadGetter")]] BadGetter {
    [[pmacro::helper("dbusto-macros::interface", "property", "Broken", "s", "read", "true")]]
    auto broken() -> rstd::async::coro<dbusto::Result<dbusto::Value>> {
        co_return Ok(dbusto::Value::Boolean(true));
    }
};
export struct LifetimeState {
    bool started {};
    bool released {};
    int  drops {};
};
export struct LifetimeProbe {
    rstd::sync::Arc<rstd::sync::Mutex<LifetimeState>> state;
    ~LifetimeProbe() { ++state->lock().unwrap()->drops; }
};
export struct [[pmacro::attr("dbusto-macros::interface", "org.example.Lifetime")]]
LifetimeInterface {
    Box<LifetimeProbe> probe;
    [[pmacro::helper("dbusto-macros::interface", "method", "Wait", "s", "s")]]
    auto wait(const dbusto::Message& call) -> rstd::async::coro<dbusto::Result<dbusto::Body>> {
        probe->state->lock().unwrap()->started = true;
        while (! probe->state->lock().unwrap()->released) co_await rstd::async::yield_now();
        co_return Ok(dbusto::pack(call.body[usize()].as_Text().value.clone()));
    }
};
