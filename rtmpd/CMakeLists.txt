set(LIBRARIES coros malog ${DEPENDENT_LIBRARIES})
add_executable(rtmpd rtmpd.cpp)
target_link_libraries(rtmpd ${LIBRARIES})
