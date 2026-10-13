#include "internal/qt_viewport_device.h"

#include <new>

#include <QQuickGraphicsDevice>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QVulkanInstance>
#include <QtQuick/qsgtexture_platform.h>

namespace ludus::editor
{
struct QtViewportDevice::State final
{
    QVulkanInstance Instance;
};
QtViewportDevice::QtViewportDevice() noexcept = default;
QtViewportDevice::~QtViewportDevice() noexcept = default;
bool QtViewportDevice::PrepareWindow(QQuickWindow& inoutWindow)
{
    mState.Reset(new (std::nothrow) State);
    if (mState.Get() == nullptr)
    {
        return false;
    }
    mState->Instance.setApiVersion(QVersionNumber(1, 1));
    if (mState->Instance.create() == false)
    {
        return false;
    }
    inoutWindow.setVulkanInstance(&mState->Instance);
    return true;
}
bool QtViewportDevice::GetDescription(QQuickWindow& inoutWindow, graphics::rhi::NativeDeviceDescription& outDescription)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    QSGRendererInterface* renderer = inoutWindow.rendererInterface();
    if (renderer->graphicsApi() != QSGRendererInterface::Vulkan || inoutWindow.vulkanInstance() == nullptr)
    {
        return false;
    }
    const VkDevice* device =
        static_cast<const VkDevice*>(renderer->getResource(&inoutWindow, QSGRendererInterface::DeviceResource));
    const VkQueue* queue =
        static_cast<const VkQueue*>(renderer->getResource(&inoutWindow, QSGRendererInterface::CommandQueueResource));
    const VkPhysicalDevice* physical = static_cast<const VkPhysicalDevice*>(
        renderer->getResource(&inoutWindow, QSGRendererInterface::PhysicalDeviceResource));
    const foundation::uint32* family = static_cast<const foundation::uint32*>(
        renderer->getResource(&inoutWindow, QSGRendererInterface::GraphicsQueueFamilyIndexResource));
    const foundation::uint32* index = static_cast<const foundation::uint32*>(
        renderer->getResource(&inoutWindow, QSGRendererInterface::GraphicsQueueIndexResource));
    if (device == nullptr || queue == nullptr || physical == nullptr || family == nullptr || index == nullptr)
    {
        return false;
    }
    const QVersionNumber version = inoutWindow.vulkanInstance()->apiVersion();
    outDescription.Identity =
    {
        .Api = graphics::rhi::Backend::Vulkan,
        .Instance = graphics::rhi::NativeInstance{reinterpret_cast<void*>(inoutWindow.vulkanInstance()->vkInstance())},
        .PhysicalDevice = graphics::rhi::NativePhysicalDevice{reinterpret_cast<void*>(*physical)},
        .Device = graphics::rhi::NativeLogicalDevice{reinterpret_cast<void*>(*device)},
        .CommandQueue = graphics::rhi::NativeCommandQueue{reinterpret_cast<void*>(*queue)},
        .QueueFamily = *family,
        .QueueIndex = *index,
    };
    outDescription.InstanceApiVersion = VK_MAKE_API_VERSION(0,
                                                            static_cast<foundation::uint32>(version.majorVersion()),
                                                            static_cast<foundation::uint32>(version.minorVersion()),
                                                            static_cast<foundation::uint32>(version.microVersion()));
    outDescription.SerializedOwnerQueue = true;
    return true;
#else
    static_cast<void>(inoutWindow);
    static_cast<void>(outDescription);
    return false;
#endif
}
bool QtViewportDevice::IsCompatible(QQuickWindow& inoutWindow,
                                    const graphics::rhi::NativeDeviceIdentity& device) const noexcept
{
    QSGRendererInterface* renderer = inoutWindow.rendererInterface();
    if (renderer->graphicsApi() != QSGRendererInterface::Vulkan || inoutWindow.vulkanInstance() == nullptr)
    {
        return false;
    }
    const VkDevice* actualDevice =
        static_cast<const VkDevice*>(renderer->getResource(&inoutWindow, QSGRendererInterface::DeviceResource));
    const VkQueue* actualQueue =
        static_cast<const VkQueue*>(renderer->getResource(&inoutWindow, QSGRendererInterface::CommandQueueResource));
    const VkPhysicalDevice* actualPhysical = static_cast<const VkPhysicalDevice*>(
        renderer->getResource(&inoutWindow, QSGRendererInterface::PhysicalDeviceResource));
    // Queue provenance comes from fromDeviceObjects above, not a guessed family.
    return actualDevice != nullptr && actualQueue != nullptr && actualPhysical != nullptr &&
           reinterpret_cast<void*>(*actualDevice) == device.Device.GetObject() &&
           reinterpret_cast<void*>(*actualQueue) == device.CommandQueue.GetObject() &&
           reinterpret_cast<void*>(*actualPhysical) == device.PhysicalDevice.GetObject() &&
           reinterpret_cast<void*>(inoutWindow.vulkanInstance()->vkInstance()) == device.Instance.GetObject();
}
QSGTexture* QtViewportDevice::WrapTexture(QQuickWindow& inoutWindow, const graphics::rhi::NativeTextureInfo& image)
{
    if (IsCompatible(inoutWindow, image.Device.Identity) == false)
    {
        return nullptr;
    }
#if VK_USE_64_BIT_PTR_DEFINES
    // Reconstruct Vulkan's opaque handle from the RHI's borrowed native bits.
    // Qt forwards this identity to Vulkan; neither consumer dereferences it.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const VkImage nativeImage = reinterpret_cast<VkImage>(image.VulkanImage.GetBits());
#else
    const VkImage nativeImage = image.VulkanImage.GetBits();
#endif
    return QNativeInterface::QSGVulkanTexture::fromNative(
        nativeImage,
        static_cast<VkImageLayout>(image.VulkanLayout),
        &inoutWindow,
        QSize(static_cast<foundation::int32>(image.Width), static_cast<foundation::int32>(image.Height)));
}
} // namespace ludus::editor
