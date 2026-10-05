# FoundationThreading

Bounded reusable CPU task graphs, with native worker pools and the same serial
executor on native/browser builds. Link `Ludus::FoundationThreading` and include
`<ludus/foundation/threading/jobs.hpp>`. See the
[architecture and reference review](../../../docs/architecture/threading.md).

```cpp
#include <ludus/foundation/threading/jobs.hpp>

namespace jobs = ludus::foundation::threading;

jobs::JobOutcome Compute(void* context) noexcept
{
    ++*static_cast<ludus::foundation::uint32*>(context);
    return jobs::JobOutcome::Succeeded;
}

int main()
{
    ludus::foundation::uint32 output = 0;
    // Contexts must outlive the graph/system, including failure paths.
    jobs::JobGraph graph;
    jobs::JobSystem system;
    jobs::JobHandle handle;
    if (graph.Initialize({1, 0}) != jobs::JobStatus::Ok ||
        system.Initialize({2}) != jobs::JobStatus::Ok ||
        graph.Add(Compute, &output, handle) != jobs::JobStatus::Ok ||
        graph.Seal() != jobs::JobStatus::Ok ||
        system.Submit(graph) != jobs::JobStatus::Ok)
    {
        return 1;
    }
    jobs::JobOutcome result;
    if (system.Wait(result) != jobs::JobStatus::Ok || result != jobs::JobOutcome::Succeeded)
    {
        return 1;
    }
    return output == 1 ? 0 : 1;
}
```

The default worker budget is zero. Native consumers explicitly select their CPU
budget. Browser builds reject a nonzero worker count. One graph remains admitted
until Wait, even after callbacks finish. Submit of a sealed graph may be repeated;
Reset invalidates its handles before rebuilding. Cancel skips queued callbacks;
running callbacks finish. GetOutcome and Snapshot expose progress.

Build/reset and pool lifecycle belong to one application owner. Wait may help
execute CPU work on its caller; use `help=false` to preserve caller responsiveness
or TLS expectations. Jobs never wait on jobs or synchronous I/O and never call
pool lifecycle methods. The owner retains immutable inputs and exclusive output
ranges through Wait, then applies results in stable order. All errors use statuses;
the engine library compiles with exceptions disabled.
