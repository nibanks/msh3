# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SOURCES msh3.cpp)
add_library(msh3 ${SOURCES})
target_link_libraries(msh3 inc warnings msquic ls-qpack msh3_headers)
