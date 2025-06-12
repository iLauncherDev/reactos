set(EXTFS_BASE_SOURCE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(EXTFS_BASE_BUILD_PATH ${CMAKE_CURRENT_BINARY_DIR})

include(${EXTFS_BASE_SOURCE_PATH}/extfs.cmake)

add_definitions(-D__KERNEL__)
add_library(extfs MODULE ${EXTFS_SOURCE} ${CMAKE_CURRENT_BINARY_DIR}/extfs.rc)
set_module_type(extfs kernelmodedriver)
add_importlibs(extfs ntoskrnl hal)
add_cd_file(TARGET extfs DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_registry_inf(extfs_reg.inf)
