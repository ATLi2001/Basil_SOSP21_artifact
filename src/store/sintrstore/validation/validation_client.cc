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
#include "store/sintrstore/common.h"

namespace sintrstore {

ValidationClient::ValidationClient(uint64_t client_id, Parameters params) : 
  client_id(client_id), params(params) {}

ValidationClient::~ValidationClient() {
}

void ValidationClient::Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry, const std::string &txnState) {
  txn = proto::ValidationTxn();
  txn.set_client_id(txn_client_id);
  txn.set_client_seq_num(txn_client_seq_num);
  readValues.clear();
  bcb(txn_client_seq_num);
}

void ValidationClient::Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) {
  validation_read_callback vrcb = [gcb, this](int status, const std::string &key,
      const std::string &val, const Timestamp &ts, bool addReadSet) {

    if (addReadSet) {
      ReadMessage *read = txn.add_read_set();
      read->set_key(key);
      ts.serialize(read->mutable_readtime());
    }

    std::cerr << "validation_read_callback on key " << BytesToHex(key, 16) << ", value " << BytesToHex(val, 16) << std::endl;

    gcb(status, key, val, ts);
  };

  std::unique_lock<std::mutex> lock(valTxnMutex);
  std::cerr << "ValidationClient::Get on key " << BytesToHex(key, 16) << std::endl;

  // read locally in buffer
  if (BufferGet(key, vrcb)) {
    std::cerr << "ValidationClient::BufferGet on key " << BytesToHex(key, 16) << std::endl;
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
}

void ValidationClient::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb,
    uint32_t timeout) {
  WriteMessage *write = txn.add_write_set();
  write->set_key(key);
  write->set_value(value);
  pcb(REPLY_OK, key, value);
}

void ValidationClient::Commit(commit_callback ccb, commit_timeout_callback ctcb,
    uint32_t timeout) {
  ccb(COMMITTED);
}

void ValidationClient::Abort(abort_callback acb, abort_timeout_callback atcb,
    uint32_t timeout) {}

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
  std::cerr << "ValidateForwardReadResult from client " << curr_client_id << ", seq num " << curr_client_seq_num << std::endl;

  if (curr_client_id != txn_client_id || curr_client_seq_num != txn_client_seq_num) {
    std::cerr << "Received stale ForwardReadResult from client " << curr_client_id << ", seq num " << curr_client_seq_num << std::endl;
    return; // this is a stale request
  }

  // need to deal with the case where the forwarded read result is for a get that the validation transaction has not yet gotten to
  std::unique_lock<std::mutex> lock(valTxnMutex);
  std::cerr << "lock(valTxnMutex) for ForwardReadResult from client " << curr_client_id << ", seq num " << curr_client_seq_num << std::endl;

  // find matching pending get by first going off txn client id and sequence number, then key
  auto itr = pendingGets.find(std::make_pair(curr_client_id, curr_client_seq_num));
  if (itr == pendingGets.end()) {
    for (const auto &read : txn.read_set()) {
      if (read.key() == curr_key) {
        return; // this is a stale request
      }
    }
    // forwarded read result is for a get that the validation transaction has not yet gotten to
    // add to readset
    std::cerr << "lock(valTxnMutex) for ForwardReadResult from client " << curr_client_id << ", seq num " << curr_client_seq_num 
              << ", before PendingGet registered for key " << BytesToHex(curr_key, 16) << std::endl;
    ReadMessage *read = txn.add_read_set();
    read->set_key(curr_key);
    *read->mutable_readtime() = fwdReadResult.timestamp();
    return;
  }
  std::cerr << "pendingGets.find() found for ForwardReadResult from client " << curr_client_id << ", seq num " << curr_client_seq_num << std::endl;
  std::vector<PendingValidationGet *> *reqs = &itr->second;
  std::cerr << reqs->size() << " ";
  for (auto req: *reqs) {
    std::cerr << BytesToHex(req->key, 16) << " ";
  }
  auto reqs_itr = std::find_if(
    reqs->begin(), reqs->end(), 
    [&curr_key](const PendingValidationGet *req) { return req->key == curr_key; }
  );
  std::cerr << "hello?" << std::endl;
  if (reqs_itr == reqs->end()) {
    for (const auto &read : txn.read_set()) {
      if (read.key() == curr_key) {
        return; // this is a stale request
      }
    }
    // forwarded read result is for a get that the validation transaction has not yet gotten to
    // add to readset
    std::cerr << "lock(valTxnMutex) for ForwardReadResult from client " << curr_client_id << ", seq num " << curr_client_seq_num 
          << ", before PendingGet registered for key " << BytesToHex(curr_key, 16) << std::endl;
    ReadMessage *read = txn.add_read_set();
    read->set_key(curr_key);
    *read->mutable_readtime() = fwdReadResult.timestamp();
    return;
  }
  std::cerr << "reqs.find() found for ForwardReadResult from client " << curr_client_id << ", seq num " << curr_client_seq_num << std::endl;
  // callback
  PendingValidationGet *req = *reqs_itr;
  req->ts = Timestamp(fwdReadResult.timestamp());
  req->vrcb(REPLY_OK, req->key, curr_value, req->ts, true);

  // remove from vector
  reqs->erase(reqs_itr);
  // free memory
  delete req;
}

proto::ValidationTxn *ValidationClient::GetCompletedValTxn(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::cerr << "ValidationClient::GetCompletedValTxn called for txn client id " << txn_client_id << " seq num " << txn_client_seq_num << std::endl;
  return &txn;
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
