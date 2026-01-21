#pragma once

//
// Memory Leak Detection Utilities for Windows Debug Builds
//
// This header provides platform-specific leak detection helpers.
// On Windows in Debug mode, enables CRT heap debugging and memory leak reporting.
// On other platforms, relies on compiler sanitizers (ASan/LSan).
//

#if defined(LUDUS_WINDOWS) && defined(LUDUS_DEBUG)
    #include <crtdbg.h>

    namespace ludus::core::debug
    {
        /// @brief Initialize Windows CRT leak detection
        /// 
        /// Call once at program startup (in main/WinMain).
        /// Enables:
        /// - Memory leak checking on exit
        /// - Frame pointers for better stack traces
        /// - Heap consistency checking
        LUDUS_INLINE void InitializeLeakDetection() noexcept
        {
            _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
        }

        /// @brief Break on specific allocation
        /// 
        /// Useful for debugging persistent leaks. Set the allocation number
        /// from a previous leak report.
        /// 
        /// @param allocationNumber The allocation number to break on
        LUDUS_INLINE void SetBreakOnAllocation(long allocationNumber) noexcept
        {
            _CrtSetBreakAlloc(allocationNumber);
        }

        /// @brief Dump all leaked memory to debug output
        /// 
        /// Call before program exit to generate a leak report.
        /// Output appears in the Debug Output window in Visual Studio.
        LUDUS_INLINE void DumpMemoryLeaks() noexcept
        {
            _CrtDumpMemoryLeaks();
        }

        /// @brief Check heap integrity
        /// 
        /// Validates the heap structure. Useful in tight loops or before/after
        /// critical operations to catch corruption early.
        /// 
        /// @return true if heap is valid, false if corruption detected
        LUDUS_INLINE bool IsHeapValid() noexcept
        {
            return _CrtCheckMemory() != 0;
        }
    }

#else

    namespace ludus::core::debug
    {
        // No-op implementations for non-Windows or non-Debug builds
        LUDUS_INLINE void InitializeLeakDetection() noexcept {}
        LUDUS_INLINE void SetBreakOnAllocation([[maybe_unused]] long allocationNumber) noexcept {}
        LUDUS_INLINE void DumpMemoryLeaks() noexcept {}
        LUDUS_INLINE bool IsHeapValid() noexcept { return true; }
    }

#endif
