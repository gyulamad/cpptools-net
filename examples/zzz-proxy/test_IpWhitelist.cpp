#define TEST
#define TEST_SHOW_DETAILS

#include "../../../misc/TEST.hpp"
#include "../../IpWhitelist.hpp"

int main(int argc, char** argv) {
    createLogger<ConsoleLogger>();
    Arguments args(argc, argv);
    tester.run(args);
}
