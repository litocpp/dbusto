module dbusto;
import :binding;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace dbusto
{
using rstd::sync::Arc;
const rstd::array<MethodSpec, 3> properties_methods {
    MethodSpec { "Get"_str, "ss"_str, "v"_str },
    MethodSpec { "GetAll"_str, "s"_str, "a{sv}"_str },
    MethodSpec { "Set"_str, "ssv"_str, ""_str }
};
const rstd::array<SignalSpec, 1> properties_signals { SignalSpec { "PropertiesChanged"_str,
                                                                   "sa{sv}as"_str } };
const rstd::array<MethodSpec, 1> introspect_methods {
    MethodSpec { "Introspect"_str, ""_str, "s"_str }
};
const rstd::array<MethodSpec, 1> peer_methods { MethodSpec { "Ping"_str, ""_str, ""_str } };
auto                             properties_interface() -> InterfaceSpec {
    return { "org.freedesktop.DBus.Properties"_str,
             properties_methods.as_slice(),
             {},
             properties_signals.as_slice() };
}
auto standard_interface(ref<str> name) -> bool {
    return name == "org.freedesktop.DBus.Properties"_str ||
           name == "org.freedesktop.DBus.Introspectable"_str ||
           name == "org.freedesktop.DBus.Peer"_str;
}
auto remote_error(ref<str> name, ref<str> reason) -> Error {
    return Error::Remote(String::make(name), String::make(reason));
}
auto validate(InterfaceSpec interface) -> Result<empty> {
    if (! valid_interface(interface.name)) return Err(type_error());
    for (usize i; i < interface.methods.len(); ++i) {
        const auto& method = interface.methods[i];
        if (! valid_member(method.name)) return Err(type_error());
        auto input  = Signature::parse(method.input);
        auto output = Signature::parse(method.output);
        if (input.is_err() || output.is_err()) return Err(type_error());
        for (usize j; j < i; ++j)
            if (interface.methods[j].name == method.name) return Err(type_error());
    }
    for (usize i; i < interface.properties.len(); ++i) {
        const auto& property = interface.properties[i];
        auto        type     = Signature::parse(property.signature);
        if (! valid_member(property.name) || type.is_err() || type->types().len() != usize(1))
            return Err(type_error());
        if (property.access != PropertyAccess::Read && property.access != PropertyAccess::Write &&
            property.access != PropertyAccess::ReadWrite)
            return Err(type_error());
        if (property.emits_changed != EmitsChangedSignal::True &&
            property.emits_changed != EmitsChangedSignal::False &&
            property.emits_changed != EmitsChangedSignal::Const &&
            property.emits_changed != EmitsChangedSignal::Invalidates)
            return Err(type_error());
        if (property.emits_changed == EmitsChangedSignal::Const &&
            property.access != PropertyAccess::Read)
            return Err(type_error());
        for (usize j; j < i; ++j)
            if (interface.properties[j].name == property.name) return Err(type_error());
    }
    for (usize i; i < interface.signals.len(); ++i) {
        const auto& signal = interface.signals[i];
        if (! valid_member(signal.name) || Signature::parse(signal.signature).is_err())
            return Err(type_error());
        for (usize j; j < i; ++j)
            if (interface.signals[j].name == signal.name) return Err(type_error());
    }
    return Ok(empty {});
}
auto append_args(String& xml, ref<str> signature, Option<ref<str>> direction) -> void {
    auto parsed = Signature::parse(signature).unwrap();
    for (const auto& type : parsed.types()) {
        xml.push_str(rstd::format("<arg type=\"{}\"", type.text).as_str());
        if (direction.is_some())
            xml.push_str(rstd::format(" direction=\"{}\"", *direction).as_str());
        xml.push_str("/>"_str);
    }
}
auto append_interface(String& xml, InterfaceSpec interface) -> void {
    xml.push_str(rstd::format("<interface name=\"{}\">", interface.name).as_str());
    for (const auto& method : interface.methods) {
        xml.push_str(rstd::format("<method name=\"{}\">", method.name).as_str());
        append_args(xml, method.input, Some("in"_str));
        append_args(xml, method.output, Some("out"_str));
        xml.push_str("</method>"_str);
    }
    for (const auto& property : interface.properties) {
        auto access = "read"_str;
        switch (property.access) {
        case PropertyAccess::Read: break;
        case PropertyAccess::Write: access = "write"_str; break;
        case PropertyAccess::ReadWrite: access = "readwrite"_str; break;
        }
        auto emits = "true"_str;
        switch (property.emits_changed) {
        case EmitsChangedSignal::True: break;
        case EmitsChangedSignal::False: emits = "false"_str; break;
        case EmitsChangedSignal::Const: emits = "const"_str; break;
        case EmitsChangedSignal::Invalidates: emits = "invalidates"_str; break;
        }
        xml.push_str(rstd::format("<property name=\"{}\" type=\"{}\" access=\"{}\"><annotation "
                                  "name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" "
                                  "value=\"{}\"/></property>",
                                  property.name,
                                  property.signature,
                                  access,
                                  emits)
                         .as_str());
    }
    for (const auto& signal : interface.signals) {
        xml.push_str(rstd::format("<signal name=\"{}\">", signal.name).as_str());
        append_args(xml, signal.signature, None());
        xml.push_str("</signal>"_str);
    }
    xml.push_str("</interface>"_str);
}
auto introspection(slice<InterfaceSpec> interfaces) -> Result<String> {
    auto xml = String::make("<node>"_str);
    for (usize i; i < interfaces.len(); ++i) {
        auto checked = validate(interfaces[i]);
        if (checked.is_err()) return Err(rstd::move(checked).unwrap_err());
        if (standard_interface(interfaces[i].name)) return Err(type_error());
        for (usize j; j < i; ++j)
            if (interfaces[j].name == interfaces[i].name) return Err(type_error());
        append_interface(xml, interfaces[i]);
    }
    append_interface(xml, properties_interface());
    append_interface(
        xml, { "org.freedesktop.DBus.Introspectable"_str, introspect_methods.as_slice(), {}, {} });
    append_interface(xml, { "org.freedesktop.DBus.Peer"_str, peer_methods.as_slice(), {}, {} });
    xml.push_str("</node>"_str);
    return Ok(rstd::move(xml));
}
auto introspection(ref<str> interface, slice<MethodSpec> methods, slice<PropertySpec> properties)
    -> Result<String> {
    const rstd::array<InterfaceSpec, 1> specs { InterfaceSpec {
        interface, methods, properties, {} } };
    return introspection(specs.as_slice());
}
auto property_spec(InterfaceSpec interface, ref<str> name) -> Result<PropertySpec> {
    for (const auto& property : interface.properties)
        if (property.name == name) return Ok(property);
    return Err(
        remote_error("org.freedesktop.DBus.Error.UnknownProperty"_str, "unknown property"_str));
}
auto validate_property_value(PropertySpec property, const Value& value) -> Result<empty> {
    if (value.signature().as_str() != property.signature) return Err(type_error());
    return Ok(empty {});
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
    if (result.is_ok() && body_signature(*result) != output)
        co_return Err(remote_error("org.freedesktop.DBus.Error.Failed"_str,
                                   "method returned an invalid signature"_str));
    co_return result;
}
auto checked_reply(rstd::async::coro<Result<Body>> work, ref<str> output)
    -> rstd::async::coro<Result<Body>> {
    return checked_owned(rstd::move(work), String::make(output));
}
} // namespace dbusto
