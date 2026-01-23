#pragma once

//
// Memory Leak Detection Utilities for Windows Debug Builds
//
// This header provides platform-specific leak detection helpers.
// On Windows in Debug mode, enables CRT heap debugging and memory leak reporting.
// On other platforms, relies on compiler sanitizers (ASan/LSan).
//

#if defined(LUDUS_WINDOWS) && defined(LUDUS_DEBUG)
    #define _CRTDBG_MAP_ALLOC
    #include <crtdbg.h>

    namespace ludus::core::debug
    {
        /// @brief Initialize Windows CRT leak detection with enhanced reporting
        /// 
        /// Call once at program startup (in main/WinMain).
        /// Enables:
        /// - Memory leak checking on exit
        /// - File/line information for allocations  
        /// - Enhanced stack traces
        /// - Heap consistency checking
        LUDUS_INLINE void InitializeLeakDetection() noexcept
        {
            // Enable detailed memory tracking with file/line info
            int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
            flags |= _CRTDBG_ALLOC_MEM_DF;        // Enable memory allocation tracking
            flags |= _CRTDBG_LEAK_CHECK_DF;       // Check for leaks at program exit
            flags |= _CRTDBG_DELAY_FREE_MEM_DF;   // Don't actually free memory (helps catch use-after-free)
            _CrtSetDbgFlag(flags);

            // Send all debug output to the debug console and debugger
            _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_WNDW);
            _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_WNDW);
            _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_WNDW);
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

        /// @brief Dump all leaked memory to debug output with detailed information
        /// 
        /// Call before program exit to generate a comprehensive leak report.
        /// Output includes file names, line numbers, and allocation sizes.
        /// Output appears in the Debug Output window in Visual Studio.
        LUDUS_INLINE void DumpMemoryLeaks() noexcept
        {
            // Dump detailed leak information
            _CrtDumpMemoryLeaks();
            
            // Additional heap state information
            _CrtMemState state;
            _CrtMemCheckpoint(&state);
            _CrtMemDumpStatistics(&state);
        }

        /// @brief Generate a memory snapshot for leak tracking
        /// 
        /// Take a snapshot of current heap state. Use with CompareMemorySnapshots
        /// to find leaks between two points in execution.
        /// 
        /// @param snapshot Pointer to store the memory snapshot
        LUDUS_INLINE void TakeMemorySnapshot(_CrtMemState* snapshot) noexcept
        {
            _CrtMemCheckpoint(snapshot);
        }

        /// @brief Compare two memory snapshots and report differences
        /// 
        /// Useful for finding leaks that occur between two specific points.
        /// 
        /// @param oldSnapshot The earlier snapshot
        /// @param newSnapshot The later snapshot  
        LUDUS_INLINE void CompareMemorySnapshots(const _CrtMemState* oldSnapshot, const _CrtMemState* newSnapshot) noexcept
        {
            _CrtMemState diff;
            if (_CrtMemDifference(&diff, oldSnapshot, newSnapshot))
            {
                _CrtMemDumpStatistics(&diff);
            }
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
        
        /// @brief RAII helper for automatic memory leak detection and reporting
        /// 
        /// Usage: Create at the start of main() - automatically handles snapshots and reporting
        class ScopedLeakDetector final
        {
        public:
            explicit ScopedLeakDetector() noexcept
            {
                InitializeLeakDetection();
                _CrtMemCheckpoint(&mStartSnapshot);
            }

            ~ScopedLeakDetector() noexcept
            {
                _CrtMemState endSnapshot;
                _CrtMemCheckpoint(&endSnapshot);
                
                // Show memory differences between start and end
                _CrtMemState diff;
                if (_CrtMemDifference(&diff, &mStartSnapshot, &endSnapshot))
                {
                    _CrtMemDumpStatistics(&diff);
                }
                
                // Dump all memory leaks with detailed information
                _CrtDumpMemoryLeaks();
            }

            /// @brief Set breakpoint on specific allocation number
            /// @param allocationNumber The allocation number from leak report
            void BreakOnAllocation(long allocationNumber) const noexcept
            {
                _CrtSetBreakAlloc(allocationNumber);
            }

        private:
            _CrtMemState mStartSnapshot{};
        };

        // Debug macros for cleaner code
        #define LUDUS_LEAK_DETECTOR() ludus::core::debug::ScopedLeakDetector leakDetector_
        #define LUDUS_BREAK_ON_ALLOC(num) leakDetector_.BreakOnAllocation(num)
    }

#else

    namespace ludus::core::debug
    {
        // No-op implementations for non-Windows or non-Debug builds
        struct _CrtMemState {};
        
        class ScopedLeakDetector
        {
        public:
            ScopedLeakDetector() noexcept {}
            static void BreakOnAllocation([[maybe_unused]] long allocationNumber) noexcept {}
        };

        // Debug macros that do nothing in non-debug builds
        #define LUDUS_LEAK_DETECTOR() ludus::core::debug::ScopedLeakDetector leakDetector_
        #define LUDUS_BREAK_ON_ALLOC(num) leakDetector_.BreakOnAllocation(num)
        
        LUDUS_INLINE void InitializeLeakDetection() noexcept {}
        LUDUS_INLINE void SetBreakOnAllocation([[maybe_unused]] long allocationNumber) noexcept {}
        LUDUS_INLINE void DumpMemoryLeaks() noexcept {}
        LUDUS_INLINE bool IsHeapValid() noexcept { return true; }
        LUDUS_INLINE void TakeMemorySnapshot([[maybe_unused]] _CrtMemState* snapshot) noexcept {}
        LUDUS_INLINE void CompareMemorySnapshots([[maybe_unused]] const _CrtMemState* oldSnapshot, [[maybe_unused]] const _CrtMemState* newSnapshot) noexcept {}
    }

#endif
