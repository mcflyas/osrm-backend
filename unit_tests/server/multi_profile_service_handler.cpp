#include "server/multi_profile_service_handler.hpp"

#include "server/api/parsed_url.hpp"
#include "util/json_container.hpp"

#include <boost/test/unit_test.hpp>

namespace
{
struct RecordingHandler final : osrm::server::ServiceHandlerInterface
{
    osrm::engine::Status RunQuery(osrm::server::api::ParsedURL url,
                                  osrm::engine::api::ResultT &) override
    {
        URL = std::move(url);
        ++Gets;
        return osrm::engine::Status::Ok;
    }
    osrm::engine::Status RunQuery(osrm::server::api::ParsedURL url,
                                  const std::string &body,
                                  osrm::engine::api::ResultT &) override
    {
        URL = std::move(url);
        Body = body;
        ++Posts;
        return osrm::engine::Status::Ok;
    }
    osrm::server::api::ParsedURL URL;
    std::string Body;
    unsigned Gets = 0;
    unsigned Posts = 0;
};
} // namespace

BOOST_AUTO_TEST_SUITE(multi_profile_service_handler)
BOOST_AUTO_TEST_CASE(dispatch_preserves_request_and_has_no_fallback)
{
    auto car = std::make_unique<RecordingHandler>();
    auto foot = std::make_unique<RecordingHandler>();
    auto *car_record = car.get();
    auto *foot_record = foot.get();
    std::unordered_map<std::string, std::unique_ptr<osrm::server::ServiceHandlerInterface>>
        handlers;
    handlers.emplace("car", std::move(car));
    handlers.emplace("foot", std::move(foot));
    osrm::server::MultiProfileServiceHandler handler(std::move(handlers));
    osrm::engine::api::ResultT result;
    const osrm::server::api::ParsedURL url{"route", 7, "foot", "1,2;3,4?steps=true", 19};
    BOOST_CHECK(handler.RunQuery(url, result) == osrm::engine::Status::Ok);
    BOOST_CHECK_EQUAL(foot_record->Gets, 1);
    BOOST_CHECK_EQUAL(car_record->Gets, 0);
    BOOST_CHECK_EQUAL(foot_record->URL.service, url.service);
    BOOST_CHECK_EQUAL(foot_record->URL.version, url.version);
    BOOST_CHECK_EQUAL(foot_record->URL.profile, url.profile);
    BOOST_CHECK_EQUAL(foot_record->URL.query, url.query);
    BOOST_CHECK_EQUAL(foot_record->URL.prefix_length, url.prefix_length);
    auto post = url;
    post.profile = "car";
    const std::string body = R"({"coordinates":[[1,2],[3,4]]})";
    BOOST_CHECK(handler.RunQuery(post, body, result) == osrm::engine::Status::Ok);
    BOOST_CHECK_EQUAL(car_record->Posts, 1);
    BOOST_CHECK_EQUAL(foot_record->Posts, 0);
    BOOST_CHECK_EQUAL(car_record->Body, body);
    BOOST_CHECK_EQUAL(car_record->URL.service, post.service);
    BOOST_CHECK_EQUAL(car_record->URL.version, post.version);
    BOOST_CHECK_EQUAL(car_record->URL.query, post.query);
    BOOST_CHECK_EQUAL(car_record->URL.prefix_length, post.prefix_length);
    post.profile = "missing";
    for (bool is_post : {false, true})
    {
        const auto status =
            is_post ? handler.RunQuery(post, body, result) : handler.RunQuery(post, result);
        BOOST_CHECK(status == osrm::engine::Status::Error);
        BOOST_CHECK_EQUAL(std::get<osrm::util::json::String>(
                              std::get<osrm::util::json::Object>(result).values.at("code"))
                              .value,
                          "InvalidProfile");
    }
    BOOST_CHECK_EQUAL(car_record->Posts, 1);
    BOOST_CHECK_EQUAL(foot_record->Gets, 1);
}
BOOST_AUTO_TEST_SUITE_END()
