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

#include "store/sintrstore/endorsement.h"

#include <algorithm>

namespace sintrstore {

Endorsement::Endorsement() : num_endorsements_needed(0) {}
Endorsement::Endorsement(uint64_t num_endorsements_needed) : num_endorsements_needed(num_endorsements_needed) {}
Endorsement::~Endorsement() {}

void Endorsement::UpdateRequirement(const proto::EndorsementPolicyMessage &endorsementPolicyMsg) {
  if (endorsementPolicyMsg.has_weight()) {
    if (endorsementPolicyMsg.weight() > num_endorsements_needed) {
      num_endorsements_needed = endorsementPolicyMsg.weight();
    }
  }

  for (const auto &client_id : endorsementPolicyMsg.access_control_list()) {
    access_control_list.insert(client_id);
  }
}

void Endorsement::AddValidation(const proto::FinishValidateTxnMessage finishValTxnMsg) {
  uint64_t peer_client_id = finishValTxnMsg.client_id();
  if (client_ids_received.find(peer_client_id) == client_ids_received.end()) {
    client_ids_received.insert(peer_client_id);
    endorsements.push_back(finishValTxnMsg);
  }
}

bool Endorsement::IsSatisfied() {
  return (
    (client_ids_received.size() >= num_endorsements_needed) 
    && (std::includes(client_ids_received.begin(), client_ids_received.end(), 
                      access_control_list.begin(), access_control_list.end()))
  );
}

} // namespace sintrstore
