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

## Conference and journal research backlog

Recorded 2026-10-06. This section preserves the recommended venues and initial
reading list as continuing context for networking work. The long-term aim is to
read broadly and review most articles retained in Ludus-relevant topic shortlists,
then try promising ideas in the engine and measure whether they help. It is not
a requirement to read every paper published by every venue.

Implement and validate the initial networking architecture through its N2-N5
delivery gates first. Record the resulting revision, workloads and measurements
as the comparison baseline, then begin the broader reading and improvement pass
below. Backend/specification checks required to implement the initial design
remain part of that implementation; this queue does not require a research-led
redesign before the baseline exists.

The [networking architecture](networking.md) owns system contracts and N2-N5 exit
gates; [ADR 0021](../decisions/0021-networking-core-and-transport-boundary.md)
owns the portable-core and transport-boundary decision. This backlog tracks
research, not feature completion. Venue descriptions, session metadata and the
survey abstract were checked when recommending these sources; the full readings
below remain queued. Their proposed experiments are Ludus research questions,
not claims that the sources have been fully reviewed or their ideas adopted.

### Venues to search

| Venue | Reading priority | Topics to screen for | Ludus work it could inform |
| --- | --- | --- | --- |
| [GDC / GDC Vault](https://www.gdcvault.com/) | First | Production replication, prediction, reconciliation, lag compensation, physics synchronization and network debugging | N3/N4 gameplay integration; N5 inspection and latency measurements |
| [NetGames proceedings archive](https://2017.acmmmsys.org/netgames/) | First | Game-specific message distribution, protocol design, relevance, latency compensation and scalability | N3 replication/relevance; N4 latency policies; N5 adverse-link testing |
| [ACM GameSys proceedings](https://gamesys22.wpi.edu/) | First | Game-system research, shared distributed state, network/system performance and player experience | N3/N4 design alternatives and evaluation |
| [ACM Computing Surveys](https://doi.org/10.1145/3519023) | First | Surveys comparing synchronization and latency-compensation techniques, assumptions and evaluation methods | N3/N4 choice of simulation mode and bounded history policies |
| [USENIX NSDI](https://www.usenix.org/conference/nsdi26/call-for-papers) | After baseline: N2 topics | Practical networked-system design, resource bounds, backpressure, testing, debugging and deployed-system experience | N2 session/adapter ownership and overload behavior; N5 harness design |
| [ACM SIGCOMM](https://www.sigcomm.org/events) | Selective | Transport, congestion control, pacing, QUIC and deadline-sensitive delivery | N2/N5 backend evaluation and lane/budget measurements |
| [ACM CoNEXT](https://www.sigcomm.org/events) | Selective | Experimental networking systems, transport tradeoffs and reproducible evaluations | N2/N5 native/browser transport experiments |
| [ACM Internet Measurement Conference (IMC)](https://www.sigcomm.org/events) | After baseline: N5 topics | Operational latency/loss/jitter distributions, measurement validity and tail behavior | N5 realistic fault profiles and performance methodology |
| [IEEE Transactions on Games](https://transactions.games/) | Selective | Game-system evaluation, latency fairness, player experience and gameplay consequences | N4 authoritative permission/rewind policies and correction evaluation |
| [IEEE Transactions on Networking](https://www.comsoc.org/publications/journals/ieee-tnet) | Selective | Transport, scheduling, congestion control, security and network measurement | N2/N5 backend and scheduling research |

NetGames and GameSys links identify historical proceedings/workshop entry points,
not a verified upcoming event schedule. Search older networking journal papers
under the former title *IEEE/ACM Transactions on Networking* as well. Priorities
and the mapping to Ludus are our selection criteria, not venue endorsements of
the engine design.

### Initial reading queue

After the initial implementation is validated, begin with the order below for
N3/N4 improvement research and screen the other venues for session, transport
and measurement ideas. Milestone labels identify the affected subsystem; they
do not move these research trials ahead of the initial implementation.

| ID | Source and attribution | Full-review status | Question and possible experiment after review |
| --- | --- | --- | --- |
| NR-01 | Shengmei Liu, Xiaokun Xu and Mark Claypool, **A Survey and Taxonomy of Latency Compensation Techniques for Network Computer Games**, *ACM Computing Surveys* 54, 11s, article 243, 2022. [DOI](https://doi.org/10.1145/3519023); [author page and paper](https://web.cs.wpi.edu/~claypool/papers/lag-taxonomy/) | Queued; metadata/abstract screened | Compare compensation assumptions before selecting N4 policies. Build a small scenario comparing local prediction/reconciliation and remote interpolation under the same seeded link profiles; measure responsiveness, correction error, fairness and retained history. |
| NR-02 | Timothy Ford, **'Overwatch' Gameplay Architecture and Netcode**, GDC 2017, Blizzard Entertainment. [Session](https://www.gdcvault.com/play/1024001/-Overwatch-Gameplay-Architecture-and) | Queued; session description screened | What simulation/world boundaries and determinism assumptions fit Ludus? Trial a tick-boundary replication bridge and bounded command replay; test convergence and suppression of repeated gameplay effects without assuming cross-platform physics determinism. |
| NR-03 | David Aldridge, **I Shot You First: Networking the Gameplay of HALO: REACH**, GDC 2011, Bungie. [Session](https://www.gdcvault.com/play/1014345/I-Shot-You-First-Networking) | Queued; session identified | Which gameplay consistency, fairness and diagnostic practices transfer to our authoritative design? Add loss/jitter journeys with bounded inspection records and permission tests, then compare disagreement/correction rates and debugging usefulness. |
| NR-04 | Benjamin Goyette, **Fighting Latency on Call of Duty Black Ops III**, GDC 2016, Activision. [Session](https://gdcvault.com/play/1023220/Fighting-Latency-on-Call-of) | Queued; session description screened; local slides available | Where does input-to-feedback latency accumulate outside socket IO? Instrument input capture, command send, server processing, snapshot receive and presentation; measure the full path and examine asymmetric-latency scenarios before tuning rewind. |
| NR-05 | Jared Cone, **It IS Rocket Science! The Physics of 'Rocket League' Detailed**, GDC 2018, Psyonix. [Session](https://www.gdcvault.com/play/1025341/It-IS-Rocket-Science-The) | Queued; session identified | Which physics synchronization and correction ideas fit our bounded histories? Prototype a small physics-heavy scenario and measure divergence, replay cost and presentation error; explicitly test the limits of determinism across supported targets. |

NR-04 already has a local copy at
`references/gdc/2016/Goyette_Benjamin_Fighting_Latency_COD.pdf`. File availability
does not mean it has been read. The ignored reference library remains separate
from authored documentation and publication artifacts; use the linked primary
source when the local copy is unavailable.

### Screening and review record

Continue by screening each venue for concrete problems in N2-N5. Add relevant
articles with stable IDs, exact author/title/year, DOI or primary-source URL,
access/local locator, priority and reading status. Use survey bibliographies to
find original papers, then read those papers before attributing an implementation
to them. Record the scope and date of each archive search so that the shortlist
and reading coverage can be revisited.

Use these statuses: **Queued**, **Reading**, **Reviewed**, **Trial planned**,
**Trial complete**, **Adopted**, **Deferred**, or **Rejected**. A metadata/abstract
screen is not a full review; an attractive hypothesis is not a measured gain.
Update the existing row and append the detailed record here when work advances.

For each completed review or trial, record:

1. Source ID, review date and exact sections, pages or talk timestamps consulted.
2. The relevant idea, its assumptions, evidence, limitations and differences from
   our architecture; distinguish design inspiration from adapted code.
3. The affected Ludus milestone/module and a testable improvement hypothesis.
4. A bounded prototype, comparison baseline and failure/regression scenarios.
5. Reproduction commands, seed, workload, toolchain/backend versions and links to
   the test, benchmark or PR. Keep generated captures/results in ignored `out/`.
6. Before/after results: tick/poll/decode percentiles, bytes, queue age, allocations,
   memory/history bounds, corrections and relevant gameplay consistency measures.
7. A decision: adopt, defer or reject, with its reason and links to any changed
   architecture/ADR and implementation attribution.

N2 trials should exercise real endpoints, admission, reconnect and backend
backpressure. N3 trials should cover object generations, baselines and relevance
under byte budgets. N4 trials should cover loss/jitter, bounded replay and
authoritative eligibility. N5 trials should cover browser/native interop and the
defined workloads. Use the architecture's existing gates rather than inventing
capacity claims from a single favorable loopback benchmark.

No new source in this queue has a completed trial or adoption record yet. Keep
the original **Read and adopted** review above intact; extend it only when a
completed review actually informs a design or implementation. Future code
adaptations retain Ludus's exception-free, bounded-work and private-backend
requirements, with a concise source acknowledgement near the affected code.
