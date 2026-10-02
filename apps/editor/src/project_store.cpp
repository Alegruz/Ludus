#include "internal/project_store.h"

#include "internal/project_descriptor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

namespace ludus::editor
{
namespace
{

// A validation failure carries a code and a field-specific message. Returned by
// value so no exception path is involved.
ParseOutcome Fail(ResultCode code, const QString& message)
{
    ParseOutcome outcome;
    outcome.Code = code;
    outcome.Message = message;
    return outcome;
}

bool WithinBytes(const QString& value, usize maxBytes)
{
    return static_cast<usize>(value.toUtf8().size()) <= maxBytes;
}

bool ContainsNul(const QString& value)
{
    return value.contains(QChar(u'\0'));
}

// A relative path with no scheme/root; intentional ".." is allowed (resolved
// later). An absolute path is rejected here (design section 4).
bool IsRelativePath(const QString& value)
{
    if (value.isEmpty())
    {
        return false;
    }
    return QDir::isRelativePath(value);
}

bool ValidTargetName(const QString& value)
{
    if (value.isEmpty() || value.size() > 256)
    {
        return false;
    }
    const QChar first = value.at(0);
    const auto isWord = [](QChar c) {
        const ushort u = c.unicode();
        return (u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9') || u == u'_';
    };
    if (!isWord(first))
    {
        return false;
    }
    for (const QChar c : value)
    {
        const ushort u = c.unicode();
        const bool extra = u == u'.' || u == u'+' || u == u'-';
        if (!isWord(c) && !extra)
        {
            return false;
        }
    }
    return true;
}

// Reject any object key not in `allowed` (unknown fields fail at every level).
QString UnknownKey(const QJsonObject& object, const QStringList& allowed)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
    {
        if (!allowed.contains(it.key()))
        {
            return it.key();
        }
    }
    return QString();
}

} // namespace

QString Sha256Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

ParseOutcome ParseDescriptor(const QByteArray& bytes)
{
    if (static_cast<usize>(bytes.size()) > limits::MaxFileBytes)
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("descriptor exceeds the 64 KiB size limit"));
    }
    // Reject invalid UTF-8 / unpaired surrogates / embedded NUL before JSON.
    // QString::fromUtf8 replaces invalid sequences; detect by round-tripping.
    const QString text = QString::fromUtf8(bytes);
    if (text.toUtf8() != bytes)
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("descriptor is not valid UTF-8"));
    }
    if (text.contains(QChar(u'\0')))
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("descriptor contains a NUL byte"));
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("malformed JSON: %1").arg(error.errorString()));
    }
    const QJsonObject root = document.object();

    static const QStringList rootKeys{QStringLiteral("version"),
                                      QStringLiteral("name"),
                                      QStringLiteral("provider"),
                                      QStringLiteral("source_dir"),
                                      QStringLiteral("preset"),
                                      QStringLiteral("target"),
                                      QStringLiteral("run")};
    const QString unknown = UnknownKey(root, rootKeys);
    if (!unknown.isEmpty())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("unknown field '%1'").arg(unknown));
    }

    // version: a JSON number numerically equal to 1; reject bool/string/float.
    if (!root.contains(QStringLiteral("version")))
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("missing field 'version'"));
    }
    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (versionValue.type() != QJsonValue::Double)
    {
        return Fail(ResultCode::UnsupportedVersion, QStringLiteral("'version' must be the number 1"));
    }
    {
        const double raw = versionValue.toDouble();
        if (raw != 1.0)
        {
            return Fail(ResultCode::UnsupportedVersion,
                        QStringLiteral("unsupported descriptor version; only version 1 is supported"));
        }
    }

    ProjectDescriptor descriptor;

    // name
    const QJsonValue nameValue = root.value(QStringLiteral("name"));
    if (!nameValue.isString())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'name' must be a string"));
    }
    descriptor.Name = nameValue.toString();
    if (descriptor.Name.isEmpty())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'name' must be nonempty"));
    }
    if (ContainsNul(descriptor.Name) || !WithinBytes(descriptor.Name, limits::MaxNameBytes))
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'name' exceeds 128 UTF-8 bytes or contains NUL"));
    }

    // provider
    const QJsonValue providerValue = root.value(QStringLiteral("provider"));
    if (!providerValue.isString())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'provider' must be a string"));
    }
    const QString provider = providerValue.toString();
    if (provider == QStringLiteral("ludus"))
    {
        descriptor.ProviderKind = Provider::Ludus;
    }
    else if (provider == QStringLiteral("cmake"))
    {
        descriptor.ProviderKind = Provider::Cmake;
    }
    else
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'provider' must be exactly 'ludus' or 'cmake'"));
    }

    // source_dir
    const QJsonValue sourceValue = root.value(QStringLiteral("source_dir"));
    if (!sourceValue.isString())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'source_dir' must be a string"));
    }
    descriptor.SourceDir = sourceValue.toString();
    if (!IsRelativePath(descriptor.SourceDir) || ContainsNul(descriptor.SourceDir) ||
        !WithinBytes(descriptor.SourceDir, limits::MaxPathBytes))
    {
        return Fail(ResultCode::InvalidProject,
                    QStringLiteral("'source_dir' must be a relative path within 4096 UTF-8 bytes"));
    }

    // preset
    const QJsonValue presetValue = root.value(QStringLiteral("preset"));
    if (!presetValue.isString())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'preset' must be a string"));
    }
    descriptor.Preset = presetValue.toString();
    if (descriptor.Preset != QStringLiteral("linux-clang-debug") &&
        descriptor.Preset != QStringLiteral("linux-clang-development"))
    {
        return Fail(ResultCode::InvalidProject,
                    QStringLiteral("'preset' must be linux-clang-debug or linux-clang-development"));
    }

    // target
    const QJsonValue targetValue = root.value(QStringLiteral("target"));
    if (!targetValue.isString())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'target' must be a string"));
    }
    descriptor.Target = targetValue.toString();
    if (!ValidTargetName(descriptor.Target) || !WithinBytes(descriptor.Target, limits::MaxTargetBytes))
    {
        return Fail(ResultCode::InvalidProject,
                    QStringLiteral("'target' must match [A-Za-z0-9_][A-Za-z0-9_.+-]* within 256 bytes"));
    }

    // run { cwd, args }
    if (!root.contains(QStringLiteral("run")))
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("missing field 'run'"));
    }
    const QJsonValue runValue = root.value(QStringLiteral("run"));
    if (!runValue.isObject())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'run' must be an object"));
    }
    const QJsonObject run = runValue.toObject();
    static const QStringList runKeys{QStringLiteral("cwd"), QStringLiteral("args")};
    const QString runUnknown = UnknownKey(run, runKeys);
    if (!runUnknown.isEmpty())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("unknown field 'run.%1'").arg(runUnknown));
    }

    const QJsonValue cwdValue = run.value(QStringLiteral("cwd"));
    if (!cwdValue.isString())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'run.cwd' must be a string"));
    }
    descriptor.RunCwd = cwdValue.toString();
    if (!IsRelativePath(descriptor.RunCwd) || ContainsNul(descriptor.RunCwd) ||
        !WithinBytes(descriptor.RunCwd, limits::MaxPathBytes))
    {
        return Fail(ResultCode::InvalidProject,
                    QStringLiteral("'run.cwd' must be a relative path within 4096 UTF-8 bytes"));
    }

    if (!run.contains(QStringLiteral("args")))
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("missing field 'run.args'"));
    }
    const QJsonValue argsValue = run.value(QStringLiteral("args"));
    if (!argsValue.isArray())
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'run.args' must be an array"));
    }
    const QJsonArray args = argsValue.toArray();
    if (static_cast<usize>(args.size()) > limits::MaxArgCount)
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'run.args' exceeds 64 entries"));
    }
    usize total = 0;
    for (const auto& item : args)
    {
        if (!item.isString())
        {
            return Fail(ResultCode::InvalidProject, QStringLiteral("every 'run.args' entry must be a string"));
        }
        const QString value = item.toString();
        if (ContainsNul(value))
        {
            return Fail(ResultCode::InvalidProject, QStringLiteral("'run.args' entries must not contain NUL"));
        }
        const usize valueBytes = static_cast<usize>(value.toUtf8().size());
        if (valueBytes > limits::MaxArgBytes)
        {
            return Fail(ResultCode::InvalidProject, QStringLiteral("a 'run.args' entry exceeds 4096 UTF-8 bytes"));
        }
        total += valueBytes;
        descriptor.RunArgs.append(value); // empty strings are valid
    }
    if (total > limits::MaxArgsTotalBytes)
    {
        return Fail(ResultCode::InvalidProject, QStringLiteral("'run.args' total exceeds 32 KiB"));
    }

    ParseOutcome outcome;
    outcome.Code = ResultCode::Ok;
    outcome.Descriptor = descriptor;
    return outcome;
}

