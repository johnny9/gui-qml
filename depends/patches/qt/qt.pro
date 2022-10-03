# Create the super cache so modules will add themselves to it.
cache(, super)

!QTDIR_build: cache(CONFIG, add, $$list(QTDIR_build))

prl = no_install_prl
CONFIG += $$prl
cache(CONFIG, add stash, prl)

TEMPLATE = subdirs
SUBDIRS = qtbase qtdeclarative qtgraphicaleffects qtquickcontrols2 qttools qttranslations qtsvg

qtdeclarative.depends = qtbase
qtgraphicaleffects.depends = qtdeclarative
qtquickcontrols2.depends = qtgraphicaleffects
qttools.depends = qtbase
qttranslations.depends = qttools
qtsvg.depends = qtbase

load(qt_configure)
