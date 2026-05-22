if (VITA)
        add_library(gdbstub INTERFACE)
else()
        add_library(gdbstub
                include/gdbstub/gdbstub.h
                src/gdbstub.cpp)
endif()

set(LIBRARIES
        cpu
        epoc
        epockern
        epocpkg)

if (WIN32)
    set(LIBRARIES ${LIBRARIES} wsock32)
endif ()

if (VITA)
        target_include_directories(gdbstub INTERFACE include)
else()
        target_include_directories(gdbstub PUBLIC include)
        target_link_libraries(gdbstub PRIVATE ${LIBRARIES})
endif()
