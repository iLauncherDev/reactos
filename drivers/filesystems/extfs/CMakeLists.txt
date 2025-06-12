
list(APPEND SOURCE
    extfs.c
    disk.c
    dispatch.c
    superblock_check.c
    inode.c
    inode-data.c
    directory.c)

add_library(extfs MODULE ${SOURCE} extfs.rc)
set_module_type(extfs kernelmodedriver)
target_link_libraries(extfs ${PSEH_LIB})
add_definitions(-D__KERNEL__)
add_importlibs(extfs ntoskrnl hal)
add_cd_file(TARGET extfs DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(extfs_reg.inf)
