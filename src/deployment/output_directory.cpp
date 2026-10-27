#include "deployment/output_directory.hpp"

#include <cerrno>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace osrm::deployment
{
struct OutputDirectory::Lock
{
#ifdef _WIN32
    HANDLE Handle = INVALID_HANDLE_VALUE;
    ~Lock()
    {
        if (Handle != INVALID_HANDLE_VALUE)
            CloseHandle(Handle);
    }
#else
    int Descriptor = -1;
    ~Lock()
    {
        if (Descriptor >= 0)
            close(Descriptor);
    }
#endif
};

OutputDirectory::OutputDirectory(std::unique_ptr<Lock> lock) : m_lock(std::move(lock)) {}
OutputDirectory::OutputDirectory(OutputDirectory &&) noexcept = default;
OutputDirectory &OutputDirectory::operator=(OutputDirectory &&) noexcept = default;
OutputDirectory::~OutputDirectory() = default;

std::optional<OutputDirectory> OutputDirectory::acquire(const std::filesystem::path &path,
                                                        std::string &error)
{
    error.clear();
    try
    {
        const auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
        std::filesystem::create_directories(canonical.parent_path());
        const auto lock_path =
            canonical.parent_path() / ("." + canonical.filename().string() + ".prepare.lock");
        auto lock = std::make_unique<Lock>();
#ifdef _WIN32
        lock->Handle = CreateFileW(lock_path.c_str(),
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr,
                                   OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
        OVERLAPPED overlapped{};
        const bool locked = lock->Handle != INVALID_HANDLE_VALUE &&
                            LockFileEx(lock->Handle,
                                       LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                                       0,
                                       MAXDWORD,
                                       MAXDWORD,
                                       &overlapped);
#else
        lock->Descriptor = open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        const bool locked =
            lock->Descriptor >= 0 && flock(lock->Descriptor, LOCK_EX | LOCK_NB) == 0;
#endif
        if (!locked)
        {
            error = "Cannot acquire output lock (busy or inaccessible): " + lock_path.string();
            return std::nullopt;
        }
        std::filesystem::create_directories(canonical);
        if (!std::filesystem::is_empty(canonical))
        {
            error = "Output directory is not empty: " + canonical.string();
            return std::nullopt;
        }
        return OutputDirectory(std::move(lock));
    }
    catch (const std::filesystem::filesystem_error &exception)
    {
        error = exception.what();
        return std::nullopt;
    }
}
} // namespace osrm::deployment
