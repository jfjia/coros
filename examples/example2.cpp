#include "log.hpp"

int main(int argc, char** argv) {
    LOG_OPEN("logs.txt", 1, 32, coros::log::POLICY_WAIT);
    log_info("this is long long long long long long long long long long %s long long long long long long %s, %s",
             "stringgggggggggggggggggggggggggggggggggggggggggggggggggggg1",
             "stringgggggggggggggggggggggggggggggggggggggggggggggggggggg2",
             "stringgggggggggggggggggggggggggggggggggggggggggggggggggggg3");
    return 0;
}
