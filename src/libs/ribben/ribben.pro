#-------------------------------------------------
#
# Ribben addon: embedded live MCP/RPC server library
#
#-------------------------------------------------

# File with common stuff for whole project
message("Entering ribben.pro")
include(../../../common.pri)

QT += core network

# Name of library
TARGET = ribben

# We want create a library
TEMPLATE = lib

CONFIG += staticlib

# This is a small, self-contained library (no vmisc/def.h-style "include
# everything" header), so a precompiled header buys nothing -- same reasoning
# seamly2d.pro itself uses to turn this off.
CONFIG -= precompile_header

include(ribben.pri)

# This is static library so no need in "make install"

# directory for executable file
DESTDIR = bin

# files created moc
MOC_DIR = moc

# objects files
OBJECTS_DIR = obj

include(warnings.pri)

include (../libs.pri)
