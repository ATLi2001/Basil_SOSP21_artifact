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

#include <google/protobuf/util/message_differencer.h>

namespace sintrstore {

Client2Client::Client2Client(transport::Configuration *config, Transport *transport,
      uint64_t client_id, int group, bool pingClients,
      Parameters params, KeyManager *keyManager, Verifier *verifier,
      TrueTime &timeServer) :
      PingInitiator(this, transport, config->n),
      client_id(client_id), transport(transport), config(config), group(group),
      timeServer(timeServer), pingClients(pingClients), params(params),
      keyManager(keyManager), verifier(verifier), lastReqId(0UL) {
  
  transport->Register(this, *config, group, client_id); 
}

Client2Client::~Client2Client() {
}

void Client2Client::ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data, void *meta_data) {

  if (type == ping.GetTypeName()) {
    ping.ParseFromString(data);
    HandlePingResponse(ping);
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
  transport->SendMessageToReplica(this, group, replica, ping);
  return true;
}
    
} // namespace sintrstore

