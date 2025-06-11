# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SOURCES h3ping.cpp)
add_executable(h3ping ${SOURCES})
target_link_libraries(h3ping msh3)
install(TARGETS h3ping EXPORT msh3 RUNTIME DESTINATION bin)