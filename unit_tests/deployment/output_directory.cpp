#include "deployment/output_directory.hpp"

#include "fixture.hpp"

#include <boost/test/unit_test.hpp>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

BOOST_FIXTURE_TEST_SUITE(deployment_output_directory, DeploymentFixture)

BOOST_AUTO_TEST_CASE(fresh_empty_and_nonempty_outputs)
{
    std::string error;
    const auto output = Directory / "output folder";
    {
        auto owner = osrm::deployment::OutputDirectory::acquire(output, error);
        BOOST_REQUIRE_MESSAGE(owner, error);
        BOOST_CHECK(std::filesystem::is_empty(output));
        auto contender = osrm::deployment::OutputDirectory::acquire(output, error);
        BOOST_CHECK(!contender);
    }
    BOOST_CHECK(osrm::deployment::OutputDirectory::acquire(output, error));
    write("output folder/valuable.txt", "preserve me");
    BOOST_CHECK(!osrm::deployment::OutputDirectory::acquire(output, error));
    BOOST_CHECK_EQUAL(std::filesystem::file_size(output / "valuable.txt"), 11);
}

#ifndef _WIN32
BOOST_AUTO_TEST_CASE(symlinked_parents_use_the_same_lock)
{
    std::string error;
    std::filesystem::create_directory(Directory / "real");
    std::filesystem::create_directory_symlink(Directory / "real", Directory / "alias");
    const auto owner = osrm::deployment::OutputDirectory::acquire(Directory / "real/out", error);
    BOOST_REQUIRE(owner);
    BOOST_CHECK(!osrm::deployment::OutputDirectory::acquire(Directory / "alias/out", error));
}

BOOST_AUTO_TEST_CASE(lock_is_released_when_owner_process_exits)
{
    int ready[2], finish[2];
    BOOST_REQUIRE_EQUAL(pipe(ready), 0);
    BOOST_REQUIRE_EQUAL(pipe(finish), 0);
    const auto pid = fork();
    BOOST_REQUIRE_GE(pid, 0);
    if (pid == 0)
    {
        close(ready[0]);
        close(finish[1]);
        std::string error;
        const auto owner = osrm::deployment::OutputDirectory::acquire(Directory / "out", error);
        const char status = owner ? '1' : '0';
        const auto written = ::write(ready[1], &status, 1);
        char command;
        const auto read_count = ::read(finish[0], &command, 1);
        _exit(written == 1 && read_count == 1 && owner ? 0 : 1);
    }
    close(ready[1]);
    close(finish[0]);
    char status = '0';
    BOOST_CHECK_EQUAL(::read(ready[0], &status, 1), 1);
    BOOST_CHECK_EQUAL(status, '1');
    std::string error;
    BOOST_CHECK(!osrm::deployment::OutputDirectory::acquire(Directory / "out", error));
    BOOST_CHECK_EQUAL(::write(finish[1], "x", 1), 1);
    int child_status;
    BOOST_CHECK_EQUAL(waitpid(pid, &child_status, 0), pid);
    BOOST_CHECK_EQUAL(child_status, 0);
    close(ready[0]);
    close(finish[1]);
    BOOST_CHECK(osrm::deployment::OutputDirectory::acquire(Directory / "out", error));
}
#endif

BOOST_AUTO_TEST_SUITE_END()
