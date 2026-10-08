#include "internal/scene_store.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>

namespace ludus::editor
{
namespace
{
constexpr foundation::int64 MAX_SCENE = 65536;
bool Regular(const QString& path)
{
    const QFileInfo info(path);
    return info.isFile() && !info.isSymLink() && info.canonicalFilePath() == info.absoluteFilePath();
}
bool Replace(const QString& path, const QByteArray& bytes)
{
    if (QFileInfo(path).isSymLink())
    {
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QByteArray Header(const QByteArray& baseline)
{
    return QByteArrayLiteral("LUDUS-SCENE-RECOVERY-1\n") +
           QCryptographicHash::hash(baseline, QCryptographicHash::Sha256).toHex() + '\n';
}
} // namespace
bool ReadScene(const QString& path, QByteArray& output)
{
    if (!Regular(path))
    {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MAX_SCENE || file.size() <= 0)
    {
        return false;
    }
    const auto bytes = file.read(MAX_SCENE + 1);
    if (bytes.size() > MAX_SCENE || file.error() != QFileDevice::NoError || bytes.size() != file.size())
    {
        return false;
    }
    output = bytes;
    return true;
}
bool EncodeScene(const SceneSnapshot& snapshot, QByteArray& output)
{
    foundation::core::Array<char> bytes;
    if (!world_demo::WriteLevel(snapshot.Level, bytes) || bytes.GetSize() > static_cast<foundation::usize>(MAX_SCENE))
    {
        return false;
    }
    output = QByteArray(bytes.GetData(), static_cast<foundation::int64>(bytes.GetSize()));
    return true;
}
bool SaveScene(const QString& path, const QByteArray& baseline, SceneDocument& document, QByteArray& saved)
{
    if (!document.Loaded() || document.Previewing() || !Regular(path))
    {
        return false;
    }
    const auto snapshot = document.Draft();
    const auto stamp = document.Stamp(0, 0);
    QByteArray bytes;
    if (!EncodeScene(snapshot, bytes))
    {
        return false;
    }
    const auto lockPath = path + QStringLiteral(".ludus-editor-lock");
    if (QFileInfo(lockPath).isSymLink())
    {
        return false;
    }
    QLockFile lock(lockPath);
    if (!lock.tryLock(0))
    {
        return false;
    }
    QByteArray disk;
    if (!ReadScene(path, disk) || disk != baseline || !Replace(path, bytes))
    {
        return false;
    }
    if (!document.AcknowledgeSave(snapshot, stamp.Document))
    {
        return false;
    }
    saved = bytes;
    if (!document.Dirty())
    {
        (void)QFile::remove(path + QStringLiteral(".ludus-recovery"));
    }
    return true;
}
bool WriteSceneRecovery(const QString& path, const QByteArray& baseline, const SceneDocument& document)
{
    if (!document.Loaded())
    {
        return false;
    }
    QByteArray bytes;
    if (!EncodeScene(document.Draft(), bytes))
    {
        return false;
    }
    const auto lockPath = path + QStringLiteral(".ludus-editor-lock");
    if (QFileInfo(lockPath).isSymLink())
    {
        return false;
    }
    QLockFile lock(lockPath);
    QByteArray disk;
    if (!lock.tryLock(0) || !ReadScene(path, disk) || disk != baseline)
    {
        return false;
    }
    // Content digest detects a truncated or changed recovery payload; the base
    // digest refuses recovery over a different saved source. Never auto-apply.
    const auto envelope =
        Header(baseline) + QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() + '\n' + bytes;
    return Replace(path + QStringLiteral(".ludus-recovery"), envelope);
}
bool ReadSceneRecovery(const QString& path, const QByteArray& baseline, SceneSnapshot& output)
{
    const auto recoveryPath = path + QStringLiteral(".ludus-recovery");
    if (!Regular(recoveryPath))
    {
        return false;
    }
    QFile file(recoveryPath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > MAX_SCENE + 256)
    {
        return false;
    }
    const auto envelope = file.read(MAX_SCENE + 257);
    const auto header = Header(baseline);
    if (file.error() != QFileDevice::NoError || !envelope.startsWith(header))
    {
        return false;
    }
    const auto payload = envelope.mid(header.size() + 65);
    if (envelope.mid(header.size(), 65) != QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex() + '\n')
    {
        return false;
    }
    SceneSnapshot candidate;
    if (world_demo::ReadLevel({payload.constData(), static_cast<foundation::usize>(payload.size())}, candidate.Level)
            .Error != world_demo::LevelError::None)
    {
        return false;
    }
    output = candidate;
    return true;
}
} // namespace ludus::editor
