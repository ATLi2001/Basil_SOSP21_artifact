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

ValidationClient::ValidationClient(Parameters params) : params(params) {}

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
  validation_read_callback vrcb = [gcb, this](int status, const std::string &key,
      const std::string &val, const Timestamp &ts, bool addReadSet) {

    if (addReadSet) {
      ReadMessage *read = txn.add_read_set();
      read->set_key(key);
      ts.serialize(read->mutable_readtime());
    }

    std::cerr << "validation_read_callback on key " << key << ", value " << val << std::endl;

    gcb(status, key, val, ts);
  };

  std::cerr << "ValidationClient::Get on key " << key << std::endl;

  // read locally in buffer
  if (BufferGet(key, vrcb)) {
    return;
  }

  // otherwise have to wait for read results to get passed over
  PendingValidationGet *pendingGet = new PendingValidationGet(txn_client_id, txn_client_seq_num);
  std::pair<uint64_t, uint64_t> txn_id = std::make_pair(txn_client_id, txn_client_seq_num);
  if (pendingGets.find(txn_id) == pendingGets.end()) {
    pendingGets.insert({txn_id, std::vector<PendingValidationGet *>()});
  }
  pendingGets[txn_id].push_back(pendingGet);
  pendingGet->key = key;
  pendingGet->vrcb = vrcb;
  pendingGet->vrtcb = gtcb;
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

void ValidationClient::SetTxnClientId(uint64_t txn_client_id) {
  this->txn_client_id = txn_client_id;
}
void ValidationClient::SetTxnClientSeqNum(uint64_t txn_client_seq_num) {
  this->txn_client_seq_num = txn_client_seq_num;
}

void ValidationClient::ValidateForwardReadResult(const proto::ForwardReadResult &fwdReadResult) {
  if (params.validateProofs) {

  }

  uint64_t curr_client_id = fwdReadResult.client_id();
  uint64_t curr_client_seq_num = fwdReadResult.client_seq_num();
  std::string curr_key = fwdReadResult.key();
  std::string curr_value = fwdReadResult.value();

  // find matching pending get by first going off txn client id and sequence number, then key
  auto itr = pendingGets.find(std::make_pair(curr_client_id, curr_client_seq_num));
  if (itr == pendingGets.end()) {
    return; // this is a stale request
  }
  std::vector<PendingValidationGet *> reqs = itr->second;
  auto reqs_itr = std::find_if(
    reqs.begin(), reqs.end(), 
    [&curr_key](const PendingValidationGet *req) { return req->key == curr_key; }
  );
  if (reqs_itr == reqs.end()) {
    return; // this is a stale request
  }
  // callback
  PendingValidationGet *req = *reqs_itr;
  req->ts = Timestamp(fwdReadResult.timestamp());
  req->vrcb(REPLY_OK, req->key, curr_value, req->ts, true);

  // remove from vector
  reqs.erase(reqs_itr);
  // free memory
  delete req;
}

bool ValidationClient::BufferGet(const std::string &key, validation_read_callback vrcb) {
  for (const auto &write : txn.write_set()) {
    if (write.key() == key) {
      vrcb(REPLY_OK, key, write.value(), Timestamp(), false);
      return true;
    }
  }

  for (const auto &read : txn.read_set()) {
    if (read.key() == key) {
      vrcb(REPLY_OK, key, readValues[key], read.readtime(), false);
      return true;
    }
  }

  return false;
}

} // namespace sintrstore
