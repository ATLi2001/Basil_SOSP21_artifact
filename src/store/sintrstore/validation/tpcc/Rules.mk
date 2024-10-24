d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), tpcc_transaction.cc new_order.cc payment.cc order_status.cc stock_level.cc delivery.cc)

OBJ-sintr-validation-tpcc-transaction := $(LIB-sintr-validation-client) $(o)tpcc_transaction.o

LIB-sintr-validation-tpcc := $(OBJ-sintr-validation-tpcc-transaction) $(LIB-tpcc) $(o)new_order.o \
	$(o)payment.o $(o)order_status.o $(o)stock_level.o $(o)delivery.o