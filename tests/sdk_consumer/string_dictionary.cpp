#include <ludus/foundation/base/types.h>

#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/memory/allocation_domain.hpp>

#include <ludus/content/string_dictionary/string_dictionary.h>
#include <ludus/content/string_dictionary/string_dictionary_writer.h>

#include <string_view>

// Cook a dictionary with the installed writer, then validate and query it with
// the installed reader, checking that record indices survive the remap.
bool ExerciseInstalledStringDictionary() noexcept
{
    using namespace ludus::content::string_dictionary;
    using ludus::foundation::Array;
    using ludus::foundation::GetSystemAllocationDomain;
    using ludus::foundation::StringIndex;
    using ludus::foundation::uint8;

    StringDictionaryWriter writer;
    StringIndex provisional;
    StringIndex remapped;
    RevisionDigest revision{};
    revision[0] = 1;
    if (CreateStringDictionaryWriter({}, GetSystemAllocationDomain(), writer) != StringDictionaryStatus::Ok ||
        writer.TryAdd("zebra", provisional) != StringDictionaryStatus::Ok ||
        writer.TryAdd("apple", provisional) != StringDictionaryStatus::Ok ||
        writer.TryFinish() != StringDictionaryStatus::Ok ||
        writer.TryRemap(provisional, remapped) != StringDictionaryStatus::Ok)
    {
        return false;
    }
    Array<uint8> bytes;
    if (bytes.TryResize(writer.GetEncodedSize()) == false ||
        writer.TryEncode(revision, {bytes.GetData(), bytes.GetSize()}) != StringDictionaryStatus::Ok)
    {
        return false;
    }
    StringDictionaryView view;
    StringDictionaryDiagnostic diagnostic;
    std::string_view spelling;
    StringIndex found;
    return ReadStringDictionary({bytes.GetData(), bytes.GetSize()}, revision, view, diagnostic) ==
               StringDictionaryStatus::Ok &&
           view.TryResolve(remapped, spelling) == StringDictionaryStatus::Ok && spelling == "apple" &&
           view.TryFind("zebra", found) == StringDictionaryStatus::Ok && found.Value == 2;
}
