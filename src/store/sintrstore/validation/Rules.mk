d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), validation_client.cc)

LIB-sintr-validation-client := $(LIB-store-frontend) $(o)validation_client.o

include $(d)tpcc/Rules.mk
