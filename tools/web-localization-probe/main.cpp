#include <ludus/localization/catalog.hpp>

#include "fixture.hpp"

using namespace ludus::localization;

// Run the production cooker/reader/binder through wasm32. Exit status is the
// machine-readable result; this corpus makes no browser text-rendering claim.
int main()
{
    Catalog source;
    Diagnostic diagnostic;
    const auto& domain = ludus::foundation::GetSystemAllocationDomain();
    if (PrepareCatalog(kSourceCatalog, domain, source, diagnostic) != Status::Ok)
    {
        return 1;
    }
    Catalog retained = source;
    MessageBinding binding;
    ResolvedText text;
    if (source.BindMessage(localization_fixture::kUnicode, binding) != Status::Ok ||
        source.ResolveStatic(binding, text) != Status::Ok || text.Text != "العربية 日本語 😀 é")
    {
        return 2;
    }
    if (PrepareCatalog(kFrenchCatalog, domain, source, diagnostic) != Status::Ok ||
        source.ResolveStatic(binding, text) != Status::WrongCatalog ||
        retained.ResolveStatic(binding, text) != Status::Ok || text.Text != "العربية 日本語 😀 é")
    {
        return 3;
    }
    if (source.BindMessage(localization_fixture::kMenuPlay, binding) != Status::Ok ||
        source.ResolveStatic(binding, text) != Status::Ok || text.Text != "Jouer" ||
        text.Provenance != Origin::Translation)
    {
        return 4;
    }
    if (PrepareCatalog(kFallbackCatalog, domain, source, diagnostic) != Status::Ok ||
        source.BindMessage(localization_fixture::kMenuPlay, binding) != Status::Ok ||
        source.ResolveStatic(binding, text) != Status::Ok || text.Provenance != Origin::SourceFallback)
    {
        return 5;
    }
    for (usize size = 0; size < sizeof(kSourceCatalog); ++size)
    {
        if (PrepareCatalog(std::span{kSourceCatalog}.first(size), domain, source, diagnostic) == Status::Ok)
        {
            return 6;
        }
    }
    return 0;
}
