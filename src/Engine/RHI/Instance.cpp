#include <Ludus/Engine/RHI/Instance.h>

#include <Ludus/Engine/Core/Container/HashMap.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/SmartPtr.hpp>

#include <volk.h>

namespace ludus::rhi
{
    struct InstanceMemberVariables final
    {
    public:
        static constexpr const int32_t INVALID_ADAPTER_INDEX = -1;

    public:
        LUDUS_INLINE explicit InstanceMemberVariables(Instance& rhiInstance)
            : SwapChain(rhiInstance)
        {
        }

        uint32_t InstanceVersion = 0;
        VkInstance Instance = VK_NULL_HANDLE;
#if defined(LUDUS_DEBUG)
        VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
#endif  // defined(LUDUS_DEBUG)
        SwapChain SwapChain;
        core::DynamicArray<Adapter> Adapters;
        int32_t MainAdapterIndex = INVALID_ADAPTER_INDEX;
    };

    void InstanceMemberVariablesDeleter::operator()(InstanceMemberVariables* ptr) const noexcept
    {
        delete ptr;
    }

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

    Instance::Instance() noexcept
        : mMemberVariables(new InstanceMemberVariables(*this))
    {
    }

    bool Instance::Initialize(const CreateInfo& createInfo) noexcept
    {
        if( initialize(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize RHI Instance.");
            return false;
        }

        SwapChain::CreateInfo swapChainCreateInfo =
        {
            .Window = createInfo.Window,
            .BufferCount = 3,
        };

        if( createSwapChain(swapChainCreateInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to create RHI SwapChain.");
            return false;
        }

        if( initializePostSwapChainInitialization(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to perform post swap chain initialization for RHI Instance.");
            return false;
        }

        return true;
    }

    bool Instance::initializeAdapters() noexcept
    {
        if( this->initializeAdaptersImpl() == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize RHI Adapters.");
            return false;
        }
        Adapter::CreateInfo adapterCreateInfo = {};
        return mMemberVariables->Adapters.GetBack().Initialize(adapterCreateInfo);
    }

    bool Instance::createSwapChain(const SwapChain::CreateInfo& createInfo) noexcept
    {
        if( this->createSwapChainImpl(createInfo) == false )
        {
            LUDUS_ASSERT_MSG(false, "Failed to create RHI SwapChain.");
            return false;
        }
        return mMemberVariables->SwapChain.Initialize(createInfo); 
    }

    bool Instance::initialize(const CreateInfo& createInfo) noexcept
    {
        VkResult vr = volkInitialize();
        if(vr != VK_SUCCESS)
        {
            LUDUS_ASSERT_MSG(false, "Failed to initialize Volk library.");
            return false;
        }

        vr = vkEnumerateInstanceVersion(&mMemberVariables->InstanceVersion);
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
            .apiVersion = mMemberVariables->InstanceVersion,
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

        vr = vkCreateInstance(&instanceCreateInfo, nullptr, &mMemberVariables->Instance);
        if(vr != VK_SUCCESS)
        {
            LUDUS_ASSERT_MSG(false, "Failed to create Vulkan instance.");
            LUDUS_ASSERT_MSG(mMemberVariables->InstanceVersion == VK_API_VERSION_1_0 || vr != VK_ERROR_INCOMPATIBLE_DRIVER, "Vulkan 1.1 or later must not fail with VK_ERROR_INCOMPATIBLE_DRIVER. Report this issue to the Vulkan SDK maintainers.");
            return false;
        }

        volkLoadInstance(mMemberVariables->Instance);

#if defined(LUDUS_DEBUG)
        vr = vkCreateDebugUtilsMessengerEXT(
            mMemberVariables->Instance,
            &debugUtilsMessengerCreateInfo,
            nullptr,
            &mMemberVariables->DebugMessenger);
#endif  // defined(LUDUS_DEBUG)

        return true;
    }

    bool Instance::initializePostSwapChainInitialization([[maybe_unused]] const CreateInfo& createInfo) noexcept
    {
        // Vulkan RHI post swap chain initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Instance post swap chain initialization is not implemented yet.");

        return true;
    }

    bool Instance::initializeAdaptersImpl() noexcept
    {
        // Vulkan RHI adapter initialization logic (if any) goes here.
        LUDUS_ASSERT_MSG(false, "Vulkan RHI Adapter initialization is not implemented yet.");

        return true;
    }

    void Instance::shutdown() noexcept
    {
        if (mMemberVariables->Instance != VK_NULL_HANDLE)
        {
#if defined(LUDUS_DEBUG)
            LUDUS_ASSERT_MSG(mMemberVariables->DebugMessenger != VK_NULL_HANDLE, "Vulkan debug messenger is null during instance shutdown.");
            vkDestroyDebugUtilsMessengerEXT(mMemberVariables->Instance, mMemberVariables->DebugMessenger, nullptr);
            mMemberVariables->DebugMessenger = VK_NULL_HANDLE;
#endif  // defined(LUDUS_DEBUG)

            // Destroy Vulkan instance
            // Note: vkDestroyInstance is loaded by volkInitialize
            vkDestroyInstance(mMemberVariables->Instance, nullptr);
            mMemberVariables->Instance = VK_NULL_HANDLE;
        }
    }

    bool Instance::createSwapChainImpl([[maybe_unused]] const SwapChain::CreateInfo& createInfo) noexcept
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

        [[maybe_unused]] Instance& instance = *static_cast<Instance*>(pUserData); // TODO: Use instance if needed

        // Handle debug messages here (e.g., log them)
        // TODO: Implement proper logging mechanism
        return VK_TRUE;
    }
#endif  // defined(LUDUS_DEBUG)
}   // namespace ludus::rhi 
