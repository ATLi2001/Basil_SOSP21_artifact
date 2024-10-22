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
  
  valThread = NULL;
  valClient = new ValidationClient(client_id, params); 
  valParseClient = new ValidationParseClient(10000); // TODO: pass arg for timeout length
  transport->Register(this, *config, group, client_transport_id); 
}

Client2Client::~Client2Client() {
  valThread->join();
  delete valClient;
}

void Client2Client::ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data, void *meta_data) {

  if (type == ping.GetTypeName()) {
    Debug("ping received");
    ping.ParseFromString(data);
    HandlePingResponse(ping);
  }
  else if (type == beginValTxnMsg.GetTypeName()) {
    beginValTxnMsg.ParseFromString(data);
    HandleBeginValidateTxnMessage(remote, beginValTxnMsg);
  }
  else if(type == fwdReadResult.GetTypeName()) {
    fwdReadResult.ParseFromString(data);
    HandleForwardReadResult(fwdReadResult);
  }
  else if(type == finishValTxnMsg.GetTypeName()) {
    finishValTxnMsg.ParseFromString(data);
    HandleFinishValidateTxnMessage(finishValTxnMsg);
  }
  else {
    Panic("Received unexpected message type: %s", type.c_str());
  }
}

bool Client2Client::SendPing(size_t replica, const PingMessage &ping) {
  // do not ping self
  if (replica != client_transport_id) {
    transport->SendMessageToReplica(this, group, replica, ping);
  }
  return true;
}

void Client2Client::SendBeginValidateTxnMessage(uint64_t id, const std::string &txnState) {
  client_seq_num = id;

  proto::BeginValidateTxnMessage beginValTxnMsg = proto::BeginValidateTxnMessage();
  beginValTxnMsg.set_client_id(client_id);
  beginValTxnMsg.set_client_seq_num(id);
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
  beginValTxnMsg.set_allocated_txn_state(protoTxnState);

  Debug("SendToAll beginValTxnMsg");
  transport->SendMessageToAll(this, beginValTxnMsg);
}

void Client2Client::ForwardReadResult(const std::string &key, const std::string &value, 
    const Timestamp &ts, const proto::CommittedProof *proof) {
  Debug("SendToAll ForwardReadResult");
  proto::ForwardReadResult fwdReadResult = proto::ForwardReadResult();
  fwdReadResult.set_client_id(client_id);
  fwdReadResult.set_client_seq_num(client_seq_num);
  // fwdReadResult.set_key(key);
  // fwdReadResult.set_value(value);
  // test data
  fwdReadResult.set_key("0");
  fwdReadResult.set_value("00");
  fwdReadResult.mutable_timestamp()->set_timestamp(ts.getTimestamp());
  fwdReadResult.mutable_timestamp()->set_id(ts.getID());
  if (params.validateProofs) {
    if (proof == NULL) {
      Debug("Missing proof for client %lu, seq num %lu", client_id, client_seq_num);
      return;
    }
    *fwdReadResult.mutable_proof() = *proof;
  }

  transport->SendMessageToAll(this, fwdReadResult);
}

void Client2Client::HandleBeginValidateTxnMessage(const TransportAddress &remote, 
    const proto::BeginValidateTxnMessage &beginValTxnMsg) {
  uint64_t curr_client_id = beginValTxnMsg.client_id();
  uint64_t curr_client_seq_num = beginValTxnMsg.client_seq_num();
  proto::TxnState txnState = beginValTxnMsg.txn_state();
  Debug(
    "HandleBeginValidateTxnMessage: from client %lu, seq num %lu", 
    curr_client_id, 
    curr_client_seq_num
  );
  valClient->SetTxnClientId(curr_client_id);
  valClient->SetTxnClientSeqNum(curr_client_seq_num);

  // create the appropriate validation transaction
  if (valThread != NULL) {
    Debug("valThread->join()");
    valThread->join();
  }
  TransportAddress *remoteCopy = remote.clone();

  valThread = new std::thread([this, txnState, curr_client_id, curr_client_seq_num, remoteCopy](){
    ValidationTransaction *valTxn = valParseClient->Parse(txnState);
    ::SyncClient syncClient(valClient);
    transaction_status_t result = valTxn->Validate(syncClient);

    if (result == COMMITTED) {
      proto::Transaction *txn = valClient->GetCompletedValTxn(curr_client_id, curr_client_seq_num);
      proto::FinishValidateTxnMessage finishValTxnMsg = proto::FinishValidateTxnMessage();
      finishValTxnMsg.set_client_id(client_id);
      *finishValTxnMsg.mutable_txn() = *txn;
      // signature later

      transport->SendMessage(this, *remoteCopy, finishValTxnMsg);
    }

    delete remoteCopy;
    delete valTxn;
  });
}

void Client2Client::HandleForwardReadResult(const proto::ForwardReadResult &fwdReadResult) {
  uint64_t curr_client_id = fwdReadResult.client_id();
  uint64_t curr_client_seq_num = fwdReadResult.client_seq_num();
  std::string curr_key = fwdReadResult.key();
  std::string curr_value = fwdReadResult.value();
  Debug(
    "HandleForwardReadResult: from client %lu, seq num %lu, key %s, value %s", 
    curr_client_id, 
    curr_client_seq_num,
    curr_key.c_str(),
    curr_value.c_str()
  );
  // tell valClient about this readReply
  valClient->ValidateForwardReadResult(fwdReadResult);
}

void Client2Client::HandleFinishValidateTxnMessage(const proto::FinishValidateTxnMessage &finishValTxnMsg) {
  uint64_t curr_client_id = finishValTxnMsg.client_id();
  Debug("HandleFinishValidateTxnMessage: from client %lu", curr_client_id);
}

} // namespace sintrstore

