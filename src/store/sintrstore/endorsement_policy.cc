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

#include "store/sintrstore/endorsement_policy.h"

#include <algorithm>

namespace sintrstore {

EndorsementPolicy::EndorsementPolicy() : weight(0) {}
EndorsementPolicy::EndorsementPolicy(uint64_t weight) : 
  weight(weight) {}
EndorsementPolicy::EndorsementPolicy(const std::set<uint64_t> &access_control_list) : 
  access_control_list(access_control_list) {}
EndorsementPolicy::EndorsementPolicy(uint64_t weight, const std::set<uint64_t> &access_control_list) : 
  weight(weight), access_control_list(access_control_list) {}
EndorsementPolicy::EndorsementPolicy(const proto::EndorsementPolicyMessage &endorsePolicyMsg) {
  if (endorsePolicyMsg.has_weight()) {
    weight = endorsePolicyMsg.weight();
  }
  for (const auto &client_id : endorsePolicyMsg.access_control_list()) {
    access_control_list.insert(client_id);
  }
}
EndorsementPolicy::~EndorsementPolicy() {}

uint64_t EndorsementPolicy::GetWeight() const {
  return weight;
}
std::set<uint64_t> EndorsementPolicy::GetAccessControlList() const {
  return access_control_list;
}

bool EndorsementPolicy::IsSatisfied(const std::set<uint64_t> &endorsements) {
  return (
    (endorsements.size() >= weight) 
    && (std::includes(endorsements.begin(), endorsements.end(), 
                      access_control_list.begin(), access_control_list.end()))
  );
}

void EndorsementPolicy::MergePolicy(const EndorsementPolicy &other) {
  if (other.GetWeight() > weight) {
    weight = other.GetWeight();
  }
  std::set<uint64_t> other_set = other.GetAccessControlList();
  access_control_list.insert(other_set.begin(), other_set.end());
}

} // namespace sintrstore
