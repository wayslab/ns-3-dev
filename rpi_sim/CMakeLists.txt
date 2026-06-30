build_exec(
  EXECNAME rpi-sim
  SOURCE_FILES
    rpi-sim.cc
    genai-message-header.cc
    genai-user.cc
    genai-server.cc
    genai-helper.cc
    lognormalmixture/lognormal-mixture-random-variable.cc
  HEADER_FILES
    genai-message-header.h
    genai-user.h
    genai-server.h
    genai-helper.h
    lognormalmixture/lognormal-mixture-random-variable.h
  LIBRARIES_TO_LINK
    ${libcore}
    ${libnetwork}
    ${libinternet}
    ${libcsma}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_CURRENT_SOURCE_DIR}
)
