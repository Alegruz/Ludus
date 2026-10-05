# Networking reference review

The initial architecture was written before this pass. This review uses
`references/game-dev-gems-toc.md` to locate chapters, then reads the local PDFs;
the TOC itself is not evidence for an algorithm's behavior. Scanned GPG3/GPG4
chapters were rendered and OCR-read. Native text was extracted for GPG7/GEG3.
The ignored personal reference library is not copied into the PR.

## Read and adopted

| Article and local locator | Useful contribution | Design/implementation change |
| --- | --- | --- |
| Pete Isensee, **Bit Packing: A Network Compression Technique**, Game Programming Gems 4, 6.5; printed pp. 571-578, PDF pp. 557-564 | Spend bits on the field's declared domain; account for readability, CPU and codec storage cost | N1 has checked bounded bit streams with known wire vectors. N3 will own semantic field ranges and quantization in explicit schemas. Keep checks in every build, not only debug. Avoid the chapter's virtual packable-object registry and obfuscation argument. |
| Andrew Kirmse, **A Network Monitoring and Simulation Tool**, Game Programming Gems 3, 5.7; PDF pp. 542-545 | LAN-only testing hides latency, loss, duplicates and ordering problems; model each direction independently | N1 has a seeded, caller-clock directional simulator with bounded storage and observable drops/overflow. It uses uniform additional jitter and equal-time duplicate copies, rather than reproducing NetTool's Gaussian timing and independently delayed duplicates. Bandwidth shaping/burst profiles remain future test tools. |
| David L. Koenig, **Game Network Debugging with Smart Packet Sniffers**, Game Programming Gems 7, 6.3; printed pp. 491-497 | A protocol-aware message inspector and shared definitions explain traffic; instrumentation can perturb timing | Reuse N1 framing decoder in future inspection; add bounded records/counters at the authenticated application boundary and format outside the tick. Keep encryption enabled; do not adopt debug cleartext traffic or rely on secrecy of the inspector. |
| Hyun-jik Baeb, **High-Level Abstraction of Game World Synchronization**, Game Programming Gems 7, 6.1; printed pp. 467-480 | Describe member synchronization behavior explicitly and centralize networking mechanics outside gameplay | N3 owns schema version, field policy, dirty masks and transport-independent replication bridges. Start with readable hand-authored descriptors; defer its definition language/code generator until repetition warrants one. |
| João Lucas Guberman Raza, **Shared Network Arrays as an Abstraction of Network Code from Game Code Logic**, Game Engine Gems 3, chapter 17; printed pp. 229-236 | Separate gameplay iteration/storage from connection managers; track object change age and propagation | Preserve the explicit replication bridge and per-peer dirty/baseline metadata. Reject shared mutable array semantics and client-proposed state winning by age: clients send validated intent and servers own state. Network object generations avoid using a local array index as global identity. |
| Jon Watte, **Authentication for Online Games**, Game Programming Gems 7, 6.2; printed pp. 481-489 | Login identity and authentication of session traffic are separate concerns | N2 must bind external admission credentials to an authenticated transport connection and fresh session generation. Do not adopt recoverable-password storage, plain fast password hashes, custom secret challenges or historical cipher choices. Epochs/checksums are not credentials. |

These changes improve testing, ownership, inspectability and bandwidth control
without importing historical platform libraries or heavy public templates.

## Located but deferred

GEG2 chapter 18, **Believable Dead Reckoning for Networked Games**, is a useful
candidate for game-specific remote-motion interpolation/extrapolation work in N4.
Its TOC does not justify importing an algorithm in N1; a full chapter/implementation
review is still required before claiming the chosen predictor derives from it.

GPG3 5.1/5.2 (RTS latency/protocol), 5.3/5.4 (MMO architecture/scaling), GPG4
6.2/6.3/6.6 (large servers/storage/multiserver consistency), and GPG8 5.3
(asynchronous IO) are candidates for later workload-specific studies. Do not
apply lockstep, sharding or an IO thread pool before a concrete game's determinism,
peer count and measured poll cost require them. DirectPlay/J2ME/lobby integrations
are outside the current platform architecture. These are TOC-based triage,
not claims of a completed review of those articles.

## Modern primary-source checks

[Valve's GameNetworkingSockets source/documentation](https://github.com/ValveSoftware/GameNetworkingSockets)
and [its message/lane API](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets)
support using a mature native transport candidate behind the engine boundary.
The proposed adoption still needs a pinned dependency/exception audit and real
headless endpoint tests. Multiple lanes isolate ordering, not all congestion:
a single connection still shares transport resources.

[W3C WebTransport](https://www.w3.org/TR/webtransport/) and
[RFC 9221](https://www.rfc-editor.org/rfc/rfc9221.html) inform the browser
transport capability boundary. Reliable streams and unreliable datagrams need a
compatible deployed endpoint. The architecture explicitly requires server-side
browser interop instead of claiming native GNS wire compatibility.

[Fiedler's snapshot interpolation](https://www.gafferongames.com/post/snapshot_interpolation/),
[snapshot compression](https://www.gafferongames.com/post/snapshot_compression/),
[state synchronization](https://www.gafferongames.com/post/state_synchronization/),
and [serialization strategies](https://www.gafferongames.com/post/serialization_strategies/)
provide reference points for N3/N4: checked schemas, acknowledged compression
baselines, delayed remote interpolation and a conscious choice of simulation mode.
The initial implementation does not claim these higher-level algorithms are present.

[Epic's Iris components](https://dev.epicgames.com/documentation/unreal-engine/components-of-iris-in-unreal-engine)
provide a modern example of separating gameplay objects from their network
representation. The Ludus design adopts that boundary at a smaller scale; it
does not copy Iris or require its framework in the engine.

Research checked 2026-10-04. Backend suitability and performance conclusions here
are engineering proposals to validate, not benchmark results.
