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
#include "store/sintrstore/common.h"
#include "lib/message.h"

#include <algorithm>
#include <google/protobuf/util/message_differencer.h>

namespace sintrstore {

Endorsement::Endorsement() : num_endorsements_needed(0) {}
Endorsement::Endorsement(uint64_t num_endorsements_needed) : num_endorsements_needed(num_endorsements_needed) {}
Endorsement::~Endorsement() {}

void Endorsement::SetExpectedTxnOutput(const std::string &expectedValTxnDigest) {
  this->expectedValTxnDigest = expectedValTxnDigest;
  
  // now also check pendingEndorsements
  for (auto const &it : pendingEndorsements) {
    if (expectedValTxnDigest == it.second) {
      client_ids_received.insert(it.first);
    }
    else {
      Debug(
        "No match on pending endorsement from client id %lu, txn digest %s; expected txn digest %s",
        it.first,
        BytesToHex(it.second, 16).c_str(),
        BytesToHex(expectedValTxnDigest, 16).c_str()
      );
    }
  }

  pendingEndorsements.clear();
}

void Endorsement::DebugSetExpectedTxnOutput(const proto::ValidationTxn &expectedValTxn) {
  this->expectedValTxn = expectedValTxn;
}
void Endorsement::DebugCheck(const proto::ValidationTxn &valTxn) {
  if (!expectedValTxn.IsInitialized()) {
    return;
  }
  if (valTxn.client_id() != expectedValTxn.client_id()) {
    Debug("client id mismatch: received %lu, expected %lu", valTxn.client_id(), expectedValTxn.client_id());
  }
  if (valTxn.client_seq_num() != expectedValTxn.client_seq_num()) {
    Debug("client seq num mismatch: received %lu, expected %lu", valTxn.client_seq_num(), expectedValTxn.client_seq_num());
  }
  if (valTxn.read_set_size() != expectedValTxn.read_set_size()) {
    Debug("read set mismatch: received size %d, expected size %d", valTxn.read_set_size(), expectedValTxn.read_set_size());
  }
  for (int i = 0; i < expectedValTxn.read_set_size(); i++) {
    if (!google::protobuf::util::MessageDifferencer::Equals(valTxn.read_set(i), expectedValTxn.read_set(i))) {
      Debug(
        "read set mismatch: received key %s, ts %lu.%lu, expected key %s, ts %lu.%lu",
        BytesToHex(valTxn.read_set(i).key(), 16).c_str(),
        valTxn.read_set(i).readtime().timestamp(),
        valTxn.read_set(i).readtime().id(),
        BytesToHex(expectedValTxn.read_set(i).key(), 16).c_str(),
        expectedValTxn.read_set(i).readtime().timestamp(),
        expectedValTxn.read_set(i).readtime().id()
      );
    }
  }
  if (valTxn.write_set_size() != expectedValTxn.write_set_size()) {
    Debug("write set mismatch: received size %d, expected size %d", valTxn.write_set_size(), expectedValTxn.write_set_size());
  }
  for (int i = 0; i < expectedValTxn.write_set_size(); i++) {
    if (!google::protobuf::util::MessageDifferencer::Equals(valTxn.write_set(i), expectedValTxn.write_set(i))) {
      Debug(
        "write set mismatch: received key %s, value %s, expected key %s, value %s",
        BytesToHex(valTxn.write_set(i).key(), 16).c_str(),
        BytesToHex(valTxn.write_set(i).value(), 16).c_str(),
        BytesToHex(expectedValTxn.write_set(i).key(), 16).c_str(),
        BytesToHex(expectedValTxn.write_set(i).value(), 16).c_str()
      );
    }
  }
}

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

void Endorsement::AddValidation(const uint64_t peer_client_id, const std::string &valTxnDigest,
    const proto::SignedMessage &signedValTxnDigest) {
  // if new peer
  if (client_ids_received.find(peer_client_id) == client_ids_received.end()) {
    if (expectedValTxnDigest.length() > 0) {
      // must match expected digest
      if (valTxnDigest == expectedValTxnDigest) {
        client_ids_received.insert(peer_client_id);
        endorsements.push_back(signedValTxnDigest);
      }
      else {
        Debug(
          "No match on endorsement from client id %lu, txn digest %s; expected txn digest %s",
          peer_client_id,
          BytesToHex(valTxnDigest, 16).c_str(),
          BytesToHex(expectedValTxnDigest, 16).c_str()
        );
      }
    }
    else {
      // possible for expected digest to be uninitialized, in which case record a pending endorsement
      pendingEndorsements[peer_client_id] = valTxnDigest;
      Debug("No expectedValTxnDigest yet");
    }
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
