set(EXTFS_FSD_PATH "${EXTFS_BASE_SOURCE_PATH}/fsd")
set(EXTFS_CORE_PATH "${EXTFS_BASE_SOURCE_PATH}/core")
set(EXTFS_INCLUDE_PATH "${EXTFS_BASE_SOURCE_PATH}/include")

list(APPEND EXTFS_CORE_SOURCE
    ${EXTFS_CORE_PATH}/allocation-entry.c
    ${EXTFS_CORE_PATH}/allocation-manager.c

    ${EXTFS_CORE_PATH}/directory.c

    ${EXTFS_CORE_PATH}/inode-data.c
    ${EXTFS_CORE_PATH}/inode.c

    ${EXTFS_CORE_PATH}/struct_check.c
    ${EXTFS_CORE_PATH}/superblock_check.c

    ${EXTFS_CORE_PATH}/utils.c
)

list(APPEND EXTFS_IO_SOURCE
    ${EXTFS_FSD_PATH}/extfs.c

    ${EXTFS_FSD_PATH}/disk.c

    ${EXTFS_FSD_PATH}/create.c
    ${EXTFS_FSD_PATH}/close.c
    
    ${EXTFS_FSD_PATH}/read.c
    ${EXTFS_FSD_PATH}/write.c

    ${EXTFS_FSD_PATH}/devctrl.c
    ${EXTFS_FSD_PATH}/dirctrl.c
    ${EXTFS_FSD_PATH}/fsctrl.c
    ${EXTFS_FSD_PATH}/lockctrl.c

    ${EXTFS_FSD_PATH}/dispatch.c

    ${EXTFS_FSD_PATH}/cache.c
    ${EXTFS_FSD_PATH}/fastio.c

    ${EXTFS_FSD_PATH}/fileinfo.c
    ${EXTFS_FSD_PATH}/volinfo.c

    ${EXTFS_FSD_PATH}/pnp.c

    ${EXTFS_FSD_PATH}/shutdown.c

)

list(APPEND EXTFS_SOURCE
    ${EXTFS_CORE_SOURCE}
    ${EXTFS_IO_SOURCE}
)

set(PRODUCT_VERSION_MAJOR 1)
set(PRODUCT_VERSION_MINOR 0)
set(PRODUCT_VERSION_PATCH 0)

include_directories(${EXTFS_INCLUDE_PATH})

configure_file(
    ${EXTFS_BASE_SOURCE_PATH}/extfs.rc
    ${EXTFS_BASE_BUILD_PATH}/extfs.rc
    @ONLY
)
