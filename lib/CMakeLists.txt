# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SOURCES msh3.cpp)
add_library(msh3 SHARED ${SOURCES})
target_link_libraries(msh3 inc warnings msquic ls-qpack msh3_headers)

install(TARGETS msh3 EXPORT msh3 DESTINATION lib)
install(FILES ../msh3.h DESTINATION include)
#install(EXPORT msh3 DESTINATION share/msh3)
