module;
#include <expected>
#include <string>
#include <string_view>
#include <vector>
export module dbusto.macros;
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
                owned[0] == "\"dbusto-macros::interface\"")
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
    std::vector<Method> methods;
    std::size_t         brace = input.item.size();
    for (std::size_t i = 0; i < input.item.size(); ++i)
        if (input.item[i].kind() == pmacro::TokenKind::Group &&
            input.item[i].delimiter() == pmacro::Delimiter::Brace) {
            brace = i;
            break;
        }
    if (brace == input.item.size()) return fail("dbusto interface requires a struct definition");
    auto                     body = input.item[brace].children();
    std::vector<std::string> pending;
    for (std::size_t i = 0; i < body.size(); ++i) {
        auto token = body[i];
        if (token.kind() == pmacro::TokenKind::Group &&
            token.delimiter() == pmacro::Delimiter::Bracket) {
            auto texts = literals(token.children());
            if (! texts.empty() && texts[0] == "\"dbusto-macros::interface\"") {
                if (! pending.empty() || texts.size() != 5 || texts[1] != "\"method\"")
                    return fail("dbusto method helper requires member, input and output strings");
                pending = std::move(texts);
            }
        }
        if (! pending.empty() && token.spelling() == "auto" && i + 1 < body.size()) {
            Method method {
                std::string(body[i + 1].spelling()), pending[2], pending[3], pending[4]
            };
            if (! valid_name(unquote(method.name), false)) return fail("invalid D-Bus member name");
            for (const auto& previous : methods)
                if (previous.name == method.name) return fail("duplicate D-Bus member name");
            methods.push_back(std::move(method));
            pending.clear();
        }
    }
    if (! pending.empty() || methods.empty())
        return fail("dbusto interface requires annotated methods");
    std::string generated = " public: static auto dbus_interface() -> rstd::ref<rstd::str> { using "
                            "namespace rstd::literals; return " +
                            args[0] + "_str; } ";
    generated += "static auto dbus_methods() -> rstd::slice<dbusto::MethodSpec> { using namespace "
                 "rstd::literals; static const rstd::array<dbusto::MethodSpec," +
                 std::to_string(methods.size()) + "> methods {";
    for (const auto& method : methods)
        generated += "dbusto::MethodSpec {" + method.name + "_str," + method.input + "_str," +
                     method.output + "_str},";
    generated +=
        "}; return methods.as_slice(); } auto dbus_dispatch(const dbusto::Message& call) -> "
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
    std::string result;
    for (std::size_t i = 0; i < input.item.size(); ++i) {
        if (i == brace)
            result += "{" + render(body) + generated + "} ";
        else {
            auto one = context.stream();
            if (! one || ! one->append(input.item[i])) return fail("unable to render interface");
            result += render(*one);
        }
    }
    return context.parse(result);
}
