#include <ludus/ui/context.h>

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace
{
using namespace ludus::ui;
struct ParentIndex final
{
    usize Value = kRoot;
};
Element Box(WidgetId id, ParentIndex parent = {}) noexcept
{
    Element element;
    element.Id = id;
    element.Parent = parent.Value;
    element.Placement.Width = {100, Unit::Pixels};
    element.Placement.Height = {40, Unit::Pixels};
    element.Fill = {0.1f, 0.2f, 0.3f, 1};
    return element;
}
constexpr Viewport kViewport{{0, 0, 320, 200}};
} // namespace

TEST_CASE("UI document replacement is transactional and capacity bounded", "[ui]")
{
    Context ui;
    REQUIRE(ui.TrySetDocument({}, kViewport) == Status::InvalidState);
    REQUIRE(ui.TryInitialize(0) == Status::InvalidDescription);
    REQUIRE(ui.TryInitialize(2) == Status::Ok);
    REQUIRE(ui.TryInitialize(2) == Status::InvalidState);
    auto button = Box(1);
    button.SemanticRole = Role::Button;
    REQUIRE(ui.TrySetDocument({&button, 1}, kViewport) == Status::Ok);
    REQUIRE(ui.TryFocus(1));
    Element bad[] = {button, Box(1)};
    REQUIRE(ui.TrySetDocument(bad, kViewport) == Status::DuplicateId);
    bad[1].Id = 2;
    bad[1].Parent = 1;
    REQUIRE(ui.TrySetDocument(bad, kViewport) == Status::InvalidDescription);
    bad[1].Parent = kRoot;
    bad[1].Placement.Width.Value = std::numeric_limits<float32>::quiet_NaN();
    REQUIRE(ui.TrySetDocument(bad, kViewport) == Status::InvalidDescription);
    const Element over[] = {button, Box(2), Box(3)};
    REQUIRE(ui.TrySetDocument(over, kViewport) == Status::CapacityExceeded);
    REQUIRE(ui.GetLayout().size() == 1);
    REQUIRE(ui.GetPaintCommands()[0].Id == 1);
    REQUIRE(ui.Route({EventKind::Activate}).Activated == 1);
    REQUIRE(ui.TrySetDocument({}, kViewport) == Status::Ok);
    REQUIRE(ui.Route({EventKind::FocusNext}).Focused == 0);
}

TEST_CASE("UI flow, safe viewport and fractional sizes share one clip", "[ui]")
{
    Context ui;
    REQUIRE(ui.TryInitialize(4) == Status::Ok);
    Element elements[] = {Box(1), Box(2, {0}), Box(3, {0}), Box(4, {2})};
    auto& root = elements[0].Placement;
    root.Width = {1, Unit::Fraction};
    root.Height = {80, Unit::Pixels};
    root.Vertical = Align::End;
    root.Children = Flow::Row;
    root.Padding = 10;
    root.Gap = 8;
    elements[1].Placement.Width = {0.5f, Unit::Fraction};
    elements[2].Placement.Width = {200, Unit::Pixels};
    elements[3].Placement.Width = {1, Unit::Fraction};
    REQUIRE(ui.TrySetDocument(elements, {{16, 24, 300, 180}}) == Status::Ok);
    const auto boxes = ui.GetLayout();
    CHECK(boxes[0].Bounds.Y == 124);
    CHECK(boxes[1].Bounds.X == 26);
    CHECK(boxes[1].Bounds.Width == 140);
    CHECK(boxes[2].Bounds.X == 174);
    CHECK(ui.GetPaintCommands()[2].Bounds.Width == 142);
    CHECK(ui.GetPaintCommands()[3].Bounds.Width == 142);
    CHECK(ui.GetStats().LayoutVisits == 4);
}

TEST_CASE("UI paint order matches blocking and pointer capture", "[ui]")
{
    Context ui;
    REQUIRE(ui.TryInitialize(3) == Status::Ok);
    Element elements[] = {Box(1), Box(2), Box(3)};
    elements[0].SemanticRole = Role::Button;
    elements[1].SemanticRole = Role::Button;
    // Topmost decoration is pointer-transparent, despite opaque paint.
    REQUIRE(ui.TrySetDocument(elements, kViewport) == Status::Ok);
    auto down = ui.Route({EventKind::PointerDown, 20, 20, 7});
    REQUIRE(down.Consumed);
    CHECK(down.Focused == 2);
    CHECK_FALSE(ui.Route({EventKind::PointerUp, 20, 20, 8}).Consumed);
    auto up = ui.Route({EventKind::PointerUp, 20, 20, 7});
    CHECK(up.Activated == 2);
    CHECK(ui.Route({EventKind::PointerDown, 20, 20, 7}).Consumed);
    up = ui.Route({EventKind::PointerUp, 200, 180, 7});
    CHECK(up.Consumed);
    CHECK(up.Activated == 0);
    elements[2].BlockPointer = true;
    REQUIRE(ui.TrySetDocument(elements, kViewport) == Status::Ok);
    CHECK(ui.Route({EventKind::PointerDown, 20, 20, 7}).Consumed);
    CHECK(ui.Route({EventKind::PointerUp, 20, 20, 7}).Activated == 0);
}

