#include "internal/preview_viewport.h"

#include <ludus/foundation/base/core.h>

#include <new>

#include <QGuiApplication>
#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>

#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
#    include "internal/qt_viewport_device.h"
#    include <QQuickItem>
#    include <QQuickWindow>
#    include <QSGSimpleTextureNode>
#    include <QSGTexture>
#    include <QThread>
#endif

namespace ludus::editor
{
using namespace foundation;
namespace renderer = graphics::renderer;
namespace rhi = graphics::rhi;
struct PreviewViewport::State final
{
    QPointer<RenderPreview> Preview;
    PreviewViewport* Widget = nullptr;
    QVBoxLayout* Layout = nullptr;
    QLabel* Message = nullptr;
    uint64 Presented = 0;
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    // Thanks to The Qt Company: QQuickWindow frame stages and Native Interfaces
    // define wrapping and submission order. We use its basic owner-thread loop,
    // and seal foreign reads afterFrameEnd, never afterRendering (still recording).
    // https://doc.qt.io/qt-6/qquickwindow.html#afterFrameEnd
    // https://doc.qt.io/qt-6/qnativeinterface-qsgmetaltexture.html
    // https://doc.qt.io/qt-6/qnativeinterface-qsgvulkantexture.html
    struct Frame final
    {
        renderer::ViewportFrame Lease{};
        rhi::NativeTextureRead Read{};
        rhi::NativeTextureInfo Image{};
        QSGTexture* Wrapper = nullptr;
        bool HasLease = false;
        bool IsRetiring = false;
    };
    class ImageItem final : public QQuickItem
    {
    public:
        explicit ImageItem(State& inoutState, QQuickItem* parent) : QQuickItem(parent), mState(inoutState)
        {
            setFlag(ItemHasContents);
        }

    protected:
        QSGNode* updatePaintNode(QSGNode* oldNodeOrNull, UpdatePaintNodeData*) override
        {
            LUDUS_REQUIRE(QThread::currentThread() == mState.Widget->thread());
            QSGSimpleTextureNode* node = static_cast<QSGSimpleTextureNode*>(oldNodeOrNull);
            if (mState.Pending >= 0)
            {
                Frame& next = mState.Frames[mState.Pending];
                next.Wrapper = mState.Device->WrapTexture(*window(), next.Image);
                if (next.Wrapper == nullptr)
                {
                    mState.Message->setText(
                        QStringLiteral("Qt cannot import this GPU texture. Editing remains available."));
                    next.IsRetiring = true;
                    mState.Pending = -1;
                    return node;
                }
                next.Wrapper->setFiltering(QSGTexture::Linear);
                if (node == nullptr)
                {
                    node = new QSGSimpleTextureNode;
                    node->setOwnsTexture(false);
                }
                if (mState.Active >= 0)
                {
                    Frame& previous = mState.Frames[mState.Active];
                    previous.IsRetiring = true;
                    delete previous.Wrapper;
                    previous.Wrapper = nullptr;
                }
                mState.Active = mState.Pending;
                mState.Pending = -1;
                node->setTexture(next.Wrapper);
                ++mState.Presented;
            }
            if (node != nullptr)
            {
                node->setRect(boundingRect());
            }
            return node;
        }

