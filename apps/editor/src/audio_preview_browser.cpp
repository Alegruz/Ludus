#include "internal/audio_preview.h"

#include <QTimer>

namespace ludus::editor
{
AudioPreview::AudioPreview(QObject* parent) : QObject(parent) {}
AudioPreview::~AudioPreview() = default;
void AudioPreview::Play(const QString&, const audio::content::Sound&)
{
    Q_EMIT Message(QStringLiteral("Audio preview requires the desktop editor in this browser preview."));
}
void AudioPreview::Play(const QString&, const audio::content::Music&)
{
    Q_EMIT Message(QStringLiteral("Audio preview requires the desktop editor in this browser preview."));
}
void AudioPreview::Import(const ContentImportSource&)
{
    QTimer::singleShot(0, this, [this]() {
        Q_EMIT Message(QStringLiteral("Audio import requires the desktop editor in this browser preview."));
    });
}
void AudioPreview::Stop() {}
void AudioPreview::Shutdown() {}
bool AudioPreview::Reset()
{
    return true;
}
bool AudioPreview::Finished() const noexcept
{
    return true;
}
} // namespace ludus::editor
