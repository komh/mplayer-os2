# makefile
#
# The actions included in this make file are:
#  Compile::C++ Compiler
#  Link::Linker

.SUFFIXES: .cpp .obj .rc .res

CC = gcc
RC = rc

RM = rm -f

COMMON = .
NAME = npmp
INCLUDEPATH = .
MPLAYERPATH = ..

CPPFLAGS = -Zomf
LDFLAGS = -Zomf -Zdll -Zmap

ifeq ($(OPT), DEBUG)
CPPFLAGS += -DDEBUG -g -O0
LDFLAGS += -g
else
CPPFLAGS += -O3
LDFLAGS += -s
endif

CPP_FLAGS += $(CRT_TYPE)

.PHONY: all clean

all: $(NAME).dll

clean:
	$(RM) *.bak
	$(RM) *.obj
	$(RM) *.res
	$(RM) *.map
	$(RM) *.dll

.cpp.obj:
	@echo " Compile::C++ Compiler "
	$(CC) $(CPPFLAGS) -o $@ -I$(INCLUDEPATH) -I$(MPLAYERPATH) -c $<

.rc.res:
	@echo " Compile::Resource Compile "
	$(RC) -i $(INCLUDEPATH) -i $(MPLAYERPATH) -r $<

$(NAME).dll: \
	$(COMMON)\npos2.obj \
	$(NAME).obj \
	$(NAME).res\
	$(NAME).def
	@echo " Link::Linker "
	@$(CC) -o $@ $(LDFLAGS) $(NAME).def $(COMMON)/npos2.obj $(NAME).obj
	@echo " Compile::Resource Bind "
	rc $(NAME).res $@

$(NAME).obj: \
	$(NAME).cpp \
	$(INCLUDEPATH)/npapi.h \
	npmp.h

$(COMMON)\npos2.obj: \
	$(COMMON)/npos2.cpp \
	$(INCLUDEPATH)/npapi.h \
	$(INCLUDEPATH)/npupp.h

$(NAME).res: \
	$(NAME).rc \
	$(INCLUDEPATH)/npapi.h \
	npmp.h \
	$(MPLAYERPATH)/osdep/keycodes.h
