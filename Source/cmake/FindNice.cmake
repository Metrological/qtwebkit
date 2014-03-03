
find_package(PkgConfig)
pkg_check_modules(NICE nice)

set(VERSION_OK TRUE)
if (NICE_VERSION)
    if (NICE_FIND_VERSION_EXACT)
        if (NOT("${NICE_FIND_VERSION}" VERSION_EQUAL "${NICE_VERSION}"))
            set(VERSION_OK FALSE)
        endif ()
    else ()
        if ("${NICE_VERSION}" VERSION_LESS "${NICE_FIND_VERSION}")
            set(VERSION_OK FALSE)
        endif ()
    endif ()
endif ()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(NICE DEFAULT_MSG NICE_INCLUDE_DIRS NICE_LIBRARIES VERSION_OK)
