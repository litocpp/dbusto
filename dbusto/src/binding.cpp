module dbusto;
import :binding;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace dbusto
{
auto introspection(ref<str> interface, slice<MethodSpec> methods, slice<PropertySpec> properties)
    -> Result<String> {
    if (! valid_interface(interface)) return Err(type_error());
    auto xml = rstd::format("<node><interface name=\"{}\">", interface);
    for (const auto& method : methods) {
        if (! valid_member(method.name)) return Err(type_error());
        xml.push_str(rstd::format("<method name=\"{}\">", method.name).as_str());
        for (auto direction : rstd::array<ref<str>, 2> { "in"_str, "out"_str }) {
            auto sig = Signature::parse(direction == "in"_str ? method.input : method.output);
            if (sig.is_err()) return Err(rstd::move(sig).unwrap_err());
            for (const auto& type : sig->types())
                xml.push_str(
                    rstd::format("<arg type=\"{}\" direction=\"{}\"/>", type.text, direction)
                        .as_str());
        }
        xml.push_str("</method>"_str);
    }
    for (const auto& property : properties) {
        auto type = Signature::parse(property.signature);
        if (! valid_member(property.name) || type.is_err() || type->types().len() != usize(1))
            return Err(type_error());
        xml.push_str(rstd::format("<property name=\"{}\" type=\"{}\" access=\"read\"/>",
                                  property.name,
                                  property.signature)
                         .as_str());
    }
    xml.push_str(
        "</interface><interface name=\"org.freedesktop.DBus.Introspectable\"><method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method></interface><interface name=\"org.freedesktop.DBus.Peer\"><method name=\"Ping\"/></interface></node>"_str);
    if (! properties.is_empty()) {
        xml.truncate(xml.len() - usize(7));
        xml.push_str(
            "<interface name=\"org.freedesktop.DBus.Properties\"><method name=\"Get\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/></method><method name=\"GetAll\"><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"out\"/></method><signal name=\"PropertiesChanged\"><arg type=\"s\"/><arg type=\"a{sv}\"/><arg type=\"as\"/></signal></interface></node>"_str);
    }
    return Ok(rstd::move(xml));
}
auto rejected(Error error) -> rstd::async::coro<Result<Body>> {
    co_return Err(rstd::move(error));
}
auto reject_call(ref<str> error, ref<str> reason) -> rstd::async::coro<Result<Body>> {
    return rejected(Error::Remote(String::make(error), String::make(reason)));
}
auto proxy_owned(Connection connection, Message call, String output)
    -> rstd::async::coro<Result<Body>> {
    auto response = co_await connection.call(rstd::move(call));
    if (response.is_err()) co_return Err(rstd::move(response).unwrap_err());
    if (body_signature(response->body) != output) co_return Err(type_error());
    co_return Ok(rstd::move(response->body));
}
auto proxy_call(Connection connection,
                String     destination,
                String     path,
                String     interface,
                MethodSpec method,
                Body       body) -> rstd::async::coro<Result<Body>> {
    if (body_signature(body).as_str() != method.input) return rejected(type_error());
    auto call = method_call(
        destination.as_str(), path.as_str(), interface.as_str(), method.name, rstd::move(body));
    return proxy_owned(rstd::move(connection), rstd::move(call), String::make(method.output));
}
auto checked_owned(rstd::async::coro<Result<Body>> work, String output)
    -> rstd::async::coro<Result<Body>> {
    auto result = co_await rstd::move(work);
    if (result.is_ok() && body_signature(*result) != output) co_return Err(type_error());
    co_return result;
}
auto checked_reply(rstd::async::coro<Result<Body>> work, ref<str> output)
    -> rstd::async::coro<Result<Body>> {
    return checked_owned(rstd::move(work), String::make(output));
}
} // namespace dbusto
