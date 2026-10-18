cmake_minimum_required(VERSION 3.16)
project(toplethos_rsvm CXX C)
set(CMAKE_CXX_STANDARD 17)
set(OIRIA_ROOT "" CACHE PATH "OIRIA_OS_espIDF root (has os_code/core/rs_vm)")
if(OIRIA_ROOT)
  set(VM "${OIRIA_ROOT}/os_code/core/rs_vm")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/rs_vm/vm/rs_vm.cpp")
  set(VM "${CMAKE_CURRENT_SOURCE_DIR}/rs_vm")
else()
  message(FATAL_ERROR "Copy watch os_code/core/rs_vm → cpp_vm/rs_vm or -DOIRIA_ROOT=")
endif()
add_executable(rsvm rsvm_cli.cpp rsvm_host_desktop.cpp
  ${VM}/vm/rs_vm.cpp ${VM}/vm/rs_vm_parse.cpp ${VM}/vm/rs_vm_latex.c
  ${VM}/vm/rs_vm_nseq.cpp
  ${VM}/vm_modules/vm_mdl_property/vm_mdl_property.cpp
  ${VM}/vm_modules/vm_mdl_immut/vm_mdl_immut.cpp
  ${VM}/vm_modules/vm_mdl_thread/vm_mdl_thread.cpp
  ${VM}/vm_modules/vm_mdl_flowmap/vm_mdl_flowmap.cpp)
target_include_directories(rsvm PRIVATE
  ${OIRIA_ROOT} ${VM}/vm ${VM} ${CMAKE_CURRENT_SOURCE_DIR})
