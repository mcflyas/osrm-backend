#ifndef OSRM_DEPLOYMENT_OUTPUT_DIRECTORY_HPP
#define OSRM_DEPLOYMENT_OUTPUT_DIRECTORY_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace osrm::deployment
{
class OutputDirectory
{
  public:
    static std::optional<OutputDirectory> acquire(const std::filesystem::path &path,
                                                  std::string &error);
    OutputDirectory(OutputDirectory &&) noexcept;
    OutputDirectory &operator=(OutputDirectory &&) noexcept;
    ~OutputDirectory();

  private:
    struct Lock;
    explicit OutputDirectory(std::unique_ptr<Lock> lock);
    std::unique_ptr<Lock> m_lock;
};
} // namespace osrm::deployment
#endif
