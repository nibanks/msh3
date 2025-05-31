# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SOURCES msh3perf.cpp)
add_executable(msh3perf ${SOURCES})
target_link_libraries(msh3perf msh3)
install(TARGETS msh3perf EXPORT msh3 RUNTIME DESTINATION bin)
