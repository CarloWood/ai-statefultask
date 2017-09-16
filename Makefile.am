AM_CPPFLAGS = -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

noinst_LTLIBRARIES = libstatefultask.la

libstatefultask_la_SOURCES = \
	AIStatefulTask.cxx \
	AIStatefulTask.h \
	AIEngine.cxx \
	AIEngine.h \
	AIPackagedTask.h \
        AIFrameTimer.cxx \
        AIFrameTimer.h \
	AITimer.cxx \
	AITimer.h \
	AIThreadPool.cxx \
	AIThreadPool.h \
	AIAuxiliaryThread.h \
	AIAuxiliaryThread.cxx \
	AIStatefulTaskMutex.h

libstatefultask_la_CXXFLAGS = -std=c++11 -fmax-errors=1 @LIBCWD_R_FLAGS@
libstatefultask_la_LIBADD = @LIBCWD_R_LIBS@

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	-rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
