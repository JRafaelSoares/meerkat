# Build ziplog

d := $(dir $(lastword $(MAKEFILE_LIST)))

#$(info d_thirdparty is $(d))
ziplog_src := $(addprefix $(d), ziplog/src/)
ziplog_network := $(addprefix $(ziplog_src), network)
ziplog_util := $(addprefix $(ziplog_src), util)
ziplog_client := $(addprefix $(ziplog_src), client)
SRCS_ZIP += $(ziplog_network)/buffer.cpp $(ziplog_network)/manager.cpp $(ziplog_network)/send_queue.cpp $(ziplog_network)/recv_queue.cpp \
        $(ziplog_util)/util.cpp $(ziplog_client)/client.cpp

ziplog_obj := $(addprefix $(o), ziplog/src/)
ziplog_network_obj := $(addprefix $(ziplog_obj), network)
ziplog_util_obj := $(addprefix $(ziplog_obj), util)
ziplog_client_obj := $(addprefix $(ziplog_obj), client)
LIB-ziplog := $(ziplog_network_obj)/buffer.o $(ziplog_network_obj)/manager.o $(ziplog_network_obj)/send_queue.o $(ziplog_network_obj)/recv_queue.o \
             $(ziplog_util_obj)/util.o $(ziplog_client_obj)/client.o
