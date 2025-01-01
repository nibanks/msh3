# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SOURCES msh3test.cpp)
add_executable(msh3test ${SOURCES})
target_link_libraries(msh3test msh3)
install(TARGETS msh3test EXPORT msh3 RUNTIME DESTINATION bin)
