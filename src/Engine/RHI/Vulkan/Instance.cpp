#include <Ludus/Engine/RHI/Instance.h>

#if defined(LUDUS_GRAPHICS_VULKAN)
#include <Ludus/Engine/Core/Container/HashMap.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <volk.h>

namespace ludus::rhi
{
    struct InstanceMemberVariablesVulkan final : public InstanceMemberVariablesBase<GraphicsApi::VULKAN>
    {
        LUDUS_INLINE explicit InstanceMemberVariablesVulkan(Instance<GraphicsApi::VULKAN>& rhiInstance)
            : InstanceMemberVariablesBase<GraphicsApi::VULKAN>(rhiInstance)
        {
        }

        uint32_t InstanceVersion = 0;
        VkInstance Instance = VK_NULL_HANDLE;
#if defined(LUDUS_DEBUG)
        VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
#endif  // defined(LUDUS_DEBUG)
    };

    enum class ExtensionValidatorObjectType : uint8_t
    {
        INSTANCE = 0,
        PHYSICAL_DEVICE,
        COUNT,
    };

    enum class ExtensionType : uint8_t
    {
        LAYER,
        EXTENSION,
        COUNT,
    };

    template<ExtensionValidatorObjectType OBJECT_TYPE, ExtensionType EXTENSION_TYPE>
    class ExtensionValidator final
    {
    public:
        LUDUS_INLINE explicit ExtensionValidator() noexcept = default;
        ExtensionValidator(const ExtensionValidator&) = delete;
        LUDUS_INLINE constexpr ExtensionValidator(ExtensionValidator&&) noexcept = default;
        ExtensionValidator& operator=(const ExtensionValidator&) = delete;
        LUDUS_INLINE constexpr ExtensionValidator& operator=(ExtensionValidator&&) noexcept = default;
        LUDUS_INLINE ~ExtensionValidator() noexcept = default;

        [[nodiscard]] bool Initialize() noexcept
        {
            VkResult vr = VK_SUCCESS;

            uint32_t extensionsCount = 0;
            if constexpr (OBJECT_TYPE == ExtensionValidatorObjectType::INSTANCE)
            {
                if constexpr (EXTENSION_TYPE == ExtensionType::LAYER)
                {
                    vr = vkEnumerateInstanceLayerProperties(&extensionsCount, nullptr);
                    if(vr != VK_SUCCESS)
                    {
                        LUDUS_ASSERT_MSG(false, "Failed to enumerate Vulkan instance layer properties.");
                        return false;
                    }

                    core::DynamicArray<VkLayerProperties> layerProperties(extensionsCount, VkLayerProperties{});
                    vr = vkEnumerateInstanceLayerProperties(&extensionsCount, layerProperties.GetData());
                    if(vr != VK_SUCCESS)
                    {
                        LUDUS_ASSERT_MSG(false, "Failed to enumerate Vulkan instance layer properties.");
                        return false;
                    }

                    mAvailableExtensions.SetCapacity(extensionsCount);
                    for(uint32_t i = 0; i < extensionsCount; ++i)
                    {
                        mAvailableExtensions.Insert(core::String(layerProperties[i].layerName), i);
                    }
                }
                else if constexpr (EXTENSION_TYPE == ExtensionType::EXTENSION)
                {
                    vr = vkEnumerateInstanceExtensionProperties(nullptr, &extensionsCount, nullptr);
                    if(vr != VK_SUCCESS)
                    {
                        LUDUS_ASSERT_MSG(false, "Failed to enumerate Vulkan instance extension properties.");
                        return false;
                    }

                    core::DynamicArray<VkExtensionProperties> extensionProperties(extensionsCount, VkExtensionProperties{});
                    vr = vkEnumerateInstanceExtensionProperties(nullptr, &extensionsCount, extensionProperties.GetData());
                    if(vr != VK_SUCCESS)
                    {
                        LUDUS_ASSERT_MSG(false, "Failed to enumerate Vulkan instance extension properties.");
                        return false;
                    }

                    mAvailableExtensions.SetCapacity(extensionsCount);
                    for(uint32_t i = 0; i < extensionsCount; ++i)
                    {
                        mAvailableExtensions.Insert(core::String(extensionProperties[i].extensionName), i);
                    }
                }
                else
                {
                    LUDUS_STATIC_ASSERT_MSG(false, "Unsupported ExtensionType.");
                }
            }
            else
            {
                LUDUS_STATIC_ASSERT_MSG(false, "Unsupported ExtensionValidatorObjectType.");
            }

            return vr == VK_SUCCESS;
        }

