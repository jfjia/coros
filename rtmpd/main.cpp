#include "malog.h"
#include "server.hpp"
#include "conn.hpp"

int main(int argc, char** argv) {
  MALOG_OPEN_STDIO(1, true);
  coros::Scheduler sched(true);
  Server s;
  s.Start();
  sched.Run();
  return 0;
}
