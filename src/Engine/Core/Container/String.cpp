#include <Ludus/Engine/Pch.hpp>

#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>

namespace ludus::core
{
    // Explicit template instantiations for the underlying array types
    template class ArrayImpl<char, ArrayType::DYNAMIC, 0, ArrayResizePolicy::DEFAULT>;
    template class ArrayImpl<wchar_t, ArrayType::DYNAMIC, 0, ArrayResizePolicy::DEFAULT>;
    template class ArrayImplBase<char, ArrayType::DYNAMIC, 0, ArrayResizePolicy::DEFAULT>;
    template class ArrayImplBase<wchar_t, ArrayType::DYNAMIC, 0, ArrayResizePolicy::DEFAULT>;

    WString ConvertStringToWString(const String& str) noexcept
    {
        if (str.IsEmpty())
        {
            return WString();
        }

#if defined(LUDUS_WINDOWS)
        // Windows: Use MultiByteToWideChar for UTF-8 to UTF-16 conversion
        const char* data = str.GetData();
        const int size = static_cast<int>(str.GetSize());
        
        // Get required buffer size (excluding null terminator)
        const int requiredSize = ::MultiByteToWideChar(CP_UTF8, 0, data, size, nullptr, 0);
        if (requiredSize <= 0)
        {
            return WString();
        }

        // Allocate temporary buffer with proper ownership using the engine's Array class
        WString buffer(static_cast<uint32_t>(requiredSize));
        
        // Perform conversion
        const int result = ::MultiByteToWideChar(CP_UTF8, 0, data, size, 
                                                  buffer.GetData(), requiredSize);
        
        WString wstr;
        if (result > 0)
        {
            wstr.Append(buffer.GetData(), static_cast<uint32_t>(result));
        }
        
        // buffer is automatically cleaned up when it goes out of scope
        return wstr;
#else
        // Linux/Mac: Use standard library conversion (deprecated but functional)
        const char* data = str.GetData();
        const uint32_t size = str.GetSize();
        
        WString wstr(size); // Pre-allocate
        
        std::mbstate_t state{};
        const char* src = data;
        const char* end = data + size;
        
        while (src < end)
        {
            wchar_t wc;
            const size_t converted = std::mbrtowc(&wc, src, end - src, &state);
            
            if (converted == static_cast<size_t>(-1) || converted == static_cast<size_t>(-2))
            {
                // Invalid sequence, skip byte
                ++src;
                continue;
            }
            
            if (converted == 0)
            {
                // Null character
                break;
            }
            
            wstr.PushBack(wc);
            src += converted;
        }
        
        return wstr;
#endif
    }

    String ConvertWStringToString(const WString& wstr) noexcept
    {
        if (wstr.IsEmpty())
        {
            return String();
        }

#if defined(LUDUS_WINDOWS)
        // Windows: Use WideCharToMultiByte for UTF-16 to UTF-8 conversion
        const wchar_t* data = wstr.GetData();
        const int size = static_cast<int>(wstr.GetSize());
        
        // Get required buffer size (excluding null terminator)
        const int requiredSize = ::WideCharToMultiByte(CP_UTF8, 0, data, size, 
                                                        nullptr, 0, nullptr, nullptr);
        if (requiredSize <= 0)
        {
            return String();
        }

        // Allocate temporary buffer with proper ownership using the engine's Array class
        String buffer(static_cast<uint32_t>(requiredSize));
        
        // Perform conversion
        const int result = ::WideCharToMultiByte(CP_UTF8, 0, data, size, 
                                                  buffer.GetData(), requiredSize, 
                                                  nullptr, nullptr);
        
        String str;
        if (result > 0)
        {
            str.Append(buffer.GetData(), static_cast<uint32_t>(result));
        }
        
        // buffer is automatically cleaned up when it goes out of scope
        return str;
#else
        // Linux/Mac: Use standard library conversion
        const wchar_t* data = wstr.GetData();
        const uint32_t size = wstr.GetSize();
        
        String str(size * 4); // UTF-8 can be up to 4 bytes per character
        
        std::mbstate_t state{};
        char buffer[MB_LEN_MAX];
        
        for (uint32_t i = 0; i < size; ++i)
        {
            const size_t converted = std::wcrtomb(buffer, data[i], &state);
            
            if (converted == static_cast<size_t>(-1))
            {
                // Invalid wide character, skip
                continue;
            }
            
            for (size_t j = 0; j < converted; ++j)
            {
                str.PushBack(buffer[j]);
            }
        }
        
        return str;
#endif
    }
} // namespace ludus::core
