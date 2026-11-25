# the license of the Unicode data that the names are made from goes with
# them
pkgdata_DATA = powwow.md $(srcdir)/release-notes.md powwow.help \
               unicode-license.txt
if HAVE_ZLIB
# powwow reads them through zlib
pkgdata_DATA += unicode-names.txt.gz emoji-names.txt.gz
CLEANFILES = unicode-names.txt.gz emoji-names.txt.gz
else
pkgdata_DATA += unicode-names.txt emoji-names.txt
endif
MAINTAINERCLEANFILES = Makefile.in release-notes.md
EXTRA_DIST = README.follow README.modules \
		Hacking demo.powwow \
		powwow.md powwow.help unicode-names.txt unicode-names.sh \
		emoji-names.txt emoji-names.sh update-unicode.sh \
                unicode-license.txt

# the notes of the release of this version, from its section of
# ChangeLog.md, which the launcher shows, as in its GitHub release; made in
# the source directory, as powwow run from its build directory takes them
# from there, as it does the manual, and in the tarball, so that building
# it needs no perl
EXTRA_DIST += release-notes.md release-notes.sh test-release-notes.sh \
              release-notes.markdownlint-cli2.yaml
$(srcdir)/release-notes.md: $(top_srcdir)/ChangeLog.md \
                            $(srcdir)/release-notes.sh \
                            $(top_srcdir)/configure.ac
	$(AM_V_GEN)cd $(top_srcdir) \
	    && sh doc/release-notes.sh '$(VERSION)' > doc/release-notes.md-t \
	    && mv doc/release-notes.md-t doc/release-notes.md

# check the notes, with markdownlint-cli2 if configure found it
TESTS = test-release-notes.sh
AM_TESTS_ENVIRONMENT = MARKDOWNLINT='$(MARKDOWNLINT)'; export MARKDOWNLINT;


unicode-names.txt.gz: unicode-names.txt
	gzip -9cn $(srcdir)/unicode-names.txt > $@

emoji-names.txt.gz: emoji-names.txt
	gzip -9cn $(srcdir)/emoji-names.txt > $@

# download the latest Unicode data, and make the files of names again
update-unicode:
	sh $(srcdir)/update-unicode.sh

.PHONY: update-unicode
