all-local: doxygen.config html.header html.footer
	@echo "doxygen doxygen.config"
	@doxygen doxygen.config
	mkdir -p $(builddir)/html/images
	if test -n "`ls $(srcdir)/images/*.{png,ico} 2>/dev/null`"; then \
	  $(INSTALL) -m 644 $(srcdir)/images/*.{png,ico} $(builddir)/html/images; \
	fi
	test -f $(srcdir)/main.css && $(INSTALL) -m 644 $(srcdir)/main.css $(builddir)/html

maintainer-clean-local:
	rm -rf html latex

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
