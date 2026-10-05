#include "reflection_test_record_schema.hpp"
#include "sample_game_reflection_schema.hpp"
#include <ludus/runtime/property_binding/scalars.hpp>

int main()
{
    using namespace ludus::foundation;
    using namespace ludus::foundation::reflection;
    ludus::reflection_test::Record source;
    uint8 storage[4096]{};
    std::span<const uint8> output;
    Diagnostic error;
    if (ludus::reflection_test::WriteRecordJson(source, storage, output, error) != Status::Ok)
    {
        return 1;
    }
    ludus::reflection_test::Record result;
    result.Unsigned = 0;
    if (ludus::reflection_test::ReadRecordJson({reinterpret_cast<const char*>(output.data()), output.size()},
                                               result,
                                               GetSystemAllocationDomain(),
                                               error) != Status::Ok ||
        result.Unsigned != source.Unsigned)
    {
        return 2;
    }
    ludus::runtime::game_api::PropertyDescriptor descriptors[2]{};
    if (ludus::runtime::property_binding::Describe(ludus::sample::BodySchema(), 0xA001, descriptors, error) !=
            Status::Ok ||
        descriptors[0].PropertyId != 0xB001 || descriptors[0].MaxFloat != 8)
    {
        return 3;
    }
    return 0;
}
