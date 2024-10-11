d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), client.cc shardclient.cc client2client.cc server.cc store.cc common.cc \
		phase1validator.cc localbatchsigner.cc sharedbatchsigner.cc \
		basicverifier.cc localbatchverifier.cc sharedbatchverifier.cc proto_bench.cc)

PROTOS += $(addprefix $(d), sintr-proto.proto)

LIB-sintr-store := $(o)server.o $(LIB-latency) \
	$(o)sintr-proto.o  $(o)common.o $(LIB-crypto) $(LIB-batched-sigs) $(LIB-bft-tapir-config) \
	$(LIB-configuration) $(LIB-store-common) $(LIB-transport) $(o)phase1validator.o \
	$(o)localbatchsigner.o $(o)sharedbatchsigner.o $(o)basicverifier.o \
	$(o)localbatchverifier.o $(o)sharedbatchverifier.o

LIB-sintr-client := $(LIB-udptransport) \
	$(LIB-store-frontend) $(LIB-store-common) $(o)sintr-proto.o \
	$(o)shardclient.o $(o)client.o $(o)client2client.o $(LIB-bft-tapir-config) \
	$(LIB-crypto) $(LIB-batched-sigs) $(o)common.o $(o)phase1validator.o \
	$(o)basicverifier.o $(o)localbatchverifier.o


LIB-proto := $(o)sintr-proto.o
#-I/home/floriansuri/Sintr/BFT-DB/src/store/common
$(d)proto_bench: $(LIB-latency) $(LIB-crypto) $(LIB-batched-sigs) $(LIB-store-common) $(LIB-proto) $(o)proto_bench.o

BINS += $(d)proto_bench

include $(d)tests/Rules.mk
