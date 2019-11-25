set(LIBRARIES coros malog ${DEPENDENT_LIBRARIES})
set(RTMPD_SRCS conn.cpp
               server.cpp
               main.cpp)
add_executable(rtmpd ${RTMPD_SRCS})
target_link_libraries(rtmpd ${LIBRARIES})
