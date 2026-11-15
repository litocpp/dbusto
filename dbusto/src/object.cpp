module dbusto;
import :binding;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace dbusto
{
using rstd::sync::Arc;
namespace async = rstd::async;

auto Interface::dispatch(Message call) const -> async::coro<Result<Body>> {
    if (call.type != MessageType::MethodCall) return reject<Body>(type_error());
    auto methods = spec_.methods;
    if (call.interface.as_str() == "org.freedesktop.DBus.Properties"_str) {
        methods = properties_interface().methods;
    } else if (call.interface.as_str() != spec_.name)
        return reject<Body>(remote_error("org.freedesktop.DBus.Error.UnknownInterface"_str,
                                         "unknown interface"_str));
    for (const auto& method : methods) {
        if (call.member.as_str() != method.name) continue;
        if (body_signature(call.body).as_str() != method.input) return reject<Body>(type_error());
        if (call.interface.as_str() == "org.freedesktop.DBus.Properties"_str &&
            call.body[usize()].as_Text().value.as_str() != spec_.name)
            return reject<Body>(remote_error("org.freedesktop.DBus.Error.UnknownInterface"_str,
                                             "unknown property interface"_str));
        return handler_->operator()(rstd::move(call));
    }
    return reject<Body>(
        remote_error("org.freedesktop.DBus.Error.UnknownMethod"_str, "unknown method"_str));
}
struct ObjectFields {
    String         path;
    Vec<Interface> interfaces = Vec<Interface>::make();
};
struct ObjectState {
    rstd::sync::Mutex<ObjectFields> fields;
    explicit ObjectState(String path): fields(ObjectFields { rstd::move(path) }) {}
};
Object::Object(Arc<ObjectState> state): state_(rstd::move(state)) {
}
Object::Object(Object&&) noexcept                    = default;
auto Object::operator=(Object&&) noexcept -> Object& = default;
Object::~Object()                                    = default;
auto Object::make(ref<str> path) -> Result<Object> {
    if (! valid_path(path)) return Err(type_error());
    return Ok(Object(Arc<ObjectState>::make(String::make(path))));
}
auto Object::clone() const -> Object {
    return Object(state_.clone());
}
auto Object::add_interface(Interface interface) -> Result<empty> {
    auto checked = validate(interface.spec());
    if (checked.is_err()) return checked;
    auto name = interface.spec().name;
    if (name == "org.freedesktop.DBus.Properties"_str ||
        name == "org.freedesktop.DBus.Introspectable"_str ||
        name == "org.freedesktop.DBus.Peer"_str)
        return Err(type_error());
    auto fields = state_->fields.lock().unwrap();
    for (const auto& existing : fields->interfaces)
        if (existing.spec().name == name) return Err(Error::Busy());
    fields->interfaces.push(rstd::move(interface));
    return Ok(empty {});
}
auto Object::remove(ref<str> interface) -> bool {
    auto fields = state_->fields.lock().unwrap();
    for (usize i; i < fields->interfaces.len(); ++i)
        if (fields->interfaces[i].spec().name == interface) {
            fields->interfaces.remove(i);
            return true;
        }
    return false;
}
auto object_introspection(const ObjectFields& fields) -> Result<String> {
    auto specs = Vec<InterfaceSpec>::make();
    for (const auto& interface : fields.interfaces) specs.push(interface.spec());
    return introspection(specs.as_slice());
}
auto Object::introspection() const -> Result<String> {
    auto fields = state_->fields.lock().unwrap();
    return object_introspection(*fields);
}
auto object_dispatch(Arc<ObjectState> state, Message call) -> async::coro<Result<Body>> {
    Option<Interface> target;
    {
        auto fields = state->fields.lock().unwrap();
        if (call.type != MessageType::MethodCall) co_return Err(type_error());
        if (call.path != fields->path)
            co_return Err(remote_error("org.freedesktop.DBus.Error.UnknownObject"_str,
                                       "unknown object path"_str));
        auto interface = call.interface.as_str();
        if (interface == "org.freedesktop.DBus.Introspectable"_str) {
            if (call.member.as_str() != "Introspect"_str)
                co_return Err(remote_error("org.freedesktop.DBus.Error.UnknownMethod"_str,
                                           "unknown method"_str));
            if (! call.body.is_empty()) co_return Err(type_error());
            auto xml = object_introspection(*fields);
            if (xml.is_err()) co_return Err(rstd::move(xml).unwrap_err());
            co_return Ok(pack(rstd::move(*xml)));
        }
        if (interface == "org.freedesktop.DBus.Peer"_str) {
            if (call.member.as_str() != "Ping"_str)
                co_return Err(remote_error("org.freedesktop.DBus.Error.UnknownMethod"_str,
                                           "unknown method"_str));
            if (! call.body.is_empty()) co_return Err(type_error());
            co_return Ok(Body::make());
        }
        if (interface == "org.freedesktop.DBus.Properties"_str) {
            Option<MethodSpec> method;
            for (const auto& candidate : properties_interface().methods)
                if (candidate.name == call.member.as_str()) method = Some(MethodSpec(candidate));
            if (method.is_none())
                co_return Err(remote_error("org.freedesktop.DBus.Error.UnknownMethod"_str,
                                           "unknown method"_str));
            if (body_signature(call.body).as_str() != method->input) co_return Err(type_error());
            interface = call.body[usize()].as_Text().value.as_str();
            if (interface == "org.freedesktop.DBus.Properties"_str ||
                interface == "org.freedesktop.DBus.Introspectable"_str ||
                interface == "org.freedesktop.DBus.Peer"_str) {
                if (call.member.as_str() == "GetAll"_str) co_return Ok(pack(PropertyMap::make()));
                co_return Err(remote_error("org.freedesktop.DBus.Error.UnknownProperty"_str,
                                           "unknown property"_str));
            }
        }
        for (const auto& candidate : fields->interfaces)
            if (candidate.spec().name == interface) {
                target = Some(candidate.clone());
                break;
            }
    }
    if (target.is_none())
        co_return Err(remote_error("org.freedesktop.DBus.Error.UnknownInterface"_str,
                                   "unknown interface"_str));
    co_return co_await target->dispatch(rstd::move(call));
}
auto Object::dispatch(Message call) const -> async::coro<Result<Body>> {
    return object_dispatch(state_.clone(), rstd::move(call));
}
auto object_handle(Object object, Connection connection, Message call)
    -> async::coro<Result<empty>> {
    bool reply_expected = call.expects_reply();
    auto reply          = method_return(call);
    auto body           = co_await object.dispatch(rstd::move(call));
    if (! reply_expected) co_return Ok(empty {});
    if (body.is_ok())
        reply.body = rstd::move(*body);
    else {
        auto error = rstd::move(body).unwrap_err();
        reply.type = MessageType::Error;
        if (error.is_Remote()) {
            reply.error_name = rstd::move(error.as_Remote().name);
            reply.body       = pack(rstd::move(error.as_Remote().text));
        } else {
            reply.error_name = String::make("org.freedesktop.DBus.Error.Failed"_str);
            reply.body       = pack(String::make(error.message()));
        }
    }
    co_return co_await connection.send(rstd::move(reply));
}
auto Object::handle(Connection connection, Message call) const -> async::coro<Result<empty>> {
    return object_handle(clone(), rstd::move(connection), rstd::move(call));
}
auto signal(ref<str> path, ref<str> interface, SignalSpec spec, Body body) -> Result<Message> {
    if (! valid_path(path) || ! valid_interface(interface) || ! valid_member(spec.name) ||
        Signature::parse(spec.signature).is_err() ||
        body_signature(body).as_str() != spec.signature)
        return Err(type_error());
    Message message;
    message.type      = MessageType::Signal;
    message.path      = String::make(path);
    message.interface = String::make(interface);
    message.member    = String::make(spec.name);
    message.body      = rstd::move(body);
    return Ok(rstd::move(message));
}
auto Object::properties_changed(ref<str>    interface,
                                PropertyMap changed,
                                Vec<String> invalidated) const -> Result<Message> {
    auto                  fields = state_->fields.lock().unwrap();
    Option<InterfaceSpec> spec;
    for (const auto& candidate : fields->interfaces)
        if (candidate.spec().name == interface) spec = Some(candidate.spec());
    if (spec.is_none())
        return Err(remote_error("org.freedesktop.DBus.Error.UnknownInterface"_str,
                                "unknown interface"_str));
    for (auto [name, value] : changed.iter()) {
        auto property = property_spec(*spec, name->as_str());
        if (property.is_err()) return Err(rstd::move(property).unwrap_err());
        if (property->emits_changed != EmitsChangedSignal::True) return Err(type_error());
        auto checked = validate_property_value(*property, *value);
        if (checked.is_err()) return Err(rstd::move(checked).unwrap_err());
    }
    for (usize i; i < invalidated.len(); ++i) {
        auto property = property_spec(*spec, invalidated[i].as_str());
        if (property.is_err()) return Err(rstd::move(property).unwrap_err());
        if (property->emits_changed != EmitsChangedSignal::Invalidates ||
            changed.contains_key(invalidated[i]))
            return Err(type_error());
        for (usize j; j < i; ++j)
            if (invalidated[i] == invalidated[j]) return Err(type_error());
    }
    return signal(fields->path.as_str(),
                  "org.freedesktop.DBus.Properties"_str,
                  properties_interface().signals[usize()],
                  pack(String::make(interface), rstd::move(changed), rstd::move(invalidated)));
}
SignalEmitter::SignalEmitter(Connection connection, String path)
    : connection_(rstd::move(connection)), path_(rstd::move(path)) {
}
auto SignalEmitter::make(Connection connection, ref<str> path) -> Result<SignalEmitter> {
    if (! valid_path(path)) return Err(type_error());
    return Ok(SignalEmitter(rstd::move(connection), String::make(path)));
}
auto SignalEmitter::clone() const -> SignalEmitter {
    return SignalEmitter(connection_.clone(), path_.clone());
}
auto SignalEmitter::emit(ref<str> interface, SignalSpec spec, Body body) const
    -> async::coro<Result<empty>> {
    auto message = signal(path_.as_str(), interface, spec, rstd::move(body));
    if (message.is_err()) return reject<empty>(rstd::move(message).unwrap_err());
    return connection_.send(rstd::move(*message));
}
} // namespace dbusto
