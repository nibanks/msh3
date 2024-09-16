# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SOURCES msh3_perf.cpp)
add_executable(msh3perf ${SOURCES})
target_link_libraries(msh3perf msh3 inc warnings msquic ls-qpack msh3_headers)
install(TARGETS msh3perf EXPORT msh3 DESTINATION lib)
