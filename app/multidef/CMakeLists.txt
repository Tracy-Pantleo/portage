#[[
This file is part of the Ristra Portage project.
Please see the license file at the root of this repository, or at:
    https://github.com/laristra/portage/blob/master/LICENSE
]]

add_executable(portage_multidef_check main.cc dummy.cc)
target_link_libraries(portage_multidef_check PRIVATE portage)

if (ENABLE_APP_TESTS)
  add_subdirectory(test)
endif (ENABLE_APP_TESTS)