    private:
        State& mState;
    };
    QQuickWindow* Window = nullptr;
    QWidget* Container = nullptr;
    ImageItem* Item = nullptr;
    UniquePtr<QtViewportDevice> Device;
    rhi::NativeDeviceLease DeviceLease{};
    rhi::NativeDeviceInfo DeviceInfo{};
    Frame Frames[2]{};
    int32 Active = -1, Pending = -1;
    uint32 SourcePins = 0;
    bool HasDeviceLease = false;
    bool IsDestroyingWindow = false;
    bool IsRecording = false;
    QTimer Service;
    static bool Retain(void* context) noexcept
    {
        State& state = *static_cast<State*>(context);
        if (state.Window == nullptr || state.IsDestroyingWindow == true)
        {
            return false;
        }
        ++state.SourcePins;
        return true;
    }
    static void Release(void* context) noexcept
    {
        State& state = *static_cast<State*>(context);
        LUDUS_REQUIRE(state.SourcePins > 0);
        --state.SourcePins;
    }
    friend class PreviewViewport;

private:
    void createItem()
    {
        Item = new ImageItem(*this, Window->contentItem());
        Item->setSize(QSizeF(Widget->size()));
    }
    void createWindow()
    {
        if (Window != nullptr || Preview.isNull() == true)
        {
            return;
        }
        if (PreviewViewport::CanPresent() == false)
        {
            Preview->Stop();
            Message->setText(QStringLiteral("Native GPU sharing needs a supported display (Vulkan: Qt 6.6+)."));
            return;
        }
        // Basic rendering keeps all RHI calls and native queue submissions on
        // the GUI/device owner. The editor has no other Qt Quick render loop.
        qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("basic"));
#    if defined(Q_OS_MACOS)
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#    else
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
#    endif
        Window = new QQuickWindow;
        Window->setObjectName(QStringLiteral("LudusGpuViewport"));
        Window->installEventFilter(Widget);
        Window->setPersistentGraphics(true);
        Window->setPersistentSceneGraph(true);
        Window->setColor(QColor(9, 12, 15));
        Device.Reset(new (std::nothrow) QtViewportDevice);
        if (Device.Get() == nullptr || Device->PrepareWindow(*Window) == false)
        {
            destroyWindow();
            Preview->Stop();
            Message->setText(QStringLiteral("Qt GPU initialization failed. Editing remains available."));
            return;
        }
        QObject::connect(
            Window,
            &QQuickWindow::sceneGraphInitialized,
            Widget,
            [this]() {
                LUDUS_REQUIRE(QThread::currentThread() == Widget->thread());
                rhi::NativeDeviceDescription description;
                if (Device->GetDescription(*Window, description) == false)
                {
                    Message->setText(QStringLiteral("Native GPU sharing is unavailable (Vulkan needs Qt 6.6+)."));
                    QTimer::singleShot(0, Widget, [this]() { Preview->Stop(); });
                    return;
                }
                description.Lifetime = { .Context = this, .Retain = Retain, .Release = Release };
                if (Preview->AdmitDevice(description) == false)
                {
                    QTimer::singleShot(0, Widget, [this]() { Preview->Stop(); });
                    return;
                }
                HasDeviceLease = Preview->PinDevice(DeviceLease) == rhi::RasterStatus::Ready;
                if (HasDeviceLease == false ||
                    rhi::GetNativeDeviceInfo(DeviceLease, DeviceInfo) != rhi::RasterStatus::Ready ||
                    Device->IsCompatible(*Window, DeviceInfo.Identity) == false)
                {
                    QTimer::singleShot(0, Widget, [this]() { Preview->Stop(); });
                }
            },
            Qt::DirectConnection);
        QObject::connect(
            Window,
            &QQuickWindow::beforeFrameBegin,
            Widget,
            [this]() { IsRecording = true; },
            Qt::DirectConnection);
        QObject::connect(
            Window,
            &QQuickWindow::afterFrameEnd,
            Widget,
            [this]() {
                IsRecording = false;
                releaseRetired();
            },
            Qt::DirectConnection);
        QObject::connect(
            Window,
            &QQuickWindow::sceneGraphInvalidated,
            Widget,
            [this]() {
                if (IsDestroyingWindow == false && Preview.isNull() == false)
                {
                    // The Qt source is still alive here; retire RHI before Qt destroys it.
                    IsDestroyingWindow = true;
                    Preview->Stop();
                }
            },
            Qt::DirectConnection);
        QObject::connect(Window,
                         &QQuickWindow::sceneGraphError,
                         Widget,
                         [this](QQuickWindow::SceneGraphError, const QString& error) {
                             Message->setText(error);
                             QTimer::singleShot(0, Widget, [this]() { Preview->Stop(); });
                         });
        createItem();
        Container = QWidget::createWindowContainer(Window, Widget);
        Container->setFocusPolicy(Qt::NoFocus);
        Container->setAttribute(Qt::WA_TransparentForMouseEvents);
        Layout->addWidget(Container);
        Message->hide();
        Container->show();
        Service.start(16);
    }
    void releaseRetired()
    {
        LUDUS_REQUIRE(QThread::currentThread() == Widget->thread());
        for (Frame& frame : Frames)
        {
            if (frame.HasLease == false || frame.IsRetiring == false)
            {
                continue;
            }
            const rhi::RasterStatus released = rhi::ReleaseNativeTextureRead(frame.Read);
            if (released == rhi::RasterStatus::Ready)
            {
                Preview->ReleaseFrame(frame.Lease);
                frame = {};
            }
        }
    }
    void clearFrames()
    {
        bool hasFrames = false;
        for (const Frame& frame : Frames)
        {
            hasFrames = hasFrames || frame.HasLease;
        }
        if (hasFrames == false)
        {
            return;
        }
        LUDUS_REQUIRE(IsRecording == false);
        // Detach the complete item from the scene graph before freeing wrappers.
        // Its obsolete node owns no texture and cannot record further sampling.
        delete Item;
        Item = nullptr;
        Active = Pending = -1;
        for (Frame& frame : Frames)
        {
            delete frame.Wrapper;
            frame.Wrapper = nullptr;
            if (frame.HasLease == true)
            {
                const rhi::RasterStatus released = rhi::DrainNativeTextureRead(frame.Read);
                LUDUS_REQUIRE(released == rhi::RasterStatus::Ready);
                Preview->ReleaseFrame(frame.Lease);
                frame = {};
            }
        }
        if (Window != nullptr && IsDestroyingWindow == false)
        {
            createItem();
        }
    }
    void destroyWindow()
    {
        Service.stop();
        IsDestroyingWindow = true;
        // Source pins must already have been released by RHI shutdown. The
        // device lease keeps the Qt source handshake until the final release.
        if (HasDeviceLease == true)
        {
            static_cast<void>(rhi::ReleaseNativeDevice(DeviceLease));
            HasDeviceLease = false;
        }
        LUDUS_REQUIRE(SourcePins == 0);
        if (Container != nullptr)
        {
            delete Container;
            Container = nullptr;
        }
        else
        {
            delete Window;
        }
        if (Window != nullptr && Item != nullptr)
        {
            // The container owns and destroys Window and its content items.
            Item = nullptr;
        }
        Window = nullptr;
        Device.Reset();
        IsDestroyingWindow = false;
        Message->show();
    }
    void showFrame()
    {
        if (Preview.isNull() == true || Window == nullptr || Pending >= 0 || Preview->HasPendingFrame() == false)
        {
            return;
        }
        for (int32 i = 0; i < 2; ++i)
        {
            Frame& frame = Frames[i];
            if (frame.HasLease == true)
            {
                continue;
            }
            renderer::ViewportImage image;
            if (Preview->TakeFrame(frame.Lease, image) == false)
            {
                return;
            }
            const rhi::RasterStatus acquired = rhi::AcquireNativeTextureRead(DeviceLease,
                                                                             DeviceInfo.Identity,
                                                                             image.Texture,
                                                                             image.Producer,
                                                                             frame.Read,
                                                                             frame.Image);
            if (acquired != rhi::RasterStatus::Ready)
            {
                Preview->ReleaseFrame(frame.Lease);
                Message->setText(QStringLiteral("GPU texture sharing failed. Editing remains available."));
                return;
            }
            frame.HasLease = true;
            Pending = i;
            Item->update();
            Window->update();
            return;
        }
    }
#endif
};
PreviewViewport::PreviewViewport(RenderPreview* preview, QWidget* parentOrNull, bool isAutomatic)
    : QWidget(parentOrNull), Preview_(preview)
{
    mState.Reset(new State);
    setMinimumSize(160, 160);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral("Drag to orbit · middle/Shift-drag to pan · wheel to zoom · double-click to frame all"));
    mState->Preview = preview;
    mState->Widget = this;
    mState->Layout = new QVBoxLayout(this);
    mState->Layout->setContentsMargins(0, 0, 0, 0);
    mState->Message = new QLabel(QStringLiteral("Open a World to render it here."), this);
    mState->Message->setAlignment(Qt::AlignCenter);
    mState->Message->setWordWrap(true);
    mState->Layout->addWidget(mState->Message);
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    connect(preview, &RenderPreview::DeviceRequested, this, [this]() { mState->createWindow(); });
    connect(preview, &RenderPreview::ResettingFrames, this, [this]() { mState->clearFrames(); });
    connect(preview, &RenderPreview::Stopped, this, [this]() {
        if (mState->IsDestroyingWindow == true)
        {
            if (mState->HasDeviceLease == true)
            {
                static_cast<void>(rhi::ReleaseNativeDevice(mState->DeviceLease));
                mState->HasDeviceLease = false;
            }
            QTimer::singleShot(0, this, [this]() { mState->destroyWindow(); });
        }
        else
        {
            mState->destroyWindow();
        }
    });
    connect(&mState->Service, &QTimer::timeout, this, [this, isAutomatic]() {
        mState->releaseRetired();
        if (isAutomatic == true)
        {
            mState->showFrame();
        }
    });
