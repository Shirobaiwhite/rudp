cmake_minimum_required(VERSION 3.20)
project(reliable-udp LANGUAGES CXX)

# The library. Modern target-based CMake: every requirement hangs off the
# target, nothing global. PUBLIC on the include dir and the C++ standard
# means consumers linking `rudp` inherit both automatically; the warning
# flags are PRIVATE because our warning policy should never leak into
# consumers' builds.
add_library(rudp
    src/socket.cpp
    src/packet.cpp
    src/connection.cpp
)
target_compile_features(rudp PUBLIC cxx_std_20)
target_include_directories(rudp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(rudp PRIVATE -Wall -Wextra -Wpedantic)

add_executable(sender examples/sender.cpp)
target_link_libraries(sender PRIVATE rudp)
target_compile_options(sender PRIVATE -Wall -Wextra -Wpedantic)

add_executable(receiver examples/receiver.cpp)
target_link_libraries(receiver PRIVATE rudp)
target_compile_options(receiver PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(drop_test tests/drop_test.cpp)
target_link_libraries(drop_test PRIVATE rudp)
target_compile_options(drop_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME drop_test COMMAND drop_test)
