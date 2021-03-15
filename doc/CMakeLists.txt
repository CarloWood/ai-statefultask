include(AICxxProject)

find_package(Doxygen OPTIONAL_COMPONENTS dot)

#==============================================================================
# OPTIONS

# Option 'BuildDocumentation' is available when Doxygen was found.
# For Release the default is ON, otherwise OFF.
cw_option(BuildDocumentation
          "Create and install the HTML based API documentation (requires Doxygen)" ${CW_BUILD_TYPE_IS_RELEASE}
          "DOXYGEN_FOUND" OFF
)

if (OptionBuildDocumentation AND NOT DOXYGEN_FOUND)
  message(FATAL_ERROR "Doxygen is needed to build the documentation. "
    "Use -DBuildDocumentation:BOOL=OFF to skip building the documentation while using build type Release.")
endif ()

if (Doxygen_dot_FOUND)
  set(HAVE_DOT "YES")
else ()
  set (HAVE_DOT "NO")
endif ()
if (OptionEnableLibcwd)
  set(DOXYGEN_CWDEBUG "CWDEBUG")
endif ()
if (OptionEnableDebug)
  set(DOXYGEN_DEBUG "DEBUG")
endif ()
if (CW_BUILD_TYPE_IS_RELEASE)
  set(DOXYGEN_ALL "ALL")
endif ()
if(DEFINED ENV{OUTPUT_DIRECTORY})
  set(HTML_OUTPUT_DIRECTORY "$ENV{OUTPUT_DIRECTORY}/statefultask")
else ()
  set(HTML_OUTPUT_DIRECTORY "html")
endif ()

#==============================================================================

if (DOXYGEN_FOUND)
  set(DOXYGEN_STRIP_FROM_PATH ${CMAKE_SOURCE_DIR})
  set(top_srcdir ${CMAKE_SOURCE_DIR})
  set(binary_dir ${CMAKE_CURRENT_BINARY_DIR})
  set(HTML_OUTPUT_DIRECTORY ${HTML_OUTPUT_DIRECTORY})
  configure_file(doxygen.config.in doxygen.config @ONLY)
  configure_file(html.header.in html.header @ONLY)
  configure_file(html.footer.in html.footer @ONLY)

  add_custom_target(doc ${DOXYGEN_ALL}
    COMMAND ${DOXYGEN_EXECUTABLE} doxygen.config
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
  )
endif ()