        [[nodiscard]] LUDUS_INLINE bool TryEnableExtension(const core::String& extensionName) noexcept
        {
            if (mAvailableExtensions.Contains(extensionName))
            {
                mEnabledExtensions.PushBack(extensionName.GetCStr());
                return true;
            }
            return false;
        }

        bool TryEnableExtensions(const core::DynamicArray<core::String>& extensionNames) noexcept
        {
            bool success = true;
            for (const core::String& extensionName : extensionNames)
            {
                const bool result = TryEnableExtension(extensionName);
                success = success && result;
            }
            return success;
        }

        [[nodiscard]] LUDUS_INLINE constexpr const core::DynamicArray<const char*>& GetEnabledExtensions() const noexcept { return mEnabledExtensions; }

    private:
        core::DynamicArray<const char*> mEnabledExtensions;
        core::HashMap<core::String, uint32_t> mAvailableExtensions;
    };

#if defined(LUDUS_DEBUG)
    static VkBool32 DebugUtilsMessengerCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT       messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT              messageTypes,
        const VkDebugUtilsMessengerCallbackDataEXT*  pCallbackData,
        void*                                        pUserData);
#endif  // defined(LUDUS_DEBUG)

#define mMemberVariablesVulkan (*static_cast<InstanceMemberVariablesVulkan*>(mMemberVariables.Get()))   // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast,-warnings-as-errors)

    template<GraphicsApi GRAPHICS_API>
    Instance<GRAPHICS_API>::Instance() noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
        : mMemberVariables(core::MakeUnique<InstanceMemberVariablesVulkan>(*this))
    {
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::initialize(const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        VkResult vr = volkInitialize();
        if(vr != VK_SUCCESS)
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize Volk library.");
            return false;
        }

        vr = vkEnumerateInstanceVersion(&mMemberVariablesVulkan.InstanceVersion);
        if(vr != VK_SUCCESS)
        {
            LUDUS_ASSERT_MSG(false, "Failed to enumerate Vulkan instance version.");
            return false;
        }

        const void* pNext = nullptr;
        
#if defined(LUDUS_DEBUG)
        const VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfo =
        {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = pNext,
            .flags = 0,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT  | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = DebugUtilsMessengerCallback,
            .pUserData = this,
        };
        pNext = &debugUtilsMessengerCreateInfo;

        const core::DynamicArray<VkValidationFeatureEnableEXT> enabledValidationFeatures
        {
            VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
            VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
            VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
            VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        };

        const VkValidationFeaturesEXT validationFeatures =
        {
            .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
            .pNext = pNext,
            .enabledValidationFeatureCount = enabledValidationFeatures.GetSize(),
            .pEnabledValidationFeatures = enabledValidationFeatures.GetData(),
            .disabledValidationFeatureCount = 0,
            .pDisabledValidationFeatures = nullptr,
        };
        pNext = &validationFeatures;
#endif  // defined(LUDUS_DEBUG)

        const VkApplicationInfo appInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = createInfo.ApplicationInfo.Name,
            .applicationVersion = createInfo.ApplicationInfo.Version,
            .pEngineName = createInfo.EngineInfo.Name,
            .engineVersion = createInfo.EngineInfo.Version,
            .apiVersion = mMemberVariablesVulkan.InstanceVersion,
        };

        ExtensionValidator<ExtensionValidatorObjectType::INSTANCE, ExtensionType::LAYER> layerValidator;
        if(!layerValidator.Initialize())
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize Vulkan layer validator.");
            return false;
        }

        const core::DynamicArray<core::String> layersToEnable
        {
        };

        layerValidator.TryEnableExtensions(layersToEnable);
        
        ExtensionValidator<ExtensionValidatorObjectType::INSTANCE, ExtensionType::EXTENSION> extensionValidator;
        if(!extensionValidator.Initialize())
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize Vulkan extension validator.");
            return false;
        }

        const core::DynamicArray<core::String> extensionsToEnable
        {
#if defined(LUDUS_DEBUG)
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME,
#endif  // defined(LUDUS_DEBUG)
        };

        extensionValidator.TryEnableExtensions(extensionsToEnable);

        const VkInstanceCreateInfo instanceCreateInfo = 
        {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = pNext,
            .flags = 0,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = layerValidator.GetEnabledExtensions().GetSize(),
            .ppEnabledLayerNames = layerValidator.GetEnabledExtensions().GetData(),
            .enabledExtensionCount = extensionValidator.GetEnabledExtensions().GetSize(),
            .ppEnabledExtensionNames = extensionValidator.GetEnabledExtensions().GetData(),
        };

        vr = vkCreateInstance(&instanceCreateInfo, nullptr, &mMemberVariablesVulkan.Instance);
        if(vr != VK_SUCCESS)
        {
            LUDUS_ASSERT_MSG(false, "Failed to create Vulkan instance.");
            LUDUS_ASSERT_MSG(mMemberVariablesVulkan.InstanceVersion == VK_API_VERSION_1_0 || vr != VK_ERROR_INCOMPATIBLE_DRIVER, "Vulkan 1.1 or later must not fail with VK_ERROR_INCOMPATIBLE_DRIVER. Report this issue to the Vulkan SDK maintainers.");
            return false;
        }

        volkLoadInstance(mMemberVariablesVulkan.Instance);

