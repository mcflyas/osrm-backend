#include "shared_memory.hpp"

#include <iostream>

int main(int argc, char *argv[])
{
    if (argc != 2)
        return 1;
    try
    {
        removeTestDatasets(argv[1]);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
