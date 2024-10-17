// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/sintr/client2client.cc:
 *   Sintr client to client.
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

#include "store/sintrstore/client2client.h"
#include "store/sintrstore/validation/validation_client.h"
#include "store/sintrstore/validation/validation_transaction.h"
#include "store/sintrstore/validation/validation_parse_client.h"
#include "store/sintrstore/validation/tpcc/tpcc-validation-proto.pb.h"

#include <google/protobuf/util/message_differencer.h>

namespace sintrstore {

Client2Client::Client2Client(transport::Configuration *config, Transport *transport,
      uint64_t client_id, int group, bool pingClients,
      Parameters params, KeyManager *keyManager, Verifier *verifier,
      TrueTime &timeServer, uint64_t client_transport_id) :
      PingInitiator(this, transport, config->n),
      client_id(client_id), client_transport_id(client_transport_id), transport(transport), config(config), group(group),
      timeServer(timeServer), pingClients(pingClients), params(params),
      keyManager(keyManager), verifier(verifier), lastReqId(0UL) {
  
  transport->Register(this, *config, group, client_transport_id); 
}

Client2Client::~Client2Client() {
}

void Client2Client::ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data, void *meta_data) {

  if (type == ping.GetTypeName()) {
    Debug("ping received");
    ping.ParseFromString(data);
    HandlePingResponse(ping);
  }
  else if (type == beginValidateTxnMessage.GetTypeName()) {
    beginValidateTxnMessage.ParseFromString(data);
    HandleBeginValidateTxnMessage(beginValidateTxnMessage);
  }
  else {
    Panic("Received unexpected message type: %s", type.c_str());
  }

  // if (type == readReply.GetTypeName()) {
  //   if(params.multiThreading){
  //     proto::ReadReply *curr_read = GetUnusedReadReply();
  //     curr_read->ParseFromString(data);
  //     HandleReadReplyMulti(curr_read);
  //   }
  //   else{
  //     readReply.ParseFromString(data);
  //     HandleReadReply(readReply);
  //   }
  // } else if (type == phase1Reply.GetTypeName()) {
  //   phase1Reply.ParseFromString(data);
  //   HandlePhase1Reply(phase1Reply);
  //   // if (params.injectFailure.enabled && params.injectFailure.type == InjectFailureType::CLIENT_EQUIVOCATE) {
  //   //   HandleP1REquivocate(phase1Reply);
  //   // } else {
  //   //   HandlePhase1Reply(phase1Reply);
  //   // }
  // } else if (type == phase2Reply.GetTypeName()) {
  //   phase2Reply.ParseFromString(data);
  //   //Use old handle Read only when proofs/signatures disabled
  //   if(!(params.validateProofs && params.signedMessages)){
  //     HandlePhase2Reply(phase2Reply);
  //   }
  //   else{ //If validateProofs and signMessages are true, then use multi view
  //     HandlePhase2Reply_MultiView(phase2Reply);
  //   }
  //   //HandlePhase2Reply_MultiView(phase2Reply);
  //   //HandlePhase2Reply(phase2Reply);
  // } else if (type == ping.GetTypeName()) {
  //   ping.ParseFromString(data);
  //   HandlePingResponse(ping);

  //   //FALLBACK readMessages
  // } else if(type == relayP1.GetTypeName()){ //receive full TX info for a dependency
  //   relayP1.ParseFromString(data);
  //   HandlePhase1Relay(relayP1); //Call into client to see if still waiting.
  // }
  // else if(type == phase1FBReply.GetTypeName()){
  //   //wait for quorum and relay to client
  //   phase1FBReply.ParseFromString(data);
  //   HandlePhase1FBReply(phase1FBReply); // update pendingFB state -- if complete, upcall to client
  // }
  // else if(type == phase2FBReply.GetTypeName()){
  //   //wait for quorum and relay to client
  //   phase2FBReply.ParseFromString(data);
  //   HandlePhase2FBReply(phase2FBReply); //update pendingFB state -- if complete, upcall to client
  // }
  // else if(type == forwardWB.GetTypeName()){
  //   forwardWB.ParseFromString(data);
  //   HandleForwardWB(forwardWB);
  // }
  // else if(type == sendView.GetTypeName()){
  //   sendView.ParseFromString(data);
  //   HandleSendViewMessage(sendView);
  // }
  // else {
  //   Panic("Received unexpected message type: %s", type.c_str());
  // }
}

bool Client2Client::SendPing(size_t replica, const PingMessage &ping) {
  // do not ping self
  if (replica != client_transport_id) {
    transport->SendMessageToReplica(this, group, replica, ping);
  }
  return true;
}

void Client2Client::SendBeginValidateTxnMessage(uint64_t id, const std::string &txnState) {
  proto::BeginValidateTxnMessage beginValidateTxnMessage = proto::BeginValidateTxnMessage();
  beginValidateTxnMessage.set_client_id(client_id);
  beginValidateTxnMessage.set_client_seq_num(id);
  proto::TxnState *protoTxnState = new proto::TxnState();
  // test data
  ::tpcc::validation::proto::Delivery delivery = ::tpcc::validation::proto::Delivery();
  delivery.set_w_id(0);
  delivery.set_d_id(0);
  delivery.set_o_carrier_id(0);
  delivery.set_ol_delivery_d(0);
  std::string deliveryStr;
  delivery.SerializeToString(&deliveryStr);
  protoTxnState->set_txn_name("tpcc_delivery");
  protoTxnState->set_txn_data(deliveryStr);
  // protoTxnState->ParseFromString(txnState);
  beginValidateTxnMessage.set_allocated_txn_state(protoTxnState);

  Debug("SendToAll beginValidateTxnMessage");
  transport->SendMessageToAll(this, beginValidateTxnMessage);
}

void Client2Client::HandleBeginValidateTxnMessage(const proto::BeginValidateTxnMessage &beginValidateTxnMessage) {
  Debug(
    "HandleBeginValidateTxnMessage: from client %lu, seq num %lu", 
    beginValidateTxnMessage.client_id(), 
    beginValidateTxnMessage.client_seq_num()
  );

  // create the appropriate validation transaction
  ValidationParseClient valParseClient = ValidationParseClient(0);
  ValidationTransaction *valTxn = valParseClient.Parse(beginValidateTxnMessage.txn_state());
  ValidationClient *valClient = new ValidationClient(); 
  ::SyncClient syncClient(valClient);
  valTxn->Validate(syncClient);

  delete valClient;
  delete valTxn;
}

} // namespace sintrstore

