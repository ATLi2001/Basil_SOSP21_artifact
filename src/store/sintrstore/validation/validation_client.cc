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

ValidationClient::ValidationClient(uint64_t client_id, uint64_t nshards, uint64_t ngroups, Partitioner *part) : 
  client_id(client_id), nshards(nshards), ngroups(ngroups), part(part) {}

ValidationClient::~ValidationClient() {
}

void ValidationClient::Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry, const std::string &txnState) {
  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(&txn_client_id, &txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  // insert into pendingValTxnStates map
  pendingValTxnStatesMap::accessor a;
  const bool isNewKey = pendingValTxnStates.insert(a, txn_id);
  if (isNewKey) {
    // create transaction
    proto::Transaction *txn = new proto::Transaction();
    txn->set_client_id(txn_client_id);
    txn->set_client_seq_num(txn_client_seq_num);
    a->second = txn;
  }
  // else not a new key
  // this means that some forwarded read result had already been registered
  // so do not overwrite it

  bcb(txn_client_seq_num);
}

void ValidationClient::Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) {
  // define callback for when get completes
  validation_read_callback vrcb = [gcb, this](int status, uint64_t txn_client_id, uint64_t txn_client_seq_num, 
      const std::string &key, const std::string &value, const Timestamp &ts) {

    Debug("validation_read_callback on key %s, value %s", BytesToHex(key, 16).c_str(), BytesToHex(value, 16).c_str());
    gcb(status, key, value, ts);
  };

  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(&txn_client_id, &txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  // edit the involved groups for txn
  pendingValTxnStatesMap::accessor a;
  if (!pendingValTxnStates.find(a, txn_id)) {
    // Get should always happen after Begin, which inserts at txn_id
    Panic("cannot find transaction %s in pendingValsTxns", txn_id.c_str());
  }
  proto::Transaction *txn = a->second;
  std::vector<int> txnGroups(txn->involved_groups().begin(), txn->involved_groups().end());
  int i = (*part)(key, nshards, -1, txnGroups) % ngroups;
  if (!IsTxnParticipant(txn, i)) {
    txn->add_involved_groups(i);
  }
  a.release();

  // first use accessor to prevent race conditions on pendingGets
  // bad case is BufferGet returns false, but then before registering the pendingGet, 
  // the corresponding ForwardReadResult appears and is processed 
  pendingGetsMap::accessor b;
  const bool isNewKey = pendingGets.insert(b, txn_id);

  // read locally in buffer
  if (BufferGet(txn_client_id, txn_client_seq_num, key, vrcb)) {
    Debug(
      "ValidationClient::BufferGet for client id %lu, seq num %lu, on key %s", 
      txn_client_id,
      txn_client_seq_num,
      BytesToHex(key, 16).c_str()
    );
    return;
  }

  Debug(
    "ValidationClient::Get registering PendingGet for client id %lu, seq num %lu on key %s", 
    txn_client_id, 
    txn_client_seq_num, 
    BytesToHex(key, 16).c_str()
  );

  // otherwise have to wait for read results to get passed over
  PendingValidationGet *pendingGet = new PendingValidationGet(txn_client_id, txn_client_seq_num);
  pendingGet->key = key;
  pendingGet->vrcb = vrcb;
  pendingGet->vrtcb = gtcb;

  if (isNewKey) {
    b->second = std::vector<PendingValidationGet *>();
  }
  b->second.push_back(pendingGet);
}

void ValidationClient::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb,
    uint32_t timeout) {
  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(&txn_client_id, &txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  pendingValTxnStatesMap::accessor a;
  const bool isNewKey = pendingValTxnStates.insert(a, txn_id);
  if (isNewKey) {
    a->second = new proto::Transaction();
  }
  proto::Transaction *txn = a->second;
  WriteMessage *write = txn->add_write_set();
  write->set_key(key);
  write->set_value(value);

  std::vector<int> txnGroups(txn->involved_groups().begin(), txn->involved_groups().end());
  int i = (*part)(key, nshards, -1, txnGroups) % ngroups;
  if (!IsTxnParticipant(txn, i)) {
    txn->add_involved_groups(i);
  }

  pcb(REPLY_OK, key, value);
}

void ValidationClient::Commit(commit_callback ccb, commit_timeout_callback ctcb,
    uint32_t timeout) {
  ccb(COMMITTED);
}

void ValidationClient::Abort(abort_callback acb, abort_timeout_callback atcb,
    uint32_t timeout) {
  // on abort, clean up stored data
  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(&txn_client_id, &txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  pendingValTxnStatesMap::accessor a;
  if (pendingValTxnStates.find(a, txn_id)) {
    pendingValTxnStates.erase(a);
  }
  readValuesMap::accessor b;
  if (readValues.find(b, txn_id)) {
    readValues.erase(b);
  }
  pendingGetsMap::accessor c;
  if (pendingGets.find(c, txn_id)) {
    pendingGets.erase(c);
  }
  txnTimestampsMap::accessor d;
  if (txnTimestamps.find(d, txn_id)) {
    txnTimestamps.erase(d);
  }

  acb();
}

void ValidationClient::SetThreadValTxnId(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  threadValTxnIdsMap::accessor a;
  threadValTxnIds.insert(a, std::this_thread::get_id());
  a->second = std::make_pair(txn_client_id, txn_client_seq_num);
}

void ValidationClient::SetTxnTimestamp(uint64_t txn_client_id, uint64_t txn_client_seq_num, const Timestamp &ts) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  txnTimestampsMap::accessor a;
  txnTimestamps.insert(a, txn_id);
  a->second = ts;
}

void ValidationClient::ProcessForwardReadResult(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    const proto::ForwardReadResult &fwdReadResult, const proto::Dependency &dep, bool hasDep) {
  std::string curr_key = fwdReadResult.key();
  std::string curr_value = fwdReadResult.value();
  Timestamp curr_ts = Timestamp(fwdReadResult.timestamp());
  Debug(
    "ProcessForwardReadResult from client id %lu, seq num %lu for key %s", 
    txn_client_id,
    txn_client_seq_num,
    BytesToHex(curr_key, 16).c_str()
  );

  // lambda for editing txn state
  auto editTxnStateCB = [this, txn_client_id, txn_client_seq_num, 
      &curr_key, &curr_value, &curr_ts, &dep, hasDep]() {
    AddReadset(txn_client_id, txn_client_seq_num, curr_key, curr_value, curr_ts);
    if (hasDep) {
      AddDep(txn_client_id, txn_client_seq_num, dep);
    }
  };

  // find matching pending get by first going off txn client id and sequence number, then key
  // if forwarded read result is for a get that the validation transaction has not yet gotten to,
  // add it to the appropriate transaction readset

  std::string curr_txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  pendingGetsMap::accessor a;
  if (!pendingGets.find(a, curr_txn_id)) {
    Debug(
      "ProcessForwardReadResult from client id %lu, seq num %lu, before txn_id in pendingGets registered for key %s", 
      txn_client_id,
      txn_client_seq_num,
      BytesToHex(curr_key, 16).c_str()
    );
    editTxnStateCB();
    return;
  }

  std::vector<PendingValidationGet *> *reqs = &a->second;
  auto reqs_itr = std::find_if(
    reqs->begin(), reqs->end(), 
    [&curr_key](const PendingValidationGet *req) { return req->key == curr_key; }
  );
  if (reqs_itr == reqs->end()) {
    Debug(
      "ProcessForwardReadResult from client id %lu, seq num %lu, before PendingGet registered for key %s", 
      txn_client_id,
      txn_client_seq_num,
      BytesToHex(curr_key, 16).c_str()
    );
    editTxnStateCB();
    return;
  }
  // callback
  PendingValidationGet *req = *reqs_itr;
  req->ts = curr_ts;
  editTxnStateCB();
  req->vrcb(REPLY_OK, txn_client_id, txn_client_seq_num, req->key, curr_value, req->ts);

  // remove from vector
  reqs->erase(reqs_itr);
  // free memory
  delete req;
}

proto::Transaction *ValidationClient::GetCompletedTxn(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  pendingValTxnStatesMap::const_accessor a;
  if (!pendingValTxnStates.find(a, txn_id)) {
    // GetCompletedValTxn is called after validation has completed
    // so txn_id must be in pendingValTxnStates
    Panic("cannot find transaction %s in pendingValTxnStates", txn_id.c_str());
  }
  proto::Transaction *txn = a->second;

  // add timestamp to txn
  txnTimestampsMap::const_accessor b;
  if (!txnTimestamps.find(b, txn_id)) {
    Panic("cannot find transaction %s in txnTimestamps", txn_id.c_str());
  }
  b->second.serialize(txn->mutable_timestamp());

  Debug(
    "ValidationClient::GetCompletedValTxn called for txn client id %lu, seq num %lu",
    txn_client_id,
    txn_client_seq_num
  );
  pendingValTxnStates.erase(a);
  return txn;
}

bool ValidationClient::BufferGet(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    const std::string &key, validation_read_callback vrcb) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  pendingValTxnStatesMap::const_accessor a;
  if (!pendingValTxnStates.find(a, txn_id)) {
    // BufferGet only happens from Get
    // Get should always happen after Begin, which inserts at txn_id
    Panic("cannot find transaction %s in pendingValsTxns", txn_id.c_str());
  }
  proto::Transaction *txn = a->second;
  for (const auto &write : txn->write_set()) {
    if (write.key() == key) {
      vrcb(REPLY_OK, txn_client_id, txn_client_seq_num, key, write.value(), Timestamp());
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
      vrcb(REPLY_OK, txn_client_id, txn_client_seq_num, key, b->second[key], read.readtime());
      return true;
    }
  }

  return false;
}

