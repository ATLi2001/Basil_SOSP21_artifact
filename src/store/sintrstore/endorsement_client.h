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

#ifndef _SINTR_ENDORSEMENT_H_
#define _SINTR_ENDORSEMENT_H_

#include "store/sintrstore/endorsement_policy.h"
#include "store/sintrstore/sintr-proto.pb.h"
#include "lib/keymanager.h"

#include <vector>
#include <set>
#include <map>

namespace sintrstore {

// this class keeps state for an ongoing transaction endorsement
class EndorsementClient {
 public:
  EndorsementClient(uint64_t client_id, uint64_t client_transport_id, KeyManager *keyManager);
  EndorsementClient(uint64_t client_id, uint64_t client_transport_id, KeyManager *keyManager, EndorsementPolicy policy);
  ~EndorsementClient();

  void SetClientSeqNum(uint64_t client_seq_num);
  void SetExpectedTxnOutput(const std::string &expectedTxnDigest);
  void DebugSetExpectedTxnOutput(const proto::Transaction &expectedTxn);
  void DebugCheck(const proto::Transaction &txn);
  // update current policy by merging with passed in policy
  // returns the difference between current policy and passed in policy
  EndorsementPolicy UpdateRequirement(const EndorsementPolicy &policy);
  void AddValidation(const uint64_t peer_client_id, const std::string &valTxnDigest, 
    const proto::SignedMessage &signedValTxnDigest);
  bool IsSatisfied();
  void Reset();

 private:
  // this client information
  const uint64_t client_id;
  const uint64_t client_transport_id;
  KeyManager *keyManager;
  
  // transaction specific
  uint64_t client_seq_num;
  // expected validation transaction digest
  std::string expectedTxnDigest;
  // debug by checking entire validation txn
  proto::Transaction expectedTxn;
  // endorsement policy which must be satisfied
  EndorsementPolicy policy;
  // which peer clients have endorsed
  std::set<uint64_t> client_ids_received;
  // confirmed endorsement signatures to send to server
  std::vector<proto::SignedMessage> endorsements;
  // also maintain pending endorsements if endorsement comes back before expectedValTxnDigest is set
  // map from client id to digest
  std::map<uint64_t, std::string> pendingEndorsements;
};

} // namespace sintrstore

#endif /* _SINTR_ENDORSEMENT_H_ */
