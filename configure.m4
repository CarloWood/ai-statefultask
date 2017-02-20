# Add --enable-montecarlo
AC_ARG_ENABLE([montecarlo], AS_HELP_STRING([--enable-montecarlo], [Include debug code to test the library.]))
AS_IF([test "x$enable_montecarlo" = "xyes"], [AC_DEFINE([CW_DEBUG_MONTECARLO], [1], [Compile statefultask with extra debugging code.])])
AM_CONDITIONAL([CW_DEBUG_MONTECARLO], [test "x$enable_montecarlo" = "xyes"])

# statefultask depends on utils and threadsafe:
m4_if(cwm4_submodule_dirname, [], [m4_append_uniq([CW_SUBMODULE_SUBDIRS], utils, [ ])])
m4_if(cwm4_submodule_dirname, [], [m4_append_uniq([CW_SUBMODULE_SUBDIRS], threadsafe, [ ])])

m4_if(cwm4_submodule_dirname, [], [m4_append_uniq([CW_SUBMODULE_SUBDIRS], cwm4_submodule_basename, [ ])])
m4_append_uniq([CW_SUBMODULE_CONFIG_FILES], cwm4_quote(cwm4_submodule_path[/Makefile]), [ ])
