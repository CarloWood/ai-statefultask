AM_CPPFLAGS = -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

SUBDIRS = doc

noinst_LTLIBRARIES = libstatefultask.la

libstatefultask_la_SOURCES = \
	AIStatefulTask.cxx \
	AIStatefulTask.h \
	AIEngine.cxx \
	AIEngine.h \
	AIPackagedTask.h \
	AITimer.cxx \
	AITimer.h \
	AIThreadPool.cxx \
	AIThreadPool.h \
	AIStatefulTaskMutex.h \
	Timer.cxx \
	Timer.h \
	TimerQueue.h \
	RunningTimers.cxx \
	RunningTimers.h

libstatefultask_la_CXXFLAGS = @LIBCWD_R_FLAGS@
libstatefultask_la_LIBADD = -lrt @LIBCWD_R_LIBS@

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	-rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