QByteArray SerializeDescriptor(const ProjectDescriptor& descriptor)
{
    // Build the object in a stable, documented order. QJsonObject sorts keys
    // alphabetically on output, which is deterministic; to control order and
    // indentation precisely we emit the text ourselves.
    const auto escape = [](const QString& value) -> QString {
        QString out;
        out.reserve(value.size() + 2);
        for (const QChar c : value)
        {
            const ushort u = c.unicode();
            switch (u)
            {
                case u'"':
                    out += QStringLiteral("\\\"");
                    break;
                case u'\\':
                    out += QStringLiteral("\\\\");
                    break;
                case u'\b':
                    out += QStringLiteral("\\b");
                    break;
                case u'\f':
                    out += QStringLiteral("\\f");
                    break;
                case u'\n':
                    out += QStringLiteral("\\n");
                    break;
                case u'\r':
                    out += QStringLiteral("\\r");
                    break;
                case u'\t':
                    out += QStringLiteral("\\t");
                    break;
                default:
                    if (u < 0x20)
                    {
                        out += QStringLiteral("\\u%1").arg(u, 4, 16, QLatin1Char('0'));
                    }
                    else
                    {
                        out += c;
                    }
            }
        }
        return out;
    };

    const QString provider =
        descriptor.ProviderKind == Provider::Ludus ? QStringLiteral("ludus") : QStringLiteral("cmake");

    QString text;
    text += QStringLiteral("{\n");
    text += QStringLiteral("  \"version\": 1,\n");
    text += QStringLiteral("  \"name\": \"%1\",\n").arg(escape(descriptor.Name));
    text += QStringLiteral("  \"provider\": \"%1\",\n").arg(provider);
    text += QStringLiteral("  \"source_dir\": \"%1\",\n").arg(escape(descriptor.SourceDir));
    text += QStringLiteral("  \"preset\": \"%1\",\n").arg(escape(descriptor.Preset));
    text += QStringLiteral("  \"target\": \"%1\",\n").arg(escape(descriptor.Target));
    text += QStringLiteral("  \"run\": {\n");
    text += QStringLiteral("    \"cwd\": \"%1\",\n").arg(escape(descriptor.RunCwd));
    if (descriptor.RunArgs.isEmpty())
    {
        text += QStringLiteral("    \"args\": []\n");
    }
    else
    {
        text += QStringLiteral("    \"args\": [\n");
        for (int i = 0; i < descriptor.RunArgs.size(); ++i)
        {
            const bool last = i + 1 == descriptor.RunArgs.size();
            text += QStringLiteral("      \"%1\"%2\n")
                        .arg(escape(descriptor.RunArgs.at(i)), last ? QString() : QStringLiteral(","));
        }
        text += QStringLiteral("    ]\n");
    }
    text += QStringLiteral("  }\n");
    text += QStringLiteral("}\n");
    return text.toUtf8();
}

