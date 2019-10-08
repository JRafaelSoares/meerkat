d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), benchClient.cc retwisClient.cc terminalClient.cc)

OBJS-all-clients := $(OBJS-meerkatstore-client)

$(d)benchClient: $(OBJS-all-clients) $(o)benchClient.o

$(d)retwisClient: $(OBJS-all-clients) $(o)retwisClient.o

$(d)terminalClient: $(OBJS-all-clients) $(o)terminalClient.o

BINS += $(d)benchClient $(d)retwisClient $(d)terminalClient
