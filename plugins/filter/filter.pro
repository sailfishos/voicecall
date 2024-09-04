TEMPLATE = subdirs
SUBDIRS = lib src tests

src.depends = lib
tests.depends = lib