TEST_CASE("UI removed or disabled capture never leaks release to gameplay", "[ui]")
{
    Context ui;
    REQUIRE(ui.TryInitialize(1) == Status::Ok);
    auto button = Box(1);
    button.SemanticRole = Role::Button;
    REQUIRE(ui.TrySetDocument({&button, 1}, kViewport) == Status::Ok);
    REQUIRE(ui.Route({EventKind::PointerDown, 20, 20, 2}).Consumed);
    REQUIRE(ui.TrySetDocument({}, kViewport) == Status::Ok);
    auto up = ui.Route({EventKind::PointerUp, 20, 20, 2});
    CHECK(up.Consumed);
    CHECK(up.Activated == 0);
    REQUIRE(ui.TrySetDocument({&button, 1}, kViewport) == Status::Ok);
    REQUIRE(ui.Route({EventKind::PointerDown, 20, 20, 2}).Consumed);
    button.Enabled = false;
    REQUIRE(ui.TrySetDocument({&button, 1}, kViewport) == Status::Ok);
    CHECK(ui.Route({EventKind::PointerUp, 20, 20, 2}).Activated == 0);
    CHECK_FALSE(ui.TryFocus(1));
    CHECK(ui.Route({EventKind::FocusNext}).Focused == 0);
}

TEST_CASE("UI clipped and inherited hidden elements cannot receive focus", "[ui]")
{
    Context ui;
    REQUIRE(ui.TryInitialize(3) == Status::Ok);
    Element elements[] = {Box(1), Box(2, {0}), Box(3, {1})};
    elements[2].SemanticRole = Role::Button;
    elements[0].Visible = false;
    REQUIRE(ui.TrySetDocument(elements, kViewport) == Status::Ok);
    CHECK(ui.GetPaintCommands().empty());
    CHECK_FALSE(ui.TryFocus(3));
    elements[0].Visible = true;
    elements[1].Placement.OffsetX = 110;
    REQUIRE(ui.TrySetDocument(elements, kViewport) == Status::Ok);
    CHECK_FALSE(ui.TryFocus(3));
    // Removing clipping at both ancestors allows an overflowing descendant.
    elements[0].ClipChildren = false;
    elements[1].ClipChildren = false;
    REQUIRE(ui.TrySetDocument(elements, kViewport) == Status::Ok);
    CHECK(ui.TryFocus(3));
    CHECK(ui.Route({EventKind::Activate}).Activated == 3);
}

TEST_CASE("UI focus order survives document reorder and cancels on focus loss", "[ui]")
{
    Context ui;
    REQUIRE(ui.TryInitialize(3) == Status::Ok);
    Element elements[] = {Box(10), Box(20), Box(30)};
    for (auto& element : elements)
    {
        element.SemanticRole = Role::Button;
    }
    elements[1].Enabled = false;
    REQUIRE(ui.TrySetDocument(elements, kViewport) == Status::Ok);
    CHECK(ui.Route({EventKind::FocusNext}).Focused == 10);
    CHECK(ui.Route({EventKind::FocusNext}).Focused == 30);
    CHECK(ui.Route({EventKind::FocusNext}).Focused == 10);
    CHECK(ui.Route({EventKind::FocusPrevious}).Focused == 30);
    const Element reordered[] = {elements[2], elements[0]};
    REQUIRE(ui.TrySetDocument(reordered, kViewport) == Status::Ok);
    CHECK(ui.Route({EventKind::Activate}).Activated == 30);
    CHECK(ui.Route({EventKind::FocusLost}).Focused == 0);
    CHECK(ui.Route({EventKind::Activate}).Activated == 0);
}

TEST_CASE("UI rejects overflow and invalid enums without publishing partial paint", "[ui]")
{
    Context ui;
    REQUIRE(ui.TryInitialize(2) == Status::Ok);
    Element elements[] = {Box(1), Box(2, {0})};
    REQUIRE(ui.TrySetDocument(elements, kViewport) == Status::Ok);
    elements[0].Placement.OffsetX = std::numeric_limits<float32>::max();
    elements[0].Placement.Width.Value = std::numeric_limits<float32>::max();
    CHECK(ui.TrySetDocument(elements, kViewport) == Status::InvalidDescription);
    CHECK(ui.GetLayout()[0].Bounds.X == 0);
    elements[0] = Box(1);
    elements[1].Placement.Children = static_cast<Flow>(255);
    CHECK(ui.TrySetDocument(elements, kViewport) == Status::InvalidDescription);
    elements[1] = Box(2, {0});
    elements[1].Fill.A = 2;
    CHECK(ui.TrySetDocument(elements, kViewport) == Status::InvalidDescription);
    elements[1].Fill.A = 1;
    elements[0].Enabled = false;
    elements[1].SemanticRole = Role::Button;
    REQUIRE(ui.TrySetDocument(elements, kViewport) == Status::Ok);
    CHECK_FALSE(ui.TryFocus(2));
}
