#include "../cpptools/misc/TEST.hpp"
#include "../cpptools/misc/ConsoleLogger.hpp"

#ifdef TEST
#include "test_TcpServer.hpp"
#include "test_HttpServer.hpp"
#include "test_TcpClientNB.hpp"
#include "test_TcpProxy.hpp"
#endif // TEST

int main(int argc, char** argv) {
    createLogger<ConsoleLogger>();
    Arguments args(argc, argv);
    tester.run(args);
}
