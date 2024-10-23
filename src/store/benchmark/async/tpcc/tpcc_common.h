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
#ifndef TPCC_COMMON_H
#define TPCC_COMMON_H

#include "lib/message.h"

#include <string>

namespace tpcc {

const std::string BENCHMARK_NAME = "tpcc";

enum TPCC_TXN_TYPE {
  TPCC_DELIVERY,
  TPCC_NEW_ORDER,
  TPCC_ORDER_STATUS,
  TPCC_PAYMENT,
  TPCC_STOCK_LEVEL
};

inline std::string GetBenchmarkTxnTypeName(TPCC_TXN_TYPE txn_type) {
  switch (txn_type) {
    case TPCC_DELIVERY:
      return "delivery";
    case TPCC_NEW_ORDER:
      return "new_order";
    case TPCC_ORDER_STATUS:
      return "order_status";
    case TPCC_PAYMENT:
      return "payment";
    case TPCC_STOCK_LEVEL:
      return "stock_level";
    default:
      Panic("Received unexpected txn type: %d", txn_type);
  }
}

inline TPCC_TXN_TYPE GetBenchmarkTxnTypeEnum(std::string &txn_type) {
  if (txn_type == "delivery") {
    return TPCC_DELIVERY;
  }
  else if (txn_type == "new_order") {
    return TPCC_NEW_ORDER;
  }
  else if (txn_type == "order_status") {
    return TPCC_ORDER_STATUS;
  }
  else if (txn_type == "payment") {
    return TPCC_PAYMENT;
  }
  else if (txn_type == "stock_level") {
    return TPCC_STOCK_LEVEL;
  }
  else {
    Panic("Received unexpected txn type: %s", txn_type.c_str());
  }
}

}

#endif /* TPCC_COMMON_H */
