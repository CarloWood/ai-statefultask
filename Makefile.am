AM_CPPFLAGS = -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

noinst_LTLIBRARIES = libstatefultask.la

libstatefultask_la_SOURCES = \
	AIStatefulTask.cxx \
	AIStatefulTask.h \
	AIEngine.cxx \
	AIEngine.h \
	AIStatefulTaskThread.cxx \
	AIStatefulTaskThread.h \
	AITimer.cxx \
	AITimer.h \
	AICondition.cxx \
	AICondition.h \
	AIAuxiliaryThread.h \
	AIAuxiliaryThread.cxx \
	AIStatefulTaskMutex.h

if CW_DEBUG_MONTECARLO
libstatefultask_la_SOURCES += montecarlo_test.cxx montecarlo_test.h
endif

libstatefultask_la_CXXFLAGS = -std=c++11 @LIBCWD_R_FLAGS@
libstatefultask_la_LIBADD = @LIBCWD_R_LIBS@

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	-rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
