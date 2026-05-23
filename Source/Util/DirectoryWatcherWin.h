#pragma once

#include <functional>
#include <memory>
#include <string>

namespace sw
{

    class DirectoryWatcherWin
    {
    public:
        using ChangeCallback = std::function<void()>;

        DirectoryWatcherWin(const std::string &directoryPath,
                            ChangeCallback onChange);
        ~DirectoryWatcherWin();

        DirectoryWatcherWin(const DirectoryWatcherWin &) = delete;
        DirectoryWatcherWin &operator=(const DirectoryWatcherWin &) = delete;

        bool start();
        void stop();

        const std::string &directoryPath() const noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> impl;
    };

} // namespace sw