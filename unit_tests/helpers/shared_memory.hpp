#ifndef OSRM_TEST_SHARED_MEMORY_HPP
#define OSRM_TEST_SHARED_MEMORY_HPP

#include "storage/shared_datatype.hpp"
#include "storage/shared_memory.hpp"
#include "storage/shared_monitor.hpp"

#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include <chrono>
#include <fstream>

inline void removeTestDatasets(const std::string &prefix)
{
    if (prefix.find("osrm-test-") != 0)
        throw std::runtime_error("Refusing non-test namespace");
    using Monitor = osrm::storage::SharedMonitor<osrm::storage::SharedRegionRegister>;
    if (!Monitor::exists())
        return;
    const auto lock_path = osrm::storage::getLockDir() / "osrm-datastore.lock";
    if (!std::filesystem::exists(lock_path))
        std::ofstream{lock_path};
    boost::interprocess::file_lock writer(lock_path.string().c_str());
    boost::interprocess::scoped_lock writer_lock(writer);
    Monitor monitor;
    boost::interprocess::scoped_lock lock(monitor.get_mutex());
    std::vector<std::string> names;
    monitor.data().List(std::back_inserter(names));
    for (const auto &name : names)
    {
        if (name.find(prefix + "/") != 0)
            continue;
        auto &region = monitor.data().GetRegion(monitor.data().Find(name));
        if (osrm::storage::RegionExists(region.proj_id))
            osrm::storage::Remove(region.proj_id);
        monitor.data().ReleaseKey(region.proj_id);
        region = osrm::storage::SharedRegion{};
    }
}

struct SharedMemoryFixture
{
    std::string Namespace =
        "osrm-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    ~SharedMemoryFixture() { removeTestDatasets(Namespace); }
};
#endif
