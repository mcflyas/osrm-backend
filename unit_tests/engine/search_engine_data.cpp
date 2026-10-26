#include "engine/search_engine_data.hpp"

#include <boost/test/unit_test.hpp>

#include <memory>

namespace
{
using MLD = osrm::engine::routing_algorithms::mld::Algorithm;
using CH = osrm::engine::routing_algorithms::ch::Algorithm;
using WorkingData = osrm::engine::SearchEngineData<MLD>;
const osrm::engine::MLDUnpackingCacheKey key{1, 2, 1, 0};

void seedCache(WorkingData &data)
{
    data.InitializeUnpackingCache(100000, 200000);
    data.unpacking_cache->insert(key, {{1, 2}, {3}});
    BOOST_REQUIRE(data.unpacking_cache->get(key));
}
} // namespace

BOOST_AUTO_TEST_SUITE(search_engine_data)

BOOST_AUTO_TEST_CASE(same_owner_preserves_cache)
{
    WorkingData data;
    const auto dataset = std::make_shared<int>(1);
    data.selectDataset(dataset);
    seedCache(data);
    data.selectDataset(dataset);
    BOOST_REQUIRE(data.unpacking_cache);
    BOOST_CHECK(data.unpacking_cache->get(key));
}

BOOST_AUTO_TEST_CASE(different_owner_clears_cache)
{
    WorkingData data;
    const auto first = std::make_shared<int>(1);
    const auto second = std::make_shared<int>(1);
    data.selectDataset(first);
    seedCache(data);
    data.InitializeOrClearFirstThreadLocalStorage(100, 50);
    data.selectDataset(second);
    BOOST_CHECK(!data.unpacking_cache);
    BOOST_CHECK(!data.forward_heap_1);
    BOOST_CHECK(!data.reverse_heap_1);
    data.InitializeUnpackingCache(100000, 200000);
    BOOST_CHECK(!data.unpacking_cache->get(key));
}

BOOST_AUTO_TEST_CASE(expired_owner_clears_cache_without_retaining_dataset)
{
    WorkingData data;
    std::weak_ptr<int> weak;
    {
        const auto dataset = std::make_shared<int>(1);
        weak = dataset;
        data.selectDataset(dataset);
        seedCache(data);
    }
    BOOST_CHECK(weak.expired());
    data.selectDataset(std::make_shared<int>(1));
    BOOST_CHECK(!data.unpacking_cache);
}

BOOST_AUTO_TEST_CASE(ch_heap_lifetime_follows_dataset)
{
    osrm::engine::SearchEngineData<CH> data;
    const auto first = std::make_shared<int>(1);
    data.selectDataset(first);
    data.InitializeOrClearFirstThreadLocalStorage(100);
    const auto *heap = data.forward_heap_1.get();
    data.selectDataset(first);
    BOOST_CHECK(data.forward_heap_1.get() == heap);
    data.selectDataset(std::make_shared<int>(2));
    BOOST_CHECK(!data.forward_heap_1);
}

BOOST_AUTO_TEST_SUITE_END()
