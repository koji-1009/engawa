// echo namespace — the vertical-slice command (conformance echo/limits tests): returns its args
// unchanged, exercising one full request/response round-trip through the wire protocol and the
// large-message / unicode integrity paths. No dot, so the namespace IS "echo" and the command is empty.
#include "adapters/Adapters.hpp"

namespace {
class EchoAdapter : public IAdapter {
public:
    std::string ns() const override { return "echo"; }
    Json handle(const std::string&, const Json& args) override { return args; }
};
}  // namespace

std::unique_ptr<IAdapter> makeEchoAdapter() { return std::make_unique<EchoAdapter>(); }
