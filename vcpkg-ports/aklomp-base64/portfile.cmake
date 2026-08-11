vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# Pin the exact upstream commit that this repository used to build base64 from
# (9e8ed65048ff0f703fad3deb03bf66ac7f78a4d7, master after the v0.5.2 release).
# Building that commit keeps the vcpkg package behavior-identical to the
# previous vendored static build.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO aklomp/base64
    REF 9e8ed65048ff0f703fad3deb03bf66ac7f78a4d7
    SHA512 a8868e219f471182884645a295ad8278347106a1823fddda51e264ded6e4067aac444c92f7f90d8f37b104e69aa8e7fcffcb8944a432e5c7948ce90687624978
    HEAD_REF master
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