#if defined(LUDUS_DEBUG)
        vr = vkCreateDebugUtilsMessengerEXT(
            mMemberVariablesVulkan.Instance,
            &debugUtilsMessengerCreateInfo,
            nullptr,
            &mMemberVariablesVulkan.DebugMessenger);
#endif  // defined(LUDUS_DEBUG)

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::initializePostSwapChainInitialization([[maybe_unused]] const CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        // Vulkan RHI post swap chain initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Instance post swap chain initialization is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::initializeAdaptersImpl() noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        // Vulkan RHI adapter initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Adapter initialization is not implemented yet.");

        return true;
    }

    template<GraphicsApi GRAPHICS_API>
    void Instance<GRAPHICS_API>::shutdown() noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        if (mMemberVariablesVulkan.Instance != VK_NULL_HANDLE)
        {
#if defined(LUDUS_DEBUG)
            LUDUS_ASSERT_MSG(mMemberVariablesVulkan.DebugMessenger != VK_NULL_HANDLE, "Vulkan debug messenger is null during instance shutdown.");
            vkDestroyDebugUtilsMessengerEXT(mMemberVariablesVulkan.Instance, mMemberVariablesVulkan.DebugMessenger, nullptr);
            mMemberVariablesVulkan.DebugMessenger = VK_NULL_HANDLE;
#endif  // defined(LUDUS_DEBUG)

            // Destroy Vulkan instance
            // Note: vkDestroyInstance is loaded by volkInitialize
            vkDestroyInstance(mMemberVariablesVulkan.Instance, nullptr);
            mMemberVariablesVulkan.Instance = VK_NULL_HANDLE;
        }
    }

    template<GraphicsApi GRAPHICS_API>
    bool Instance<GRAPHICS_API>::createSwapChainImpl([[maybe_unused]] const SwapChain<GRAPHICS_API>::CreateInfo& createInfo) noexcept requires(GRAPHICS_API == GraphicsApi::VULKAN)
    {
        LUDUS_ASSERT_MSG(false, "Vulkan RHI SwapChain creation is not implemented yet.");

        return true;
    }

#if defined(LUDUS_DEBUG)
    VkBool32 DebugUtilsMessengerCallback(
        [[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT       messageSeverity,  // TODO: Use messageSeverity
        [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT              messageTypes,     // TODO: Use messageTypes
        const VkDebugUtilsMessengerCallbackDataEXT*  pCallbackData,
        void*                                        pUserData)
    {
        if(pUserData == nullptr || pCallbackData == nullptr)
        {
            LUDUS_ASSERT_MSG(false, "Invalid user data or callback data in DebugUtilsMessengerCallback.");
            return VK_FALSE;
        }

        [[maybe_unused]] Instance<GraphicsApi::VULKAN>& instance = *static_cast<Instance<GraphicsApi::VULKAN>*>(pUserData); // TODO: Use instance if needed

        // Handle debug messages here (e.g., log them)
        // TODO: Implement proper logging mechanism
        return VK_TRUE;
    }
#endif  // defined(LUDUS_DEBUG)

    template class Instance<GraphicsApi::VULKAN>;
}   // namespace ludus::rhi 
#endif  // defined(LUDUS_GRAPHICS_VULKAN)