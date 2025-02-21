#[[
This file is part of the Ristra portage project.
Please see the license file at the root of this repository, or at:
    https://github.com/laristra/portage/blob/master/LICENSE
]]
#-----------------------------------------------------------------------------~#

project(portage_driver)

add_library(portage_driver INTERFACE)

target_include_directories(portage_driver INTERFACE
  $<BUILD_INTERFACE:${portage_SOURCE_DIR}>
  $<BUILD_INTERFACE:${portage_BINARY_DIR}>
  $<INSTALL_INTERFACE:include>
  )

set(portage_driver_HEADERS
  mmdriver.h
  fix_mismatch.h
  coredriver.h
  uberdriver.h
  parts.h
  driver_swarm.h
  driver_mesh_swarm_mesh.h)

if (PORTAGE_HAS_TANGRAM)
  list(APPEND portage_driver_headers
    write_to_gmv.h
    )
endif ()


# Not yet allowed for INTERFACE libraries
# 
# set_target_properties(portage_driver PROPERTIES
#   PUBLIC_HEADER "${portage_driver_HEADERS}")
#
# Directly install files instead
#
install(FILES ${portage_driver_HEADERS} DESTINATION include/portage/driver)

target_link_libraries(portage_driver INTERFACE portage_support)
target_link_libraries(portage_driver INTERFACE portage_search)
target_link_libraries(portage_driver INTERFACE portage_distributed)
target_link_libraries(portage_driver INTERFACE portage_accumulate)
target_link_libraries(portage_driver INTERFACE portage_estimate)
target_link_libraries(portage_driver INTERFACE portage_intersect)
target_link_libraries(portage_driver INTERFACE portage_interpolate)

if (PORTAGE_HAS_TANGRAM)
  target_link_libraries(portage_driver INTERFACE ${TANGRAM_LIBRARIES})
endif ()

target_link_libraries(portage INTERFACE portage_driver)

install(TARGETS portage_driver
  EXPORT portage_LIBRARIES
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include/portage/driver
  INCLUDES DESTINATION include/portage/driver
  )



# Unit tests

if (ENABLE_UNIT_TESTS)

   if (WONTON_ENABLE_Jali)
       portage_add_unittest(test_driver
       SOURCES test/test_driver.cc 
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)

       portage_add_unittest(test_driver_multivar
       SOURCES test/test_driver_multivar.cc 
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)

       portage_add_unittest(test_mismatch_fixup
       SOURCES test/test_driver_mismatch_fixup.cc 
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)

       portage_add_unittest(test_mismatch_disjoint
       SOURCES test/test_driver_mismatch_disjoint.cc
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)

       portage_add_unittest(test_driver_core_mismatch_fixup
       SOURCES test/test_driver_core_mismatch_fixup 
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)

       portage_add_unittest(test_driver_swarm_distributed
       SOURCES test/test_driver_swarm_distributed.cc
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 4)

     if (PORTAGE_HAS_TANGRAM)

       portage_add_unittest(test_driver_multimat
         SOURCES test/test_driver_multimat.cc
         LIBRARIES portage_driver ${TANGRAM_LIBRARIES}
         POLICY MPI
         THREADS 1)

       portage_add_unittest(test_driver_multimat_second_order
         SOURCES test/test_driver_multimat_second_order.cc
         LIBRARIES portage_driver ${TANGRAM_LIBRARIES}
         POLICY MPI
         THREADS 1)

       portage_add_unittest(test_driver_multimat_uber
         SOURCES test/test_driver_multimat_uber.cc
         LIBRARIES portage_driver ${TANGRAM_LIBRARIES}
         POLICY MPI
         THREADS 1)

       if (TANGRAM_ENABLE_XMOF2D)
	 portage_add_unittest(test_driver_multimat_uber_swept
           SOURCES test/test_driver_multimat_uber_swept.cc
           LIBRARIES portage_driver ${TANGRAM_LIBRARIES}
           POLICY MPI
           THREADS 1)

	 portage_add_unittest(test_driver_multimat_uber_swept_mismatch
           SOURCES test/test_driver_multimat_uber_swept_mismatch.cc
           LIBRARIES portage_driver ${TANGRAM_LIBRARIES}
           POLICY MPI
           THREADS 1)
       endif ()

       portage_add_unittest(test_driver_multimat_distributed
       SOURCES test/test_driver_multimat_distributed.cc
       LIBRARIES portage_driver ${TANGRAM_LIBRARIES}
       POLICY MPI
       THREADS 4)

       portage_add_unittest(test_driver_multimat_uber_distributed
       SOURCES test/test_driver_multimat_uber_distributed
       LIBRARIES portage_driver ${TANGRAM_LIBRARIES}
       POLICY MPI
       THREADS 4)

       portage_add_unittest(test_driver_reverse_weights
         SOURCES test/test_driver_reverse_weights.cc
         LIBRARIES portage_driver ${TANGRAM_LIBRARIES}
         POLICY MPI
         THREADS 4)
     endif (PORTAGE_HAS_TANGRAM)

     portage_add_unittest(test_driver_core_single_mat_serial
       SOURCES test/test_driver_core_single_mat.cc
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)

     portage_add_unittest(test_driver_core_single_mat_parallel
       SOURCES test/test_driver_core_single_mat.cc
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 4)

     portage_add_unittest(test_driver_part
       SOURCES test/test_driver_part.cc
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)

     portage_add_unittest(test_driver_part_mismatch
       SOURCES test/test_driver_part_mismatch.cc
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)

     portage_add_unittest(test_driver_swarm
       SOURCES test/test_driver_swarm.cc
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)

     portage_add_unittest(test_driver_mesh_swarm_mesh
       SOURCES test/test_driver_mesh_swarm_mesh.cc
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)

     portage_add_unittest(test_driver_part_by_particle
       SOURCES test/test_driver_part_by_particle
       LIBRARIES portage_driver
       POLICY MPI
       THREADS 1)
  endif (WONTON_ENABLE_Jali)

endif (ENABLE_UNIT_TESTS)
