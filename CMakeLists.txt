cmake_minimum_required(VERSION 3.21)
project(TunnelBrowser LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Windows x64 is required")
endif()
set(CEF_VERSION "150.0.23+g4ccc4ca+chromium-150.0.7871.287")
set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef_binary_${CEF_VERSION}_windows64_minimal")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")
set(PROJECT_ARCH "x86_64")
set(USE_SANDBOX ON CACHE BOOL "Keep Chromium sandbox enabled" FORCE)
set(USE_ATL OFF CACHE BOOL "No ATL dependency" FORCE)
find_package(CEF REQUIRED)
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
add_library(TunnelBrowser SHARED src/app.cc src/node.cc src/profile.cc)
SET_LIBRARY_TARGET_PROPERTIES(TunnelBrowser)
target_include_directories(TunnelBrowser PRIVATE "${CEF_ROOT}" "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(TunnelBrowser PRIVATE libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} winhttp.lib)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  # CEF diagnostics embed __FILE__ in the release DLL.
  cmake_path(NATIVE_PATH CMAKE_SOURCE_DIR NORMALIZE build_source_path)
  foreach(target TunnelBrowser libcef_dll_wrapper)
    target_compile_options(${target} PRIVATE
      "$<$<CONFIG:Release>:/experimental:deterministic>"
      "$<$<CONFIG:Release>:/pathmap:${build_source_path}=.>")
  endforeach()
  target_compile_options(TunnelBrowser PRIVATE "$<$<CONFIG:Release>:/Brepro>")
  target_link_options(TunnelBrowser PRIVATE "$<$<CONFIG:Release>:/Brepro>")
endif()
set_target_properties(TunnelBrowser PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app")
add_custom_command(TARGET TunnelBrowser POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_ROOT}/Release" "$<TARGET_FILE_DIR:TunnelBrowser>"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_ROOT}/Resources" "$<TARGET_FILE_DIR:TunnelBrowser>"
  COMMAND ${CMAKE_COMMAND} -E copy "${CEF_ROOT}/Release/bootstrap.exe" "$<TARGET_FILE_DIR:TunnelBrowser>/TunnelBrowser.exe"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/third_party/libxray/libxray-windows-x64/libXray.dll" "$<TARGET_FILE_DIR:TunnelBrowser>/libXray.dll")
