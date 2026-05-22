if (UNIX)
    set(libclang_search
        /opt/homebrew/opt/llvm/include
        /opt/homebrew/opt/llvm/lib
        /usr/local/opt/llvm/include
        /usr/local/opt/llvm/lib
        /usr/lib/llvm-18/include
        /usr/lib/llvm-18/lib
        /usr/lib/llvm-17/include
        /usr/lib/llvm-17/lib
        /usr/lib/llvm-16/include
        /usr/lib/llvm-16/lib
        /usr/lib/llvm-15/include
        /usr/lib/llvm-15/lib
        /usr/lib/llvm-14/include
        /usr/lib/llvm-14/lib
        /usr/local/llvm/include
        /usr/local/llvm/lib
        /opt/llvm/include
        /opt/llvm/lib)
elseif (WIN32)
    set(libclang_search
        $ENV{PATH}
        $ENV{INCLUDE}
        $ENV{LIB}
        C:\\Program Files\\LLVM\\include
        C:\\Program Files\\LLVM\\lib)
endif()

find_path(LibClang_INCLUDE_DIR NAMES clang-c/Index.h PATHS ${libclang_search})
find_library(LibClang_LIBRARY NAMES clang libclang PATHS ${libclang_search})

set(LIBCLANG_LIBRARIES ${LibClang_LIBRARY})
set(LIBCLANG_INCLUDE_DIRS ${LibClang_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibClang DEFAULT_MSG LibClang_LIBRARY LibClang_INCLUDE_DIR)

mark_as_advanced(LibClang_LIBRARY LibClang_INCLUDE_DIR)
