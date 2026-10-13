#include <ludus/foundation/containers/borrowed_view.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation;

TEST_CASE("Borrowed views preserve extent and distinguish invalid input from absence", "[containers]")
{
    const BorrowedView<const uint32> empty;
    CHECK(empty.IsValid() == true);
    CHECK(empty.IsEmpty() == true);
    CHECK(empty.GetDataOrNull() == nullptr);
    const BorrowedView<const uint32> invalid{nullptr, 1};
    CHECK(invalid.IsValid() == false);
    CHECK(invalid.IsEmpty() == false);
    CHECK(invalid.GetSize() == 1);
    uint32 elements[]{7, 9};
    const BorrowedView<uint32> writable{elements};
    writable[1] = 11;
    const BorrowedView<const uint32> readable{elements};
    CHECK(readable.GetSize() == 2);
    CHECK(readable.GetDataOrNull() == elements);
    CHECK(readable[1] == 11);
    const BorrowedView<const uint32> copied = readable;
    CHECK(copied.GetDataOrNull() == elements);
}
