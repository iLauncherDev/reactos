
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers
                    inc)

list(APPEND SOURCE
    src/fastio.c
    src/write.c
    src/pnp.c
    src/flush.c
    src/linux.c
    src/shutdown.c
    src/nls.c
    src/cmcb.c
    src/nls/nls_cp874.c
    src/nls/nls_koi8-ru.c
    src/nls/nls_iso8859-7.c
    src/nls/nls_cp932.c
    src/nls/nls_koi8-r.c
    src/nls/nls_iso8859-1.c
    src/nls/nls_cp850.c
    src/nls/nls_iso8859-14.c
    src/nls/nls_cp855.c
    src/nls/nls_cp861.c
    src/nls/nls_iso8859-2.c
    src/nls/nls_cp1250.c
    src/nls/nls_cp869.c
    src/nls/nls_iso8859-4.c
    src/nls/nls_iso8859-3.c
    src/nls/nls_cp437.c
    src/nls/nls_cp862.c
    src/nls/nls_cp863.c
    src/nls/nls_cp860.c
    src/nls/nls_euc-jp.c
    src/nls/nls_iso8859-9.c
    src/nls/nls_cp1251.c
    src/nls/nls_koi8-u.c
    src/nls/nls_cp775.c
    src/nls/nls_cp1255.c
    src/nls/nls_base.c
    src/nls/nls_cp950.c
    src/nls/nls_utf8.c
    src/nls/nls_iso8859-15.c
    src/nls/nls_cp949.c
    src/nls/nls_cp857.c
    src/nls/nls_cp865.c
    src/nls/nls_ascii.c
    src/nls/nls_iso8859-13.c
    src/nls/nls_cp852.c
    src/nls/nls_cp864.c
    src/nls/nls_cp737.c
    src/nls/nls_cp866.c
    src/nls/nls_iso8859-5.c
    src/nls/nls_iso8859-6.c
    src/nls/nls_cp936.c
    src/ea.c
    src/dirctl.c
    src/lock.c
    src/ext4/ext4_jbd2.c
    src/ext4/ext4_extents.c
    src/ext4/ext4_bh.c
    src/ext4/extents.c
    src/ext4/ext4_csum.c
    src/ext4/ext4_xattr.c
    src/memory.c
    src/fsctl.c
    src/create.c
    src/read.c
    src/dispatch.c
    src/volinfo.c
    src/misc.c
    src/block.c
    src/ext3/indirect.c
    src/ext3/generic.c
    src/ext3/htree.c
    src/ext3/recover.c
    src/except.c
    src/rbtree.c
    src/devctl.c
    src/cleanup.c
    src/debug.c
    src/jbd2/recovery.c
    src/jbd2/revoke.c
    src/jbd2/readme.txt
    src/jbd2/journal.c
    src/jbd2/transaction.c
    src/close.c
    src/init.c
    src/fileinfo.c
    src/access.c
    inc/ext2fs.h
)

add_library(ext4fs MODULE ${SOURCE} ext4fs.rc)

if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    # Disable warning C4101: 'i': unreferenced local variable
    # Disable warning C4189: 'sbi': local variable is initialized but not referenced
    # Disable warning C4267: '=': conversion from 'size_t' to 'USHORT', possible loss of data
    target_compile_options(ext4fs PRIVATE /wd4101 /wd4189 /wd4267)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ext4fs PRIVATE
        -Wno-pointer-sign -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-unused-but-set-variable -Wno-incompatible-pointer-types)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ext4fs PRIVATE
        -Wno-parentheses-equality
        -Wno-incompatible-pointer-types-discards-qualifiers
        "-Wno-#pragma-messages;-Wno-cast-calling-convention")
endif()

target_link_libraries(ext4fs memcmp ${PSEH_LIB})
add_definitions(-D__KERNEL__ -D_CRT_NO_POSIX_ERROR_CODES -D_CRT_DECLARE_NONSTDC_NAMES=1)
set_module_type(ext4fs kernelmodedriver)
add_importlibs(ext4fs ntoskrnl hal)
add_pch(ext4fs inc/ext2fs.h SOURCE)

add_cd_file(TARGET ext4fs DESTINATION reactos/system32/drivers FOR all)
add_registry_inf(ext4fs_reg.inf)
