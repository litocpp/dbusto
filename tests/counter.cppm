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
