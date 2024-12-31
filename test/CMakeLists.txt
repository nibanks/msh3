# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SOURCES msh3test.cpp)
add_executable(msh3test ${SOURCES})
target_link_libraries(msh3test msh3 msh3_headers)
install(TARGETS msh3test EXPORT msh3 DESTINATION lib)
