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
struct MethodSpec {
    ref<str> name;
    ref<str> input;
    ref<str> output;
};
struct PropertySpec {
    ref<str> name;
    ref<str> signature;
};
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
} // namespace dbusto
