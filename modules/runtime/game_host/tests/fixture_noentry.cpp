// A gameplay-module-shaped shared object that deliberately exports no
// LudusGetGameApi entry. The loader must reject it with EntryMissing, proving
// ordinary loader failures are explicit and distinct from crashes (design 6).

extern "C" int ludus_fixture_noentry_marker() noexcept
{
    return 0;
}
