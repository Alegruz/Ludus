// Example gameplay module for live editing during play (project-live-reload
// design 2/9/12). ONE implementation: ludus_add_game builds it as a reloadable
// module, a project host, and a static shipping executable. It links ONLY
// Ludus::GameApi — no engine singleton runtime. It exposes a tunable "Speed"
// property, a read-only "Bounces" output, and a tint-colored fullscreen clear
// so a live edit or a code reload produces an observable rendered change.

#include <ludus/runtime/game_api/api.h>
#include <ludus/runtime/game_api/properties.h>
#include <ludus/runtime/game_api/services.h>

#include <cstring>
#include <new>

namespace
{
using namespace ludus::runtime::game_api;
using ludus::foundation::float32;
using ludus::foundation::int32;
using ludus::foundation::uint32;
using ludus::foundation::uint64;
using ludus::foundation::uint8;
using ludus::foundation::usize;

constexpr uint32 kCheckpointSchema = 1;
constexpr uint32 kPropertySchema = 1;
constexpr uint32 kCapabilities = static_cast<uint32>(Capability::Reload) |
                                 static_cast<uint32>(Capability::Properties);

constexpr uint64 kObjectId = 0xA001;
constexpr uint64 kPropSpeed = 0xB001;
constexpr uint64 kPropBounces = 0xB002;

#ifndef LUDUS_EXAMPLE_IDENTITY
#define LUDUS_EXAMPLE_IDENTITY "example-identity-unset"
#endif
constexpr const char* kIdentity = LUDUS_EXAMPLE_IDENTITY;

struct Body
{
    float32 PositionX = 0.0F;
    float32 Velocity = 0.5F;
    float32 Speed = 1.0F;
    int32 Bounces = 0;
    uint64 Rng = 0x243F6A8885A308D3ULL;
    double SimTime = 0.0;
};

struct State
{
    Body Sim;
    uint64 Revision = 1;
    bool Paused = false;
};

uint64 Fnv1a(const uint8* data, usize size) noexcept
{
    uint64 hash = 0xCBF29CE484222325ULL;
    for (usize i = 0; i < size; ++i)
    {
        hash ^= data[i];
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

State* AsState(GameInstance* instance) noexcept
{
    return reinterpret_cast<State*>(instance);
}

Status GameQuery(GameMetadata* out) noexcept
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

Status GameCreate(const CreateInfo* info, GameInstance** outInstance) noexcept
{
    if (info == nullptr || outInstance == nullptr)
    {
        return Status::InvalidArgument;
    }
    auto* state = new (std::nothrow) State();
    if (state == nullptr)
    {
        return Status::Internal;
    }
    *outInstance = reinterpret_cast<GameInstance*>(state);
    return Status::Ok;
}

void GameDestroy(GameInstance* instance) noexcept
{
    delete AsState(instance);
}

Status GameUpdate(GameInstance* instance, const FrameInput* input, RenderParams* outRender) noexcept
{
    State* state = AsState(instance);
    if (state == nullptr || input == nullptr || outRender == nullptr)
    {
        return Status::InvalidArgument;
    }
    if (!state->Paused && input->Paused == 0)
    {
        state->Sim.SimTime = input->ElapsedSeconds;
        state->Sim.PositionX += state->Sim.Velocity * state->Sim.Speed * static_cast<float32>(input->DeltaSeconds);
        if (state->Sim.PositionX > 1.0F || state->Sim.PositionX < 0.0F)
        {
            state->Sim.PositionX = state->Sim.PositionX > 1.0F ? 1.0F : 0.0F;
            state->Sim.Velocity = -state->Sim.Velocity;
            state->Sim.Bounces += 1;
            state->Revision += 1;
        }
    }
    outRender->ClearRed = state->Sim.PositionX;
    outRender->ClearGreen = 0.1F;
    outRender->ClearBlue = 1.0F - state->Sim.PositionX;
    outRender->ClearAlpha = 1.0F;
    outRender->Uniform0 = state->Sim.PositionX;
    outRender->Uniform1 = static_cast<float32>(state->Sim.Bounces);
    return Status::Ok;
}

Status GameQuiesce(GameInstance* instance) noexcept
{
    State* s = AsState(instance);
    if (s == nullptr)
    {
        return Status::InvalidArgument;
    }
    s->Paused = true;
    return Status::Ok;
}

Status GameResume(GameInstance* instance) noexcept
{
    State* s = AsState(instance);
    if (s == nullptr)
    {
        return Status::InvalidArgument;
    }
    s->Paused = false;
    return Status::Ok;
}

Status GameCheckpointSize(GameInstance* instance, usize* outBodySize) noexcept
{
    if (instance == nullptr || outBodySize == nullptr)
    {
        return Status::InvalidArgument;
    }
    *outBodySize = sizeof(Body);
    return Status::Ok;
}

Status GameWriteCheckpoint(GameInstance* instance, CheckpointHeader* outHeader, ByteSpan body,
                           usize* outBytesWritten) noexcept
{
    State* s = AsState(instance);
    if (s == nullptr || outHeader == nullptr || outBytesWritten == nullptr)
    {
        return Status::InvalidArgument;
    }
    if (body.Data == nullptr || body.Capacity < sizeof(Body))
    {
        return Status::BufferTooSmall;
    }
    std::memcpy(body.Data, &s->Sim, sizeof(Body));
    outHeader->FormatVersion = 1;
    outHeader->SchemaVersion = kCheckpointSchema;
    outHeader->BodyLength = sizeof(Body);
    outHeader->BodyDigest = Fnv1a(body.Data, sizeof(Body));
    *outBytesWritten = sizeof(Body);
    return Status::Ok;
}

Status GameCreateCandidate(const CreateInfo* info, const CheckpointHeader* header, ByteView body,
                           GameCandidate** outCandidate) noexcept
{
    if (info == nullptr || header == nullptr || outCandidate == nullptr || body.Data == nullptr)
    {
        return Status::InvalidArgument;
    }
    if (header->SchemaVersion != kCheckpointSchema || header->BodyLength != body.Size || body.Size < sizeof(Body))
    {
        return Status::MigrationUnsupported;
    }
    if (Fnv1a(body.Data, body.Size) != header->BodyDigest)
    {
        return Status::InvalidArgument;
    }
    auto* state = new (std::nothrow) State();
    if (state == nullptr)
    {
        return Status::Internal;
    }
    std::memcpy(&state->Sim, body.Data, sizeof(Body));
    state->Paused = true;
    *outCandidate = reinterpret_cast<GameCandidate*>(state);
    return Status::Ok;
}

Status GameValidateCandidate(GameCandidate* candidate) noexcept
{
    auto* s = reinterpret_cast<State*>(candidate);
    return (s != nullptr && s->Sim.Speed >= 0.0F) ? Status::Ok : Status::OutOfRange;
}

Status GameCommitCandidate(GameCandidate* candidate, GameInstance** outInstance) noexcept
{
    if (candidate == nullptr || outInstance == nullptr)
    {
        return Status::InvalidArgument;
    }
    *outInstance = reinterpret_cast<GameInstance*>(candidate);
    return Status::Ok;
}

void GameDiscardCandidate(GameCandidate* candidate) noexcept
{
    delete reinterpret_cast<State*>(candidate);
}

Status GameDescribePropertiesSize(GameInstance* instance, usize* outByteSize) noexcept
{
    if (instance == nullptr || outByteSize == nullptr)
    {
        return Status::InvalidArgument;
    }
    *outByteSize = sizeof(PropertyDescriptor) * 2;
    return Status::Ok;
}

Status GameDescribeProperties(GameInstance* instance, ByteSpan out, usize* outBytesWritten) noexcept
{
    if (instance == nullptr || outBytesWritten == nullptr)
    {
        return Status::InvalidArgument;
    }
    const usize needed = sizeof(PropertyDescriptor) * 2;
    if (out.Data == nullptr || out.Capacity < needed)
    {
        return Status::BufferTooSmall;
    }
    PropertyDescriptor descs[2] = {};
    descs[0].ObjectId = kObjectId;
    descs[0].PropertyId = kPropSpeed;
    descs[0].Kind = static_cast<uint32>(PropertyKind::Float32);
    descs[0].Scope = static_cast<uint32>(PropertyScope::Persistable);
    descs[0].Writable = 1;
    descs[0].MinFloat = 0.0F;
    descs[0].MaxFloat = 8.0F;
    std::memcpy(descs[0].Label, "Speed", 5);
    descs[0].LabelLength = 5;

    descs[1].ObjectId = kObjectId;
    descs[1].PropertyId = kPropBounces;
    descs[1].Kind = static_cast<uint32>(PropertyKind::Int32);
    descs[1].Scope = static_cast<uint32>(PropertyScope::ReadOnly);
    descs[1].Writable = 0;
    std::memcpy(descs[1].Label, "Bounces", 7);
    descs[1].LabelLength = 7;

    std::memcpy(out.Data, descs, needed);
    *outBytesWritten = needed;
    return Status::Ok;
}

Status GameReadProperties(GameInstance* instance, ByteSpan out, usize* outBytesWritten) noexcept
{
    State* s = AsState(instance);
    if (s == nullptr || outBytesWritten == nullptr)
    {
        return Status::InvalidArgument;
    }
    const usize needed = sizeof(PropertyValue) * 2;
    if (out.Data == nullptr || out.Capacity < needed)
    {
        return Status::BufferTooSmall;
    }
    PropertyValue values[2] = {};
    values[0].ObjectId = kObjectId;
    values[0].PropertyId = kPropSpeed;
    values[0].ObjectRevision = s->Revision;
    values[0].Kind = static_cast<uint32>(PropertyKind::Float32);
    values[0].Float = s->Sim.Speed;
    values[1].ObjectId = kObjectId;
    values[1].PropertyId = kPropBounces;
    values[1].ObjectRevision = s->Revision;
    values[1].Kind = static_cast<uint32>(PropertyKind::Int32);
    std::memcpy(&values[1].IntOrEnum, &s->Sim.Bounces, sizeof(int32));
    std::memcpy(out.Data, values, needed);
    *outBytesWritten = needed;
    return Status::Ok;
}

struct Plan
{
    float32 Speed = 0.0F;
    bool HasSpeed = false;
};

Status GamePrepareEdits(GameInstance* instance, ByteView edits, GameEditPlan** outPlan) noexcept
{
    State* s = AsState(instance);
    if (s == nullptr || outPlan == nullptr || edits.Data == nullptr || edits.Size < sizeof(EditBatchHeader))
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
    if (edits.Size < sizeof(EditBatchHeader) + sizeof(PropertyEdit) * header.Count)
    {
        return Status::InvalidArgument;
    }
    auto* plan = new (std::nothrow) Plan();
    if (plan == nullptr)
    {
        return Status::Internal;
    }
    const auto* array = reinterpret_cast<const PropertyEdit*>(edits.Data + sizeof(EditBatchHeader));
    for (uint32 i = 0; i < header.Count; ++i)
    {
        const PropertyEdit& e = array[i];
        if (e.ObjectId != kObjectId || e.ExpectedRevision != s->Revision)
        {
            delete plan;
            return Status::StaleRevision;
        }
        if (e.PropertyId == kPropSpeed && e.Kind == static_cast<uint32>(PropertyKind::Float32))
        {
            if (e.Float < 0.0F || e.Float > 8.0F)
            {
                delete plan;
                return Status::OutOfRange;
            }
            plan->HasSpeed = true;
            plan->Speed = e.Float;
        }
        else
        {
            delete plan;
            return Status::InvalidArgument;
        }
    }
    *outPlan = reinterpret_cast<GameEditPlan*>(plan);
    return Status::Ok;
}

Status GameCommitEdits(GameInstance* instance, GameEditPlan* planHandle) noexcept
{
    State* s = AsState(instance);
    auto* plan = reinterpret_cast<Plan*>(planHandle);
    if (s == nullptr || plan == nullptr)
    {
        return Status::InvalidArgument;
    }
    if (plan->HasSpeed)
    {
        s->Sim.Speed = plan->Speed;
    }
    s->Revision += 1;
    delete plan;
    return Status::Ok;
}

void GameDiscardEdits(GameInstance* instance, GameEditPlan* planHandle) noexcept
{
    (void)instance;
    delete reinterpret_cast<Plan*>(planHandle);
}

const GameApiTable& Table() noexcept
{
    static const GameApiTable table = [] {
        GameApiTable t = {};
        t.StructSize = static_cast<uint32>(sizeof(GameApiTable));
        t.AbiMajor = kAbiMajor;
        t.AbiMinor = kAbiMinor;
        t.Query = &GameQuery;
        t.Create = &GameCreate;
        t.Destroy = &GameDestroy;
        t.Update = &GameUpdate;
        t.Quiesce = &GameQuiesce;
        t.Resume = &GameResume;
        t.CheckpointSize = &GameCheckpointSize;
        t.WriteCheckpoint = &GameWriteCheckpoint;
        t.CreateCandidate = &GameCreateCandidate;
        t.ValidateCandidate = &GameValidateCandidate;
        t.CommitCandidate = &GameCommitCandidate;
        t.DiscardCandidate = &GameDiscardCandidate;
        t.DescribePropertiesSize = &GameDescribePropertiesSize;
        t.DescribeProperties = &GameDescribeProperties;
        t.ReadProperties = &GameReadProperties;
        t.PrepareEdits = &GamePrepareEdits;
        t.CommitEdits = &GameCommitEdits;
        t.DiscardEdits = &GameDiscardEdits;
        return t;
    }();
    return table;
}
} // namespace

// NOLINTBEGIN(bugprone-easily-swappable-parameters) — fixed ABI entry signature
extern "C" LUDUS_GAME_API_EXPORT ludus::runtime::game_api::Status LudusGetGameApi(
    ludus::foundation::uint32 hostAbiMajor, ludus::foundation::uint32 hostAbiMinor,
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
    const uint32 hostSize = outTable->StructSize;
    const uint32 common =
        hostSize < static_cast<uint32>(sizeof(GameApiTable)) ? hostSize : static_cast<uint32>(sizeof(GameApiTable));
    std::memcpy(outTable, &Table(), common);
    outTable->StructSize = common;
    return Status::Ok;
}
