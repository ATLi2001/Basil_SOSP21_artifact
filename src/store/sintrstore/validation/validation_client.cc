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
#include "lib/message.h"

namespace sintrstore {

ValidationClient::ValidationClient(uint64_t client_id, Parameters params) : 
  client_id(client_id), params(params) {}

ValidationClient::~ValidationClient() {
}

void ValidationClient::Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry, const std::string &txnState) {
  // create validation transaction
  proto::ValidationTxn *txn = new proto::ValidationTxn();
  txn->set_client_id(txn_client_id);
  txn->set_client_seq_num(txn_client_seq_num);
  // insert into pendingValTxns map
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  pendingValTxnsMap::accessor a;
  pendingValTxns.insert(a, txn_id);
  a->second = txn;

  bcb(txn_client_seq_num);
}

void ValidationClient::Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) {
  // define callback for when get completes
  validation_read_callback vrcb = [gcb, this](int status, uint64_t txn_client_id, uint64_t txn_client_seq_num, 
      const std::string &key, const std::string &value, const Timestamp &ts, bool addReadSet) {

    if (addReadSet) {
      AddReadset(txn_client_id, txn_client_seq_num, key, value, ts);
    }

    Debug("validation_read_callback on key %s, value %s", BytesToHex(key, 16).c_str(), BytesToHex(value, 16).c_str());

    gcb(status, key, value, ts);
  };

  Debug(
    "ValidationClient::Get for client id %lu, seq num %lu on key %s", 
    txn_client_id, 
    txn_client_seq_num, 
    BytesToHex(key, 16).c_str()
  );

  // read locally in buffer
  if (BufferGet(txn_client_id, txn_client_seq_num, key, vrcb)) {
    Debug("ValidationClient::BufferGet on key %s", BytesToHex(key, 16).c_str());
    return;
  }

  // otherwise have to wait for read results to get passed over
  PendingValidationGet *pendingGet = new PendingValidationGet(txn_client_id, txn_client_seq_num);
  pendingGet->key = key;
  pendingGet->vrcb = vrcb;
  pendingGet->vrtcb = gtcb;

  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  pendingGetsMap::accessor a;
  const bool isNewKey = pendingGets.insert(a, txn_id);
  if (isNewKey) {
    a->second = std::vector<PendingValidationGet *>();
  }
  a->second.push_back(pendingGet);
}

void ValidationClient::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb,
    uint32_t timeout) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  pendingValTxnsMap::accessor a;
  const bool isNewKey = pendingValTxns.insert(a, txn_id);
  if (isNewKey) {
    a->second = new proto::ValidationTxn();
  }
  proto::ValidationTxn *txn = a->second;
  WriteMessage *write = txn->add_write_set();
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
  Timestamp curr_ts = Timestamp(fwdReadResult.timestamp());
  Debug(
    "ValidateForwardReadResult from client id %lu, seq num %lu for key %s", 
    curr_client_id,
    curr_client_seq_num,
    BytesToHex(curr_key, 16).c_str()
  );

  // find matching pending get by first going off txn client id and sequence number, then key
  // if forwarded read result is for a get that the validation transaction has not yet gotten to,
  // add it to the appropriate transaction readset

  std::string curr_txn_id = ToTxnId(curr_client_id, curr_client_seq_num);

  pendingGetsMap::accessor a;
  if (!pendingGets.find(a, curr_txn_id)) {
    Debug(
      "ValidateForwardReadResult from client id %lu, seq num %lu, before txn_id in pendingGets registered for key %s", 
      curr_client_id,
      curr_client_seq_num,
      BytesToHex(curr_key, 16).c_str()
    );
    AddReadset(curr_client_id, curr_client_seq_num, curr_key, curr_value, curr_ts);
    return;
  }

  std::vector<PendingValidationGet *> *reqs = &a->second;
  auto reqs_itr = std::find_if(
    reqs->begin(), reqs->end(), 
    [&curr_key](const PendingValidationGet *req) { return req->key == curr_key; }
  );
  if (reqs_itr == reqs->end()) {
    Debug(
      "ValidateForwardReadResult from client id %lu, seq num %lu, before PendingGet registered for key %s", 
      curr_client_id,
      curr_client_seq_num,
      BytesToHex(curr_key, 16).c_str()
    );
    AddReadset(curr_client_id, curr_client_seq_num, curr_key, curr_value, curr_ts);
    return;
  }
  // callback
  PendingValidationGet *req = *reqs_itr;
  req->ts = curr_ts;
  req->vrcb(REPLY_OK, curr_client_id, curr_client_seq_num, req->key, curr_value, req->ts, true);

  // remove from vector
  reqs->erase(reqs_itr);
  // free memory
  delete req;
}

