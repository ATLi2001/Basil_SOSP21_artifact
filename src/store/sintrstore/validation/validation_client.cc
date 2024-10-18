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

#include "store/sintrstore/validation/validation_client.h"

namespace sintrstore {

ValidationClient::ValidationClient(uint64_t txn_client_id, uint64_t txn_client_seq_num) : 
  txn_client_id(txn_client_id), txn_client_seq_num(txn_client_seq_num), lastReqId(0UL) {}

ValidationClient::~ValidationClient() {
}

void ValidationClient::Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry, const std::string &txnState) {
  txn = proto::Transaction();
  txn.set_client_id(txn_client_id);
  txn.set_client_seq_num(txn_client_seq_num);
  readValues.clear();
  bcb(txn_client_seq_num);
};

void ValidationClient::Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) {
  read_callback rcb = [gcb, this](int status, const std::string &key,
      const std::string &val, const Timestamp &ts, const proto::Dependency &dep,
      bool hasDep, bool addReadSet) {

    if (addReadSet) {
      ReadMessage *read = txn.add_read_set();
      read->set_key(key);
      ts.serialize(read->mutable_readtime());
    }
    if (hasDep) {
      *txn.add_deps() = dep;
    }
    gcb(status, key, val, ts);
  };

  // read locally in buffer
  if (BufferGet(key, rcb)) {
    return;
  }

  // otherwise have to wait for read results to get passed over
  uint64_t reqId = lastReqId++;
  PendingValidationGet *pendingGet = new PendingValidationGet(reqId);
  pendingGets[reqId] = pendingGet;
  pendingGet->key = key;
  pendingGet->rcb = rcb;
  pendingGet->rtcb = gtcb;
};

void ValidationClient::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb,
    uint32_t timeout) {
  WriteMessage *write = txn.add_write_set();
  write->set_key(key);
  write->set_value(value);
  pcb(REPLY_OK, key, value);
};

void ValidationClient::Commit(commit_callback ccb, commit_timeout_callback ctcb,
    uint32_t timeout) {};

void ValidationClient::Abort(abort_callback acb, abort_timeout_callback atcb,
    uint32_t timeout) {};

bool ValidationClient::BufferGet(const std::string &key, read_callback rcb) {
  for (const auto &write : txn.write_set()) {
    if (write.key() == key) {
      rcb(REPLY_OK, key, write.value(), Timestamp(), proto::Dependency(),
          false, false);
      return true;
    }
  }

  for (const auto &read : txn.read_set()) {
    if (read.key() == key) {
      rcb(REPLY_OK, key, readValues[key], read.readtime(), proto::Dependency(),
          false, false);
      return true;
    }
  }

  return false;
}

} // namespace sintrstore
