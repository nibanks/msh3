# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(SOURCES msh3_app.cpp)
add_executable(msh3app ${SOURCES})
target_link_libraries(msh3app msh3)
install(TARGETS msh3app EXPORT msh3 RUNTIME DESTINATION bin)
