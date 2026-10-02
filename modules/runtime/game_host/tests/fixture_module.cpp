// Reference gameplay fixture module (project-live-reload L0-L5 acceptance).
//
// One gameplay implementation compiled into every reload fixture. It links ONLY
// Ludus::GameApi (no engine singleton runtime), exports exactly one unmangled
// entry (LudusGetGameApi) with hidden default visibility, and implements the
// full ABI table: mandatory Create/Update/Destroy, reload (Quiesce/Resume,
// checkpoint, candidate), live properties and one supported asset replacement.
//
// Two build variants (A and B) differ only by LUDUS_FIXTURE_VARIANT so the
// loader test can prove A and B run their own code concurrently, and B can
// migrate A's checkpoint. The identity string is baked in from the SDK variant
// so the host accepts a matching module and rejects a mismatched one.

#include <ludus/runtime/game_api/api.h>
#include <ludus/runtime/game_api/properties.h>
#include <ludus/runtime/game_api/services.h>

#include <cstring>
#include <new>

#ifndef LUDUS_FIXTURE_VARIANT
#    define LUDUS_FIXTURE_VARIANT A
#endif
#ifndef LUDUS_FIXTURE_IDENTITY
#    define LUDUS_FIXTURE_IDENTITY "unset-identity"
#endif

