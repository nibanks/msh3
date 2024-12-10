# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

if (WIN32)
    set(SOURCES msh3.cpp msh3.rc)
else()
    set(SOURCES msh3.cpp)
endif()
add_library(msh3 SHARED ${SOURCES})
target_link_libraries(msh3 PRIVATE inc warnings msquic ls-qpack msh3_headers)
if (NOT BUILD_SHARED_LIBS)
    target_link_libraries(msh3 PRIVATE base_link)
endif()
target_link_libraries(msh3 PRIVATE msquic_platform) # For selfsign APIs

if(WIN32)
    SET_TARGET_PROPERTIES(msh3
        PROPERTIES LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/win32/msh3.def\"")
elseif (CX_PLATFORM STREQUAL "linux")
    SET_TARGET_PROPERTIES(msh3
        PROPERTIES LINK_FLAGS "-Wl,--version-script=\"${CMAKE_CURRENT_SOURCE_DIR}/linux/exports.txt\"")
elseif (CX_PLATFORM STREQUAL "darwin")
    SET_TARGET_PROPERTIES(msh3
        PROPERTIES LINK_FLAGS "-exported_symbols_list \"${CMAKE_CURRENT_SOURCE_DIR}/darwin/exports.txt\"")
endif()

install(TARGETS msh3 EXPORT msh3 DESTINATION lib)
install(FILES ../msh3.h DESTINATION include)
configure_file(msh3-config.cmake.in ${CMAKE_BINARY_DIR}/msh3-config.cmake)
install(FILES ${CMAKE_BINARY_DIR}/msh3-config.cmake DESTINATION share/msh3)
install(EXPORT msh3 DESTINATION share/msh3)
set(prefix ${CMAKE_INSTALL_PREFIX})
configure_file(libmsh3.pc.in ${CMAKE_BINARY_DIR}/libmsh3.pc @ONLY)
if(NOT WIN32)
    install(FILES ${CMAKE_BINARY_DIR}/libmsh3.pc DESTINATION lib/pkgconfig)
endif()
