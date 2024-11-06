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

#include "store/sintrstore/sintr-proto.pb.h"

#include <vector>
#include <set>
#include <map>

namespace sintrstore {

// this class represents the endorsement neccessary for a transaction
class Endorsement {
 public:
  Endorsement();
  Endorsement(uint64_t num_endorsements_needed);
  ~Endorsement();

  void SetExpectedTxnOutput(const std::string &expectedValTxnDigest);
  void DebugSetExpectedTxnOutput(const proto::ValidationTxn &expectedValTxn);
  void DebugCheck(const proto::ValidationTxn &valTxn);
  void UpdateRequirement(const proto::EndorsementPolicyMessage &endorsementPolicyMsg);
  void AddValidation(const uint64_t peer_client_id, const std::string &valTxnDigest, 
    const proto::SignedMessage &signedValTxnDigest);
  bool IsSatisfied();

 private:
  // expected validation transaction digest
  std::string expectedValTxnDigest;
  // debug by checking entire validation txn
  proto::ValidationTxn expectedValTxn;
  // weight based endorsement style
  uint64_t num_endorsements_needed;
  // access control list based
  std::set<uint64_t> access_control_list;
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
