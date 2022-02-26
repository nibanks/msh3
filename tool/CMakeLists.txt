# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SOURCES msh3_app.cpp)
add_executable(msh3app ${SOURCES})
target_link_libraries(msh3app msh3 inc warnings msquic ls-qpack msh3_headers)
install(TARGETS msh3app EXPORT msh3 DESTINATION lib)