#endif
    if (isAutomatic == true)
    {
        connect(preview, &RenderPreview::FrameReady, this, &PreviewViewport::ShowFrame);
    }
}
PreviewViewport::~PreviewViewport()
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    if (mState->Preview.isNull() == false)
    {
        mState->Preview->Stop();
    }
    mState->destroyWindow();
#endif
}
void PreviewViewport::ShowFrame()
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    mState->showFrame();
#endif
}
void PreviewViewport::ClearFrame(const QString& text)
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    mState->clearFrames();
#endif
    mState->Message->setText(text);
}
bool PreviewViewport::eventFilter(QObject* watched, QEvent* event)
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    if (watched == mState->Window)
    {
        switch (event->type())
        {
            case QEvent::MouseButtonPress:
                mousePressEvent(static_cast<QMouseEvent*>(event));
                return true;
            case QEvent::MouseMove:
                mouseMoveEvent(static_cast<QMouseEvent*>(event));
                return true;
            case QEvent::MouseButtonDblClick:
                mouseDoubleClickEvent(static_cast<QMouseEvent*>(event));
                return true;
            case QEvent::Wheel:
                wheelEvent(static_cast<QWheelEvent*>(event));
                return true;
            default:
                break;
        }
    }
#endif
    return QWidget::eventFilter(watched, event);
}
void PreviewViewport::resizeSurface()
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    if (mState->Item != nullptr)
    {
        mState->Item->setSize(QSizeF(size()));
        mState->Item->update();
    }
#endif
}
bool PreviewViewport::CanPresent() noexcept
{
#if defined(LUDUS_EDITOR_NATIVE_PREVIEW)
    const QString platform = QGuiApplication::platformName();
#    if defined(Q_OS_MACOS)
    return platform == QStringLiteral("cocoa");
#    elif QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    return platform == QStringLiteral("xcb") || platform.startsWith(QStringLiteral("wayland"));
#    else
    return false;
#    endif
#else
    return false;
#endif
}
uint64 PreviewViewport::PresentedFrames() const noexcept
{
    return mState->Presented;
}
} // namespace ludus::editor