namespace
{
using namespace ludus::runtime::game_api;
using ludus::foundation::float32;
using ludus::foundation::float64;
using ludus::foundation::int32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;
using ludus::foundation::usize;

#define LUDUS_FIXTURE_STRINGIFY_(x) #x
#define LUDUS_FIXTURE_STRINGIFY(x) LUDUS_FIXTURE_STRINGIFY_(x)
constexpr const char* kVariantName = LUDUS_FIXTURE_STRINGIFY(LUDUS_FIXTURE_VARIANT);
constexpr const char* kIdentity = LUDUS_FIXTURE_IDENTITY;

constexpr bool kIsVariantB = (kVariantName[0] == 'B');

// Checkpoint/property schema versions. Variant B bumps the checkpoint schema to
// exercise explicit migration (design 8).
constexpr uint32 kCheckpointSchemaA = 1;
constexpr uint32 kCheckpointSchemaB = 2;
constexpr uint32 kCheckpointSchema = kIsVariantB ? kCheckpointSchemaB : kCheckpointSchemaA;
constexpr uint32 kPropertySchema = 1;

constexpr uint32 kCapabilities = static_cast<uint32>(Capability::Reload) | static_cast<uint32>(Capability::Properties) |
                                 static_cast<uint32>(Capability::AssetReload);

// Stable property IDs (never inferred from ordering).
constexpr uint64 kObjectId = 0x1001;
constexpr uint64 kPropSpeed = 0x2001;   // float32, persistable
constexpr uint64 kPropBounces = 0x2002; // int32, read-only simulation output
constexpr uint64 kPropLabel = 0x2003;   // string, session-only

// Logical asset ID for the supported asset-reload fixture.
constexpr uint64 kAssetTint = 0x3001;

// A tagged on-disk checkpoint body with stable field IDs (never a raw dump).
// Shared across variants; B's reader fills Energy with a declared default when
// reading an A (schema 1) body.
struct CheckpointBodyV1
{
    float32 PositionX = 0.0F;
    float32 Velocity = 0.6F;
    float32 Speed = 1.0F;
    int32 Bounces = 0;
    uint64 Rng = 0x9E3779B97F4A7C15ULL;
    float64 SimTime = 0.0;
    float32 TintR = 0.2F;
    float32 TintG = 0.3F;
    float32 TintB = 0.8F;
    char Label[32] = "fixture";
};

struct CheckpointBodyV2 : CheckpointBodyV1
{
    float32 Energy = 0.0F; // Added in B; migrated from A with default 0.
};

// Live simulation state. POD. The body types above are the serialized subset.
struct SimState
{
    float32 PositionX = 0.0F;
    float32 Velocity = 0.6F;
    float32 Speed = 1.0F;
    int32 Bounces = 0;
    uint64 Rng = 0x9E3779B97F4A7C15ULL;
    float64 SimTime = 0.0;
    float32 TintR = 0.2F;
    float32 TintG = 0.3F;
    float32 TintB = 0.8F;
    float32 Energy = 0.0F;
    char Label[32] = "fixture";
    uint64 Revision = 1; // Advances when a mutable value changes.
    bool Paused = false;
    const HostServices* Services = nullptr;
};

// xorshift so the RNG state is deterministic and survives a reload exactly.
uint64 NextRng(uint64& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

[[nodiscard]] uint64 Fnv1a(const uint8* data, usize size) noexcept
{
    uint64 hash = 0xCBF29CE484222325ULL;
    for (usize i = 0; i < size; ++i)
    {
        hash ^= data[i];
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

void HostLog(const SimState* state, LogSeverity severity, const char* text) noexcept
{
    if (state != nullptr && state->Services != nullptr && state->Services->Log != nullptr)
    {
        state->Services->Log(state->Services->Context, severity, text, std::strlen(text));
    }
}

// Each GameInstance/GameCandidate/GameEditPlan is just a SimState or an edit.
SimState* AsState(GameInstance* instance) noexcept
{
    return reinterpret_cast<SimState*>(instance);
}

// --- ABI implementations ---------------------------------------------------

Status FixtureQuery(GameMetadata* out) noexcept
{
    if (out == nullptr || out->StructSize < sizeof(GameMetadata))
    {
        return Status::InvalidArgument;
    }
    out->AbiMajor = kAbiMajor;
    out->AbiMinor = kAbiMinor;
    out->Capabilities = kCapabilities;
    out->PropertySchemaVersion = kPropertySchema;
    out->CheckpointSchemaVersion = kCheckpointSchema;
    const usize len = std::strlen(kIdentity);
    const usize bounded = len > kIdentityMax ? kIdentityMax : len;
    out->IdentityLength = static_cast<uint32>(bounded);
    std::memset(out->Identity, 0, sizeof(out->Identity));
    std::memcpy(out->Identity, kIdentity, bounded);
    return Status::Ok;
}

SimState* NewState(const CreateInfo* info) noexcept
{
    auto* state = new (std::nothrow) SimState();
    if (state == nullptr)
    {
        return nullptr;
    }
    if (info != nullptr)
    {
        state->Services = info->Services;
    }
    return state;
}

Status FixtureCreate(const CreateInfo* info, GameInstance** outInstance) noexcept
{
    if (info == nullptr || outInstance == nullptr)
    {
        return Status::InvalidArgument;
    }
    SimState* state = NewState(info);
    if (state == nullptr)
    {
        return Status::Internal;
    }
    HostLog(state, LogSeverity::Info, "fixture create");
    *outInstance = reinterpret_cast<GameInstance*>(state);
    return Status::Ok;
}

void FixtureDestroy(GameInstance* instance) noexcept
{
    delete AsState(instance);
}

Status FixtureUpdate(GameInstance* instance, const FrameInput* input, RenderParams* outRender) noexcept
{
    SimState* state = AsState(instance);
    if (state == nullptr || input == nullptr || outRender == nullptr)
    {
        return Status::InvalidArgument;
    }
    if (!state->Paused && input->Paused == 0)
    {
        state->SimTime = input->ElapsedSeconds;
        state->PositionX += state->Velocity * state->Speed * static_cast<float32>(input->DeltaSeconds);
        if (state->PositionX > 1.0F)
        {
            state->PositionX = 1.0F;
            state->Velocity = -state->Velocity;
            state->Bounces += 1;
            state->Energy += 0.1F;
            (void)NextRng(state->Rng);
            state->Revision += 1;
        }
        else if (state->PositionX < 0.0F)
        {
            state->PositionX = 0.0F;
            state->Velocity = -state->Velocity;
            state->Bounces += 1;
            state->Energy += 0.1F;
            (void)NextRng(state->Rng);
            state->Revision += 1;
        }
    }
    // Variant B renders a distinctly brighter frame so a rendered change is
    // observable; both derive the clear color from the asset-backed tint.
    const float32 variantBoost = kIsVariantB ? 0.4F : 0.0F;
    outRender->ClearRed = state->TintR + variantBoost;
    outRender->ClearGreen = state->TintG;
    outRender->ClearBlue = state->TintB;
    outRender->ClearAlpha = 1.0F;
    outRender->Uniform0 = state->PositionX;
    outRender->Uniform1 = static_cast<float32>(state->Bounces);
    outRender->Uniform2 = state->Energy;
    outRender->Uniform3 = kIsVariantB ? 1.0F : 0.0F;
    return Status::Ok;
}

Status FixtureQuiesce(GameInstance* instance) noexcept
{
    SimState* state = AsState(instance);
    if (state == nullptr)
    {
        return Status::InvalidArgument;
    }
    state->Paused = true;
    return Status::Ok;
}

Status FixtureResume(GameInstance* instance) noexcept
{
    SimState* state = AsState(instance);
    if (state == nullptr)
    {
        return Status::InvalidArgument;
    }
    state->Paused = false;
    return Status::Ok;
}

Status FixtureCheckpointSize(GameInstance* instance, usize* outBodySize) noexcept
{
    if (instance == nullptr || outBodySize == nullptr)
    {
        return Status::InvalidArgument;
    }
    *outBodySize = kIsVariantB ? sizeof(CheckpointBodyV2) : sizeof(CheckpointBodyV1);
    return Status::Ok;
}

template <typename BodyT>
void FillBody(const SimState& state, BodyT& body) noexcept
{
    body.PositionX = state.PositionX;
    body.Velocity = state.Velocity;
    body.Speed = state.Speed;
    body.Bounces = state.Bounces;
    body.Rng = state.Rng;
    body.SimTime = state.SimTime;
    body.TintR = state.TintR;
    body.TintG = state.TintG;
    body.TintB = state.TintB;
    std::memcpy(body.Label, state.Label, sizeof(body.Label));
}

Status FixtureWriteCheckpoint(GameInstance* instance,
                              CheckpointHeader* outHeader,
                              ByteSpan body,
                              usize* outBytesWritten) noexcept
{
    SimState* state = AsState(instance);
    if (state == nullptr || outHeader == nullptr || outBytesWritten == nullptr)
    {
        return Status::InvalidArgument;
    }
    const usize needed = kIsVariantB ? sizeof(CheckpointBodyV2) : sizeof(CheckpointBodyV1);
    if (body.Data == nullptr || body.Capacity < needed)
    {
        return Status::BufferTooSmall;
    }
    if (kIsVariantB)
    {
        CheckpointBodyV2 b;
        FillBody(*state, b);
        b.Energy = state->Energy;
        std::memcpy(body.Data, &b, sizeof(b));
    }
    else
    {
        CheckpointBodyV1 b;
        FillBody(*state, b);
        std::memcpy(body.Data, &b, sizeof(b));
    }
    outHeader->FormatVersion = 1;
    outHeader->SchemaVersion = kCheckpointSchema;
    outHeader->BodyLength = needed;
    outHeader->BodyDigest = Fnv1a(body.Data, needed);
    *outBytesWritten = needed;
    return Status::Ok;
}

Status FixtureCreateCandidate(const CreateInfo* info,
                              const CheckpointHeader* header,
                              ByteView body,
                              GameCandidate** outCandidate) noexcept
{
    if (info == nullptr || header == nullptr || outCandidate == nullptr || body.Data == nullptr)
    {
        return Status::InvalidArgument;
    }
    // Validate digest and bounds before trusting the body (design 8).
    if (header->BodyLength != body.Size || Fnv1a(body.Data, body.Size) != header->BodyDigest)
    {
        return Status::InvalidArgument;
    }

    SimState* state = NewState(info);
    if (state == nullptr)
    {
        return Status::Internal;
    }

    // Explicit schema acceptance / migration. B accepts schema 1 (migrating,
    // Energy defaulted) and schema 2; A accepts only schema 1. Anything else
    // is an unsupported migration (host keeps A and offers Restart).
    if (header->SchemaVersion == kCheckpointSchemaA && body.Size >= sizeof(CheckpointBodyV1))
    {
        CheckpointBodyV1 b;
        std::memcpy(&b, body.Data, sizeof(b));
        state->PositionX = b.PositionX;
        state->Velocity = b.Velocity;
        state->Speed = b.Speed;
        state->Bounces = b.Bounces;
        state->Rng = b.Rng;
        state->SimTime = b.SimTime;
        state->TintR = b.TintR;
        state->TintG = b.TintG;
        state->TintB = b.TintB;
        std::memcpy(state->Label, b.Label, sizeof(state->Label));
        state->Energy = 0.0F; // declared default when migrating A -> B
    }
    else if (kIsVariantB && header->SchemaVersion == kCheckpointSchemaB && body.Size >= sizeof(CheckpointBodyV2))
    {
        CheckpointBodyV2 b;
        std::memcpy(&b, body.Data, sizeof(b));
        state->PositionX = b.PositionX;
        state->Velocity = b.Velocity;
        state->Speed = b.Speed;
        state->Bounces = b.Bounces;
        state->Rng = b.Rng;
        state->SimTime = b.SimTime;
        state->TintR = b.TintR;
        state->TintG = b.TintG;
        state->TintB = b.TintB;
        state->Energy = b.Energy;
        std::memcpy(state->Label, b.Label, sizeof(state->Label));
    }
    else
    {
        delete state;
        return Status::MigrationUnsupported;
    }

    state->Paused = true; // Candidate starts paused; host resumes after commit.
    *outCandidate = reinterpret_cast<GameCandidate*>(state);
    return Status::Ok;
}

Status FixtureValidateCandidate(GameCandidate* candidate) noexcept
{
    auto* state = reinterpret_cast<SimState*>(candidate);
    if (state == nullptr)
    {
        return Status::InvalidArgument;
    }
    // Reject impossible migrated state (design 8: validate before commit).
    if (state->Speed < 0.0F || state->Bounces < 0)
    {
        return Status::OutOfRange;
    }
    return Status::Ok;
}

Status FixtureCommitCandidate(GameCandidate* candidate, GameInstance** outInstance) noexcept
{
    if (candidate == nullptr || outInstance == nullptr)
    {
        return Status::InvalidArgument;
    }
    // The candidate SimState becomes the live instance; no allocation here.
    *outInstance = reinterpret_cast<GameInstance*>(candidate);
    return Status::Ok;
}

void FixtureDiscardCandidate(GameCandidate* candidate) noexcept
{
    delete reinterpret_cast<SimState*>(candidate);
}

// --- Properties ------------------------------------------------------------

usize PropertyDescribeBytes() noexcept
{
    return sizeof(PropertyDescriptor) * 3;
}

Status FixtureDescribePropertiesSize(GameInstance* instance, usize* outByteSize) noexcept
{
    if (instance == nullptr || outByteSize == nullptr)
    {
        return Status::InvalidArgument;
    }
    *outByteSize = PropertyDescribeBytes();
    return Status::Ok;
}

PropertyDescriptor
MakeDescriptor(uint64 propId, PropertyKind kind, PropertyScope scope, bool writable, const char* label) noexcept
{
    PropertyDescriptor d;
    d.ObjectId = kObjectId;
    d.PropertyId = propId;
    d.Kind = static_cast<uint32>(kind);
    d.Scope = static_cast<uint32>(scope);
    d.Writable = writable ? 1 : 0;
    const usize len = std::strlen(label);
    d.LabelLength = static_cast<uint8>(len > kMaxLabelBytes ? kMaxLabelBytes : len);
    std::memcpy(d.Label, label, d.LabelLength);
    d.MinFloat = 0.0F;
    d.MaxFloat = 10.0F;
    d.MinInt = 0;
    d.MaxInt = 1000000;
    return d;
}

Status FixtureDescribeProperties(GameInstance* instance, ByteSpan out, usize* outBytesWritten) noexcept
{
    if (instance == nullptr || outBytesWritten == nullptr)
    {
        return Status::InvalidArgument;
    }
    const usize needed = PropertyDescribeBytes();
    if (out.Data == nullptr || out.Capacity < needed)
    {
        return Status::BufferTooSmall;
    }
    PropertyDescriptor descs[3] = {
        MakeDescriptor(kPropSpeed, PropertyKind::Float32, PropertyScope::Persistable, true, "Speed"),
        MakeDescriptor(kPropBounces, PropertyKind::Int32, PropertyScope::ReadOnly, false, "Bounces"),
        MakeDescriptor(kPropLabel, PropertyKind::String, PropertyScope::SessionOnly, true, "Label"),
    };
    std::memcpy(out.Data, descs, needed);
    *outBytesWritten = needed;
    return Status::Ok;
}

Status FixtureReadProperties(GameInstance* instance, ByteSpan out, usize* outBytesWritten) noexcept
{
    SimState* state = AsState(instance);
    if (state == nullptr || outBytesWritten == nullptr)
    {
        return Status::InvalidArgument;
    }
    const usize needed = sizeof(PropertyValue) * 3;
    if (out.Data == nullptr || out.Capacity < needed)
    {
        return Status::BufferTooSmall;
    }
    PropertyValue values[3] = {};
    values[0].ObjectId = kObjectId;
    values[0].PropertyId = kPropSpeed;
    values[0].ObjectRevision = state->Revision;
    values[0].Kind = static_cast<uint32>(PropertyKind::Float32);
    values[0].Float = state->Speed;

    values[1].ObjectId = kObjectId;
    values[1].PropertyId = kPropBounces;
    values[1].ObjectRevision = state->Revision;
    values[1].Kind = static_cast<uint32>(PropertyKind::Int32);
    std::memcpy(&values[1].IntOrEnum, &state->Bounces, sizeof(int32));

    values[2].ObjectId = kObjectId;
    values[2].PropertyId = kPropLabel;
    values[2].ObjectRevision = state->Revision;
    values[2].Kind = static_cast<uint32>(PropertyKind::String);
    const usize labelLen = std::strlen(state->Label);
    values[2].StringLength = static_cast<uint32>(labelLen);
    std::memcpy(values[2].String, state->Label, labelLen);

    std::memcpy(out.Data, values, needed);
    *outBytesWritten = needed;
    return Status::Ok;
}

// Edit plan: validated edits copied into a heap record applied at CommitEdits.
struct EditPlan
{
    bool HasSpeed = false;
    float32 Speed = 0.0F;
    bool HasLabel = false;
    char Label[32] = {};
};

Status FixturePrepareEdits(GameInstance* instance, ByteView edits, GameEditPlan** outPlan) noexcept
{
    SimState* state = AsState(instance);
    if (state == nullptr || outPlan == nullptr || edits.Data == nullptr)
    {
        return Status::InvalidArgument;
    }
    if (edits.Size < sizeof(EditBatchHeader))
    {
        return Status::InvalidArgument;
    }
    EditBatchHeader header;
    std::memcpy(&header, edits.Data, sizeof(header));
    if (header.Count == 0 || header.Count > kMaxEditBatch)
    {
        return Status::OutOfRange;
    }
    if (header.SchemaVersion != kPropertySchema)
    {
        return Status::SchemaChanged;
    }
    const usize expected = sizeof(EditBatchHeader) + sizeof(PropertyEdit) * header.Count;
    if (edits.Size < expected)
    {
        return Status::InvalidArgument;
    }

    auto* plan = new (std::nothrow) EditPlan();
    if (plan == nullptr)
    {
        return Status::Internal;
    }
    const auto* editArray = reinterpret_cast<const PropertyEdit*>(edits.Data + sizeof(EditBatchHeader));
    for (uint32 i = 0; i < header.Count; ++i)
    {
        const PropertyEdit& e = editArray[i];
        if (e.ObjectId != kObjectId || e.ExpectedRevision != state->Revision)
        {
            delete plan;
            return Status::StaleRevision;
        }
        if (e.PropertyId == kPropSpeed && e.Kind == static_cast<uint32>(PropertyKind::Float32))
        {
            if (e.Float < 0.0F || e.Float > 10.0F)
            {
                delete plan;
                return Status::OutOfRange;
            }
            plan->HasSpeed = true;
            plan->Speed = e.Float;
        }
        else if (e.PropertyId == kPropLabel && e.Kind == static_cast<uint32>(PropertyKind::String))
        {
            if (e.StringLength >= sizeof(plan->Label))
            {
                delete plan;
                return Status::OutOfRange;
            }
            plan->HasLabel = true;
            std::memcpy(plan->Label, e.String, e.StringLength);
            plan->Label[e.StringLength] = '\0';
        }
        else
        {
            delete plan; // read-only or unknown property rejected
            return Status::InvalidArgument;
        }
    }
    *outPlan = reinterpret_cast<GameEditPlan*>(plan);
    return Status::Ok;
}

Status FixtureCommitEdits(GameInstance* instance, GameEditPlan* planHandle) noexcept
{
    SimState* state = AsState(instance);
    auto* plan = reinterpret_cast<EditPlan*>(planHandle);
    if (state == nullptr || plan == nullptr)
    {
        return Status::InvalidArgument;
    }
    if (plan->HasSpeed)
    {
        state->Speed = plan->Speed;
    }
    if (plan->HasLabel)
    {
        std::memcpy(state->Label, plan->Label, sizeof(state->Label));
    }
    state->Revision += 1;
    delete plan;
    return Status::Ok;
}

void FixtureDiscardEdits(GameInstance* instance, GameEditPlan* planHandle) noexcept
{
    (void)instance;
    delete reinterpret_cast<EditPlan*>(planHandle);
}

// --- Asset reload ----------------------------------------------------------

Status FixtureReloadAsset(GameInstance* instance,
                          uint64 logicalAssetId,
                          ByteView cookedArtifact,
                          uint64 artifactDigest) noexcept
{
    SimState* state = AsState(instance);
    if (state == nullptr || cookedArtifact.Data == nullptr)
    {
        return Status::InvalidArgument;
    }
    if (logicalAssetId != kAssetTint)
    {
        return Status::Unsupported; // explicit unsupported asset kind
    }
    // Cooked artifact is exactly three float32 RGB tint channels.
    if (cookedArtifact.Size != sizeof(float32) * 3)
    {
        return Status::InvalidArgument;
    }
    if (Fnv1a(cookedArtifact.Data, cookedArtifact.Size) != artifactDigest)
    {
        return Status::InvalidArgument; // validation failed -> old asset retained
    }
    float32 rgb[3];
    std::memcpy(rgb, cookedArtifact.Data, sizeof(rgb));
    state->TintR = rgb[0];
    state->TintG = rgb[1];
    state->TintB = rgb[2];
    state->Revision += 1;
    return Status::Ok;
}

const GameApiTable& FixtureTable() noexcept
{
    static const GameApiTable table = [] {
        GameApiTable t = {};
        t.StructSize = static_cast<uint32>(sizeof(GameApiTable));
        t.AbiMajor = kAbiMajor;
        t.AbiMinor = kAbiMinor;
        t.Query = &FixtureQuery;
        t.Create = &FixtureCreate;
        t.Destroy = &FixtureDestroy;
        t.Update = &FixtureUpdate;
        t.Quiesce = &FixtureQuiesce;
        t.Resume = &FixtureResume;
        t.CheckpointSize = &FixtureCheckpointSize;
        t.WriteCheckpoint = &FixtureWriteCheckpoint;
        t.CreateCandidate = &FixtureCreateCandidate;
        t.ValidateCandidate = &FixtureValidateCandidate;
        t.CommitCandidate = &FixtureCommitCandidate;
        t.DiscardCandidate = &FixtureDiscardCandidate;
        t.DescribePropertiesSize = &FixtureDescribePropertiesSize;
        t.DescribeProperties = &FixtureDescribeProperties;
        t.ReadProperties = &FixtureReadProperties;
        t.PrepareEdits = &FixturePrepareEdits;
        t.CommitEdits = &FixtureCommitEdits;
        t.DiscardEdits = &FixtureDiscardEdits;
        t.ReloadAsset = &FixtureReloadAsset;
        return t;
    }();
    return table;
}
} // namespace

// NOLINTBEGIN(bugprone-easily-swappable-parameters) — fixed ABI entry signature
extern "C" LUDUS_GAME_API_EXPORT ludus::runtime::game_api::Status
LudusGetGameApi(ludus::foundation::uint32 hostAbiMajor,
                ludus::foundation::uint32 hostAbiMinor,
                ludus::runtime::game_api::GameApiTable* outTable) noexcept
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    using namespace ludus::runtime::game_api;
    (void)hostAbiMinor;
    if (outTable == nullptr)
    {
        return Status::InvalidArgument;
    }
    if (hostAbiMajor != kAbiMajor)
    {
        return Status::IncompatibleAbi;
    }
    // Negotiate the common table size: copy only up to the host-provided size.
    const uint32 hostSize = outTable->StructSize;
    const GameApiTable& full = FixtureTable();
    const uint32 common =
        hostSize < static_cast<uint32>(sizeof(GameApiTable)) ? hostSize : static_cast<uint32>(sizeof(GameApiTable));
    std::memcpy(outTable, &full, common);
    outTable->StructSize = common;
    return Status::Ok;
}
