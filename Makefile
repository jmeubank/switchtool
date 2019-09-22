
SWITCHTOOL_OBJS = \
  libtelnet/libtelnet.o \
  tinyxml/tinystr.o \
  tinyxml/tinyxml.o \
  tinyxml/tinyxmlerror.o \
  tinyxml/tinyxmlparser.o \
  yajl/src/yajl.o \
  yajl/src/yajl_alloc.o \
  yajl/src/yajl_buf.o \
  yajl/src/yajl_encode.o \
  yajl/src/yajl_gen.o \
  yajl/src/yajl_lex.o \
  yajl/src/yajl_parser.o \
  yajl/src/yajl_tree.o \
  yajl/src/yajl_version.o \
  calixaeont.o \
  calixeseries.o \
  ciscoios.o \
  junosswitch.o \
  main.o \
  proptree.o \
  snmp.o \
  terminal.o \
  ubnt-airos.o

CFLAGS += -O2 -I.
CXXFLAGS += -O2 -DPCRE_STATIC=1 -DTIXML_USE_STL=1 -I.

switchtool: $(SWITCHTOOL_OBJS)
	$(CXX) -s -o $@ $(SWITCHTOOL_OBJS) -lssh2 -lpcrecpp -lpcre
