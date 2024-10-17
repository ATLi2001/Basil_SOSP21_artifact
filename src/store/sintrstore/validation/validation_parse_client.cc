/***********************************************************************
 *
 * Copyright 2024 Austin Li <atl63@cornell.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/

#include "store/sintrstore/validation/validation_parse_client.h"
#include "store/sintrstore/validation/tpcc/tpcc_transaction.h"
#include "store/sintrstore/validation/tpcc/delivery.h"
#include "store/sintrstore/validation/tpcc/tpcc-validation-proto.pb.h"


namespace sintrstore {

ValidationTransaction *ValidationParseClient::Parse(const proto::TxnState& txnState) {
  std::string txn_name(txnState.txn_name());
  
  size_t pos = txn_name.find("_");
  if (pos == std::string::npos) {
    Panic("Received unexpected txn name: %s", txn_name.c_str());
  }

  std::string txn_bench = txn_name.substr(0, pos);
  std::string txn_type = txn_name.substr(pos+1);

  if (txn_bench == "tpcc") {
    if (txn_type == "delivery") {
      ::tpcc::validation::proto::Delivery valTxnData = ::tpcc::validation::proto::Delivery();
      valTxnData.ParseFromString(txnState.txn_data());
      return new ::tpcc::ValidationDelivery(
        timeout, 
        valTxnData.w_id(), 
        valTxnData.d_id(), 
        valTxnData.o_carrier_id(), 
        valTxnData.ol_delivery_d()
      );
    }
    else {
      Panic("Received unexpected txn type: %s", txn_type.c_str());
    }
  }
  else {
    Panic("Received unexpected txn benchmark: %s", txn_bench.c_str());
  }
};

} // namespace sintrstore