LoadOutcome ProjectStore::Load(const QString& descriptorPath) const
{
    LoadOutcome outcome;
    QFile file(descriptorPath);
    const QFileInfo info(descriptorPath);
    if (info.size() > static_cast<qint64>(limits::MaxFileBytes))
    {
        outcome.Parse = Fail(ResultCode::InvalidProject, QStringLiteral("descriptor exceeds the 64 KiB size limit"));
        return outcome;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        outcome.Parse =
            Fail(ResultCode::InvalidProject, QStringLiteral("cannot open descriptor: %1").arg(file.errorString()));
        return outcome;
    }
    outcome.Bytes = file.readAll();
    file.close();
    outcome.Digest = Sha256Hex(outcome.Bytes);
    outcome.Parse = ParseDescriptor(outcome.Bytes);
    return outcome;
}

SaveOutcome
ProjectStore::Save(const QString& descriptorPath, const ProjectDescriptor& draft, const QString& expectedDigest) const
{
    SaveOutcome outcome;

    // Validate the complete draft by serializing then reparsing: the only bytes
    // ever written are bytes that parse back to the same descriptor.
    const QByteArray bytes = SerializeDescriptor(draft);
    const ParseOutcome check = ParseDescriptor(bytes);
    if (!check.Ok() || !(check.Descriptor == draft))
    {
        outcome.Code = ResultCode::InvalidProject;
        outcome.Message = check.Ok() ? QStringLiteral("draft failed round-trip validation") : check.Message;
        return outcome;
    }

    const QFileInfo info(descriptorPath);
    const QDir settingsDir = info.absoluteDir();
    const QString lockDirPath = settingsDir.filePath(QStringLiteral(".ludus"));
    QDir().mkpath(lockDirPath);
    QLockFile lock(QDir(lockDirPath).filePath(info.fileName() + QStringLiteral(".lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(250))
    {
        outcome.Code = ResultCode::Busy;
        outcome.Message = QStringLiteral("another editor holds the project settings lock");
        return outcome;
    }

    // Optimistic conflict detection: the current disk bytes must still match the
    // digest recorded at Open/last Save. External change/deletion => Conflict,
    // keep the draft and offer explicit Reload.
    if (!expectedDigest.isEmpty())
    {
        QFile current(descriptorPath);
        if (!current.exists())
        {
            outcome.Code = ResultCode::Conflict;
            outcome.Message = QStringLiteral("the saved descriptor was deleted on disk; reload or save as new");
            return outcome;
        }
        if (current.open(QIODevice::ReadOnly))
        {
            const QByteArray diskBytes = current.readAll();
            current.close();
            if (Sha256Hex(diskBytes) != expectedDigest)
            {
                outcome.Code = ResultCode::Conflict;
                outcome.Message = QStringLiteral("the descriptor changed on disk since it was opened");
                return outcome;
            }
        }
        else
        {
            outcome.Code = ResultCode::Conflict;
            outcome.Message = QStringLiteral("cannot read the current descriptor to check for conflicts");
            return outcome;
        }
    }

    QSaveFile save(descriptorPath);
    // Direct-write fallback disabled: without the temporary-file replacement we
    // would lose atomic-replacement protection (design section 4).
    save.setDirectWriteFallback(false);
    if (!save.open(QIODevice::WriteOnly))
    {
        outcome.Code = ResultCode::InvalidProject;
        outcome.Message = QStringLiteral("cannot open descriptor for writing: %1").arg(save.errorString());
        return outcome;
    }

    // Honor a test-injected short write, otherwise write all bytes.
    QByteArray toWrite = bytes;
    if (Hooks_.TruncateWriteTo)
    {
        const foundation::isize truncate = Hooks_.TruncateWriteTo(bytes);
        if (truncate >= 0 && truncate < static_cast<foundation::isize>(bytes.size()))
        {
            toWrite = bytes.left(static_cast<int>(truncate));
        }
    }
    const qint64 written = save.write(toWrite);
    if (written != bytes.size())
    {
        // Short/failed write: abandon the temporary file; destination unchanged.
        save.cancelWriting();
        outcome.Code = ResultCode::InvalidProject;
        outcome.Message =
            QStringLiteral("short write while saving descriptor (%1 of %2 bytes)").arg(written).arg(bytes.size());
        return outcome;
    }

    if (Hooks_.FailCommit && Hooks_.FailCommit())
    {
        save.cancelWriting();
        outcome.Code = ResultCode::InvalidProject;
        outcome.Message = QStringLiteral("commit failed while saving descriptor");
        return outcome;
    }

    if (!save.commit())
    {
        // Commit failure preserves the previous destination and the dirty draft.
        outcome.Code = ResultCode::InvalidProject;
        outcome.Message = QStringLiteral("commit failed while saving descriptor: %1").arg(save.errorString());
        return outcome;
    }

    outcome.Code = ResultCode::Ok;
    outcome.Digest = Sha256Hex(bytes);
    return outcome;
}

} // namespace ludus::editor
