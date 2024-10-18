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

#ifndef _VALIDATION_CLIENT_API_H_
#define _VALIDATION_CLIENT_API_H_

#include "store/common/frontend/client.h"
#include "store/common/promise.h"
#include "store/common/timestamp.h"
#include "store/sintrstore/sintr-proto.pb.h"
#include "store/sintrstore/shardclient.h"

#include <string>
#include <vector>
#include <map>

namespace sintrstore {

// this acts as a dummy workload client for validation of one transaction at a time
// validation transactions will invoke this through a SyncClient interface
class ValidationClient : public ::Client {
 public:
  ValidationClient(uint64_t txn_client_id, uint64_t txn_client_seq_num);
  virtual ~ValidationClient();

  // Begin a transaction.
  virtual void Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry = false, const std::string &txnState = std::string()) override;

  // Get the value corresponding to key.
  virtual void Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) override;

  // Set the value for the given key.
  virtual void Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb, uint32_t timeout) override;

  // Commit all Get(s) and Put(s) since Begin().
  virtual void Commit(commit_callback ccb, commit_timeout_callback ctcb, uint32_t timeout) override;

  // Abort all Get(s) and Put(s) since Begin().
  virtual void Abort(abort_callback acb, abort_timeout_callback atcb, uint32_t timeout) override;

 private:
  struct PendingValidationGet {
    PendingValidationGet(uint64_t reqId) : reqId(reqId) {}
    ~PendingValidationGet() {}
    uint64_t reqId;
    std::string key;
    std::string value;
    proto::Dependency dep;
    bool hasDep;
    read_callback rcb;
    read_timeout_callback rtcb;
  };
  
  bool BufferGet(const std::string &key, read_callback rcb);

  // ID of client that initiated the transaction 
  uint64_t txn_client_id;
  // Ongoing transaction ID.
  uint64_t txn_client_seq_num;
  // Current transaction.
  proto::Transaction txn;
  // ValidationClient internal request id tracking
  uint64_t lastReqId;
  // map of buffered key-value pairs
  std::map<std::string, std::string> readValues;
  // map from internal request id to pending validation get
  std::unordered_map<uint64_t, PendingValidationGet *> pendingGets;
};

} // namespace sintrstore

#endif /* _VALIDATION_CLIENT_API_H_ */