proto::ValidationTxn *ValidationClient::GetCompletedValTxn(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  pendingValTxnsMap::const_accessor a;
  if (!pendingValTxns.find(a, txn_id)) {
    // GetCompletedValTxn is called after validation has completed
    // so txn_id must be in pendingValTxns
    Panic("cannot find transaction %s in pendingValsTxns", txn_id.c_str());
  }
  proto::ValidationTxn *txn = a->second;
  Debug(
    "ValidationClient::GetCompletedValTxn called for txn client id %lu, seq num %lu",
    txn_client_id,
    txn_client_seq_num
  );
  pendingValTxns.erase(a);
  return txn;
}

bool ValidationClient::BufferGet(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    const std::string &key, validation_read_callback vrcb) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  pendingValTxnsMap::const_accessor a;
  if (!pendingValTxns.find(a, txn_id)) {
    // BufferGet only happens from Get
    // Get should always happen after Begin, which inserts at txn_id
    Panic("cannot find transaction %s in pendingValsTxns", txn_id.c_str());
  }
  proto::ValidationTxn *txn = a->second;
  for (const auto &write : txn->write_set()) {
    if (write.key() == key) {
      vrcb(REPLY_OK, txn_client_id, txn_client_seq_num, key, write.value(), Timestamp(), false);
      return true;
    }
  }

  for (const auto &read : txn->read_set()) {
    if (read.key() == key) {
      readValuesMap::accessor b;
      if (!readValues.find(b, txn_id)) {
        // readValues should never be out of sync with txn readset
        Panic("cannot find transaction %s in readValues", txn_id.c_str());
      }
      vrcb(REPLY_OK, txn_client_id, txn_client_seq_num, key, b->second[key], read.readtime(), false);
      return true;
    }
  }

  return false;
}

void ValidationClient::AddReadset(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    const std::string &key, const std::string &value, const Timestamp &ts) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  pendingValTxnsMap::accessor a;
  const bool isNewKeyPendingValTxns = pendingValTxns.insert(a, txn_id);
  // if this txn_id has not been seen yet create a new ValidationTxn for it
  // this is possible if ForwardReadResults get ahead of actual validations
  if (isNewKeyPendingValTxns) {
    a->second = new proto::ValidationTxn();
    a->second->set_client_id(txn_client_id);
    a->second->set_client_seq_num(txn_client_seq_num);
  }

  // try to add to readset
  proto::ValidationTxn *txn = a->second;
  for (const auto &read : txn->read_set()) {
    if (read.key() == key) {
      return; // this is a stale request
    }
  }
  ReadMessage *read = txn->add_read_set();
  read->set_key(key);
  ts.serialize(read->mutable_readtime());

  // add to readValues for future BufferGets
  readValuesMap::accessor b;
  const bool isNewKeyReadValues = readValues.insert(b, txn_id);
  if (isNewKeyReadValues) {
    b->second = std::map<std::string, std::string>();
  }
  b->second[key] = value;
}


std::string ValidationClient::ToTxnId(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  return std::to_string(txn_client_id) + "_" + std::to_string(txn_client_seq_num);
}

} // namespace sintrstore
