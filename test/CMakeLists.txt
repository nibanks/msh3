# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SOURCES msh3test.cpp)
add_executable(msh3test ${SOURCES})
find_package(Threads REQUIRED)
target_link_libraries(msh3test msh3 ${CMAKE_THREAD_LIBS_INIT})
install(TARGETS msh3test EXPORT msh3 RUNTIME DESTINATION bin)
