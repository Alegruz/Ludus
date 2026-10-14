#include "internal/render_scene.h"

#include <ludus/content/scene_format/luscn.h>
#include <ludus/foundation/containers/borrowed_view.hpp>
#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/math/transform.hpp>
#include <ludus/foundation/math/vector.hpp>
#include <ludus/foundation/parsing/json.hpp>

#include <string_view>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>

#include <QSet>

namespace ludus::editor
{
namespace
{
using namespace foundation;
namespace math = foundation::math;
namespace format = ludus::content::scene_format;
namespace camera = gameplay::camera;
namespace rr = graphics::renderer;
bool Fail(QString& error, const QString& text)
{
    error = text;
    return false;
}
uint64 ComputeSourceId(const QString& id)
{
    const QByteArray hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256);
    uint64 value = 0;
    for (usize i = 0; i < sizeof(value); ++i)
    {
        value |= static_cast<uint64>(static_cast<uint8>(hash[static_cast<int32>(i)])) << (i * 8);
    }
    return value == 0 ? 1 : value;
}
bool WriteChartedMesh(QDataStream& inoutStream,
                      foundation::BorrowedView<const math::Vector3> positions,
                      foundation::BorrowedView<const math::Vector3> normals,
                      foundation::BorrowedView<const math::Vector2> texcoords,
                      foundation::BorrowedView<const uint32> indices,
                      usize& inoutVertexCount,
                      QString& outError)
{
    format::BakeChartLayout layout;
    if (positions.IsValid() == false || normals.IsValid() == false || texcoords.IsValid() == false ||
        indices.IsValid() == false || positions.IsEmpty() == true || indices.GetSize() % 3 != 0 ||
        texcoords.GetSize() != positions.GetSize() ||
        (normals.IsEmpty() == false && normals.GetSize() != positions.GetSize()) ||
        indices.GetSize() > format::MAX_VERTICES - inoutVertexCount ||
        format::TryMakeBakeChartLayout(static_cast<uint32>(indices.GetSize() / 3), layout) == false)
    {
        return Fail(outError, QStringLiteral("World geometry exceeds the separate triangle-chart profile."));
    }
    inoutVertexCount += indices.GetSize();
    inoutStream << static_cast<uint32>(indices.GetSize()) << static_cast<uint32>(indices.GetSize());
    for (usize triangle = 0; triangle < indices.GetSize() / 3; ++triangle)
    {
        for (usize corner = 0; corner < 3; ++corner)
        {
            if (indices[triangle * 3 + corner] >= positions.GetSize())
            {
                return Fail(outError, QStringLiteral("Invalid World mesh index."));
            }
        }
        math::Vector3 faceNormal;
        if (normals.IsEmpty() == true &&
            math::TryNormalize(math::Cross(positions[indices[triangle * 3 + 1]] - positions[indices[triangle * 3]],
                                           positions[indices[triangle * 3 + 2]] - positions[indices[triangle * 3]]),
                               faceNormal) != math::MathStatus::Success)
        {
            return Fail(outError, QStringLiteral("Schema mesh has a degenerate shading normal."));
        }
        for (uint32 corner = 0; corner < 3; ++corner)
        {
            const uint32 index = indices[triangle * 3 + corner];
            const math::Vector3 position = positions[index];
            const math::Vector3 normal = normals.IsEmpty() == true ? faceNormal : normals[index];
            const math::Vector2 uv = texcoords[index];
            float32 u = 0;
            float32 v = 0;
            if (format::TryGetBakeChartUv(layout, static_cast<uint32>(triangle), corner, u, v) == false)
            {
                return Fail(outError, QStringLiteral("Invalid triangle bake chart."));
            }
            inoutStream << position.X << position.Y << position.Z << normal.X << normal.Y << normal.Z << uv.X << uv.Y
                        << u << v;
        }
    }
    for (usize i = 0; i < indices.GetSize(); ++i)
    {
        inoutStream << static_cast<uint32>(i);
    }
    return true;
}
bool Read(const QString& root, const QString& relative, QByteArray& bytes, QString& error, int64 limit = 1048576)
{
    const auto path = QDir(root).absoluteFilePath(relative);
    const QFileInfo info(path);
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) ||
        relative.split(QLatin1Char('/')).contains(QStringLiteral("..")) || relative.contains(QChar(0)) ||
        !info.isFile() || info.isSymLink() || info.canonicalFilePath() != info.absoluteFilePath() ||
        !path.startsWith(root + QLatin1Char('/')))
    {
        return Fail(error, QStringLiteral("Asset must be a regular file inside the project: %1").arg(relative));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > limit)
    {
        return Fail(
            error,
            QStringLiteral("Missing, empty or oversized preview file (%1 bytes maximum): %2").arg(limit).arg(relative));
    }
    bytes = file.read(limit + 1);
    return (file.error() == QFileDevice::NoError && bytes.size() == file.size() && bytes.size() <= limit) ||
           Fail(error, QStringLiteral("Preview file changed or could not be read: %1").arg(relative));
}
bool Decode(const QByteArray& bytes, parsing::JsonDocument& document, QString& error)
{
    parsing::ParseError diagnostic;
    parsing::JsonLimits limits;
    limits.MaxInputBytes = 1048576;
    limits.MaxArrayElements = 12288;
    limits.MaxObjectMembers = 64;
    limits.MaxValues = 65536;
    limits.MaxStringBytes = 1024;
    if (document.Read({bytes.constData(), static_cast<usize>(bytes.size())}, diagnostic, limits) !=
            parsing::ParseStatus::Ok ||
        !document.Root().Object())
    {
        return Fail(error, QStringLiteral("Invalid preview JSON, duplicate keys or document limits exceeded."));
    }
    return true;
}
// Only the Ludus-owned parser/conversion API reads text. Qt values are private
// presentation copies; no new Qt/std text-to-number parser is introduced.
bool CopyJson(parsing::JsonValue source, QJsonValue& output)
{
    if (source.Object())
    {
        QJsonObject object;
        for (usize i = 0; i < source.MemberCount(); ++i)
        {
            std::string_view key;
            parsing::JsonValue value;
            QJsonValue copy;
            if (!source.MemberAt(i, key, value) || !CopyJson(value, copy))
            {
                return false;
            }
            object.insert(QString::fromUtf8(key.data(), static_cast<isize>(key.size())), copy);
        }
        output = object;
        return true;
    }
    if (source.Array())
    {
        QJsonArray array;
        for (usize i = 0; i < source.Count(); ++i)
        {
            QJsonValue copy;
            if (!CopyJson(source.At(i), copy))
            {
                return false;
            }
            array.append(copy);
        }
        output = array;
        return true;
    }
    if (source.Null())
    {
        output = QJsonValue(QJsonValue::Null);
        return true;
    }
    std::string_view string;
    bool boolean = false;
    float64 number = 0;
    if (source.String(string))
    {
        output = QString::fromUtf8(string.data(), static_cast<isize>(string.size()));
        return true;
    }
    if (source.Boolean(boolean))
    {
        output = boolean;
        return true;
    }
    if (source.Number(number) && math::IsFinite(number))
    {
        output = number;
        return true;
    }
    return false;
}
bool Object(const QByteArray& bytes, QJsonObject& output, QString& error)
{
    parsing::JsonDocument document;
    QJsonValue copy;
    if (!Decode(bytes, document, error) || !CopyJson(document.Root(), copy))
    {
        return Fail(error, error.isEmpty() ? QStringLiteral("Preview values could not be copied.") : error);
    }
    output = copy.toObject();
    return true;
}
bool Fields(const QJsonObject& object, const QStringList& fields)
{
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (!fields.contains(it.key()))
        {
            return false;
        }
    }
    return object.size() == fields.size();
}
bool Number(const QJsonValue& value, float32& output)
{
    if (!value.isDouble() || !math::IsFinite(value.toDouble()) || value.toDouble() < -1e6 || value.toDouble() > 1e6)
    {
        return false;
    }
    output = static_cast<float32>(value.toDouble());
    return math::IsFinite(output);
}
bool Mesh(const QByteArray& bytes, PreviewMesh& mesh, QString& error)
{
    parsing::JsonDocument document;
    if (!Decode(bytes, document, error))
    {
        return false;
    }
    const auto object = document.Root();
    const auto positions = object.Get("positions"), uvs = object.Get("uvs"), indices = object.Get("indices");
    uint64 version = 0;
    if (!object.Fields({"version", "positions", "uvs", "indices"}) || object.MemberCount() != 4 ||
        !object.Get("version").Integer(version) || version != 1 || !positions.Array() || positions.Count() == 0 ||
        positions.Count() > 4096 || !uvs.Array() || uvs.Count() != positions.Count() || !indices.Array() ||
        indices.Count() == 0 || indices.Count() > 12288 || indices.Count() % 3 != 0)
    {
        return Fail(error, QStringLiteral("Mesh needs v1, 1–4096 XYZ positions, matching UVs and triangle indices."));
    }
    const auto number = [](parsing::JsonValue source, float32& output) {
        float64 value = 0;
        if (!source.Number(value) || !math::IsFinite(value) || value < -1e6 || value > 1e6)
        {
            return false;
        }
        output = static_cast<float32>(value);
        return math::IsFinite(output);
    };
    for (usize i = 0; i < positions.Count(); ++i)
    {
        const auto xyz = positions.At(i), uv = uvs.At(i);
        math::Vector3 p;
        math::Vector2 t;
        if (!xyz.Array() || xyz.Count() != 3 || !uv.Array() || uv.Count() != 2 || !number(xyz.At(0), p.X) ||
            !number(xyz.At(1), p.Y) || !number(xyz.At(2), p.Z) || !number(uv.At(0), t.X) || !number(uv.At(1), t.Y))
        {
            return Fail(error, QStringLiteral("Mesh positions/UVs must be finite numbers within ±1,000,000."));
        }
        mesh.Positions.append(p);
        mesh.Uvs.append(t);
    }
    for (usize i = 0; i < indices.Count(); ++i)
    {
        uint64 index = 0;
        if (!indices.At(i).Integer(index) || index >= positions.Count())
        {
            return Fail(
                error,
                QStringLiteral("Mesh indices must be nonnegative integer JSON tokens within the vertex array."));
        }
        mesh.Indices.append(static_cast<uint32>(index));
    }
    return true;
}
bool Binding(const WorldDocument& world,
             const QJsonObject& descriptor,
             const QString& role,
             const QStringList& names,
             const QStringList& types,
             QJsonObject& output,
             QString& error)
{
    const auto binding = descriptor.value(role).toObject();
    const auto component = world.Component(binding.value(QStringLiteral("component")).toString());
    const auto properties = binding.value(QStringLiteral("properties")).toObject();
    if (!Fields(binding, {QStringLiteral("component"), QStringLiteral("properties")}) || component.isEmpty() ||
        !Fields(properties, names))
    {
        return Fail(error,
                    QStringLiteral("%1 binding must name a declared component and its semantic properties.").arg(role));
    }
    QSet<QString> used;
    for (int32 i = 0; i < names.size(); ++i)
    {
        const auto key = properties.value(names[i]).toString();
        bool found = false;
        for (const auto value : component.value(QStringLiteral("properties")).toArray())
        {
            const auto p = value.toObject();
            if (p.value(QStringLiteral("id")) == QJsonValue(key) &&
                p.value(QStringLiteral("type")) == QJsonValue(types[i]))
            {
                found = true;
            }
        }
        if (key.isEmpty() || used.contains(key) || !found)
        {
            return Fail(
                error,
                QStringLiteral("%1.%2 must map to a unique property of type %3.").arg(role, names[i], types[i]));
        }
        used.insert(key);
    }
    output = binding;
    return true;
}
QJsonObject Values(const QJsonObject& entity, const QJsonObject& binding)
{
    const auto component = entity.value(QStringLiteral("components"))
                               .toObject()
                               .value(binding.value(QStringLiteral("component")).toString())
                               .toObject();
    QJsonObject values;
    const auto properties = binding.value(QStringLiteral("properties")).toObject();
    for (auto it = properties.begin(); it != properties.end(); ++it)
    {
        values.insert(it.key(), component.value(it.value().toString()));
    }
    return values;
}
bool Attached(const QJsonObject& entity, const QJsonObject& binding)
{
    return entity.value(QStringLiteral("components"))
        .toObject()
        .contains(binding.value(QStringLiteral("component")).toString());
}
bool Transform(const QJsonObject& values, math::TransformTRS& output, QString& error)
{
    math::TransformTRS candidate;
    float32 x = 0, y = 0, z = 0;
    math::Quaternion qx, qy, qz;
    const auto get = [&](const char* key, float32& v) { return Number(values.value(QString::fromLatin1(key)), v); };
    if (!get("x", candidate.Translation.X) || !get("y", candidate.Translation.Y) ||
        !get("z", candidate.Translation.Z) || !get("sx", candidate.Scale.X) || !get("sy", candidate.Scale.Y) ||
        !get("sz", candidate.Scale.Z) || candidate.Scale.X <= 0 || candidate.Scale.Y <= 0 || candidate.Scale.Z <= 0 ||
        !get("rx", x) || !get("ry", y) || !get("rz", z) ||
        math::TryFromAxisAngle({1, 0, 0}, x * 0.01745329252F, qx) != math::MathStatus::Success ||
        math::TryFromAxisAngle({0, 1, 0}, y * 0.01745329252F, qy) != math::MathStatus::Success ||
        math::TryFromAxisAngle({0, 0, 1}, z * 0.01745329252F, qz) != math::MathStatus::Success ||
        math::TryNormalize(qz * qy * qx, candidate.Rotation) != math::MathStatus::Success)
    {
        return Fail(error, QStringLiteral("Preview transform needs finite XYZ/degree rotations and positive scales."));
    }
    output = candidate;
    return true;
}
template <class T>
int32 Find(const QVector<T>& values, const QString& id)
{
    for (int32 i = 0; i < values.size(); ++i)
    {
        if (values[i].Id == id)
        {
            return i;
        }
    }
    return -1;
}
} // namespace
bool RenderScene::Load(const QString& root, const WorldDocument& world, RenderScene& output, QString& error)
{
    error.clear();
    const auto canonical = QFileInfo(root).canonicalFilePath();
    QByteArray bytes;
    QJsonObject descriptor;
    if (!world.Loaded() || canonical.isEmpty() ||
        !Read(canonical, QStringLiteral("authoring/render.preview.json"), bytes, error) ||
        !Object(bytes, descriptor, error))
    {
        return Fail(error,
                    error.isEmpty() ? QStringLiteral("Open a world in a project with authoring/render.preview.json.")
                                    : error);
    }
    QJsonObject requiredDescriptor = descriptor;
    requiredDescriptor.remove(QStringLiteral("light"));
    if (Fields(requiredDescriptor,
               {QStringLiteral("version"),
                QStringLiteral("schema"),
                QStringLiteral("transform"),
                QStringLiteral("renderable"),
                QStringLiteral("camera"),
                QStringLiteral("meshes"),
                QStringLiteral("textures")}) == false ||
        descriptor.value(QStringLiteral("version")) != QJsonValue(1) ||
        descriptor.value(QStringLiteral("schema")) != world.Schema().value(QStringLiteral("id")) ||
        !descriptor.value(QStringLiteral("meshes")).isArray() ||
        !descriptor.value(QStringLiteral("textures")).isArray())
    {
        return Fail(error,
                    QStringLiteral("Preview descriptor must use v1, this world schema and explicit bindings/assets."));
    }
    QJsonObject transform;
    QJsonObject renderable;
    QJsonObject cameraBinding;
    QJsonObject lightBinding;
    const QString number = QStringLiteral("number");
    if (!Binding(world,
                 descriptor,
                 QStringLiteral("transform"),
                 {QStringLiteral("x"),
                  QStringLiteral("y"),
                  QStringLiteral("z"),
                  QStringLiteral("rx"),
                  QStringLiteral("ry"),
                  QStringLiteral("rz"),
                  QStringLiteral("sx"),
                  QStringLiteral("sy"),
                  QStringLiteral("sz")},
                 {number, number, number, number, number, number, number, number, number},
                 transform,
                 error) ||
        !Binding(world,
                 descriptor,
                 QStringLiteral("renderable"),
                 {QStringLiteral("mesh"),
                  QStringLiteral("texture"),
                  QStringLiteral("visible"),
                  QStringLiteral("r"),
                  QStringLiteral("g"),
                  QStringLiteral("b")},
                 {QStringLiteral("asset"), QStringLiteral("asset"), QStringLiteral("boolean"), number, number, number},
                 renderable,
                 error) ||
        (!descriptor.value(QStringLiteral("camera")).isNull() &&
         !Binding(world,
                  descriptor,
                  QStringLiteral("camera"),
                  {QStringLiteral("projection"),
                   QStringLiteral("fov"),
                   QStringLiteral("span"),
                   QStringLiteral("near"),
                   QStringLiteral("far"),
                   QStringLiteral("follow")},
                  {QStringLiteral("enum"), number, number, number, number, QStringLiteral("entity")},
                  cameraBinding,
                  error)))
    {
        return false;
    }
    if (descriptor.contains(QStringLiteral("light")) == true &&
        descriptor.value(QStringLiteral("light")).isNull() == false &&
        Binding(
            world,
            descriptor,
            QStringLiteral("light"),
            {QStringLiteral("kind"),
             QStringLiteral("enabled"),
             QStringLiteral("r"),
             QStringLiteral("g"),
             QStringLiteral("b"),
             QStringLiteral("intensity"),
             QStringLiteral("range"),
             QStringLiteral("innerAngle"),
             QStringLiteral("outerAngle")},
            {QStringLiteral("enum"), QStringLiteral("boolean"), number, number, number, number, number, number, number},
            lightBinding,
            error) == false)
    {
        return false;
    }
    RenderScene candidate;
    candidate.RenderableBinding = renderable;
    QByteArray identity = world.Encode() + bytes;
    QByteArray assetsIdentity;
    const auto meshes = descriptor.value(QStringLiteral("meshes")).toArray();
    const auto textures = descriptor.value(QStringLiteral("textures")).toArray();
    if (meshes.isEmpty() || meshes.size() > 6 || textures.size() > 8)
    {
        return Fail(error, QStringLiteral("Preview supports 1–6 meshes and 0–8 PNG textures."));
    }
    for (const bool texture : {false, true})
    {
        QSet<QString> seen;
        for (const auto value : texture ? textures : meshes)
        {
            const auto entry = value.toObject();
            const auto id = entry.value(QStringLiteral("id")).toString();
            bool declared = false;
            for (const auto asset : world.Schema().value(QStringLiteral("assets")).toArray())
            {
                const auto a = asset.toObject();
                declared = declared || (a.value(QStringLiteral("id")) == QJsonValue(id) &&
                                        a.value(QStringLiteral("kind")) ==
                                            QJsonValue(texture ? QStringLiteral("texture") : QStringLiteral("mesh")));
            }
            if (!Fields(entry, {QStringLiteral("id"), QStringLiteral("path")}) || !declared || seen.contains(id) ||
                !Read(canonical,
                      entry.value(QStringLiteral("path")).toString(),
                      bytes,
                      error,
                      texture ? 4194304 : 1048576))
            {
                return Fail(
                    error,
                    QStringLiteral("%1: invalid/duplicate asset or unreadable project file. %2").arg(id, error));
            }
            seen.insert(id);
            assetsIdentity += texture ? 'T' : 'M';
            assetsIdentity += id.toUtf8() + '\0' + QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
            if (texture)
            {
                QBuffer buffer(&bytes);
                (void)buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer, QByteArrayLiteral("png"));
                const auto size = reader.size();
                if (!size.isValid() || size.width() > 2048 || size.height() > 2048)
                {
                    return Fail(error, QStringLiteral("%1: PNG preview must be 1–2048 pixels per axis.").arg(id));
                }
                auto image = reader.read().convertToFormat(QImage::Format_RGBA8888);
                if (image.isNull() || image.size() != size)
                {
                    return Fail(error, QStringLiteral("%1: PNG could not be decoded.").arg(id));
                }
                candidate.Textures.append({id, image});
            }
            else
            {
                PreviewMesh mesh;
                mesh.Id = id;
                if (!Mesh(bytes, mesh, error))
                {
                    error = id + QStringLiteral(": ") + error;
                    return false;
                }
                candidate.Meshes.append(mesh);
            }
        }
    }
    bool bounds = false;
    bool hasAuthoredLighting = false;
    math::Vector3 low{}, high{};
    for (const auto value : world.Draft().value(QStringLiteral("entities")).toArray())
    {
        const auto entity = value.toObject();
        if (entity.contains(QStringLiteral("scene")))
        {
            continue;
        }
        if (Attached(entity, renderable) == false && Attached(entity, cameraBinding) == false &&
            Attached(entity, lightBinding) == false)
        {
            continue;
        }
        const auto id = entity.value(QStringLiteral("id")).toString();
        const auto name = entity.value(QStringLiteral("name")).toString();
        math::TransformTRS trs;
        if (!Attached(entity, transform) || !Transform(Values(entity, transform), trs, error))
        {
            return Fail(error, name + QStringLiteral(": missing or invalid transform. ") + error);
        }
        if (Attached(entity, lightBinding) == true)
        {
            hasAuthoredLighting = true;
            const QJsonObject values = Values(entity, lightBinding);
            if (values.value(QStringLiteral("enabled")).toBool() == true)
            {
                rr::DirectLight light;
                const QString kind = values.value(QStringLiteral("kind")).toString();
                light.Kind = kind == QStringLiteral("directional") ? rr::LightKind::Directional
                             : kind == QStringLiteral("point")     ? rr::LightKind::Point
                                                                   : rr::LightKind::Spot;
                float32 innerDegrees = 0;
                float32 outerDegrees = 0;
                if ((kind != QStringLiteral("directional") && kind != QStringLiteral("point") &&
                     kind != QStringLiteral("spot")) ||
                    Number(values.value(QStringLiteral("r")), light.Color.X) == false ||
                    Number(values.value(QStringLiteral("g")), light.Color.Y) == false ||
                    Number(values.value(QStringLiteral("b")), light.Color.Z) == false ||
                    Number(values.value(QStringLiteral("intensity")), light.Intensity) == false ||
                    Number(values.value(QStringLiteral("range")), light.Range) == false ||
                    Number(values.value(QStringLiteral("innerAngle")), innerDegrees) == false ||
                    Number(values.value(QStringLiteral("outerAngle")), outerDegrees) == false || innerDegrees < 0 ||
                    innerDegrees >= outerDegrees || outerDegrees >= 90 || trs.Scale != math::Vector3{1, 1, 1})
                {
                    return Fail(error, name + QStringLiteral(": invalid light terms, cone or non-unit scale."));
                }
                light.Position = trs.Translation;
                light.Direction = math::Rotate(trs.Rotation, {0, 0, -1});
                light.InnerCos = math::Cos(math::DegreesToRadians(innerDegrees));
                light.OuterCos = math::Cos(math::DegreesToRadians(outerDegrees));
                light.SourceId = ComputeSourceId(id);
                if (candidate.AuthoredLights.Count >= sizeof(candidate.AuthoredLights.Lights) / sizeof(rr::DirectLight))
                {
                    return Fail(error, QStringLiteral("World exceeds the authored-light capacity."));
                }
                candidate.AuthoredLights.Lights[candidate.AuthoredLights.Count++] = light;
                assetsIdentity += QJsonDocument(QJsonObject{{QStringLiteral("id"), id},
                                                            {QStringLiteral("light"), values},
                                                            {QStringLiteral("transform"), Values(entity, transform)}})
                                      .toJson(QJsonDocument::Compact);
            }
        }
        if (Attached(entity, renderable))
        {
            const auto values = Values(entity, renderable);
            if (values.value(QStringLiteral("visible")).toBool())
            {
                PreviewObject object;
                object.Id = id;
                object.Name = name;
                object.Mesh = Find(candidate.Meshes, values.value(QStringLiteral("mesh")).toString());
                const auto textureId = values.value(QStringLiteral("texture")).toString();
                object.Texture = textureId.isEmpty() ? -1 : Find(candidate.Textures, textureId);
                if (object.Mesh < 0 || (!textureId.isEmpty() && object.Texture < 0) ||
                    !Number(values.value(QStringLiteral("r")), object.Tint.X) ||
                    !Number(values.value(QStringLiteral("g")), object.Tint.Y) ||
                    !Number(values.value(QStringLiteral("b")), object.Tint.Z) || object.Tint.X < 0 ||
                    object.Tint.X > 1 || object.Tint.Y < 0 || object.Tint.Y > 1 || object.Tint.Z < 0 ||
                    object.Tint.Z > 1 || math::TryToAffine3(trs, 1e-4F, object.Transform) != math::MathStatus::Success)
                {
                    return Fail(error, name + QStringLiteral(": choose mapped mesh/texture, RGB in [0,1]."));
                }
                if (object.Texture >= 0)
                {
                    for (int32 i = 0; i < candidate.Surfaces.size(); ++i)
                    {
                        if (candidate.Surfaces[i].Texture == object.Texture &&
                            candidate.Surfaces[i].Tint == object.Tint)
                        {
                            object.Surface = i;
                            break;
                        }
                    }
                    if (object.Surface < 0)
                    {
                        if (candidate.Surfaces.size() == 8)
                        {
                            return Fail(error,
                                        QStringLiteral("Preview supports eight distinct texture/tint combinations; "
                                                       "identical surfaces are shared."));
                        }
                        object.Surface = static_cast<int32>(candidate.Surfaces.size());
                        candidate.Surfaces.append({object.Texture, object.Tint});
                    }
                }
                for (const auto vertex : candidate.Meshes[object.Mesh].Positions)
                {
                    const auto p = math::TransformPoint(object.Transform, vertex);
                    if (!math::IsFinite(p) || math::Abs(p.X) > 1e6F || math::Abs(p.Y) > 1e6F || math::Abs(p.Z) > 1e6F)
                    {
                        return Fail(error, name + QStringLiteral(": transformed geometry exceeds preview bounds."));
                    }
                    if (!bounds)
                    {
                        low = high = p;
                        bounds = true;
                    }
                    low = {math::Min(low.X, p.X), math::Min(low.Y, p.Y), math::Min(low.Z, p.Z)};
                    high = {math::Max(high.X, p.X), math::Max(high.Y, p.Y), math::Max(high.Z, p.Z)};
                }
                candidate.Objects.append(object);
            }
        }
        if (Attached(entity, cameraBinding))
        {
            const auto values = Values(entity, cameraBinding);
            if (trs.Scale != math::Vector3{1, 1, 1})
            {
                return Fail(error, name + QStringLiteral(": cameras require unit scale (a rigid pose)."));
            }
            camera::CameraRigDefinition definition;
            const auto authoredPose = Values(entity, transform);
            definition.Pose.Position = {authoredPose.value(QStringLiteral("x")).toDouble(),
                                        authoredPose.value(QStringLiteral("y")).toDouble(),
                                        authoredPose.value(QStringLiteral("z")).toDouble()};
            definition.Pose.Orientation = trs.Rotation;
            const auto projection = values.value(QStringLiteral("projection")).toString();
            definition.Lens.Projection =
                projection == QStringLiteral("perspective") ? camera::CameraProjection::PerspectiveFinite
                : projection == QStringLiteral("infinite")  ? camera::CameraProjection::PerspectiveInfinite
                                                            : camera::CameraProjection::OrthographicFinite;
            if ((projection != QStringLiteral("perspective") && projection != QStringLiteral("infinite") &&
                 projection != QStringLiteral("orthographic")) ||
                !Number(values.value(QStringLiteral("fov")), definition.Lens.VerticalFov) ||
                !Number(values.value(QStringLiteral("span")), definition.Lens.VerticalSpan) ||
                !Number(values.value(QStringLiteral("near")), definition.Lens.NearPlane) ||
                !Number(values.value(QStringLiteral("far")), definition.Lens.FarPlane))
            {
                return Fail(error, name + QStringLiteral(": invalid camera lens."));
            }
            definition.Lens.VerticalFov *= 0.01745329252F;
            camera::CameraTargetSample target;
            const auto follow = values.value(QStringLiteral("follow")).toString();
            if (!follow.isEmpty())
            {
                const auto e = world.Entity(follow);
                math::TransformTRS targetTransform;
                if (!Attached(e, transform) || !Transform(Values(e, transform), targetTransform, error))
                {
                    return Fail(error, name + QStringLiteral(": follow target needs a valid transform."));
                }
                definition.Recipe = camera::CameraRecipe::OrthographicFollow;
                const auto targetValues = Values(e, transform);
                target.Position = {targetValues.value(QStringLiteral("x")).toDouble(),
                                   targetValues.value(QStringLiteral("y")).toDouble(),
                                   targetValues.value(QStringLiteral("z")).toDouble()};
            }
            PreviewCamera rig;
            rig.Id = id;
            rig.Name = name;
            rig.FollowId = follow;
            if (camera::TryEvaluateCamera(definition,
                                          {},
                                          follow.isEmpty() ? nullptr : &target,
                                          rig.Sample,
                                          &rig.Trace) != camera::CameraStatus::Success)
            {
                return Fail(error, name + QStringLiteral(": lens/follow is unsupported by the C0 camera contract."));
            }
            candidate.Cameras.append(rig);
        }
    }
    rr::LightSelection normalized;
    if (rr::NormalizeDirectLights(candidate.AuthoredLights.Lights, candidate.AuthoredLights.Count, normalized) !=
        graphics::rhi::RasterStatus::Ready)
    {
        return Fail(error, QStringLiteral("Invalid light color/intensity/range or conflicting source identity."));
    }
    candidate.AuthoredLights = normalized;
    candidate.Center = (low + high) * 0.5F;
    candidate.Radius = bounds ? math::Max(0.001F, math::Length(high - low) * 0.5F) : 1.0F;
    candidate.AssetsIdentity = QCryptographicHash::hash(assetsIdentity, QCryptographicHash::Sha256);
    candidate.Identity = QCryptographicHash::hash(identity + candidate.AssetsIdentity, QCryptographicHash::Sha256);
    if (hasAuthoredLighting == true && candidate.Objects.isEmpty() == false)
    {
        const math::Vector3 eye = candidate.Center + math::Vector3{0, 0, candidate.Radius * 3};
        const QJsonObject camera{
            {QStringLiteral("eye"),
             QJsonArray{static_cast<float64>(eye.X), static_cast<float64>(eye.Y), static_cast<float64>(eye.Z)}},
            {QStringLiteral("target"),
             QJsonArray{static_cast<float64>(candidate.Center.X),
                        static_cast<float64>(candidate.Center.Y),
                        static_cast<float64>(candidate.Center.Z)}},
            {QStringLiteral("up"), QJsonArray{0, 1, 0}},
            {QStringLiteral("fov"), 1.0}};
        const QJsonObject level{{QStringLiteral("id"), QStringLiteral("schema-preview")},
                                {QStringLiteral("objects"), QJsonArray{}},
                                {QStringLiteral("camera"), camera}};
        return Merge(candidate, {}, level, output, error, {});
    }
    output = candidate;
    error.clear();
    return true;
}
bool RenderScene::View(const PreviewViewOptions& options, rr::ViewDescription& output, QString& error) const
{
    const auto selected = options.Camera;
    const auto yaw = options.OrbitDegrees.X, pitch = options.OrbitDegrees.Y;
    const auto distance = options.Distance;
    const auto width = options.Width, height = options.Height;
    const auto reference = options.Reference;
    const auto* orbitCenter = options.OrbitCenter;
    const auto orbitRadius = options.OrbitRadius;
    rr::ViewMapping mapping;
    rr::ProjectionDescription projection;
    math::Vector3 position, forward;
    if (selected < -1 || selected >= Cameras.size() ||
        rr::ResolveViewMapping({static_cast<float32>(width), static_cast<float32>(height), rr::ScreenFit::Stretch},
                               {0, 0, width, height},
                               width,
                               height,
                               mapping) != graphics::rhi::RasterStatus::Ready)
    {
        return Fail(error, QStringLiteral("Camera selection or drawable dimensions are unavailable."));
    }
    if (selected == -1)
    {
        const auto center = orbitCenter == nullptr ? Center : *orbitCenter;
        const auto radius = orbitCenter == nullptr ? Radius : orbitRadius;
        if (!math::IsFinite(yaw) || !math::IsFinite(pitch) || !math::IsFinite(distance) || distance < radius * 1.2F ||
            distance > radius * 20 || pitch < -80 || pitch > 80)
        {
            return Fail(error, QStringLiteral("Editor orbit is outside its safe navigation range."));
        }
        const auto y = yaw * 0.01745329252F, p = pitch * 0.01745329252F;
        position =
            center + math::Vector3{math::Sin(y) * math::Cos(p), math::Sin(p), math::Cos(y) * math::Cos(p)} * distance;
        if (math::TryLookAt(position, center, {0, 1, 0}, projection.WorldToView) != math::MathStatus::Success ||
            math::TryNormalize(center - position, forward) != math::MathStatus::Success)
        {
            return Fail(error, QStringLiteral("Editor orbit cannot produce a valid view."));
        }
        projection.Kind = rr::ProjectionKind::PerspectiveInfinite;
        projection.Near = math::Max(0.000001F, radius * 0.001F);
    }
    else
    {
        const auto& sample = Cameras[selected].Sample;
        math::TransformTRS pose;
        pose.Translation = {static_cast<float32>(sample.Pose.Position.X),
                            static_cast<float32>(sample.Pose.Position.Y),
                            static_cast<float32>(sample.Pose.Position.Z)};
        pose.Rotation = sample.Pose.Orientation;
        math::Affine3 inverse;
        if (math::TryInverse(math::ToAffine3(pose), inverse) != math::MathStatus::Success)
        {
            return Fail(error, QStringLiteral("Authored camera pose cannot be inverted."));
        }
        projection.WorldToView = math::ToMatrix4(inverse);
        projection.Kind = sample.Lens.Projection == camera::CameraProjection::PerspectiveFinite
                              ? rr::ProjectionKind::Perspective
                          : sample.Lens.Projection == camera::CameraProjection::PerspectiveInfinite
                              ? rr::ProjectionKind::PerspectiveInfinite
                              : rr::ProjectionKind::Orthographic;
        projection.VerticalFovRadians = sample.Lens.VerticalFov;
        projection.OrthographicHeight = sample.Lens.VerticalSpan;
        projection.Near = sample.Lens.NearPlane;
        projection.Far = sample.Lens.FarPlane;
        position = pose.Translation;
        forward = math::Rotate(pose.Rotation, {0, 0, -1});
    }
    rr::ViewDescription view;
    if (rr::BuildViewDescription(projection, mapping, view) != graphics::rhi::RasterStatus::Ready)
    {
        return Fail(error, QStringLiteral("Camera lens cannot be represented by the renderer."));
    }
    view.CameraPosition = position;
    view.CameraForward = forward;
    view.OrthographicCamera = projection.Kind == rr::ProjectionKind::Orthographic;
    view.Reference = reference;
    output = view;
    error.clear();
    return true;
}
bool RenderScene::Merge(const RenderScene& authored,
                        const QByteArray& cooked,
                        const QJsonObject& level,
                        RenderScene& output,
                        QString& error,
                        const QJsonObject& revisions)
{
    if (authored.Objects.isEmpty() == true && authored.AuthoredLights.Count == 0)
    {
        if (!cooked.isEmpty())
        {
            return LoadCooked(cooked, level, output, error, revisions);
        }
        output = {};
        error.clear();
        return true;
    }
    resources::SceneDefinition imported;
    if (!cooked.isEmpty() && resources::SceneDefinition::Decode(reinterpret_cast<const uint8*>(cooked.constData()),
                                                                static_cast<usize>(cooked.size()),
                                                                imported) != resources::Status::Ok)
    {
        return Fail(error, QStringLiteral("Invalid imported scene during World composition."));
    }
    const usize meshCount = imported.Meshes.GetSize() +
                            (authored.Objects.isEmpty() == true ? 0 : static_cast<usize>(authored.Meshes.size()));
    const usize materialCount = imported.Materials.GetSize() + static_cast<usize>(authored.Objects.size());
    const usize textureCount = imported.Textures.GetSize() + static_cast<usize>(authored.Objects.size());
    const usize objectCount = imported.Objects.GetSize() + static_cast<usize>(authored.Objects.size());
    if (meshCount > format::MAX_MESHES || materialCount > format::MAX_MATERIALS ||
        textureCount > format::MAX_TEXTURES || objectCount > format::MAX_OBJECTS)
    {
        return Fail(
            error,
            QStringLiteral("Combined World exceeds six meshes, eight materials/textures or 128 visible instances."));
    }
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream.writeRawData(format::MAGIC.data(), static_cast<int32>(format::MAGIC.size()));
    stream << format::VERSION_3 << static_cast<uint32>(meshCount) << static_cast<uint32>(materialCount)
           << static_cast<uint32>(textureCount) << static_cast<uint32>(objectCount);
    const auto camera = level.value(QStringLiteral("camera")).toObject();
    for (const auto& key : {QStringLiteral("eye"), QStringLiteral("target"), QStringLiteral("up")})
    {
        for (const auto n : camera.value(key).toArray())
        {
            stream << static_cast<float32>(n.toDouble());
        }
    }
    stream << static_cast<float32>(camera.value(QStringLiteral("fov")).toDouble());
    for (const auto& texture : imported.Textures)
    {
        stream << texture.Width << texture.Height << static_cast<uint32>(texture.Pixels.GetSize());
        stream.writeRawData(reinterpret_cast<const char*>(texture.Pixels.GetData()),
                            static_cast<int32>(texture.Pixels.GetSize()));
    }
    // The reference profile has no base-color multiplier. Bake the validated
    // schema tint into transient sRGB pixels; durable source assets stay intact.
    for (const auto& object : authored.Objects)
    {
        QImage image = object.Texture < 0 ? QImage(1, 1, QImage::Format_RGBA8888)
                                          : authored.Textures[object.Texture].Pixels.copy();
        if (image.isNull())
        {
            return Fail(error, QStringLiteral("World texture allocation failed."));
        }
        if (object.Texture < 0)
        {
            image.fill(Qt::white);
        }
        for (int32 y = 0; y < image.height(); ++y)
        {
            auto* pixels = image.scanLine(y);
            for (int32 x = 0; x < image.width(); ++x)
            {
                const float32 tint[3]{object.Tint.X, object.Tint.Y, object.Tint.Z};
                for (int32 channel = 0; channel < 3; ++channel)
                {
                    const auto linear = math::DecodeSrgb(static_cast<float32>(pixels[x * 4 + channel]) / 255.0F);
                    pixels[x * 4 + channel] = static_cast<uint8>(
                        math::Floor(static_cast<float64>(math::EncodeSrgb(linear * tint[channel])) * 255.0 + .5));
                }
            }
        }
        stream << static_cast<uint32>(image.width()) << static_cast<uint32>(image.height())
               << static_cast<uint32>(image.width() * image.height() * 4);
        const int32 rowBytes = image.width() * 4; // Preview textures are bounded to 2048 pixels per axis.
        for (int32 y = 0; y < image.height(); ++y)
        {
            stream.writeRawData(reinterpret_cast<const char*>(image.constScanLine(y)), rowBytes);
        }
    }
    for (const resources::SceneMaterial& material : imported.Materials)
    {
        stream << material.Texture << uint32{material.Repeat ? 1U : 0U} << material.Metallic << material.Roughness
               << material.Emission.X << material.Emission.Y << material.Emission.Z;
        const uint32 flags = (material.IsEmissionTwoSided == true ? format::EMISSION_TWO_SIDED : 0U) |
                             (material.HasEmissionPower == true ? format::EMISSION_HAS_POWER : 0U);
        stream << flags << material.AreaSourceId << material.EmissionPower << uint32{0};
    }
    for (int32 i = 0; i < authored.Objects.size(); ++i)
    {
        stream << static_cast<uint32>(imported.Textures.GetSize() + static_cast<usize>(i)) << uint32{0} << float32{0}
               << float32{.5F} << float32{0} << float32{0} << float32{0} << uint32{0} << uint64{0} << float32{0}
               << uint32{0};
    }
    usize expandedVertices = 0;
    for (const resources::SceneMesh& mesh : imported.Meshes)
    {
        if (WriteChartedMesh(stream,
                             {mesh.Positions.GetData(), mesh.Positions.GetSize()},
                             {mesh.Normals.GetData(), mesh.Normals.GetSize()},
                             {mesh.Texcoords.GetData(), mesh.Texcoords.GetSize()},
                             {mesh.Indices.GetData(), mesh.Indices.GetSize()},
                             expandedVertices,
                             error) == false)
        {
            return false;
        }
    }
    if (authored.Objects.isEmpty() == false)
    {
        for (const PreviewMesh& mesh : authored.Meshes)
        {
            if (WriteChartedMesh(stream,
                                 {mesh.Positions.constData(), static_cast<usize>(mesh.Positions.size())},
                                 {mesh.Normals.constData(), static_cast<usize>(mesh.Normals.size())},
                                 {mesh.Uvs.constData(), static_cast<usize>(mesh.Uvs.size())},
                                 {mesh.Indices.constData(), static_cast<usize>(mesh.Indices.size())},
                                 expandedVertices,
                                 error) == false)
            {
                return false;
            }
        }
    }
    for (const resources::SceneObject& object : imported.Objects)
    {
        stream << object.Mesh << object.Material;
        for (const auto column : object.Transform.Columns)
        {
            stream << column.X << column.Y << column.Z;
        }
        stream << object.Transform.Translation.X << object.Transform.Translation.Y << object.Transform.Translation.Z
               << object.SourceId;
    }
    QJsonObject mergedLevel = level;
    QJsonArray objects = level.value(QStringLiteral("objects")).toArray();
    for (int32 i = 0; i < authored.Objects.size(); ++i)
    {
        const PreviewObject& object = authored.Objects[i];
        stream << static_cast<uint32>(imported.Meshes.GetSize() + static_cast<usize>(object.Mesh))
               << static_cast<uint32>(imported.Materials.GetSize() + static_cast<usize>(i));
        for (const math::Vector3 column : object.Transform.Columns)
        {
            stream << column.X << column.Y << column.Z;
        }
        stream << object.Transform.Translation.X << object.Transform.Translation.Y << object.Transform.Translation.Z
               << ComputeSourceId(object.Id);
        objects.append(
            QJsonObject{{QStringLiteral("id"), object.Id},
                        {QStringLiteral("name"), object.Name},
                        {QStringLiteral("mesh"), QStringLiteral("preview/") + authored.Meshes[object.Mesh].Id},
                        {QStringLiteral("material"), QStringLiteral("preview/") + object.Id}});
    }
    const usize lightCount = imported.Lights.GetSize() + authored.AuthoredLights.Count;
    if (lightCount > format::MAX_LIGHTS)
    {
        return Fail(error, QStringLiteral("Combined authored lights exceed the scene capacity."));
    }
    stream << format::LIGHTING_PROFILE_REC709_PHOTOMETRIC << static_cast<uint32>(lightCount)
           << imported.Environment.Width << imported.Environment.Height << imported.Environment.Rgb.X
           << imported.Environment.Rgb.Y << imported.Environment.Rgb.Z;
    for (const resources::SceneLight& light : imported.Lights)
    {
        stream << light.SourceId << static_cast<uint32>(light.Kind)
               << uint32{light.IsEnabled == true ? format::LIGHT_ENABLED : 0} << light.IntensityRgb.X
               << light.IntensityRgb.Y << light.IntensityRgb.Z << light.PositionWorld.X << light.PositionWorld.Y
               << light.PositionWorld.Z << light.DirectionWorld.X << light.DirectionWorld.Y << light.DirectionWorld.Z
               << light.Range << light.InnerCos << light.OuterCos;
    }
    for (usize i = 0; i < authored.AuthoredLights.Count; ++i)
    {
        const rr::DirectLight& light = authored.AuthoredLights.Lights[i];
        const math::Vector3 rgb = light.Color * light.Intensity;
        stream << light.SourceId << static_cast<uint32>(light.Kind) << format::LIGHT_ENABLED << rgb.X << rgb.Y << rgb.Z
               << light.Position.X << light.Position.Y << light.Position.Z << light.Direction.X << light.Direction.Y
               << light.Direction.Z << light.Range << light.InnerCos << light.OuterCos;
    }
    for (const math::Vector3 pixel : imported.Environment.Pixels)
    {
        stream << pixel.X << pixel.Y << pixel.Z;
    }
    mergedLevel.insert(QStringLiteral("objects"), objects);
    if (stream.status() != QDataStream::Ok || bytes.size() > 67108864)
    {
        return Fail(error, QStringLiteral("Combined World exceeds its cooked byte budget."));
    }
    RenderScene candidate;
    if (LoadCooked(bytes, mergedLevel, candidate, error, revisions) == false)
    {
        return false;
    }
    candidate.AuthoredLights = authored.AuthoredLights;
    candidate.RenderableBinding = authored.RenderableBinding;
    if (authored.Cameras.isEmpty() == false)
    {
        candidate.Cameras = authored.Cameras;
    }
    candidate.AssetsIdentity =
        QCryptographicHash::hash(candidate.AssetsIdentity + authored.AssetsIdentity, QCryptographicHash::Sha256);
    candidate.Identity = QCryptographicHash::hash(candidate.Identity + authored.Identity, QCryptographicHash::Sha256);
    output = candidate;
    return true;
}
bool RenderScene::LoadCooked(const QByteArray& bytes,
                             const QJsonObject& level,
                             RenderScene& output,
                             QString& error,
                             const QJsonObject& revisions)
{
    RenderScene candidate;
    candidate.Definition = QSharedPointer<resources::SceneDefinition>::create();
    if (resources::SceneDefinition::Decode(reinterpret_cast<const uint8*>(bytes.constData()),
                                           static_cast<usize>(bytes.size()),
                                           *candidate.Definition) != resources::Status::Ok)
    {
        return Fail(error,
                    QStringLiteral("Cooked scene failed bounded runtime validation. Previous viewport retained."));
    }
    const auto& scene = *candidate.Definition;
    candidate.Lit = true;
    candidate.LogicalId = QStringLiteral("level/") + level.value(QStringLiteral("id")).toString();
    for (const auto& source : scene.Meshes)
    {
        PreviewMesh mesh;
        (void)source; // Geometry stays in the immutable leased definition.
        candidate.Meshes.append(mesh);
    }
    for (const auto& source : scene.Textures)
    {
        PreviewTexture texture;
        texture.Pixels = QImage(source.Pixels.GetData(),
                                static_cast<int32>(source.Width),
                                static_cast<int32>(source.Height),
                                QImage::Format_RGBA8888);
        if (texture.Pixels.isNull())
        {
            return Fail(error, QStringLiteral("Texture allocation failed."));
        }
        candidate.Textures.append(texture);
    }
    for (const auto& source : scene.Materials)
    {
        PreviewSurface surface;
        surface.Texture = source.Texture == ~uint32{0} ? -1 : static_cast<int32>(source.Texture);
        surface.Tint = surface.Texture < 0 ? math::Vector4{0, 0, 0, 1} : math::Vector4{1, 1, 1, 1};
        surface.Repeat = source.Repeat;
        surface.Metallic = source.Metallic;
        surface.Roughness = source.Roughness;
        surface.Emission = source.Emission;
        candidate.Surfaces.append(surface);
    }
    const auto objects = level.value(QStringLiteral("objects")).toArray();
    if (objects.size() != static_cast<qsizetype>(scene.Objects.GetSize()))
    {
        return Fail(error, QStringLiteral("Level object count mismatch."));
    }
    for (usize i = 0; i < scene.Objects.GetSize(); ++i)
    {
        const auto& source = scene.Objects[i];
        const auto persistent = objects[static_cast<qsizetype>(i)].toObject();
        PreviewObject object;
        object.Id = persistent.value(QStringLiteral("id")).toString();
        object.Name = persistent.value(QStringLiteral("name")).toString();
        object.Mesh = static_cast<int32>(source.Mesh);
        object.Surface = static_cast<int32>(source.Material);
        object.Texture = candidate.Surfaces[object.Surface].Texture;
        object.Transform = source.Transform;
        candidate.Objects.append(object);
    }
    candidate.PacketIdentity = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    QJsonArray bindings;
    QStringList resourceIds;
    for (const auto& value : objects)
    {
        const auto object = value.toObject();
        const auto mesh = object.value(QStringLiteral("mesh")).toString();
        const auto material = object.value(QStringLiteral("material")).toString();
        bindings.append(QJsonObject{{QStringLiteral("id"), object.value(QStringLiteral("id"))},
                                    {QStringLiteral("mesh"), mesh},
                                    {QStringLiteral("material"), material}});
        if (!resourceIds.contains(mesh))
        {
            resourceIds.append(mesh);
        }
        if (!resourceIds.contains(material))
        {
            resourceIds.append(material);
        }
    }
    resourceIds.sort();
    const auto identities = QJsonDocument(QJsonArray::fromStringList(resourceIds)).toJson(QJsonDocument::Compact);
    QCryptographicHash assets(QCryptographicHash::Sha256);
    assets.addData(identities);
    assets.addData(QJsonDocument(revisions).toJson(QJsonDocument::Compact));
    candidate.AssetsIdentity = assets.result();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(candidate.PacketIdentity);
    hash.addData(candidate.AssetsIdentity);
    hash.addData(QJsonDocument(QJsonObject{{QStringLiteral("level"), level.value(QStringLiteral("id"))},
                                           {QStringLiteral("bindings"), bindings},
                                           {QStringLiteral("resources"), QJsonArray::fromStringList(resourceIds)}})
                     .toJson(QJsonDocument::Compact));
    candidate.Identity = hash.result();
    output = candidate;
    error.clear();
    return true;
}
} // namespace ludus::editor
