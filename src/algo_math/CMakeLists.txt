target_sources(window_engine PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/vec3.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/mat4.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/camera.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/quaternion.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/frustum.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/screen_space_metrics.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/compare.cpp
)
