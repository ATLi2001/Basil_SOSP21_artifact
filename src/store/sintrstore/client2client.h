// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/sintr/client2client.h:
 *   Sintr client to client interface.
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

#ifndef _SINTR_CLIENT2CLIENT_H_
#define _SINTR_CLIENT2CLIENT_H_


#include "lib/keymanager.h"
#include "lib/assert.h"
#include "lib/configuration.h"
#include "lib/crypto.h"
#include "lib/message.h"
#include "lib/transport.h"
#include "store/common/timestamp.h"
#include "store/common/truetime.h"
#include "store/common/transaction.h"
#include "store/common/common-proto.pb.h"
#include "store/sintrstore/sintr-proto.pb.h"
#include "store/common/pinginitiator.h"
#include "store/sintrstore/common.h"
#include "store/sintrstore/validation/validation_client.h"

#include <map>
#include <string>
#include <vector>

namespace sintrstore {

class Client2Client : public TransportReceiver, public PingInitiator, public PingTransport {
 public:
  Client2Client(transport::Configuration *config, Transport *transport,
      uint64_t client_id, int group, bool pingClients,
      Parameters params, KeyManager *keyManager, Verifier *verifier,
      TrueTime &timeServer, uint64_t client_transport_id);
  virtual ~Client2Client();

  virtual void ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data,
      void *meta_data) override;

  virtual bool SendPing(size_t replica, const PingMessage &ping);

  // start up the sintr validation for current transaction id and name 
  // sends BeginValidateTxnMessage to peers
  void SendBeginValidateTxnMessage(uint64_t id, const std::string &txnState);

  // forward server read reply to other peers
  void SendReadReplyMessage(const proto::ReadReply readReply);

  void SetFailureFlag(bool f) {
    failureActive = f;
  }

 private:

  void HandleBeginValidateTxnMessage(const proto::BeginValidateTxnMessage &beginValidateTxnMessage);
  void HandleReadReplyMessage(const proto::ReadReply &readReply);

  const uint64_t client_id; // Unique ID for this client.
  const uint64_t client_transport_id; // unique transport id for this client
  Transport *transport; // Transport layer.
  transport::Configuration *config;
  const int group; // which group this client belongs to
  TrueTime &timeServer;
  const bool pingClients;
  const Parameters params;
  KeyManager *keyManager;
  Verifier *verifier;
  bool failureActive;

  ValidationClient *valClient;
  uint64_t lastReqId;
  proto::Transaction txn;
  std::map<std::string, std::string> readValues;

  proto::BeginValidateTxnMessage beginValidateTxnMessage;
  proto::ReadReply readReply;
  PingMessage ping;
};

} // namespace sintrstore

#endif /* _SINTR_CLIENT2CLIENT_H_ */
