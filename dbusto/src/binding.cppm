module;
#include <rstd/enum.hpp>
export module dbusto:binding;
export import :connection;
using namespace rstd::prelude;
using namespace rstd::literals;
export namespace dbusto
{
inline auto type_error() -> Error {
    return Error::Remote(String::make("org.freedesktop.DBus.Error.InvalidArgs"_str),
                         String::make("argument type mismatch"_str));
}
template<class T>
struct Codec;
#define DBUSTO_SCALAR(Type, Branch, Signature)                                   \
    template<>                                                                   \
    struct Codec<Type> {                                                         \
        static auto signature() -> String { return String::make(Signature); }    \
        static auto encode(Type value) -> Value { return Value::Branch(value); } \
        static auto decode(const Value& value) -> Result<Type> {                 \
            if (! value.is_##Branch()) return Err(type_error());                 \
            return Ok(Type(value.as_##Branch().value));                          \
        }                                                                        \
    };
DBUSTO_SCALAR(u8, Byte, "y"_str)
DBUSTO_SCALAR(bool, Boolean, "b"_str)
DBUSTO_SCALAR(i16, Int16, "n"_str)
DBUSTO_SCALAR(u16, UInt16, "q"_str)
DBUSTO_SCALAR(i32, Int32, "i"_str)
DBUSTO_SCALAR(u32, UInt32, "u"_str)
DBUSTO_SCALAR(i64, Int64, "x"_str)
DBUSTO_SCALAR(u64, UInt64, "t"_str)
DBUSTO_SCALAR(double, Floating, "d"_str)
#undef DBUSTO_SCALAR
template<>
struct Codec<String> {
    static auto signature() -> String { return String::make("s"_str); }
    static auto encode(String value) -> Value { return Value::Text(rstd::move(value)); }
    static auto decode(const Value& value) -> Result<String> {
        if (! value.is_Text()) return Err(type_error());
        return Ok(value.as_Text().value.clone());
    }
};
struct ObjectPath {
    String text;
};
template<>
struct Codec<ObjectPath> {
    static auto signature() -> String { return String::make("o"_str); }
    static auto encode(ObjectPath value) -> Value { return Value::Path(rstd::move(value.text)); }
    static auto decode(const Value& value) -> Result<ObjectPath> {
        if (! value.is_Path()) return Err(type_error());
        return Ok(ObjectPath { value.as_Path().value.clone() });
    }
};
template<>
struct Codec<Bytes> {
    static auto signature() -> String { return String::make("ay"_str); }
    static auto encode(Bytes value) -> Value { return Value::Octets(rstd::move(value)); }
    static auto decode(const Value& value) -> Result<Bytes> {
        if (! value.is_Octets()) return Err(type_error());
        return Ok(value.as_Octets().value.clone());
    }
};
template<>
struct Codec<Value> {
    static auto signature() -> String { return String::make("v"_str); }
    static auto encode(Value value) -> Value {
        return Value::Variant(Box<Value>::make(rstd::move(value)));
    }
    static auto decode(const Value& value) -> Result<Value> {
        if (! value.is_Variant()) return Err(type_error());
        return Ok(value.as_Variant().value->clone());
    }
};
template<class T>
struct Codec<Vec<T>> {
    static auto signature() -> String { return rstd::format("a{}", Codec<T>::signature()); }
    static auto encode(Vec<T> values) -> Value {
        auto out = Body::make();
        for (auto& value : values) out.push(Codec<T>::encode(rstd::move(value)));
        return Value::Array(Codec<T>::signature(), rstd::move(out));
    }
    static auto decode(const Value& value) -> Result<Vec<T>> {
        if (! value.is_Array() || value.as_Array().element != Codec<T>::signature())
            return Err(type_error());
        auto out = Vec<T>::make();
        for (const auto& child : value.as_Array().values) {
            auto decoded = Codec<T>::decode(child);
            if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err());
            out.push(rstd::move(decoded).unwrap());
        }
        return Ok(rstd::move(out));
    }
};
template<class... T>
auto pack(T... values) -> Body {
    auto out = Body::make();
    (out.push(Codec<T>::encode(rstd::move(values))), ...);
    return out;
}
template<class... T>
auto unpack(const Body& body) -> Result<rstd::tuple<T...>> {
    if (body.len() != usize(sizeof...(T))) return Err(type_error());
    rstd::tuple<Option<T>...> values;
    Option<Error>             error;
    [&]<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
        auto read = [&]<rstd::size_t N, class V>() {
            auto value = Codec<V>::decode(body[usize(N)]);
            if (value.is_err())
                error = Some(rstd::move(value).unwrap_err());
            else
                values.template get<N>() = Some(rstd::move(value).unwrap());
        };
        (read.template operator()<I, T>(), ...);
    }(rstd::mtp::make_index_sequence<sizeof...(T)> {});
    if (error.is_some()) return Err(rstd::move(*error));
    return [&]<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) -> Result<rstd::tuple<T...>> {
        return Ok(rstd::tuple<T...> { rstd::move(*values.template get<I>())... });
    }(rstd::mtp::make_index_sequence<sizeof...(T)> {});
}
template<class... T>
struct Codec<rstd::tuple<T...>> {
    static auto signature() -> String {
        auto out = String::make("("_str);
        (out.push_str(Codec<T>::signature().as_str()), ...);
        out.push(')');
        return out;
    }
    static auto encode(rstd::tuple<T...> values) -> Value {
        return [&]<rstd::size_t... I>(rstd::mtp::index_sequence<I...>) {
            return Value::Structure(pack(rstd::move(values.template get<I>())...));
        }(rstd::mtp::make_index_sequence<sizeof...(T)> {});
    }
    static auto decode(const Value& value) -> Result<rstd::tuple<T...>> {
        if (! value.is_Structure()) return Err(type_error());
        return unpack<T...>(value.as_Structure().values);
    }
};
template<class K, class V>
struct Codec<rstd::collections::BTreeMap<K, V>> {
    static auto signature() -> String {
        return rstd::format("a{{{}{}}}", Codec<K>::signature(), Codec<V>::signature());
    }
    static auto encode(rstd::collections::BTreeMap<K, V> values) -> Value {
        auto entries = Body::make();
        for (auto [key, value] : rstd::move(values).into_iter())
            entries.push(Value::Entry(pack(rstd::move(key), rstd::move(value))));
        return Value::Array(rstd::format("{{{}{}}}", Codec<K>::signature(), Codec<V>::signature()),
                            rstd::move(entries));
    }
    static auto decode(const Value& value) -> Result<rstd::collections::BTreeMap<K, V>> {
        auto element = rstd::format("{{{}{}}}", Codec<K>::signature(), Codec<V>::signature());
        if (! value.is_Array() || value.as_Array().element != element) return Err(type_error());
        auto out = rstd::collections::BTreeMap<K, V>::make();
        for (const auto& entry : value.as_Array().values) {
            if (! entry.is_Entry()) return Err(type_error());
            auto pair = unpack<K, V>(entry.as_Entry().values);
            if (pair.is_err()) return Err(rstd::move(pair).unwrap_err());
            if (out.contains_key(pair->template get<0>())) return Err(type_error());
            out.insert(rstd::move(pair->template get<0>()), rstd::move(pair->template get<1>()));
        }
        return Ok(rstd::move(out));
    }
};
using PropertyMap = rstd::collections::BTreeMap<String, Value>;
struct MethodSpec {
    ref<str> name;
    ref<str> input;
    ref<str> output;
};
enum class PropertyAccess
{
    Read,
    Write,
    ReadWrite
};
enum class EmitsChangedSignal
{
    True,
    False,
    Const,
    Invalidates
};
struct PropertySpec {
    ref<str>           name;
    ref<str>           signature;
    PropertyAccess     access { PropertyAccess::Read };
    EmitsChangedSignal emits_changed { EmitsChangedSignal::True };
};
struct SignalSpec {
    ref<str> name;
    ref<str> signature;
};
// Descriptor strings and slices must have static storage duration.
struct InterfaceSpec {
    ref<str>            name;
    slice<MethodSpec>   methods;
    slice<PropertySpec> properties;
    slice<SignalSpec>   signals;
};
auto validate(InterfaceSpec interface) -> Result<empty>;
auto properties_interface() -> InterfaceSpec;
auto introspection(slice<InterfaceSpec> interfaces) -> Result<String>;
auto introspection(ref<str>            interface,
                   slice<MethodSpec>   methods,
                   slice<PropertySpec> properties = {}) -> Result<String>;
auto checked_reply(rstd::async::coro<Result<Body>> result, ref<str> output)
    -> rstd::async::coro<Result<Body>>;
auto reject_call(ref<str> error, ref<str> reason) -> rstd::async::coro<Result<Body>>;
auto proxy_call(Connection connection,
                String     destination,
                String     path,
                String     interface,
                MethodSpec method,
                Body       body) -> rstd::async::coro<Result<Body>>;
auto property_spec(InterfaceSpec interface, ref<str> name) -> Result<PropertySpec>;
auto validate_property_value(PropertySpec property, const Value& value) -> Result<empty>;
auto remote_error(ref<str> name, ref<str> reason) -> Error;
template<class T>
auto reject(Error error) -> rstd::async::coro<Result<T>> {
    co_return Err(rstd::move(error));
}

class Interface {
    using Handler = rstd::sync::Arc<rstd::dyn<rstd::Fn<rstd::async::coro<Result<Body>>(Message)>>>;
    InterfaceSpec spec_;
    Handler       handler_;
    Interface(InterfaceSpec spec, Handler handler): spec_(spec), handler_(rstd::move(handler)) {}
    template<class T>
    static auto invoke(rstd::sync::Arc<rstd::async::Mutex<T>> state, Message call)
        -> rstd::async::coro<Result<Body>> {
        auto guard = co_await state->lock();
        if (call.interface.as_str() != "org.freedesktop.DBus.Properties"_str)
            co_return co_await guard->dbus_dispatch(call);
        auto spec = T::dbus_spec();
        if (call.member.as_str() == "GetAll"_str) {
            auto values = PropertyMap::make();
            for (const auto& property : spec.properties) {
                if (property.access == PropertyAccess::Write) continue;
                auto value = co_await guard->dbus_get(property.name);
                if (value.is_err()) {
                    auto error = rstd::move(value).unwrap_err();
                    if (error.is_Remote() && error.as_Remote().name.as_str() ==
                                                 "org.freedesktop.DBus.Error.AccessDenied"_str)
                        continue;
                    co_return Err(rstd::move(error));
                }
                auto checked = validate_property_value(property, *value);
                if (checked.is_err())
                    co_return Err(remote_error("org.freedesktop.DBus.Error.Failed"_str,
                                               "property getter returned an invalid type"_str));
                values.insert(String::make(property.name), rstd::move(*value));
            }
            co_return Ok(pack(rstd::move(values)));
        }
        const auto& name     = call.body[usize(1)].as_Text().value;
        auto        property = property_spec(spec, name.as_str());
        if (property.is_err()) co_return Err(rstd::move(property).unwrap_err());
        if (call.member.as_str() == "Get"_str) {
            if (property->access == PropertyAccess::Write)
                co_return Err(remote_error("org.freedesktop.DBus.Error.AccessDenied"_str,
                                           "property is not readable"_str));
            auto value = co_await guard->dbus_get(property->name);
            if (value.is_err()) co_return Err(rstd::move(value).unwrap_err());
            auto checked = validate_property_value(*property, *value);
            if (checked.is_err())
                co_return Err(remote_error("org.freedesktop.DBus.Error.Failed"_str,
                                           "property getter returned an invalid type"_str));
            co_return Ok(pack(rstd::move(*value)));
        }
        if (property->access == PropertyAccess::Read)
            co_return Err(remote_error("org.freedesktop.DBus.Error.PropertyReadOnly"_str,
                                       "property is read only"_str));
        const auto& value   = *call.body[usize(2)].as_Variant().value;
        auto        checked = validate_property_value(*property, value);
        if (checked.is_err()) co_return Err(rstd::move(checked).unwrap_err());
        auto result = co_await guard->dbus_set(property->name, value);
        if (result.is_err()) co_return Err(rstd::move(result).unwrap_err());
        co_return Ok(Body::make());
    }

public:
    template<class T>
    static auto make(T value) -> Interface {
        auto state = rstd::sync::Arc<rstd::async::Mutex<T>>::make(rstd::move(value));
        return Interface(T::dbus_spec(), Handler::make([state = rstd::move(state)](Message call) {
                             return invoke(state.clone(), rstd::move(call));
                         }));
    }
    auto clone() const -> Interface { return Interface(spec_, handler_.clone()); }
    auto spec() const -> InterfaceSpec { return spec_; }
    auto dispatch(Message call) const -> rstd::async::coro<Result<Body>>;
};

struct ObjectState;
class Object {
    rstd::sync::Arc<ObjectState> state_;
    explicit Object(rstd::sync::Arc<ObjectState> state);
    auto add_interface(Interface interface) -> Result<empty>;

public:
    Object(Object&&) noexcept;
    auto operator=(Object&&) noexcept -> Object&;
    Object(const Object&) = delete;
    ~Object();
    static auto make(ref<str> path) -> Result<Object>;
    auto        clone() const -> Object;
    auto add(Interface interface) -> Result<empty> { return add_interface(rstd::move(interface)); }
    template<class T>
    auto add(T interface) -> Result<empty> {
        return add_interface(Interface::make(rstd::move(interface)));
    }
    auto remove(ref<str> interface) -> bool;
    auto introspection() const -> Result<String>;
    // Calls are serialized per interface. The owned message survives every suspension.
    auto dispatch(Message call) const -> rstd::async::coro<Result<Body>>;
    auto handle(Connection connection, Message call) const -> rstd::async::coro<Result<empty>>;
    // Notify after authoritative state changes; Set itself does not infer completion or emit.
    auto properties_changed(ref<str>    interface,
                            PropertyMap changed,
                            Vec<String> invalidated = Vec<String>::make()) const -> Result<Message>;
};

auto signal(ref<str> path, ref<str> interface, SignalSpec spec, Body body) -> Result<Message>;
class SignalEmitter {
    Connection connection_;
    String     path_;
    SignalEmitter(Connection connection, String path);

public:
    static auto make(Connection connection, ref<str> path) -> Result<SignalEmitter>;
    auto        clone() const -> SignalEmitter;
    auto        emit(ref<str> interface, SignalSpec spec, Body body) const
        -> rstd::async::coro<Result<empty>>;
};
} // namespace dbusto
