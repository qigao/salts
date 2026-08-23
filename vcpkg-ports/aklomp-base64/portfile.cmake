vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# Pin the exact upstream commit that this repository used to build base64 from.
# Its tar archive contains a Unicode CI fixture that Windows CMake/libarchive
# cannot materialize; the commit ZIP preserves the source and extracts there.
set(AKLOMP_BASE64_COMMIT 9e8ed65048ff0f703fad3deb03bf66ac7f78a4d7)
vcpkg_download_distfile(AKLOMP_BASE64_ARCHIVE
    URLS "https://github.com/aklomp/base64/archive/${AKLOMP_BASE64_COMMIT}.zip"
    FILENAME "aklomp-base64-${AKLOMP_BASE64_COMMIT}.zip"
    SHA512 b12a0d7932dd4b2b958efba9faa63878b7e017992511475118a28dcd696203e8459237970a226aaf4fe2be30319b77ae214e67d2509d7e38e908a1921676c09f
)
vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${AKLOMP_BASE64_ARCHIVE}"
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_SHARED_LIBS=OFF
        -DBASE64_BUILD_CLI=OFF
        -DBASE64_REGENERATE_TABLES=OFF
        -DBASE64_WERROR=OFF
        -DBASE64_WITH_OpenMP=OFF
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_cmake_config_fixup(
    PACKAGE_NAME base64
    CONFIG_PATH "lib/cmake/base64"
)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
