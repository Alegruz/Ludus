// Native port of the retired scripts/python/ludus_tools/scene_cook.py.
//
// Thanks to Matt Pharr, Wenzel Jakob and Greg Humphreys, pbrt-v4 Input File
// Format (https://pbrt.org/fileformat-v4), Transformations, Cameras, Materials
// and Shapes, for camera-from-world and scoped graphics-state semantics.
// This is an original offline converter, not a PBRT renderer or copied parser.
//
// Every arithmetic step mirrors the CPython 3.12 reference in float64 order,
// including Neumaier-compensated sum(), so outputs are byte-identical.

#include <ludus/content/scene_cook/scene_cook.h>

#include "native_files.h"
#include "python_compat.h"

#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/parsing/parsing.hpp>
#include <ludus/foundation/strings/string.hpp>

#include <bit>

namespace ludus::content::scene_cook
{
namespace
{
using foundation::Array;
using foundation::String;
using foundation::StringStatus;
using namespace detail;

constexpr float64 IDENTITY[16] = {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
constexpr std::string_view COMMANDS[] = {
    "Integrator",
    "Transform",
    "Sampler",
    "PixelFilter",
    "Film",
    "Camera",
    "WorldBegin",
    "Texture",
    "MakeNamedMaterial",
    "NamedMaterial",
    "Material",
    "Shape",
    "AreaLightSource",
    "AttributeBegin",
    "AttributeEnd",
    "TransformBegin",
    "TransformEnd",
    "WorldEnd",
};

[[nodiscard]] bool IsCommand(std::string_view token) noexcept
{
    for (const std::string_view command : COMMANDS)
    {
        if (command == token)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string_view View(const Array<uint8>& bytes) noexcept
{
    return {reinterpret_cast<const char*>(bytes.GetData()), bytes.GetSize()};
}

[[nodiscard]] bool EndsWith(std::string_view text, std::string_view suffix) noexcept
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

[[nodiscard]] bool IsFinite(float64 value) noexcept
{
    return (std::bit_cast<uint64>(value) & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
}

[[nodiscard]] float64 Abs(float64 value) noexcept
{
    return value < 0.0 ? -value : value;
}

// ---------------------------------------------------------------------------
// Failure plumbing.

struct Failure final
{
    CookError& Error;
    bool Failed = false;

    bool Fail(Status status, std::string_view message, std::string_view detail = {}) noexcept
    {
        if (!Failed)
        {
            Failed = true;
            Error.Result = status;
            char text[256]{};
            usize at = 0;
            for (const std::string_view part : {message, detail})
            {
                for (usize i = 0; i < part.size() && at < 255; ++i)
                {
                    text[at++] = part[i];
                }
            }
            (void)Error.Message.Set(std::string_view(text, at));
        }
        return false;
    }
    bool Invalid(std::string_view message, std::string_view detail = {}) noexcept
    {
        return Fail(Status::Invalid, message, detail);
    }
    bool Memory() noexcept
    {
        return Fail(Status::OutOfMemory, "out of memory");
    }
};

// ---------------------------------------------------------------------------
// Parsed scene description.

struct Arg final
{
    bool List = false;
    String Text;
    Array<String> Items;
};

struct Param final
{
    String Key;
    Array<String> Values;
    bool Assigned = false;
};

struct Command final
{
    std::string_view Name;
    Array<Arg> Names;
    Array<Param> Params;
};

struct MaterialRef final
{
    usize CommandIndex = 0;
    bool Inline = false; // Material directive: "string type" comes from the names.
};

struct State final
{
    float64 Transform[16]{};
    int64 Material = -1;
    bool HasEmission = false;
    float64 Emission[3]{};
};

struct Shape final
{
    State Graphics;
    usize CommandIndex = 0;
};

struct TextureDecl final
{
    std::string_view Name;
    std::string_view File;
};

struct Scene final
{
    Array<Command> Commands;
    Array<TextureDecl> Textures;
    Array<std::string_view> MaterialNames;
    Array<MaterialRef> Materials; // Named materials in declaration order, then inline ones.
    Array<Shape> Shapes;
    float64 Camera[16]{};
    float64 Fov = 0.0;
};

[[nodiscard]] const Param* FindParam(const Command& command, std::string_view key) noexcept
{
    for (const Param& param : command.Params)
    {
        if (param.Key.GetView() == key)
        {
            return &param;
        }
    }
    return nullptr;
}

[[nodiscard]] bool
NamesEqual(const Array<Arg>& names, usize first, std::initializer_list<std::string_view> expected) noexcept
{
    if (names.GetSize() < first || names.GetSize() - first != expected.size())
    {
        return false;
    }
    usize index = first;
    for (const std::string_view value : expected)
    {
        if (names[index].List || names[index].Text.GetView() != value)
        {
            return false;
        }
        ++index;
    }
    return true;
}

[[nodiscard]] bool ValuesEqual(const Param* param, std::string_view value) noexcept
{
    return param != nullptr && param->Values.GetSize() == 1 && param->Values[0].GetView() == value;
}

// ---------------------------------------------------------------------------
// Cooking context.

struct ReadRecord final
{
    String Path;
    Digest Hash;
};

struct Context final
{
    Context(Failure& errors, const Cancellation* cancel) noexcept : Errors(errors), Cancel(cancel) {}

    Failure& Errors;
    const Cancellation* Cancel = nullptr;
    String Root;
    Array<ReadRecord> Reads;

    [[nodiscard]] bool CheckCancel() noexcept
    {
        if (Cancel != nullptr && Cancel->Requested != nullptr && Cancel->Requested(Cancel->Context))
        {
            return Errors.Fail(Status::Cancelled, "Scene conversion cancelled");
        }
        return true;
    }

    // asset_path(): reject absolute and parent segments; normalise like pathlib.
    [[nodiscard]] bool AssetPath(std::string_view name, String& output) noexcept
    {
        if (!name.empty() && name.front() == '/')
        {
            return Errors.Invalid("asset path escapes source directory");
        }
        if (output.TryAssign("") != StringStatus::Ok)
        {
            return Errors.Memory();
        }
        usize begin = 0;
        for (usize i = 0; i <= name.size(); ++i)
        {
            if (i != name.size() && name[i] != '/')
            {
                continue;
            }
            const std::string_view part = name.substr(begin, i - begin);
            begin = i + 1;
            if (part.empty() || part == ".")
            {
                continue;
            }
            if (part == "..")
            {
                return Errors.Invalid("asset path escapes source directory");
            }
            if ((output.GetSize() != 0 && output.TryAppend("/") != StringStatus::Ok) ||
                output.TryAppend(part) != StringStatus::Ok)
            {
                return Errors.Memory();
            }
        }
        if (output.GetSize() == 0)
        {
            return Errors.Fail(Status::IoError, "Is a directory: ", name);
        }
        return true;
    }

    // bounded_bytes(): bounded read plus repeated-read conflict detection.
    [[nodiscard]] bool Read(std::string_view relative, usize cap, Bytes& output) noexcept
    {
        if (!CheckCancel())
        {
            return false;
        }
        Bytes data;
        const Status status = ReadFile(Root.GetView(), relative, cap, data);
        switch (status)
        {
            case Status::Ok:
                break;
            case Status::Limit:
                return Errors.Fail(Status::Limit, "source file byte limit");
            case Status::NotFound:
                return Errors.Fail(Status::NotFound, "No such file or directory: ", relative);
            case Status::Invalid:
                return Errors.Invalid("unsupported source path (symlinks and reserved characters are rejected): ",
                                      relative);
            case Status::OutOfMemory:
                return Errors.Memory();
            case Status::Conflict:
                return Errors.Fail(Status::Conflict, "Source changed during conversion: ", BaseName(relative));
            default:
                return Errors.Fail(Status::IoError, "cannot read source file: ", relative);
        }
        const Digest digest = Hash(data.Data());
        for (const ReadRecord& record : Reads)
        {
            if (record.Path.GetView() == relative)
            {
                if (!(record.Hash == digest))
                {
                    return Errors.Fail(Status::Conflict, "Source changed during conversion: ", BaseName(relative));
                }
                output = static_cast<Bytes&&>(data);
                return true;
            }
        }
        ReadRecord record;
        record.Hash = digest;
        if (record.Path.TryAssign(relative) != StringStatus::Ok || !Reads.TryAdd(static_cast<ReadRecord&&>(record)))
        {
            return Errors.Memory();
        }
        output = static_cast<Bytes&&>(data);
        return true;
    }

    // finite(): float() every token and reject NaN/infinity.
    [[nodiscard]] bool Finite(std::span<const std::string_view> tokens, Array<float64>& output) noexcept
    {
        switch (ParseFloats(tokens, output))
        {
            case NumberResult::Ok:
                return true;
            case NumberResult::Nonfinite:
                return Errors.Invalid("nonfinite source number");
            case NumberResult::OutOfMemory:
                return Errors.Memory();
            default:
                return Errors.Invalid("could not convert string to float");
        }
    }

    [[nodiscard]] bool Finite(const Array<String>& values, Array<float64>& output) noexcept
    {
        Array<std::string_view> views;
        if (!views.TryResize(values.GetSize()))
        {
            return Errors.Memory();
        }
        for (usize i = 0; i < values.GetSize(); ++i)
        {
            views[i] = values[i].GetView();
        }
        return Finite(views.AsSpan(), output);
    }

    [[nodiscard]] bool FiniteParam(const Param* param, Array<float64>& output, std::string_view key) noexcept
    {
        if (param == nullptr)
        {
            return Errors.Invalid("missing parameter ", key);
        }
        return Finite(param->Values, output);
    }
};

// ---------------------------------------------------------------------------
// parse_scene().

[[nodiscard]] uint32 DecodeAt(std::string_view text, usize at, usize& length) noexcept
{
    const auto lead = static_cast<uint8>(text[at]);
    if (lead < 0x80 || at + 1 >= text.size())
    {
        length = 1;
        return lead;
    }
    if (lead >= 0xF0 && at + 3 < text.size())
    {
        length = 4;
        return ((lead & 0x07U) << 18) | ((static_cast<uint8>(text[at + 1]) & 0x3FU) << 12) |
               ((static_cast<uint8>(text[at + 2]) & 0x3FU) << 6) | (static_cast<uint8>(text[at + 3]) & 0x3FU);
    }
    if (lead >= 0xE0 && at + 2 < text.size())
    {
        length = 3;
        return ((lead & 0x0FU) << 12) | ((static_cast<uint8>(text[at + 1]) & 0x3FU) << 6) |
               (static_cast<uint8>(text[at + 2]) & 0x3FU);
    }
    length = 2;
    return ((lead & 0x1FU) << 6) | (static_cast<uint8>(text[at + 1]) & 0x3FU);
}

// re.findall(r'"(?:[^"\\]|\\.)*"|\[|\]|[^\s\[\]"]+', text) over decoded code points.
[[nodiscard]] bool Tokenize(std::string_view text, Array<std::string_view>& tokens) noexcept
{
    usize at = 0;
    while (at < text.size())
    {
        const char value = text[at];
        if (value == '"')
        {
            usize scan = at + 1;
            bool closed = false;
            while (scan < text.size())
            {
                if (text[scan] == '"')
                {
                    closed = true;
                    break;
                }
                if (text[scan] == '\\')
                {
                    if (scan + 1 >= text.size() || text[scan + 1] == '\n')
                    {
                        break;
                    }
                    scan += 2;
                    continue;
                }
                ++scan;
            }
            if (closed)
            {
                if (!tokens.TryAdd(text.substr(at, scan + 1 - at)))
                {
                    return false;
                }
                at = scan + 1;
            }
            else
            {
                ++at; // No alternative matches an unterminated quote; findall skips it.
            }
            continue;
        }
        if (value == '[' || value == ']')
        {
            if (!tokens.TryAdd(text.substr(at, 1)))
            {
                return false;
            }
            ++at;
            continue;
        }
        usize length = 1;
        if (IsPySpace(DecodeAt(text, at, length)))
        {
            at += length;
            continue;
        }
        const usize begin = at;
        while (at < text.size() && text[at] != '"' && text[at] != '[' && text[at] != ']')
        {
            if (IsPySpace(DecodeAt(text, at, length)))
            {
                break;
            }
            at += length;
        }
        if (!tokens.TryAdd(text.substr(begin, at - begin)))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool DecodeValue(Context& context, std::string_view token, String& output) noexcept
{
    if (!token.empty() && token.front() == '"')
    {
        if (!DecodeJsonString(token, output))
        {
            return context.Errors.Invalid("invalid string literal");
        }
        return true;
    }
    return output.TryAssign(token) == StringStatus::Ok || context.Errors.Memory();
}

struct AllowedParams final
{
    std::string_view Command;
    std::initializer_list<std::string_view> Keys;
};

[[nodiscard]] bool ParamAllowed(std::string_view command, std::string_view key) noexcept
{
    static const AllowedParams TABLE[] = {
        {"Integrator", {"integer maxdepth", "string lightsampler", "float trainingBudgetRatio"}},
        {"Sampler", {"integer pixelsamples"}},
        {"Film", {"string filename", "integer xresolution", "integer yresolution"}},
        {"Camera", {"float fov"}},
        {"Texture", {"string filename", "string filter"}},
        {"MakeNamedMaterial",
         {"string type",
          "rgb reflectance",
          "texture reflectance",
          "float uroughness",
          "float vroughness",
          "bool remaproughness",
          "rgb eta",
          "rgb k"}},
        {"Material", {"rgb reflectance"}},
        {"AreaLightSource", {"rgb L"}},
        {"Shape", {"string filename", "float radius", "point3 P", "normal N", "point2 uv", "integer indices"}},
    };
    for (const AllowedParams& entry : TABLE)
    {
        if (entry.Command == command)
        {
            for (const std::string_view allowed : entry.Keys)
            {
                if (allowed == key)
                {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

[[nodiscard]] bool AppendDecoded(Context& context, std::string_view token, Array<String>& values) noexcept
{
    String value;
    if (!DecodeValue(context, token, value))
    {
        return false;
    }
    return values.TryAdd(static_cast<String&&>(value)) || context.Errors.Memory();
}

[[nodiscard]] bool ParseCommands(Context& context, std::span<const std::string_view> tokens, Scene& scene) noexcept
{
    Failure& errors = context.Errors;
    usize i = 0;
    while (i < tokens.size())
    {
        const std::string_view name = tokens[i++];
        if (!IsCommand(name))
        {
            return errors.Invalid("unsupported directive ", name);
        }
        Command command;
        command.Name = name;
        Array<Arg> arguments;
        while (i < tokens.size() && !IsCommand(tokens[i]))
        {
            Arg arg;
            if (tokens[i] == "[")
            {
                ++i;
                arg.List = true;
                while (i < tokens.size() && tokens[i] != "]")
                {
                    String item;
                    if (item.TryAssign(tokens[i]) != StringStatus::Ok || !arg.Items.TryAdd(static_cast<String&&>(item)))
                    {
                        return errors.Memory();
                    }
                    ++i;
                }
                if (i == tokens.size())
                {
                    return errors.Invalid("unterminated array");
                }
                ++i;
            }
            else
            {
                if (arg.Text.TryAssign(tokens[i]) != StringStatus::Ok)
                {
                    return errors.Memory();
                }
                ++i;
            }
            if (!arguments.TryAdd(static_cast<Arg&&>(arg)))
            {
                return errors.Memory();
            }
        }
        for (Arg& arg : arguments)
        {
            const usize paramCount = command.Params.GetSize();
            if (paramCount != 0 && !command.Params[paramCount - 1].Assigned)
            {
                Param& param = command.Params[paramCount - 1];
                param.Assigned = true;
                if (arg.List)
                {
                    for (const String& item : arg.Items)
                    {
                        if (!AppendDecoded(context, item.GetView(), param.Values))
                        {
                            return false;
                        }
                    }
                }
                else if (!AppendDecoded(context, arg.Text.GetView(), param.Values))
                {
                    return false;
                }
            }
            else if (!arg.List && !arg.Text.GetView().empty() && arg.Text.GetView().front() == '"')
            {
                String value;
                if (!DecodeValue(context, arg.Text.GetView(), value))
                {
                    return false;
                }
                if (value.GetView().find(' ') == std::string_view::npos && paramCount == 0)
                {
                    Arg decoded;
                    decoded.Text = static_cast<String&&>(value);
                    if (!command.Names.TryAdd(static_cast<Arg&&>(decoded)))
                    {
                        return errors.Memory();
                    }
                }
                else
                {
                    if (FindParam(command, value.GetView()) != nullptr)
                    {
                        return errors.Invalid("duplicate parameter");
                    }
                    Param param;
                    param.Key = static_cast<String&&>(value);
                    if (!command.Params.TryAdd(static_cast<Param&&>(param)))
                    {
                        return errors.Memory();
                    }
                }
            }
            else if (!command.Names.TryAdd(static_cast<Arg&&>(arg)))
            {
                return errors.Memory();
            }
        }
        for (const Param& param : command.Params)
        {
            if (!param.Assigned || !ParamAllowed(name, param.Key.GetView()))
            {
                return errors.Invalid("unsupported or missing parameter for ", name);
            }
        }
        if (!scene.Commands.TryAdd(static_cast<Command&&>(command)))
        {
            return errors.Memory();
        }
    }
    return true;
}

[[nodiscard]] bool FirstName(Context& context, const Command& command, std::string_view& output) noexcept
{
    if (command.Names.GetSize() == 0)
    {
        return context.Errors.Invalid("list index out of range");
    }
    if (command.Names[0].List)
    {
        return context.Errors.Invalid("unhashable type: 'list'");
    }
    output = command.Names[0].Text.GetView();
    return true;
}

[[nodiscard]] bool StripComments(std::string_view text, Array<uint8>& output) noexcept
{
    // re.sub(r"#[^\n]*", "", text) applies everywhere, including inside strings.
    Sink sink(output);
    usize at = 0;
    while (at < text.size())
    {
        const usize hash = text.find('#', at);
        if (hash == std::string_view::npos)
        {
            sink.Append(text.substr(at));
            break;
        }
        sink.Append(text.substr(at, hash - at));
        const usize newline = text.find('\n', hash);
        at = newline == std::string_view::npos ? text.size() : newline;
    }
    return sink.Ok();
}

struct Saved final
{
    std::string_view Start;
    State Graphics;
};

struct SceneState final
{
    State Current;
    Array<Saved> Stack;
    bool HaveCamera = false;
    bool World = false;
    Array<float64> Values;
};

[[nodiscard]] bool ApplyTransform(Context& context, const Command& command, SceneState& state) noexcept
{
    Failure& errors = context.Errors;
    if (command.Names.GetSize() == 0)
    {
        return errors.Invalid("list index out of range");
    }
    if (!command.Names[0].List)
    {
        return errors.Invalid("non-affine transform");
    }
    Array<float64>& values = state.Values;
    if (!context.Finite(command.Names[0].Items, values))
    {
        return false;
    }
    if (values.GetSize() != 16 || values[3] != 0.0 || values[7] != 0.0 || values[11] != 0.0 || values[15] != 1.0)
    {
        return errors.Invalid("non-affine transform");
    }
    for (usize k = 0; k < 16; ++k)
    {
        state.Current.Transform[k] = values[k];
    }
    return true;
}

[[nodiscard]] bool ApplyCamera(Context& context, const Command& command, SceneState& state, Scene& scene) noexcept
{
    Failure& errors = context.Errors;
    if (!NamesEqual(command.Names, 0, {"perspective"}))
    {
        return errors.Invalid("only perspective camera is supported");
    }
    if (state.HaveCamera)
    {
        return errors.Invalid("duplicate camera");
    }
    for (usize k = 0; k < 16; ++k)
    {
        scene.Camera[k] = state.Current.Transform[k];
    }
    if (!context.FiniteParam(FindParam(command, "float fov"), state.Values, "float fov"))
    {
        return false;
    }
    if (state.Values.GetSize() == 0)
    {
        return errors.Invalid("list index out of range");
    }
    scene.Fov = state.Values[0];
    state.HaveCamera = true;
    if (!(0.0 < scene.Fov && scene.Fov < 180.0))
    {
        return errors.Invalid("camera field of view");
    }
    return true;
}

[[nodiscard]] bool ApplyTexture(Context& context, const Command& command, Scene& scene) noexcept
{
    Failure& errors = context.Errors;
    if (!NamesEqual(command.Names, 1, {"spectrum", "imagemap"}))
    {
        return errors.Invalid("unsupported texture");
    }
    std::string_view textureName;
    if (!FirstName(context, command, textureName))
    {
        return false;
    }
    bool duplicate = false;
    for (const TextureDecl& texture : scene.Textures)
    {
        duplicate = duplicate || texture.Name == textureName;
    }
    const Param* filter = FindParam(command, "string filter");
    if (duplicate || (filter != nullptr && !ValuesEqual(filter, "trilinear")))
    {
        return errors.Invalid("unsupported texture profile");
    }
    const Param* file = FindParam(command, "string filename");
    if (file == nullptr || file->Values.GetSize() == 0)
    {
        return errors.Invalid("missing parameter string filename");
    }
    return scene.Textures.TryAdd(TextureDecl{textureName, file->Values[0].GetView()}) || errors.Memory();
}

[[nodiscard]] bool
ApplyMaterial(Context& context, const Command& command, usize index, SceneState& state, Scene& scene) noexcept
{
    Failure& errors = context.Errors;
    const std::string_view name = command.Name;
    if (name == "Material")
    {
        if (!scene.Materials.TryAdd(MaterialRef{index, true}))
        {
            return errors.Memory();
        }
        state.Current.Material = static_cast<int64>(scene.Materials.GetSize() - 1);
        return true;
    }
    std::string_view materialName;
    if (!FirstName(context, command, materialName))
    {
        return false;
    }
    if (name == "MakeNamedMaterial")
    {
        for (const std::string_view existing : scene.MaterialNames)
        {
            if (existing == materialName)
            {
                return errors.Invalid("duplicate material");
            }
        }
        return (scene.MaterialNames.TryAdd(materialName) && scene.Materials.TryAdd(MaterialRef{index, false})) ||
               errors.Memory();
    }
    for (usize k = 0; k < scene.MaterialNames.GetSize(); ++k)
    {
        if (scene.MaterialNames[k] == materialName)
        {
            state.Current.Material = static_cast<int64>(k);
            return true;
        }
    }
    return errors.Invalid("unknown material ", materialName);
}

[[nodiscard]] bool ApplyEmitter(Context& context, const Command& command, SceneState& state) noexcept
{
    Failure& errors = context.Errors;
    if (!NamesEqual(command.Names, 0, {"diffuse"}))
    {
        return errors.Invalid("unsupported emitter");
    }
    if (!context.FiniteParam(FindParam(command, "rgb L"), state.Values, "rgb L"))
    {
        return false;
    }
    if (state.Values.GetSize() != 3)
    {
        return errors.Invalid("emission profile");
    }
    for (usize k = 0; k < 3; ++k)
    {
        if (state.Values[k] < 0.0 || state.Values[k] > 10000.0)
        {
            return errors.Invalid("emission profile");
        }
        state.Current.Emission[k] = state.Values[k];
    }
    state.Current.HasEmission = true;
    return true;
}

[[nodiscard]] bool ApplyStack(Context& context, std::string_view name, SceneState& state) noexcept
{
    Failure& errors = context.Errors;
    if (name == "AttributeBegin" || name == "TransformBegin")
    {
        return state.Stack.TryAdd(Saved{name, state.Current}) || errors.Memory();
    }
    if (state.Stack.GetSize() == 0)
    {
        return errors.Invalid("unbalanced state stack");
    }
    const Saved previous = state.Stack[state.Stack.GetSize() - 1];
    state.Stack.RemoveLast();
    const bool attribute = previous.Start == "AttributeBegin";
    if (attribute != (name == "AttributeEnd"))
    {
        return errors.Invalid("mismatched state stack");
    }
    if (attribute)
    {
        state.Current = previous.Graphics;
        return true;
    }
    for (usize k = 0; k < 16; ++k)
    {
        state.Current.Transform[k] = previous.Graphics.Transform[k];
    }
    return true;
}

[[nodiscard]] bool ParseScene(Context& context, Scene& scene) noexcept
{
    Failure& errors = context.Errors;
    Bytes raw;
    if (!context.Read("scene-v4.pbrt", usize{1024} * 1024, raw))
    {
        return false;
    }
    foundation::parsing::ParseError utf8{};
    if (foundation::parsing::ValidateUtf8(raw.String(), utf8) != foundation::parsing::ParseStatus::Ok)
    {
        return errors.Invalid("'utf-8' codec can't decode scene-v4.pbrt");
    }
    Array<uint8> stripped;
    Array<std::string_view> tokens;
    if (!StripComments(raw.String(), stripped) || !Tokenize(View(stripped), tokens))
    {
        return errors.Memory();
    }
    if (!ParseCommands(context, tokens.AsSpan(), scene))
    {
        return false;
    }
    SceneState state;
    for (usize k = 0; k < 16; ++k)
    {
        state.Current.Transform[k] = IDENTITY[k];
    }
    for (usize index = 0; index < scene.Commands.GetSize(); ++index)
    {
        const Command& command = scene.Commands[index];
        const std::string_view name = command.Name;
        bool ok = true;
        if (name == "Transform")
        {
            ok = ApplyTransform(context, command, state);
        }
        else if (name == "Camera")
        {
            ok = ApplyCamera(context, command, state, scene);
        }
        else if (name == "WorldBegin")
        {
            state.World = true;
            for (usize k = 0; k < 16; ++k)
            {
                state.Current.Transform[k] = IDENTITY[k];
            }
        }
        else if (name == "Texture")
        {
            ok = ApplyTexture(context, command, scene);
        }
        else if (name == "MakeNamedMaterial" || name == "NamedMaterial" || name == "Material")
        {
            ok = ApplyMaterial(context, command, index, state, scene);
        }
        else if (name == "AreaLightSource")
        {
            ok = ApplyEmitter(context, command, state);
        }
        else if (name == "AttributeBegin" || name == "TransformBegin" || name == "AttributeEnd" ||
                 name == "TransformEnd")
        {
            ok = ApplyStack(context, name, state);
        }
        else if (name == "Shape")
        {
            if (!state.World || state.Current.Material < 0)
            {
                return errors.Invalid("shape outside world or without material");
            }
            ok = scene.Shapes.TryAdd(Shape{state.Current, index}) || errors.Memory();
        }
        else if (name == "WorldEnd")
        {
            state.World = false;
        }
        // Integrator, Sampler, PixelFilter and Film are replaced by the documented L6 capture profile.
        if (!ok)
        {
            return false;
        }
    }
    if (state.Stack.GetSize() != 0 || !state.HaveCamera || scene.Shapes.GetSize() == 0)
    {
        return errors.Invalid("incomplete scene");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Geometry helpers mirroring the reference tuple arithmetic.

struct Vec3 final
{
    float64 V[3]{};
};

[[nodiscard]] Vec3 Sub(const Vec3& a, const Vec3& b) noexcept
{
    return {{a.V[0] - b.V[0], a.V[1] - b.V[1], a.V[2] - b.V[2]}};
}
[[nodiscard]] Vec3 Add(const Vec3& a, const Vec3& b) noexcept
{
    return {{a.V[0] + b.V[0], a.V[1] + b.V[1], a.V[2] + b.V[2]}};
}
[[nodiscard]] Vec3 Mul(const Vec3& a, float64 b) noexcept
{
    return {{a.V[0] * b, a.V[1] * b, a.V[2] * b}};
}
[[nodiscard]] float64 Dot(const Vec3& a, const Vec3& b) noexcept
{
    PySum sum;
    for (usize i = 0; i < 3; ++i)
    {
        sum.Add(a.V[i] * b.V[i]);
    }
    return sum.Result();
}
[[nodiscard]] Vec3 Cross(const Vec3& a, const Vec3& b) noexcept
{
    return {{a.V[1] * b.V[2] - a.V[2] * b.V[1], a.V[2] * b.V[0] - a.V[0] * b.V[2], a.V[0] * b.V[1] - a.V[1] * b.V[0]}};
}
[[nodiscard]] bool Unit(Failure& errors, const Vec3& a, Vec3& output) noexcept
{
    const float64 length = math::Sqrt(Dot(a, a));
    if (length < 1e-12)
    {
        return errors.Invalid("degenerate geometry");
    }
    output = Mul(a, 1.0 / length);
    return true;
}
[[nodiscard]] Vec3 TransformVector(const float64 (&matrix)[16], const Vec3& p) noexcept
{
    Vec3 output;
    for (usize r = 0; r < 3; ++r)
    {
        PySum sum;
        for (usize c = 0; c < 3; ++c)
        {
            sum.Add(matrix[4 * c + r] * p.V[c]);
        }
        output.V[r] = sum.Result();
    }
    return output;
}
[[nodiscard]] Vec3 TransformPoint(const float64 (&matrix)[16], const Vec3& p) noexcept
{
    Vec3 output = TransformVector(matrix, p);
    for (usize r = 0; r < 3; ++r)
    {
        output.V[r] = output.V[r] + matrix[12 + r];
    }
    return output;
}

struct Point final
{
    Vec3 P;
    Vec3 N;
    uint8 NormalCount = 0; // 3 for a normal; 0 when absent (Python None or an empty tuple).
    float64 Uv[2]{};
};

struct Triangle final
{
    int64 I[3]{};
};

struct Vertex final
{
    Vec3 P;
    Vec3 N;
    float64 Uv[2]{};
};

struct Mesh final
{
    Array<Vertex> Vertices;
    Array<uint32> Indices;
};

struct Image final
{
    uint32 Width = 0;
    uint32 Height = 0;
    Array<uint8> Rgba;
};

struct Material final
{
    uint32 Texture = 0;
    float64 Metallic = 0.0;
    float64 Roughness = 0.0;
    float64 Emission[3]{};
};

struct Item final
{
    uint32 Mesh = 0;
    uint32 Material = 0;
    float64 Transform[12]{};
};

// ---------------------------------------------------------------------------
// read_ply() and read_tga().

[[nodiscard]] uint32 LoadU32(const uint8* data) noexcept
{
    return static_cast<uint32>(data[0]) | (static_cast<uint32>(data[1]) << 8) | (static_cast<uint32>(data[2]) << 16) |
           (static_cast<uint32>(data[3]) << 24);
}

[[nodiscard]] bool SplitLines(std::string_view header, Array<std::string_view>& lines) noexcept
{
    // str.splitlines() boundaries for ASCII text.
    usize begin = 0;
    usize at = 0;
    while (at < header.size())
    {
        const char value = header[at];
        if (value == '\n' || value == '\r' || value == '\x0b' || value == '\x0c' || value == '\x1c' ||
            value == '\x1d' || value == '\x1e')
        {
            if (!lines.TryAdd(header.substr(begin, at - begin)))
            {
                return false;
            }
            at += value == '\r' && at + 1 < header.size() && header[at + 1] == '\n' ? 2 : 1;
            begin = at;
        }
        else
        {
            ++at;
        }
    }
    return begin >= header.size() || lines.TryAdd(header.substr(begin));
}

[[nodiscard]] bool SplitFields(std::string_view text, Array<std::string_view>& fields) noexcept
{
    // str.split() with no separator.
    fields.Clear();
    usize at = 0;
    while (at < text.size())
    {
        while (at < text.size() && IsPySpace(static_cast<uint8>(text[at])))
        {
            ++at;
        }
        const usize begin = at;
        while (at < text.size() && !IsPySpace(static_cast<uint8>(text[at])))
        {
            ++at;
        }
        if (at > begin && !fields.TryAdd(text.substr(begin, at - begin)))
        {
            return false;
        }
    }
    return true;
}

struct PlyHeader final
{
    Array<std::string_view> Props;
    int64 Vertices = -1;
    int64 Faces = -1;
    char IndexFormat = 0;
};

[[nodiscard]] bool ParsePlyHeader(Failure& errors, std::string_view header, PlyHeader& ply) noexcept
{
    for (const char value : header)
    {
        if (static_cast<uint8>(value) >= 0x80)
        {
            return errors.Invalid("'ascii' codec can't decode PLY header");
        }
    }
    Array<std::string_view> lines;
    if (!SplitLines(header, lines))
    {
        return errors.Memory();
    }
    if (lines.GetSize() < 2 || lines[0] != "ply" || lines[1] != "format binary_little_endian 1.0")
    {
        return errors.Invalid("unsupported PLY encoding");
    }
    std::string_view element;
    bool haveElement = false;
    Array<std::string_view> fields;
    for (usize line = 2; line < lines.GetSize(); ++line)
    {
        if (!SplitFields(lines[line], fields))
        {
            return errors.Memory();
        }
        if (fields.GetSize() == 0 || fields[0] == "comment")
        {
            continue;
        }
        if (fields[0] == "element")
        {
            if (fields.GetSize() < 2)
            {
                return errors.Invalid("list index out of range");
            }
            element = fields[1];
            haveElement = true;
            if (element != "vertex" && element != "face")
            {
                return errors.Invalid("unsupported PLY element");
            }
            int64 count = 0;
            if (fields.GetSize() < 3 || !ParseInt(fields[2], count))
            {
                return errors.Invalid("invalid PLY element count");
            }
            (element == "vertex" ? ply.Vertices : ply.Faces) = count;
        }
        else if (fields[0] == "property" && haveElement && element == "vertex")
        {
            if (fields.GetSize() < 3)
            {
                return errors.Invalid("list index out of range");
            }
            if (fields[1] != "float")
            {
                return errors.Invalid("unsupported vertex property");
            }
            if (!ply.Props.TryAdd(fields[2]))
            {
                return errors.Memory();
            }
        }
        else if (fields[0] == "property" && haveElement && element == "face")
        {
            if (fields.GetSize() < 3 || fields[1] != "list" || (fields[2] != "uint8" && fields[2] != "uchar"))
            {
                return errors.Invalid("unsupported face list");
            }
            if (fields.GetSize() < 4)
            {
                return errors.Invalid("list index out of range");
            }
            if (fields[3] != "int" && fields[3] != "uint")
            {
                return errors.Invalid("unsupported index type");
            }
            ply.IndexFormat = fields[3] == "int" ? 'i' : 'I';
        }
    }
    return true;
}

[[nodiscard]] bool
ReadPly(Context& context, std::string_view path, Array<Point>& points, Array<Triangle>& triangles) noexcept
{
    Failure& errors = context.Errors;
    Bytes raw;
    if (!context.Read(path, MAX_SOURCE_BYTES, raw))
    {
        return false;
    }
    const std::string_view data = raw.String();
    const usize boundary = data.find("end_header\n");
    if (boundary == std::string_view::npos || boundary > 4096)
    {
        return errors.Invalid("invalid PLY header");
    }
    PlyHeader ply;
    if (!ParsePlyHeader(errors, data.substr(0, boundary), ply))
    {
        return false;
    }
    const std::span<const uint8> payload = raw.Data().subspan(boundary + 11);
    constexpr std::string_view KNOWN[] = {"x", "y", "z", "nx", "ny", "nz", "u", "v", "s", "t"};
    usize slot[10]{};
    bool present[10]{};
    bool schemaOk = true;
    for (usize p = 0; p < ply.Props.GetSize(); ++p)
    {
        bool known = false;
        for (usize k = 0; k < 10; ++k)
        {
            if (ply.Props[p] == KNOWN[k])
            {
                known = true;
                schemaOk = schemaOk && !present[k];
                present[k] = true;
                slot[k] = p;
            }
        }
        schemaOk = schemaOk && known;
    }
    if (!schemaOk || !present[0] || !present[1] || !present[2])
    {
        return errors.Invalid("unsupported PLY vertex schema");
    }
    const bool anyNormal = present[3] || present[4] || present[5];
    const bool allNormal = present[3] && present[4] && present[5];
    if (anyNormal && !allNormal)
    {
        return errors.Invalid("incomplete PLY normals");
    }
    if (ply.Vertices < 0 || ply.Faces < 0 || ply.IndexFormat == 0 || !(0 < ply.Vertices && ply.Vertices <= 1000000) ||
        !(0 < ply.Faces && ply.Faces <= 1000000))
    {
        return errors.Invalid("PLY limits");
    }
    const usize rowSize = 4 * ply.Props.GetSize();
    usize offset = 0;
    if (!points.TryResize(static_cast<usize>(ply.Vertices)))
    {
        return errors.Memory();
    }
    float64 row[10]{};
    for (usize index = 0; index < static_cast<usize>(ply.Vertices); ++index)
    {
        if (index % 256 == 0 && !context.CheckCancel())
        {
            return false;
        }
        if (payload.size() < offset + rowSize)
        {
            return errors.Invalid("unpack_from requires a buffer of more bytes");
        }
        for (usize p = 0; p < ply.Props.GetSize(); ++p)
        {
            row[p] = static_cast<float64>(std::bit_cast<float32>(LoadU32(payload.data() + offset + 4 * p)));
            if (!IsFinite(row[p]))
            {
                return errors.Invalid("nonfinite source number");
            }
        }
        offset += rowSize;
        Point& point = points[index];
        for (usize k = 0; k < 3; ++k)
        {
            point.P.V[k] = row[slot[k]];
        }
        point.NormalCount = 0;
        if (allNormal)
        {
            point.NormalCount = 3;
            for (usize k = 0; k < 3; ++k)
            {
                point.N.V[k] = row[slot[3 + k]];
            }
        }
        point.Uv[0] = present[6] ? row[slot[6]] : present[8] ? row[slot[8]] : 0.0;
        point.Uv[1] = 1.0 - (present[7] ? row[slot[7]] : present[9] ? row[slot[9]] : 0.0);
    }
    for (int64 index = 0; index < ply.Faces; ++index)
    {
        if (index % 256 == 0 && !context.CheckCancel())
        {
            return false;
        }
        if (offset >= payload.size())
        {
            return errors.Invalid("index out of range");
        }
        const uint8 count = payload[offset];
        offset += 1;
        if (count != 3 && count != 4)
        {
            return errors.Invalid("only triangle/convex quad faces are supported");
        }
        if (payload.size() < offset + 4 * usize{count})
        {
            return errors.Invalid("unpack_from requires a buffer of more bytes");
        }
        int64 indices[4]{};
        for (usize k = 0; k < count; ++k)
        {
            const uint32 bits = LoadU32(payload.data() + offset + 4 * k);
            indices[k] =
                ply.IndexFormat == 'i' ? static_cast<int64>(static_cast<int32>(bits)) : static_cast<int64>(bits);
        }
        offset += 4 * usize{count};
        for (usize k = 0; k < count; ++k)
        {
            if (indices[k] < 0 || indices[k] >= ply.Vertices)
            {
                return errors.Invalid("PLY index range");
            }
        }
        for (usize k = 1; k + 1 < count; ++k)
        {
            if (!triangles.TryAdd(Triangle{{indices[0], indices[k], indices[k + 1]}}))
            {
                return errors.Memory();
            }
        }
    }
    if (offset != payload.size())
    {
        return errors.Invalid("unexpected PLY trailing bytes");
    }
    return true;
}

[[nodiscard]] bool ReadTga(Context& context, std::string_view path, Image& image) noexcept
{
    Failure& errors = context.Errors;
    Bytes raw;
    if (!context.Read(path, MAX_SOURCE_BYTES, raw))
    {
        return false;
    }
    const std::span<const uint8> data = raw.Data();
    if (data.size() < 18)
    {
        return errors.Invalid("unpack_from requires a buffer of at least 18 bytes");
    }
    const usize ident = data[0];
    const uint8 cmap = data[1];
    const uint8 kind = data[2];
    const usize width = static_cast<usize>(data[12]) | (static_cast<usize>(data[13]) << 8);
    const usize height = static_cast<usize>(data[14]) | (static_cast<usize>(data[15]) << 8);
    const uint8 bits = data[16];
    const uint8 flags = data[17];
    if (cmap != 0 || kind != 2 || (bits != 24 && bits != 32) || (flags & 0xC0) != 0 || !(0 < width && width <= 4096) ||
        !(0 < height && height <= 4096))
    {
        return errors.Invalid("only bounded uncompressed true-color TGA is supported");
    }
    if (width * height * 4 > usize{16} * 1024 * 1024)
    {
        return errors.Invalid("decoded texture byte limit");
    }
    const usize step = bits / 8;
    const usize offset = 18 + ident;
    if (data.size() < offset + width * height * step)
    {
        return errors.Invalid("truncated TGA");
    }
    if (!image.Rgba.TryResize(width * height * 4))
    {
        return errors.Memory();
    }
    for (usize y = 0; y < height; ++y)
    {
        for (usize x = 0; x < width; ++x)
        {
            const usize sx = (flags & 0x10) != 0 ? width - 1 - x : x;
            const usize sy = (flags & 0x20) != 0 ? y : height - 1 - y;
            const usize index = offset + (sy * width + sx) * step;
            uint8* out = image.Rgba.GetData() + (y * width + x) * 4;
            out[0] = data[index + 2];
            out[1] = data[index + 1];
            out[2] = data[index];
            out[3] = step == 4 ? data[index + 3] : 255;
        }
    }
    for (usize i = 3; i < image.Rgba.GetSize(); i += 4)
    {
        if (image.Rgba[i] != 255)
        {
            return errors.Invalid("alpha requires a separate conversion profile");
        }
    }
    image.Width = static_cast<uint32>(width);
    image.Height = static_cast<uint32>(height);
    return true;
}

// ---------------------------------------------------------------------------
// rectangle(), clip() and atlas_triangles().

[[nodiscard]] bool
Rectangle(Failure& errors, const Array<Point>& points, const Array<Triangle>& triangles, float64 (&output)[12]) noexcept
{
    // min()/max() keep the first of equal values, which preserves the sign of zeroes.
    Vec3 low = points[0].P;
    Vec3 high = points[0].P;
    for (usize i = 0; i < 3; ++i)
    {
        for (const Point& point : points)
        {
            low.V[i] = point.P.V[i] < low.V[i] ? point.P.V[i] : low.V[i];
            high.V[i] = point.P.V[i] > high.V[i] ? point.P.V[i] : high.V[i];
        }
    }
    usize axes[3]{};
    usize axisCount = 0;
    for (usize i = 0; i < 3; ++i)
    {
        if (high.V[i] - low.V[i] > 1e-5)
        {
            axes[axisCount++] = i;
        }
    }
    if (axisCount != 2)
    {
        return errors.Invalid("emissive PLY is not an axis-aligned planar rectangle");
    }
    const Triangle& first = triangles[0];
    const Vec3& origin = points[static_cast<usize>(first.I[0])].P;
    Vec3 normal;
    if (!Unit(errors,
              Cross(Sub(points[static_cast<usize>(first.I[1])].P, origin),
                    Sub(points[static_cast<usize>(first.I[2])].P, origin)),
              normal))
    {
        return false;
    }
    Vec3 u;
    Vec3 v;
    u.V[axes[0]] = (high.V[axes[0]] - low.V[axes[0]]) / 2.0;
    v.V[axes[1]] = (high.V[axes[1]] - low.V[axes[1]]) / 2.0;
    if (Dot(Cross(u, v), normal) < 0.0)
    {
        v = Mul(v, -1.0);
    }
    PySum area;
    for (const Triangle& triangle : triangles)
    {
        const Vec3& a = points[static_cast<usize>(triangle.I[0])].P;
        const Vec3 n = Cross(Sub(points[static_cast<usize>(triangle.I[1])].P, a),
                             Sub(points[static_cast<usize>(triangle.I[2])].P, a));
        area.Add(math::Sqrt(Dot(n, n)) / 2.0);
    }
    const float64 total = area.Result();
    const float64 tolerance = 1e-5 >= total * 1e-4 ? 1e-5 : total * 1e-4;
    if (Abs(total - 4.0 * math::Sqrt(Dot(u, u) * Dot(v, v))) > tolerance)
    {
        return errors.Invalid("emitter is not a complete rectangle");
    }
    for (usize i = 0; i < 3; ++i)
    {
        output[i] = u.V[i];
        output[3 + i] = v.V[i];
        output[6 + i] = normal.V[i];
        output[9 + i] = (low.V[i] + high.V[i]) / 2.0;
    }
    return true;
}

[[nodiscard]] float64 Component(const Vertex& vertex, usize index) noexcept
{
    return index < 3 ? vertex.P.V[index] : index < 6 ? vertex.N.V[index - 3] : vertex.Uv[index - 6];
}

void SetComponent(Vertex& vertex, usize index, float64 value) noexcept
{
    if (index < 3)
    {
        vertex.P.V[index] = value;
    }
    else if (index < 6)
    {
        vertex.N.V[index - 3] = value;
    }
    else
    {
        vertex.Uv[index - 6] = value;
    }
}

[[nodiscard]] bool
Clip(const Array<Vertex>& poly, usize axis, float64 limit, bool greater, Array<Vertex>& result) noexcept
{
    result.Clear();
    const usize count = poly.GetSize();
    for (usize i = 0; i < count; ++i)
    {
        const Vertex& previous = poly[(i + count - 1) % count];
        const Vertex& current = poly[i];
        const float64 a = previous.Uv[axis];
        const float64 b = current.Uv[axis];
        const bool insideA = greater ? a >= limit : a <= limit;
        const bool insideB = greater ? b >= limit : b <= limit;
        if (insideA != insideB)
        {
            const float64 t = (limit - a) / (b - a);
            Vertex mixed;
            for (usize k = 0; k < 8; ++k)
            {
                const float64 x = Component(previous, k);
                const float64 y = Component(current, k);
                SetComponent(mixed, k, x + t * (y - x));
            }
            if (!result.TryAdd(mixed))
            {
                return false;
            }
        }
        if (insideB && !result.TryAdd(current))
        {
            return false;
        }
    }
    return true;
}

struct Piece final
{
    Vertex V[3];
};

struct ClipPlane final
{
    usize Axis = 0;
    float64 Limit = 0.0;
    bool Greater = false;
};

[[nodiscard]] bool AtlasCell(Failure& errors,
                             const Vertex (&vertices)[3],
                             float64 x,
                             float64 y,
                             float64 atlasWidth,
                             float64 atlasHeight,
                             Array<Piece>& output) noexcept
{
    Array<Vertex> poly;
    Array<Vertex> next;
    if (!poly.TryAddRange(std::span<const Vertex>(vertices, 3)))
    {
        return errors.Memory();
    }
    const ClipPlane planes[4] = {{0, x, true}, {0, x + 1.0, false}, {1, y, true}, {1, y + 1.0, false}};
    for (const ClipPlane& plane : planes)
    {
        if (poly.GetSize() == 0)
        {
            break;
        }
        if (!Clip(poly, plane.Axis, plane.Limit, plane.Greater, next))
        {
            return errors.Memory();
        }
        Array<Vertex> swap = static_cast<Array<Vertex>&&>(poly);
        poly = static_cast<Array<Vertex>&&>(next);
        next = static_cast<Array<Vertex>&&>(swap);
    }
    for (usize i = 1; i + 1 < poly.GetSize(); ++i)
    {
        const Vertex triangle[3] = {poly[0], poly[i], poly[i + 1]};
        const Vec3 geometric = Cross(Sub(triangle[1].P, triangle[0].P), Sub(triangle[2].P, triangle[0].P));
        if (Dot(geometric, geometric) < 1e-20)
        {
            continue;
        }
        Piece piece;
        for (usize k = 0; k < 3; ++k)
        {
            piece.V[k] = triangle[k];
            piece.V[k].Uv[0] = (2.0 + (triangle[k].Uv[0] - x) * 512.0) / atlasWidth;
            piece.V[k].Uv[1] = (2.0 + (triangle[k].Uv[1] - y) * 512.0) / atlasHeight;
        }
        if (!output.TryAdd(piece))
        {
            return errors.Memory();
        }
    }
    return true;
}

[[nodiscard]] bool AtlasTriangles(Failure& errors,
                                  const Vertex (&vertices)[3],
                                  float64 atlasWidth,
                                  float64 atlasHeight,
                                  Array<Piece>& output) noexcept
{
    float64 minimum[2] = {vertices[0].Uv[0], vertices[0].Uv[1]};
    float64 maximum[2] = {vertices[0].Uv[0], vertices[0].Uv[1]};
    for (usize axis = 0; axis < 2; ++axis)
    {
        for (const Vertex& vertex : vertices)
        {
            minimum[axis] = vertex.Uv[axis] < minimum[axis] ? vertex.Uv[axis] : minimum[axis];
            maximum[axis] = vertex.Uv[axis] > maximum[axis] ? vertex.Uv[axis] : maximum[axis];
        }
    }
    const float64 lowX = math::Floor(minimum[0]);
    const float64 lowY = math::Floor(minimum[1]);
    const float64 cellsX = math::Floor(maximum[0]) - lowX + 1.0;
    const float64 cellsY = math::Floor(maximum[1]) - lowY + 1.0;
    if (cellsX * cellsY > 256.0)
    {
        return errors.Invalid("wallpaper repeat clipping limit");
    }
    for (float64 ix = 0.0; ix < cellsX; ix += 1.0)
    {
        for (float64 iy = 0.0; iy < cellsY; iy += 1.0)
        {
            if (!AtlasCell(errors, vertices, lowX + ix, lowY + iy, atlasWidth, atlasHeight, output))
            {
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// _cook().

struct Color final
{
    float64 V[3]{};
};

[[nodiscard]] bool SameColor(const Color& a, const Color& b) noexcept
{
    return a.V[0] == b.V[0] && a.V[1] == b.V[1] && a.V[2] == b.V[2];
}

struct Cooker final
{
    static constexpr usize ATLAS_WIDTH = 544;
    static constexpr usize ATLAS_HEIGHT = 516;

    Cooker(Context& context, Scene& source) noexcept : Ctx(context), Source(source) {}

    Context& Ctx;
    Scene& Source;
    Array<Image> Images;
    usize Wallpaper = 0;
    Array<uint8> Atlas;
    Array<Color> Colors;
    Mesh Meshes[6];
    Array<Material> Materials;
    Array<Color> Emissions;
    Array<Item> EmitterItems;
    Array<std::string_view> Inputs;
    int64 SourceTriangles = 0;
    int64 SourceMeshes = 0;
    int64 DegenerateTriangles = 0;
    int64 CookedVertices = 0;

    [[nodiscard]] bool AddInput(std::string_view path) noexcept
    {
        for (const std::string_view existing : Inputs)
        {
            if (existing == path)
            {
                return true;
            }
        }
        return Inputs.TryAdd(path) || Ctx.Errors.Memory();
    }

    // solid(): one 16x16 sRGB block per distinct base colour.
    [[nodiscard]] bool Solid(const Array<float64>& color, float64 (&uv)[2]) noexcept
    {
        Failure& errors = Ctx.Errors;
        if (color.GetSize() != 3)
        {
            return errors.Invalid("base color profile");
        }
        Color value;
        for (usize i = 0; i < 3; ++i)
        {
            if (color[i] < 0.0 || color[i] > 1.0)
            {
                return errors.Invalid("base color profile");
            }
            value.V[i] = color[i];
        }
        usize index = Colors.GetSize();
        for (usize i = 0; i < Colors.GetSize() && index == Colors.GetSize(); ++i)
        {
            index = SameColor(Colors[i], value) ? i : index;
        }
        if (index == Colors.GetSize() && !Colors.TryAdd(value))
        {
            return errors.Memory();
        }
        if (index >= 16)
        {
            return errors.Invalid("solid atlas limit");
        }
        uint8 rgba[4] = {0, 0, 0, 255};
        for (usize i = 0; i < 3; ++i)
        {
            const float64 v = value.V[i];
            const float64 encoded = v <= 0.0031308 ? 12.92 * v : 1.055 * math::Pow(v, 1.0 / 2.4) - 0.055;
            const float64 rounded = RoundHalfEven(255.0 * encoded);
            rgba[i] = static_cast<uint8>(rounded < 0.0 ? 0.0 : rounded > 255.0 ? 255.0 : rounded);
        }
        for (usize y = index * 16; y < (index + 1) * 16; ++y)
        {
            for (usize x = 520; x < 536; ++x)
            {
                uint8* out = Atlas.GetData() + (y * ATLAS_WIDTH + x) * 4;
                for (usize k = 0; k < 4; ++k)
                {
                    out[k] = rgba[k];
                }
            }
        }
        uv[0] = 528.5 / static_cast<float64>(ATLAS_WIDTH);
        uv[1] = (static_cast<float64>(index * 16) + 8.5) / static_cast<float64>(ATLAS_HEIGHT);
        return true;
    }

    [[nodiscard]] const Param* MaterialParam(const MaterialRef& material, std::string_view key) const noexcept
    {
        return FindParam(Source.Commands[material.CommandIndex], key);
    }

    [[nodiscard]] bool MaterialType(const MaterialRef& material, std::string_view& type) noexcept
    {
        const Command& command = Source.Commands[material.CommandIndex];
        if (material.Inline)
        {
            if (command.Names.GetSize() == 0)
            {
                return Ctx.Errors.Invalid("list index out of range");
            }
            type = command.Names[0].List ? std::string_view("<list>") : command.Names[0].Text.GetView();
            return true;
        }
        const Param* param = FindParam(command, "string type");
        if (param == nullptr)
        {
            return Ctx.Errors.Invalid("missing parameter string type");
        }
        if (param->Values.GetSize() == 0)
        {
            return Ctx.Errors.Invalid("list index out of range");
        }
        type = param->Values[0].GetView();
        return true;
    }

    // finite(material.get(key, [])) == [expected]
    [[nodiscard]] bool
    RoughnessIs(const MaterialRef& material, std::string_view key, float64 expected, bool& equal) noexcept
    {
        const Param* param = MaterialParam(material, key);
        Array<float64> values;
        if (param != nullptr && !Ctx.Finite(param->Values, values))
        {
            return false;
        }
        equal = values.GetSize() == 1 && values[0] == expected;
        return true;
    }

    [[nodiscard]] bool
    RoughnessProfile(const MaterialRef& material, float64 expected, std::string_view message) noexcept
    {
        if (!ValuesEqual(MaterialParam(material, "bool remaproughness"), "false"))
        {
            return Ctx.Errors.Invalid(message);
        }
        bool uOk = false;
        bool vOk = false;
        if (!RoughnessIs(material, "float uroughness", expected, uOk) ||
            (uOk && !RoughnessIs(material, "float vroughness", expected, vOk)))
        {
            return false;
        }
        return (uOk && vOk) || Ctx.Errors.Invalid(message);
    }

    [[nodiscard]] bool
    LoadTriangleMesh(const Command& command, Array<Point>& points, Array<Triangle>& triangles) noexcept
    {
        Failure& errors = Ctx.Errors;
        Array<float64> positions;
        Array<float64> normals;
        Array<float64> uv;
        if (!Ctx.FiniteParam(FindParam(command, "point3 P"), positions, "point3 P") ||
            !Ctx.FiniteParam(FindParam(command, "normal N"), normals, "normal N") ||
            !Ctx.FiniteParam(FindParam(command, "point2 uv"), uv, "point2 uv"))
        {
            return false;
        }
        if (positions.GetSize() % 3 != 0)
        {
            return errors.Invalid("trianglemesh positions are not whole points");
        }
        const usize count = positions.GetSize() / 3;
        if (!points.TryResize(count))
        {
            return errors.Memory();
        }
        for (usize p = 0; p < count; ++p)
        {
            Point& point = points[p];
            for (usize k = 0; k < 3; ++k)
            {
                point.P.V[k] = positions[3 * p + k];
            }
            const usize available = normals.GetSize() > 3 * p ? normals.GetSize() - 3 * p : 0;
            point.NormalCount = static_cast<uint8>(available >= 3 ? 3 : available);
            for (usize k = 0; k < point.NormalCount; ++k)
            {
                point.N.V[k] = normals[3 * p + k];
            }
            if (uv.GetSize() <= 2 * p + 1)
            {
                return errors.Invalid("list index out of range");
            }
            point.Uv[0] = uv[2 * p];
            point.Uv[1] = 1.0 - uv[2 * p + 1];
        }
        const Param* indices = FindParam(command, "integer indices");
        if (indices == nullptr)
        {
            return errors.Invalid("missing parameter integer indices");
        }
        if (indices->Values.GetSize() % 3 != 0)
        {
            return errors.Invalid("list index out of range");
        }
        for (usize i = 0; i < indices->Values.GetSize(); i += 3)
        {
            Triangle triangle;
            for (usize k = 0; k < 3; ++k)
            {
                if (!ParseInt(indices->Values[i + k].GetView(), triangle.I[k]))
                {
                    return errors.Invalid("invalid literal for int() with base 10");
                }
            }
            if (!triangles.TryAdd(triangle))
            {
                return errors.Memory();
            }
        }
        SourceTriangles += static_cast<int64>(triangles.GetSize());
        return true;
    }

    [[nodiscard]] bool
    LoadPoints(const Shape& shape, std::string_view kind, Array<Point>& points, Array<Triangle>& triangles) noexcept
    {
        Failure& errors = Ctx.Errors;
        const Command& command = Source.Commands[shape.CommandIndex];
        if (kind == "plymesh")
        {
            const Param* file = FindParam(command, "string filename");
            if (file == nullptr || file->Values.GetSize() == 0)
            {
                return errors.Invalid("missing parameter string filename");
            }
            const std::string_view path = file->Values[0].GetView();
            String normalised;
            if (!AddInput(path) || !Ctx.AssetPath(path, normalised) ||
                !ReadPly(Ctx, normalised.GetView(), points, triangles))
            {
                return false;
            }
            SourceMeshes += 1;
            SourceTriangles += static_cast<int64>(triangles.GetSize());
            return true;
        }
        if (kind == "trianglemesh")
        {
            return LoadTriangleMesh(command, points, triangles);
        }
        if (kind == "disk")
        {
            return true;
        }
        return errors.Invalid("unsupported shape ", kind);
    }

    [[nodiscard]] bool DiskItem(const Command& command, const float64 (&transform)[16], Item& item) noexcept
    {
        Failure& errors = Ctx.Errors;
        Array<float64> radius;
        if (!Ctx.FiniteParam(FindParam(command, "float radius"), radius, "float radius"))
        {
            return false;
        }
        if (radius.GetSize() == 0)
        {
            return errors.Invalid("list index out of range");
        }
        if (!(0.0 < radius[0] && radius[0] <= 1.0))
        {
            return errors.Invalid("disk radius profile");
        }
        const Vec3 x = Mul(TransformVector(transform, Vec3{{1.0, 0.0, 0.0}}), radius[0]);
        const Vec3 y = Mul(TransformVector(transform, Vec3{{0.0, 1.0, 0.0}}), radius[0]);
        const Vec3 z = TransformVector(transform, Vec3{{0.0, 0.0, 1.0}});
        const Vec3 o = TransformPoint(transform, Vec3{{0.0, 0.0, 0.0}});
        item.Mesh = 5;
        for (usize k = 0; k < 3; ++k)
        {
            item.Transform[k] = x.V[k];
            item.Transform[3 + k] = y.V[k];
            item.Transform[6 + k] = z.V[k];
            item.Transform[9 + k] = o.V[k];
        }
        return true;
    }

    [[nodiscard]] bool QuadItem(const Array<Point>& points,
                                const Array<Triangle>& triangles,
                                const float64 (&transform)[16],
                                Item& item) noexcept
    {
        Failure& errors = Ctx.Errors;
        const bool quad = points.GetSize() == 4 && triangles.GetSize() == 2 && triangles[0].I[0] == 0 &&
                          triangles[0].I[1] == 1 && triangles[0].I[2] == 2 && triangles[1].I[0] == 0 &&
                          triangles[1].I[1] == 2 && triangles[1].I[2] == 3;
        if (!quad)
        {
            return errors.Invalid("unsupported emissive quad");
        }
        Vec3 p[4];
        for (usize k = 0; k < 4; ++k)
        {
            p[k] = TransformPoint(transform, points[k].P);
        }
        const Vec3 u = Mul(Sub(p[1], p[0]), 0.5);
        const Vec3 v = Mul(Sub(p[3], p[0]), 0.5);
        const Vec3 corner = Add(p[0], Add(Mul(u, 2.0), Mul(v, 2.0)));
        float64 deviation = Abs(corner.V[0] - p[2].V[0]);
        for (usize k = 1; k < 3; ++k)
        {
            const float64 value = Abs(corner.V[k] - p[2].V[k]);
            deviation = value > deviation ? value : deviation;
        }
        if (deviation > 1e-4)
        {
            return errors.Invalid("non-parallelogram emitter");
        }
        Vec3 normal;
        if (!Unit(errors, Cross(u, v), normal))
        {
            return false;
        }
        const Vec3 centre = Mul(Add(p[0], p[2]), 0.5);
        item.Mesh = 4;
        for (usize k = 0; k < 3; ++k)
        {
            item.Transform[k] = u.V[k];
            item.Transform[3 + k] = v.V[k];
            item.Transform[6 + k] = normal.V[k];
            item.Transform[9 + k] = centre.V[k];
        }
        return true;
    }

    [[nodiscard]] bool Emitter(const Shape& shape,
                               std::string_view kind,
                               const Array<Point>& points,
                               const Array<Triangle>& triangles) noexcept
    {
        Failure& errors = Ctx.Errors;
        const Color emission{{shape.Graphics.Emission[0], shape.Graphics.Emission[1], shape.Graphics.Emission[2]}};
        usize index = Emissions.GetSize();
        for (usize i = 0; i < Emissions.GetSize() && index == Emissions.GetSize(); ++i)
        {
            index = SameColor(Emissions[i], emission) ? i : index;
        }
        if (index == Emissions.GetSize() && !Emissions.TryAdd(emission))
        {
            return errors.Memory();
        }
        Item item;
        item.Material = static_cast<uint32>(4 + index);
        const float64(&transform)[16] = shape.Graphics.Transform;
        bool ok = false;
        if (kind == "disk")
        {
            ok = DiskItem(Source.Commands[shape.CommandIndex], transform, item);
        }
        else if (kind == "plymesh")
        {
            for (usize k = 0; k < 16; ++k)
            {
                if (transform[k] != IDENTITY[k])
                {
                    return errors.Invalid("transformed rectangle profile not supported");
                }
            }
            item.Mesh = 4;
            ok = Rectangle(errors, points, triangles, item.Transform);
        }
        else
        {
            ok = QuadItem(points, triangles, transform, item);
        }
        return ok && (EmitterItems.TryAdd(item) || errors.Memory());
    }

    // Material classification: group index plus an optional fixed solid-colour UV.
    [[nodiscard]] bool
    Classify(const MaterialRef& material, usize& group, bool& hasFixedUv, float64 (&fixedUv)[2]) noexcept
    {
        Failure& errors = Ctx.Errors;
        std::string_view type;
        if (!MaterialType(material, type))
        {
            return false;
        }
        hasFixedUv = false;
        if (type == "conductor")
        {
            if (!RoughnessProfile(material, 0.1, "conductor roughness profile"))
            {
                return false;
            }
            Array<float64> eta;
            Array<float64> k;
            if (!Ctx.FiniteParam(MaterialParam(material, "rgb eta"), eta, "rgb eta") ||
                !Ctx.FiniteParam(MaterialParam(material, "rgb k"), k, "rgb k"))
            {
                return false;
            }
            if (eta.GetSize() != 3 || k.GetSize() != 3)
            {
                return errors.Invalid("conductor eta/k profile");
            }
            Array<float64> color;
            if (!color.TryResize(3))
            {
                return errors.Memory();
            }
            for (usize i = 0; i < 3; ++i)
            {
                if (eta[i] < 0.0 || k[i] < 0.0)
                {
                    return errors.Invalid("conductor eta/k profile");
                }
            }
            for (usize i = 0; i < 3; ++i)
            {
                const float64 e = eta[i];
                const float64 a = k[i];
                color[i] = (math::Pow(e - 1.0, 2.0) + a * a) / (math::Pow(e + 1.0, 2.0) + a * a);
            }
            group = 3;
            hasFixedUv = true;
            return Solid(color, fixedUv);
        }
        if (type != "diffuse" && type != "coateddiffuse")
        {
            return errors.Invalid("unsupported material ", type);
        }
        const Param* reflectance = MaterialParam(material, "texture reflectance");
        bool hasTexture = false;
        std::string_view texture;
        if (reflectance != nullptr)
        {
            if (reflectance->Values.GetSize() == 0)
            {
                return errors.Invalid("list index out of range");
            }
            hasTexture = true;
            texture = reflectance->Values[0].GetView();
        }
        std::string_view textureFile;
        bool known = false;
        usize textureIndex = 0;
        for (usize i = 0; hasTexture && i < Source.Textures.GetSize(); ++i)
        {
            if (Source.Textures[i].Name == texture)
            {
                textureFile = Source.Textures[i].File;
                textureIndex = i;
                known = true;
            }
        }
        group = EndsWith(textureFile, "wood5.tga") ? 1 : EndsWith(textureFile, "Tiles.tga") ? 2 : 0;
        if (hasTexture && !known)
        {
            return errors.Invalid("unknown reflectance texture");
        }
        if (hasTexture && group == 0 && textureIndex != Wallpaper)
        {
            return errors.Invalid("diffuse texture profile");
        }
        if (type == "coateddiffuse")
        {
            if (group != 1 && group != 2)
            {
                return errors.Invalid("coated diffuse roughness profile");
            }
            if (!RoughnessProfile(material, group == 1 ? 0.1 : 0.01, "coated diffuse roughness profile"))
            {
                return false;
            }
        }
        else if (group != 0)
        {
            return errors.Invalid("diffuse material profile");
        }
        if (hasTexture && !texture.empty())
        {
            return true;
        }
        Array<float64> color;
        const Param* rgb = MaterialParam(material, "rgb reflectance");
        if (rgb != nullptr)
        {
            if (!Ctx.Finite(rgb->Values, color))
            {
                return false;
            }
        }
        else if (!color.TryResize(3, 0.5))
        {
            return errors.Memory();
        }
        hasFixedUv = true;
        return Solid(color, fixedUv);
    }

    [[nodiscard]] bool TriangleVertices(const Array<Point>& points,
                                        const Triangle& triangle,
                                        Vertex (&vertices)[3],
                                        bool& degenerate) noexcept
    {
        Failure& errors = Ctx.Errors;
        const auto count = static_cast<int64>(points.GetSize());
        const Point* source[3]{};
        for (usize k = 0; k < 3; ++k)
        {
            int64 index = triangle.I[k];
            index = index < 0 ? index + count : index; // Python negative indexing.
            if (index < 0 || index >= count)
            {
                return errors.Invalid("list index out of range");
            }
            source[k] = &points[static_cast<usize>(index)];
        }
        const Vec3 geometric = Cross(Sub(source[1]->P, source[0]->P), Sub(source[2]->P, source[0]->P));
        degenerate = Dot(geometric, geometric) < 1e-24;
        if (degenerate)
        {
            return true;
        }
        Vec3 normal;
        if (!Unit(errors, geometric, normal))
        {
            return false;
        }
        for (usize k = 0; k < 3; ++k)
        {
            vertices[k].P = source[k]->P;
            vertices[k].Uv[0] = source[k]->Uv[0];
            vertices[k].Uv[1] = source[k]->Uv[1];
            if (source[k]->NormalCount == 3)
            {
                if (!Unit(errors, source[k]->N, vertices[k].N))
                {
                    return false;
                }
            }
            else if (source[k]->NormalCount == 0)
            {
                vertices[k].N = normal;
            }
            else
            {
                return errors.Invalid("incomplete trianglemesh normal");
            }
        }
        return true;
    }

    [[nodiscard]] bool
    Surface(const Shape& shape, const Array<Point>& points, const Array<Triangle>& triangles) noexcept
    {
        Failure& errors = Ctx.Errors;
        usize group = 0;
        bool hasFixedUv = false;
        float64 fixedUv[2]{};
        if (!Classify(Source.Materials[static_cast<usize>(shape.Graphics.Material)], group, hasFixedUv, fixedUv))
        {
            return false;
        }
        for (usize k = 0; k < 16; ++k)
        {
            if (shape.Graphics.Transform[k] != IDENTITY[k])
            {
                return errors.Invalid("transformed room mesh profile not supported");
            }
        }
        Mesh& mesh = Meshes[group];
        Array<Piece> pieces;
        for (usize triangleIndex = 0; triangleIndex < triangles.GetSize(); ++triangleIndex)
        {
            if (triangleIndex % 256 == 0 && !Ctx.CheckCancel())
            {
                return false;
            }
            Vertex vertices[3];
            bool degenerate = false;
            if (!TriangleVertices(points, triangles[triangleIndex], vertices, degenerate))
            {
                return false;
            }
            if (degenerate)
            {
                DegenerateTriangles += 1;
                continue;
            }
            pieces.Clear();
            if (hasFixedUv)
            {
                Piece piece{{vertices[0], vertices[1], vertices[2]}};
                for (Vertex& vertex : piece.V)
                {
                    vertex.Uv[0] = fixedUv[0];
                    vertex.Uv[1] = fixedUv[1];
                }
                if (!pieces.TryAdd(piece))
                {
                    return errors.Memory();
                }
            }
            else if (group == 0)
            {
                if (!AtlasTriangles(errors,
                                    vertices,
                                    static_cast<float64>(ATLAS_WIDTH),
                                    static_cast<float64>(ATLAS_HEIGHT),
                                    pieces))
                {
                    return false;
                }
            }
            else if (!pieces.TryAdd(Piece{{vertices[0], vertices[1], vertices[2]}}))
            {
                return errors.Memory();
            }
            for (const Piece& piece : pieces)
            {
                CookedVertices += 3;
                if (CookedVertices > 500000)
                {
                    return errors.Fail(Status::Limit, "expanded vertex limit");
                }
                const auto base = static_cast<uint32>(mesh.Vertices.GetSize());
                for (usize k = 0; k < 3; ++k)
                {
                    if (!mesh.Indices.TryAdd(base + static_cast<uint32>(k)) || !mesh.Vertices.TryAdd(piece.V[k]))
                    {
                        return errors.Memory();
                    }
                }
            }
        }
        return true;
    }
};

[[nodiscard]] bool LoadTextures(Cooker& cooker) noexcept
{
    Context& context = cooker.Ctx;
    Failure& errors = context.Errors;
    usize decoded = 0;
    if (!cooker.Images.TryResize(cooker.Source.Textures.GetSize()))
    {
        return errors.Memory();
    }
    for (usize i = 0; i < cooker.Source.Textures.GetSize(); ++i)
    {
        const std::string_view path = cooker.Source.Textures[i].File;
        String normalised;
        if (!cooker.AddInput(path) || !context.AssetPath(path, normalised) ||
            !ReadTga(context, normalised.GetView(), cooker.Images[i]))
        {
            return false;
        }
        decoded += cooker.Images[i].Rgba.GetSize();
        if (decoded > usize{32} * 1024 * 1024)
        {
            return errors.Invalid("decoded texture byte limit");
        }
    }
    bool found = false;
    for (usize i = 0; i < cooker.Source.Textures.GetSize() && !found; ++i)
    {
        if (EndsWith(cooker.Source.Textures[i].File, "Wallpaper.tga"))
        {
            cooker.Wallpaper = i;
            found = true;
        }
    }
    if (!found)
    {
        return errors.Invalid("supported PBRT reference profile requires Wallpaper.tga");
    }
    const Image& wallpaper = cooker.Images[cooker.Wallpaper];
    if (wallpaper.Width != 512 || wallpaper.Height != 512)
    {
        return errors.Invalid("staircase2 wallpaper profile requires 512x512");
    }
    constexpr usize AW = Cooker::ATLAS_WIDTH;
    if (!cooker.Atlas.TryResize(AW * Cooker::ATLAS_HEIGHT * 4, 0))
    {
        return errors.Memory();
    }
    // 2-pixel wrapped border around the 512x512 wallpaper.
    for (usize y = 0; y < 516; ++y)
    {
        for (usize x = 0; x < 516; ++x)
        {
            const usize src = (((y + 510) % 512) * 512 + (x + 510) % 512) * 4;
            const usize dst = (y * AW + x) * 4;
            for (usize k = 0; k < 4; ++k)
            {
                cooker.Atlas[dst + k] = wallpaper.Rgba[src + k];
            }
        }
    }
    return true;
}

[[nodiscard]] bool FindTexture(Cooker& cooker, std::string_view suffix, usize& index) noexcept
{
    for (usize i = 0; i < cooker.Source.Textures.GetSize(); ++i)
    {
        if (EndsWith(cooker.Source.Textures[i].File, suffix))
        {
            index = i;
            return true;
        }
    }
    return cooker.Ctx.Errors.Invalid("supported PBRT reference profile requires ", suffix);
}

[[nodiscard]] bool AddFixedMeshes(Cooker& cooker) noexcept
{
    // Unit quad (mesh 4) and 64-segment disk (mesh 5).
    Mesh& quad = cooker.Meshes[4];
    const float64 corners[4][2] = {{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}};
    for (const auto& corner : corners)
    {
        if (!quad.Vertices.TryAdd(Vertex{Vec3{{corner[0], corner[1], 0.0}}, Vec3{{0.0, 0.0, 1.0}}, {0.0, 0.0}}))
        {
            return false;
        }
    }
    for (const uint32 index : {0U, 1U, 2U, 0U, 2U, 3U})
    {
        if (!quad.Indices.TryAdd(index))
        {
            return false;
        }
    }
    Mesh& disk = cooker.Meshes[5];
    if (!disk.Vertices.TryAdd(Vertex{Vec3{{0.0, 0.0, 0.0}}, Vec3{{0.0, 0.0, 1.0}}, {0.0, 0.0}}))
    {
        return false;
    }
    constexpr float64 TAU = 6.283185307179586;
    for (uint32 i = 0; i < 64; ++i)
    {
        const float64 angle = static_cast<float64>(i) * TAU / 64.0;
        if (!disk.Vertices.TryAdd(
                Vertex{Vec3{{math::Cos(angle), math::Sin(angle), 0.0}}, Vec3{{0.0, 0.0, 1.0}}, {0.0, 0.0}}))
        {
            return false;
        }
    }
    for (uint32 i = 0; i < 64; ++i)
    {
        if (!disk.Indices.TryAdd(0U) || !disk.Indices.TryAdd(1U + i) || !disk.Indices.TryAdd(1U + (i + 1U) % 64U))
        {
            return false;
        }
    }
    return true;
}

struct CameraFrame final
{
    Vec3 Eye;
    Vec3 Target;
    Vec3 Up;
};

[[nodiscard]] bool DeriveCamera(Failure& errors, const float64 (&camera)[16], CameraFrame& frame) noexcept
{
    // PBRT's CTM is camera-from-world. This profile accepts rigid cameras only.
    Vec3 rows[3];
    for (usize r = 0; r < 3; ++r)
    {
        for (usize c = 0; c < 3; ++c)
        {
            rows[r].V[c] = camera[c * 4 + r];
        }
    }
    for (usize a = 0; a < 3; ++a)
    {
        for (usize b = 0; b < 3; ++b)
        {
            if (Abs(Dot(rows[a], rows[b]) - (a == b ? 1.0 : 0.0)) > 1e-5)
            {
                return errors.Invalid("camera must be rigid");
            }
        }
    }
    for (usize c = 0; c < 3; ++c)
    {
        PySum sum;
        for (usize r = 0; r < 3; ++r)
        {
            sum.Add(rows[r].V[c] * camera[12 + r]);
        }
        frame.Eye.V[c] = -sum.Result();
    }
    frame.Up = rows[1];
    frame.Target = Add(frame.Eye, rows[2]);
    return true;
}

void PackMesh(Sink& blob, const Mesh& mesh) noexcept
{
    blob.U32(static_cast<uint32>(mesh.Vertices.GetSize()));
    blob.U32(static_cast<uint32>(mesh.Indices.GetSize()));
    for (const Vertex& vertex : mesh.Vertices)
    {
        for (usize k = 0; k < 8; ++k)
        {
            blob.F32(Component(vertex, k));
        }
    }
    for (const uint32 index : mesh.Indices)
    {
        blob.U32(index);
    }
}

[[nodiscard]] bool PackBundle(Cooker& cooker,
                              const Array<Item>& items,
                              const CameraFrame& frame,
                              usize wood,
                              usize tiles,
                              Array<uint8>& bundle) noexcept
{
    Failure& errors = cooker.Ctx.Errors;
    bundle.Clear();
    Sink blob(bundle);
    blob.Append(std::string_view("LUSCN01\0", 8));
    blob.U32(1);
    blob.U32(6);
    blob.U32(static_cast<uint32>(cooker.Materials.GetSize()));
    blob.U32(3);
    blob.U32(static_cast<uint32>(items.GetSize()));
    for (const Vec3* vector : {&frame.Eye, &frame.Target, &frame.Up})
    {
        for (const float64 value : vector->V)
        {
            blob.F32(value);
        }
    }
    // math.radians(x) is x * (pi / 180).
    constexpr float64 DEGREES_TO_RADIANS = 3.141592653589793 / 180.0;
    blob.F32(cooker.Source.Fov * DEGREES_TO_RADIANS);
    blob.U32(static_cast<uint32>(Cooker::ATLAS_WIDTH));
    blob.U32(static_cast<uint32>(Cooker::ATLAS_HEIGHT));
    blob.U32(static_cast<uint32>(cooker.Atlas.GetSize()));
    blob.Bytes(cooker.Atlas.AsSpan());
    for (const usize index : {wood, tiles})
    {
        const Image& image = cooker.Images[index];
        blob.U32(image.Width);
        blob.U32(image.Height);
        blob.U32(static_cast<uint32>(image.Rgba.GetSize()));
        blob.Bytes(image.Rgba.AsSpan());
    }
    for (const Material& material : cooker.Materials)
    {
        blob.U32(material.Texture);
        blob.F32(material.Metallic);
        blob.F32(material.Roughness);
        for (const float64 value : material.Emission)
        {
            blob.F32(value);
        }
    }
    for (const Mesh& mesh : cooker.Meshes)
    {
        if (!cooker.Ctx.CheckCancel())
        {
            return false;
        }
        PackMesh(blob, mesh);
    }
    for (const Item& item : items)
    {
        blob.U32(item.Mesh);
        blob.U32(item.Material);
        for (const float64 value : item.Transform)
        {
            blob.F32(value);
        }
    }
    if (blob.Overflow())
    {
        return errors.Invalid("float too large to pack with f format");
    }
    if (!blob.Ok())
    {
        return errors.Memory();
    }
    if (bundle.GetSize() > MAX_SOURCE_BYTES)
    {
        return errors.Fail(Status::Limit, "bundle byte limit");
    }
    return true;
}

struct ManifestFacts final
{
    bool Staircase = false;
    std::string_view SourceName;
    const Array<std::string_view>* Inputs = nullptr;
    const Array<Digest>* InputDigests = nullptr;
    Digest Bundle;
    int64 SourceMeshes = 0;
    int64 SourceTriangles = 0;
    int64 Degenerate = 0;
    int64 Materials = 0;
    int64 Items = 0;
    int64 CookedTriangles = 0;
    int64 RenderedTriangles = 0;
    int64 Bytes = 0;
    const CameraFrame* Camera = nullptr;
    float64 Fov = 0.0;
};

void HexString(PyJson& json, const Digest& digest) noexcept
{
    char text[64]{};
    constexpr char HEX[] = "0123456789abcdef";
    for (usize i = 0; i < 32; ++i)
    {
        text[2 * i] = HEX[digest.Data[i] >> 4];
        text[2 * i + 1] = HEX[digest.Data[i] & 15];
    }
    json.String(std::string_view(text, sizeof(text)));
}

[[nodiscard]] bool WriteManifest(const ManifestFacts& facts, Array<uint8>& output) noexcept
{
    output.Clear();
    Sink sink(output);
    PyJson json(sink);
    json.BeginObject();
    json.Key("version");
    json.Integer(1);
    json.Key("source");
    json.String(facts.Staircase ? std::string_view("Modern Hall / staircase2") : facts.SourceName);
    json.Key("author");
    json.String(facts.Staircase ? std::string_view("NewSee2l035") : std::string_view("See retained LICENSE.txt"));
    json.Key("license");
    json.String(facts.Staircase ? std::string_view("https://creativecommons.org/licenses/by/3.0/")
                                : std::string_view("See retained LICENSE.txt"));
    json.Key("source_format");
    json.String("PBRT v4 subset + binary PLY + uncompressed TGA");
    json.Key("inputs");
    json.BeginObject();
    for (usize i = 0; i < facts.Inputs->GetSize(); ++i)
    {
        json.Key((*facts.Inputs)[i]);
        HexString(json, (*facts.InputDigests)[i]);
    }
    json.EndObject();
    json.Key("bundle_sha256");
    HexString(json, facts.Bundle);
    const struct
    {
        std::string_view Key;
        int64 Value;
    } counts[] = {
        {"source_ply_meshes", facts.SourceMeshes},
        {"source_triangles", facts.SourceTriangles},
        {"discarded_zero_area_triangles", facts.Degenerate},
        {"meshes", 6},
        {"materials", facts.Materials},
        {"items", facts.Items},
        {"cooked_triangles", facts.CookedTriangles},
        {"rendered_triangles", facts.RenderedTriangles},
        {"bytes", facts.Bytes},
    };
    for (const auto& count : counts)
    {
        json.Key(count.Key);
        json.Integer(count.Value);
    }
    json.Key("camera");
    json.BeginObject();
    const struct
    {
        std::string_view Key;
        const Vec3* Value;
    } vectors[3] = {{"eye", &facts.Camera->Eye}, {"target", &facts.Camera->Target}, {"up", &facts.Camera->Up}};
    for (const auto& vector : vectors)
    {
        json.Key(vector.Key);
        json.BeginArray();
        for (const float64 value : vector.Value->V)
        {
            json.Float(value);
        }
        json.EndArray();
    }
    json.Key("fov_degrees");
    json.Float(facts.Fov);
    json.EndObject();
    json.Key("approximations");
    json.BeginArray();
    for (const std::string_view text : {
             "PBRT coated diffuse mapped to frozen RGB GGX/Lambert; no layer multiple scattering",
             "conductor eta/k reduced to RGB normal-incidence F0 and Schlick",
             "diffuse surfaces use L6 dielectric F0 0.04",
             "L6 surface emission is two-sided; PBRT diffuse area lights default to one-sided",
             "single retained texture mip; no PBRT footprint/trilinear filtering",
             "disk tessellation: 64 segments",
             "emissive planar grids represented by equivalent quads",
             "PBRT integrator/sampler options replaced by explicit L6 capture settings",
         })
    {
        json.String(text);
    }
    json.EndArray();
    json.EndObject();
    sink.Append('\n');
    return sink.Ok();
}

[[nodiscard]] bool CookShapes(Cooker& cooker) noexcept
{
    Context& context = cooker.Ctx;
    Array<Point> points;
    Array<Triangle> triangles;
    for (const Shape& shape : cooker.Source.Shapes)
    {
        if (!context.CheckCancel())
        {
            return false;
        }
        std::string_view kind;
        if (!FirstName(context, cooker.Source.Commands[shape.CommandIndex], kind))
        {
            return false;
        }
        points.Clear();
        triangles.Clear();
        if (!cooker.LoadPoints(shape, kind, points, triangles))
        {
            return false;
        }
        const bool ok = shape.Graphics.HasEmission ? cooker.Emitter(shape, kind, points, triangles)
                                                   : cooker.Surface(shape, points, triangles);
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool HashInputs(Cooker& cooker, Array<Digest>& digests) noexcept
{
    // sorted(inputs) by code point, each re-read through asset_path().
    Array<std::string_view>& inputs = cooker.Inputs;
    for (usize i = 1; i < inputs.GetSize(); ++i)
    {
        for (usize j = i; j > 0 && inputs[j] < inputs[j - 1]; --j)
        {
            const std::string_view swap = inputs[j];
            inputs[j] = inputs[j - 1];
            inputs[j - 1] = swap;
        }
    }
    for (const std::string_view input : inputs)
    {
        String normalised;
        Bytes data;
        if (!cooker.Ctx.AssetPath(input, normalised) || !cooker.Ctx.Read(normalised.GetView(), MAX_SOURCE_BYTES, data))
        {
            return false;
        }
        if (!digests.TryAdd(Hash(data.Data())))
        {
            return cooker.Ctx.Errors.Memory();
        }
    }
    return true;
}

[[nodiscard]] bool CookScene(Context& context, CookResult& result) noexcept
{
    Failure& errors = context.Errors;
    Scene scene;
    if (!ParseScene(context, scene))
    {
        return false;
    }
    usize emitters = 0;
    for (const Shape& shape : scene.Shapes)
    {
        emitters += shape.Graphics.HasEmission ? 1 : 0;
    }
    if (4 + emitters > 128)
    {
        return errors.Fail(Status::Limit, "renderer profile capacity: at most 128 scene items");
    }
    Cooker cooker(context, scene);
    if (!cooker.AddInput("scene-v4.pbrt") || !cooker.AddInput("LICENSE.txt") || !LoadTextures(cooker))
    {
        return false;
    }
    usize wood = 0;
    usize tiles = 0;
    if (!FindTexture(cooker, "wood5.tga", wood) || !FindTexture(cooker, "Tiles.tga", tiles))
    {
        return false;
    }
    // Diffuse, wood-coated approximation, tile-coated approximation, conductor.
    const Material base[4] = {
        {0, 0.0, 1.0, {0.0, 0.0, 0.0}},
        {1, 0.0, math::Sqrt(0.1), {0.0, 0.0, 0.0}},
        {2, 0.0, math::Sqrt(0.01), {0.0, 0.0, 0.0}},
        {0, 1.0, math::Sqrt(0.1), {0.0, 0.0, 0.0}},
    };
    if (!cooker.Materials.TryAddRange(std::span<const Material>(base, 4)))
    {
        return errors.Memory();
    }
    if (!CookShapes(cooker))
    {
        return false;
    }
    for (const Color& emission : cooker.Emissions)
    {
        if (!cooker.Materials.TryAdd(Material{0xFFFFFFFFU, 0.0, 1.0, {emission.V[0], emission.V[1], emission.V[2]}}))
        {
            return errors.Memory();
        }
    }
    if (!AddFixedMeshes(cooker))
    {
        return errors.Memory();
    }
    Array<Item> items;
    for (uint32 i = 0; i < 4; ++i)
    {
        if (!items.TryAdd(Item{i, i, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0}}))
        {
            return errors.Memory();
        }
    }
    if (!items.TryAddRange(cooker.EmitterItems.AsSpan()))
    {
        return errors.Memory();
    }
    if (cooker.Materials.GetSize() != 8 || items.GetSize() > 128)
    {
        return errors.Invalid("renderer profile capacity");
    }
    CameraFrame frame;
    if (!DeriveCamera(errors, scene.Camera, frame) || !PackBundle(cooker, items, frame, wood, tiles, result.Bundle))
    {
        return false;
    }
    Bytes license;
    if (!context.Read("LICENSE.txt", usize{1024} * 1024, license))
    {
        return false;
    }
    foundation::parsing::ParseError utf8{};
    if (foundation::parsing::ValidateUtf8(license.String(), utf8) != foundation::parsing::ParseStatus::Ok)
    {
        return errors.Invalid("'utf-8' codec can't decode LICENSE.txt");
    }
    Array<Digest> digests;
    if (!HashInputs(cooker, digests))
    {
        return false;
    }
    ManifestFacts facts;
    facts.Staircase = license.String().find("NewSee2l035") != std::string_view::npos;
    facts.SourceName = BaseName(context.Root.GetView());
    facts.Inputs = &cooker.Inputs;
    facts.InputDigests = &digests;
    facts.Bundle = Hash(result.Bundle.AsSpan());
    facts.SourceMeshes = cooker.SourceMeshes;
    facts.SourceTriangles = cooker.SourceTriangles;
    facts.Degenerate = cooker.DegenerateTriangles;
    facts.Materials = static_cast<int64>(cooker.Materials.GetSize());
    facts.Items = static_cast<int64>(items.GetSize());
    for (const Mesh& mesh : cooker.Meshes)
    {
        facts.CookedTriangles += static_cast<int64>(mesh.Indices.GetSize() / 3);
    }
    for (const Item& item : items)
    {
        facts.RenderedTriangles += static_cast<int64>(cooker.Meshes[item.Mesh].Indices.GetSize() / 3);
    }
    facts.Bytes = static_cast<int64>(result.Bundle.GetSize());
    facts.Camera = &frame;
    facts.Fov = scene.Fov;
    result.License.Clear();
    if (!WriteManifest(facts, result.Manifest) || !result.License.TryAddRange(license.Data()))
    {
        return errors.Memory();
    }
    return true;
}
} // namespace

bool Cook(std::string_view source, CookResult& result, CookError& error, const Cancellation* cancel) noexcept
{
    error = CookError{};
    Failure failure{error};
    Context context(failure, cancel);
    const Status resolved = ResolveDirectory(source, context.Root);
    if (resolved != Status::Ok)
    {
        return failure.Fail(resolved, "cannot resolve source directory: ", source);
    }
    return CookScene(context, result);
}

bool WriteCook(std::string_view output, const CookResult& result, CookError& error) noexcept
{
    error = CookError{};
    Failure failure{error};
    const Status made = MakeDirectories(output);
    if (made != Status::Ok)
    {
        return failure.Fail(made, "cannot create output directory: ", output);
    }
    const struct
    {
        std::string_view Name;
        std::span<const uint8> Data;
    } files[3] = {{"scene.luscn", result.Bundle.AsSpan()},
                  {"manifest.json", result.Manifest.AsSpan()},
                  {"LICENSE.txt", result.License.AsSpan()}};
    for (const auto& file : files)
    {
        const Status written = WriteFileAtomic(output, file.Name, file.Data);
        if (written != Status::Ok)
        {
            return failure.Fail(written, "cannot write ", file.Name);
        }
    }
    return true;
}
} // namespace ludus::content::scene_cook
