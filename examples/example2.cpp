#include "malog.h"

int main(int argc, char** argv) {
    MALOG_OPEN("logs.txt", 1, 32, true);
    malog_info("this is long long long long long long long long long long %s long long long long long long %s, %s",
               "stringgggggggggggggggggggggggggggggggggggggggggggggggggggg1",
               "stringgggggggggggggggggggggggggggggggggggggggggggggggggggg2",
               "stringgggggggggggggggggggggggggggggggggggggggggggggggggggg3");
    return 0;
}
