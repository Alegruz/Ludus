#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/containers/sorted_map.hpp>

int ExerciseInstalledContainers() noexcept
{
    using namespace ludus::foundation;
    SortedMap<uint64, uint32> resources;
    if (!resources.TryEnsureCapacity(2) || !resources.TryAddInPlace(30, 300U).Inserted ||
        !resources.TryAddInPlace(10, 100U).Inserted)
    {
        return 30;
    }
    const uint32* value = resources.Find(30);
    if (value == nullptr || *value != 300 || resources.AsSpan()[0].Key != 10 || !resources.Remove(10))
    {
        return 31;
    }
    Array<uint32> values;
    if (values.TryInsertAtInPlace(0, 7U) == nullptr || values.TryInsertAt(0, values[0]) == nullptr ||
        values.GetSize() != 2 || values[1] != 7)
    {
        return 32;
    }
    return 0;
}
