#include "DirectoryWatcherWin.h"

#include <filesystem>
#include <thread>
#include <atomic>
#include <array>
#include <cstddef>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace sw
{

    class DirectoryWatcherWin::Impl
    {
    public:
        Impl(std::string path,
             ChangeCallback callback)
            : watchedDirectoryPath(std::move(path)),
              onChange(std::move(callback))
        {
#ifdef _WIN32
            stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#endif
        }

        ~Impl()
        {
            stop();

#ifdef _WIN32
            if (stopEvent != nullptr)
                CloseHandle(stopEvent);
#endif
        }

        bool start()
        {
#ifdef _WIN32
            if (worker.joinable())
                return true;

            if (stopEvent == nullptr || watchedDirectoryPath.empty())
                return false;

            ResetEvent(stopEvent);
            stopRequested.store(false, std::memory_order_release);
            worker = std::thread([this]()
                                 { run(); });
            return true;
#else
            return false;
#endif
        }

        void stop()
        {
#ifdef _WIN32
            stopRequested.store(true, std::memory_order_release);

            if (stopEvent != nullptr)
                SetEvent(stopEvent);

            HANDLE activeDirectoryHandle = INVALID_HANDLE_VALUE;
            {
                std::lock_guard<std::mutex> lock(directoryHandleMutex);
                activeDirectoryHandle = directoryHandle;
            }

            if (activeDirectoryHandle != INVALID_HANDLE_VALUE)
                CancelIoEx(activeDirectoryHandle, nullptr);

            if (worker.joinable())
                worker.join();
#endif
        }

        const std::string &directoryPath() const noexcept
        {
            return watchedDirectoryPath;
        }

    private:
#ifdef _WIN32
        static constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME |
                                               FILE_NOTIFY_CHANGE_DIR_NAME |
                                               FILE_NOTIFY_CHANGE_SIZE |
                                               FILE_NOTIFY_CHANGE_LAST_WRITE |
                                               FILE_NOTIFY_CHANGE_CREATION;

        void run()
        {
            const std::filesystem::path path(watchedDirectoryPath);
            if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path))
                return;

            const auto widePath = path.wstring();
            HANDLE localDirectoryHandle = CreateFileW(widePath.c_str(),
                                                      FILE_LIST_DIRECTORY,
                                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                      nullptr,
                                                      OPEN_EXISTING,
                                                      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                                      nullptr);
            if (localDirectoryHandle == INVALID_HANDLE_VALUE)
                return;

            {
                std::lock_guard<std::mutex> lock(directoryHandleMutex);
                directoryHandle = localDirectoryHandle;
            }

            HANDLE changeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (changeEvent == nullptr)
            {
                CloseHandle(localDirectoryHandle);
                std::lock_guard<std::mutex> lock(directoryHandleMutex);
                directoryHandle = INVALID_HANDLE_VALUE;
                return;
            }

            OVERLAPPED overlapped{};
            overlapped.hEvent = changeEvent;
            std::array<std::byte, 64 * 1024> buffer{};
            HANDLE waitHandles[] = {stopEvent, changeEvent};

            while (!stopRequested.load(std::memory_order_acquire))
            {
                ResetEvent(changeEvent);

                if (!ReadDirectoryChangesW(localDirectoryHandle,
                                           buffer.data(),
                                           static_cast<DWORD>(buffer.size()),
                                           TRUE,
                                           kNotifyFilter,
                                           nullptr,
                                           &overlapped,
                                           nullptr))
                {
                    if (stopRequested.load(std::memory_order_acquire))
                        break;

                    break;
                }

                const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                if (waitResult == WAIT_OBJECT_0)
                {
                    CancelIoEx(localDirectoryHandle, &overlapped);
                    break;
                }

                if (waitResult != (WAIT_OBJECT_0 + 1))
                    break;

                DWORD bytesTransferred = 0;
                if (!GetOverlappedResult(localDirectoryHandle, &overlapped, &bytesTransferred, FALSE))
                {
                    if (stopRequested.load(std::memory_order_acquire))
                        break;

                    continue;
                }

                if (bytesTransferred > 0 && onChange)
                    onChange();
            }

            CloseHandle(changeEvent);
            CloseHandle(localDirectoryHandle);

            std::lock_guard<std::mutex> lock(directoryHandleMutex);
            directoryHandle = INVALID_HANDLE_VALUE;
        }

        HANDLE stopEvent = nullptr;
        HANDLE directoryHandle = INVALID_HANDLE_VALUE;
        std::mutex directoryHandleMutex;
#endif

        std::string watchedDirectoryPath;
        ChangeCallback onChange;
        std::thread worker;
        std::atomic<bool> stopRequested{false};
    };

    DirectoryWatcherWin::DirectoryWatcherWin(const std::string &directoryPath,
                                             ChangeCallback onChange)
        : impl(std::make_unique<Impl>(directoryPath, std::move(onChange)))
    {
    }

    DirectoryWatcherWin::~DirectoryWatcherWin() = default;

    bool DirectoryWatcherWin::start()
    {
        return impl->start();
    }

    void DirectoryWatcherWin::stop()
    {
        impl->stop();
    }

    const std::string &DirectoryWatcherWin::directoryPath() const noexcept
    {
        return impl->directoryPath();
    }

} // namespace sw