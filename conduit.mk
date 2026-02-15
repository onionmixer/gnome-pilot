%.conduit: %.conduit.in
	sed -e 's|\@LIBDIR\@|$(libdir)|' 	\
	-e 's|\@DATADIR\@|$(datadir)|' 	$< > $@
