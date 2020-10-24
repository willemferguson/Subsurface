# the profile widget
include_directories(.
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_BINARY_DIR}
)

set(SUBSURFACE_STATSGRAPHS_UI
	TabDiveStatistics.ui
)

source_group("Subsurface statsgraphs interface" FILES ${SUBSURFACE_STATSGRAPHS_UI})

set(SUBSURFACE_STATSGRAPHS_LIB_SRCS
	TabDiveStatistics.cpp
	TabDiveStatistics.h
)

source_group("Subsurface statsgraphs code" FILES ${SUBSURFACE_STATSGRAPHS_LIB_SRCS})

add_library(subsurface_statsgraphs STATIC ${SUBSURFACE_STATSGRAPHS_LIB_SRCS} ${SUBSURFACE_STATSGRAPHS_UI_HDRS})
target_link_libraries(subsurface_statsgraphs ${QT_LIBRARIES})

