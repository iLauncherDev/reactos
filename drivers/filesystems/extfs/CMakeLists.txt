
list(APPEND SOURCE
    extfs.c
    utils.c
    cache.c
    fastio.c
    disk.c
    dispatch.c
    create.c
    close.c
    lockctrl.c
    read.c
    fileinfo.c
    fsctrl.c
    superblock_check.c
    inode.c
    inode-data.c
    directory.c)

add_definitions(-D__KERNEL__)
add_library(extfs MODULE ${SOURCE} extfs.rc)
set_module_type(extfs kernelmodedriver)
target_link_libraries(extfs ${PSEH_LIB})
add_importlibs(extfs ntoskrnl hal)
add_cd_file(TARGET extfs DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_registry_inf(extfs_reg.inf)
