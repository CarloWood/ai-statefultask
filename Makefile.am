AM_CPPFLAGS = -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

SUBDIRS = doc

noinst_LTLIBRARIES = libstatefultask.la

libstatefultask_la_SOURCES = \
	AIStatefulTask.cxx \
	AIStatefulTask.h \
	AIEngine.cxx \
	AIEngine.h \
	AITimer.cxx \
	AITimer.h \
	AIStatefulTaskMutex.h \
	AIDelayedFunction.h \
	AIPackagedTask.h

libstatefultask_la_CXXFLAGS = @LIBCWD_R_FLAGS@
libstatefultask_la_LIBADD = -lrt @LIBCWD_R_LIBS@

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	-rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
