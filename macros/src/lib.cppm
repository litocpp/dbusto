export module dbusto.macros;
import std;
import pmacro;
namespace
{
using namespace std::literals;
struct Method {
    std::string cpp;
    std::string name;
    std::string input;
    std::string output;
};
struct Property {
    std::string cpp;
    std::string name;
    std::string signature;
    std::string access;
    std::string emits;
    std::string setter;
};
struct Signal {
    std::string cpp;
    std::string name;
    std::string signature;
};
auto literals(pmacro::TokenStream stream) -> std::vector<std::string> {
    std::vector<std::string> out;
    for (auto token : stream) {
        if (token.kind() == pmacro::TokenKind::Literal) out.emplace_back(token.spelling());
        if (token.kind() == pmacro::TokenKind::Group)
            for (auto& text : literals(token.children())) out.push_back(std::move(text));
    }
    return out;
}
auto render(pmacro::TokenStream stream) -> std::string {
    std::string out;
    for (auto token : stream) {
        if (token.kind() == pmacro::TokenKind::Group) {
            auto owned = literals(token.children());
            if (token.delimiter() == pmacro::Delimiter::Bracket && ! owned.empty() &&
                owned[0] == "\"dbusto-macro::interface\"")
                continue;
            auto delimiter = token.delimiter();
            char a         = delimiter == pmacro::Delimiter::Brace         ? '{'
                             : delimiter == pmacro::Delimiter::Bracket     ? '['
                             : delimiter == pmacro::Delimiter::Parenthesis ? '('
                                                                           : ' ';
            char b         = delimiter == pmacro::Delimiter::Brace         ? '}'
                             : delimiter == pmacro::Delimiter::Bracket     ? ']'
                             : delimiter == pmacro::Delimiter::Parenthesis ? ')'
                                                                           : ' ';
            out += a;
            out += render(token.children());
            out += b;
            out += ' ';
        } else {
            out += token.spelling();
            if (token.spacing() != pmacro::Spacing::Joint) out += ' ';
        }
    }
    return out;
}
auto unquote(std::string_view text) -> std::string {
    return text.size() >= 2 ? std::string(text.substr(1, text.size() - 2)) : std::string();
}
auto valid_name(std::string_view name, bool dotted) -> bool {
    bool first = true;
    bool dot   = false;
    if (name.empty() || name.size() > 255) return false;
    for (char c : name) {
        if (dotted && c == '.') {
            if (first) return false;
            first = true;
            dot   = true;
            continue;
        }
        if (! (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (! first && c >= '0' && c <= '9')))
            return false;
        first = false;
    }
    return ! first && (! dotted || dot);
}
} // namespace
[[pmacro::define]]
auto interface(pmacro::AttributeInput input, pmacro::Context& context) noexcept
    -> std::expected<pmacro::TokenStream, pmacro::Error> {
    auto fail = [&](std::string_view message) -> std::expected<pmacro::TokenStream, pmacro::Error> {
        context.diagnostic(pmacro::DiagnosticLevel::Error, input.item_span, message);
        return std::unexpected(pmacro::Error::InvalidArgument);
    };
    auto args = literals(input.arguments);
    if (args.size() != 1 || ! valid_name(unquote(args[0]), true))
        return fail("dbusto interface requires one interface name string");
    std::vector<Method>                              methods;
    std::vector<Property>                            properties;
    std::vector<Signal>                              signals;
    std::vector<std::pair<std::size_t, std::size_t>> removed;
    std::size_t                                      brace = input.item.size();
    for (std::size_t i = 0; i < input.item.size(); ++i)
        if (input.item[i].kind() == pmacro::TokenKind::Group &&
            input.item[i].delimiter() == pmacro::Delimiter::Brace) {
            brace = i;
            break;
        }
    if (brace == input.item.size()) return fail("dbusto interface requires a struct definition");
    auto                     body = input.item[brace].children();
    std::vector<std::string> pending;
    std::size_t              pending_start {};
    for (std::size_t i = 0; i < body.size(); ++i) {
        auto token = body[i];
        if (token.kind() == pmacro::TokenKind::Group &&
            token.delimiter() == pmacro::Delimiter::Bracket) {
            auto texts = literals(token.children());
            if (! texts.empty() && texts[0] == "\"dbusto-macro::interface\"") {
                if (! pending.empty() || texts.size() < 2)
                    return fail("invalid dbusto member helper");
                pending       = std::move(texts);
                pending_start = i;
            }
        }
        if (! pending.empty() && token.spelling() == "auto" && i + 1 < body.size()) {
            const auto cpp = std::string(body[i + 1].spelling());
            if (! valid_name(cpp, false)) return fail("invalid C++ handler name");
            if (pending[1] == "\"method\"") {
                if (pending.size() != 5)
                    return fail("dbusto method helper requires member, input and output strings");
                Method method { cpp, pending[2], pending[3], pending[4] };
                if (! valid_name(unquote(method.name), false))
                    return fail("invalid D-Bus method name");
                for (const auto& previous : methods)
                    if (previous.name == method.name) return fail("duplicate D-Bus method name");
                methods.push_back(std::move(method));
            } else if (pending[1] == "\"property\"") {
                if (pending.size() != 6 && pending.size() != 7)
                    return fail("dbusto property requires name, signature, access, notification "
                                "and optional setter");
                auto access = unquote(pending[4]);
                auto emits  = unquote(pending[5]);
                if ((access != "read" && access != "readwrite") ||
                    (access == "readwrite") != (pending.size() == 7))
                    return fail("readwrite property requires exactly one setter; read property "
                                "forbids a setter");
                if (emits != "true" && emits != "false" && emits != "const" &&
                    emits != "invalidates")
                    return fail("invalid EmitsChangedSignal policy");
                if (emits == "const" && access != "read")
                    return fail("const property cannot be writable");
                if (! valid_name(unquote(pending[2]), false) ||
                    (pending.size() == 7 && ! valid_name(unquote(pending[6]), false)))
                    return fail("invalid property or setter name");
                for (const auto& previous : properties)
                    if (previous.name == pending[2]) return fail("duplicate D-Bus property name");
                properties.push_back({ cpp,
                                       pending[2],
                                       pending[3],
                                       access,
                                       emits,
                                       pending.size() == 7 ? unquote(pending[6]) : "" });
            } else if (pending[1] == "\"signal\"") {
                if (pending.size() != 4 || ! valid_name(unquote(pending[2]), false))
                    return fail("dbusto signal requires member and signature strings");
                for (const auto& previous : signals)
                    if (previous.name == pending[2]) return fail("duplicate D-Bus signal name");
                if (i + 2 >= body.size() || body[i + 2].kind() != pmacro::TokenKind::Group ||
                    body[i + 2].delimiter() != pmacro::Delimiter::Parenthesis ||
                    body[i + 2].children().size() != 0)
                    return fail("signal descriptor must be a parameterless declaration");
                auto end = i + 3;
                while (end < body.size() && body[end].spelling() != ";") {
                    if (body[end].kind() == pmacro::TokenKind::Group &&
                        body[end].delimiter() == pmacro::Delimiter::Brace)
                        return fail("signal descriptor must not have a body");
                    ++end;
                }
                if (end == body.size()) return fail("unterminated signal descriptor");
                removed.emplace_back(pending_start, end);
                signals.push_back({ cpp, pending[2], pending[3] });
                i = end;
            } else
                return fail("unknown dbusto member helper");
            pending.clear();
        }
    }
    if (! pending.empty() || (methods.empty() && properties.empty() && signals.empty()))
        return fail("dbusto interface requires annotated members");
    std::string generated = " public: static auto dbus_interface() -> rstd::ref<rstd::str> { using "
                            "namespace rstd::literals; return " +
                            args[0] + "_str; } ";
    generated += "static auto dbus_methods() -> rstd::slice<dbusto::MethodSpec> { using namespace "
                 "rstd::literals; static const rstd::array<dbusto::MethodSpec," +
                 std::to_string(methods.size()) + "> methods {";
    for (const auto& method : methods)
        generated += "dbusto::MethodSpec {" + method.name + "_str," + method.input + "_str," +
                     method.output + "_str},";
    generated += "}; return methods.as_slice(); } ";
    generated += "static auto dbus_properties() -> rstd::slice<dbusto::PropertySpec> { using "
                 "namespace rstd::literals; static const rstd::array<dbusto::PropertySpec," +
                 std::to_string(properties.size()) + "> properties {";
    for (const auto& property : properties) {
        auto access = property.access == "read" ? "Read" : "ReadWrite";
        auto emits  = property.emits == "true"    ? "True"
                      : property.emits == "false" ? "False"
                      : property.emits == "const" ? "Const"
                                                  : "Invalidates";
        generated += "dbusto::PropertySpec{" + property.name + "_str," + property.signature +
                     "_str,dbusto::PropertyAccess::" + access +
                     ",dbusto::EmitsChangedSignal::" + emits + "},";
    }
    generated += "}; return properties.as_slice(); } ";
    generated += "static auto dbus_signals() -> rstd::slice<dbusto::SignalSpec> { using namespace "
                 "rstd::literals; static const rstd::array<dbusto::SignalSpec," +
                 std::to_string(signals.size()) + "> signals {";
    for (const auto& signal : signals)
        generated += "dbusto::SignalSpec{" + signal.name + "_str," + signal.signature + "_str},";
    generated += "}; return signals.as_slice(); } ";
    for (std::size_t i = 0; i < signals.size(); ++i)
        generated += "static auto " + signals[i].cpp +
                     "() -> dbusto::SignalSpec { return dbus_signals()[rstd::usize(" +
                     std::to_string(i) + ")]; } ";
    generated += "static auto dbus_spec() -> dbusto::InterfaceSpec { return "
                 "{dbus_interface(),dbus_methods(),dbus_properties(),dbus_signals()}; } ";
    generated +=
        "auto dbus_get(rstd::ref<rstd::str> name) -> "
        "rstd::async::coro<dbusto::Result<dbusto::Value>> { using namespace rstd::literals; ";
    for (const auto& property : properties)
        generated += "if(name == " + property.name + "_str) return " + property.cpp + "(); ";
    generated += "return "
                 "dbusto::reject<dbusto::Value>(dbusto::remote_error(\"org.freedesktop.DBus.Error."
                 "UnknownProperty\"_str,\"unknown property\"_str)); } ";
    generated +=
        "auto dbus_set(rstd::ref<rstd::str> name, const dbusto::Value& value) -> "
        "rstd::async::coro<dbusto::Result<rstd::empty>> { using namespace rstd::literals; ";
    for (const auto& property : properties)
        if (! property.setter.empty())
            generated +=
                "if(name == " + property.name + "_str) return " + property.setter + "(value); ";
    generated += "return "
                 "dbusto::reject<rstd::empty>(dbusto::remote_error(\"org.freedesktop.DBus.Error."
                 "PropertyReadOnly\"_str,\"property is read only\"_str)); } ";
    generated +=
        "auto dbus_dispatch(const dbusto::Message& call) -> "
        "rstd::async::coro<dbusto::Result<dbusto::Body>> { using namespace rstd::literals; ";
    generated += "if(call.interface.as_str() != dbus_interface()) return "
                 "dbusto::reject_call(\"org.freedesktop.DBus.Error.UnknownInterface\"_str,"
                 "\"unknown interface\"_str); ";
    for (const auto& method : methods) {
        generated += "if(call.member.as_str() == " + method.name +
                     "_str) { if(dbusto::body_signature(call.body).as_str() != " + method.input +
                     "_str) return "
                     "dbusto::reject_call(\"org.freedesktop.DBus.Error.InvalidArgs\"_str,\"invalid "
                     "method signature\"_str); return dbusto::checked_reply(" +
                     method.cpp + "(call)," + method.output + "_str); } ";
    }
    generated +=
        "return dbusto::reject_call(\"org.freedesktop.DBus.Error.UnknownMethod\"_str,\"unknown "
        "method\"_str); } ";
    generated += "struct Proxy { dbusto::Connection connection; rstd::prelude::String destination; "
                 "rstd::prelude::String path; ";
    for (std::size_t i = 0; i < methods.size(); ++i)
        generated +=
            "auto " + methods[i].cpp +
            "(dbusto::Body body) const -> rstd::async::coro<dbusto::Result<dbusto::Body>> { return "
            "dbusto::proxy_call(connection.clone(),destination.clone(),path.clone(),rstd::prelude::"
            "String::make(dbus_interface()),dbus_methods()[rstd::usize(" +
            std::to_string(i) + ")],rstd::move(body)); } ";
    generated += "}; ";
    auto retained = context.stream();
    if (! retained) return fail("unable to retain interface tokens");
    for (std::size_t i = 0; i < body.size(); ++i) {
        bool skip = false;
        for (const auto& range : removed)
            if (i >= range.first && i <= range.second) skip = true;
        if (! skip && ! retained->append(body[i])) return fail("unable to retain interface token");
    }
    std::string result;
    for (std::size_t i = 0; i < input.item.size(); ++i) {
        if (i == brace)
            result += "{" + render(*retained) + generated + "} ";
        else {
            auto one = context.stream();
            if (! one || ! one->append(input.item[i])) return fail("unable to render interface");
            result += render(*one);
        }
    }
    return context.parse(result);
}