void ValidationClient::AddReadset(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    const std::string &key, const std::string &value, const Timestamp &ts) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  pendingValTxnStatesMap::accessor a;
  const bool isNewKeypendingValTxnStates = pendingValTxnStates.insert(a, txn_id);
  // if this txn_id has not been seen yet create a new transaction for it
  // this is possible if ForwardReadResults get ahead of actual validations
  if (isNewKeypendingValTxnStates) {
    a->second = new proto::Transaction();
    a->second->set_client_id(txn_client_id);
    a->second->set_client_seq_num(txn_client_seq_num);
  }

  // add to readset
  proto::Transaction *txn = a->second;
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

void ValidationClient::AddDep(uint64_t txn_client_id, uint64_t txn_client_seq_num, const proto::Dependency &dep) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  pendingValTxnStatesMap::accessor a;
  const bool isNewKeypendingValTxnStates = pendingValTxnStates.insert(a, txn_id);
  // if this txn_id has not been seen yet create a new transaction for it
  // this is possible if ForwardReadResults get ahead of actual validations
  if (isNewKeypendingValTxnStates) {
    a->second = new proto::Transaction();
    a->second->set_client_id(txn_client_id);
    a->second->set_client_seq_num(txn_client_seq_num);
  }

  proto::Transaction *txn = a->second;
  *txn->add_deps() = dep;
}

bool ValidationClient::IsTxnParticipant(proto::Transaction *txn, int g) {
  for (const auto &participant : txn->involved_groups()) {
    if (participant == g) {
      return true;
    }
  }
  return false;
}

void ValidationClient::GetThreadValTxnId(uint64_t *txn_client_id, uint64_t *txn_client_seq_num) {
  threadValTxnIdsMap::const_accessor a;
  if (!threadValTxnIds.find(a, std::this_thread::get_id())) {
    Panic("Current thread does not validate transactions");
  }

  *txn_client_id = a->second.first;
  *txn_client_seq_num = a->second.second;
}

std::string ValidationClient::ToTxnId(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  return std::to_string(txn_client_id) + "_" + std::to_string(txn_client_seq_num);
}

} // namespace sintrstore
