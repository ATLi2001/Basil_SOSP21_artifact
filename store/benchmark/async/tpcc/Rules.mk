d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), tpcc_client.cc tpcc_transaction.cc random_generator.cc new_order.cc)

OBJ-tpcc-transaction := $(LIB-store-frontend) $(o)tpcc_transaction.o

OBJ-tpcc-client := $(o)tpcc_client.o

LIB-tpcc := $(OBJ-tpcc-client) $(OBJ-tpcc-transaction) $(o)random_generator.o $(o)new_order.o
