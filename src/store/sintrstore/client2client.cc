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
#include "store/sintrstore/basicverifier.h"
#include "store/sintrstore/validation/validation_client.h"
#include "store/sintrstore/validation/validation_transaction.h"
#include "store/benchmark/async/tpcc/tpcc-validation-proto.pb.h"
#include "store/sintrstore/common.h"

#include <google/protobuf/util/message_differencer.h>

namespace sintrstore {

Client2Client::Client2Client(transport::Configuration *config, transport::Configuration *clients_config, Transport *transport,
      uint64_t client_id, uint64_t client_transport_id, uint64_t nshards, uint64_t ngroups, int group, bool pingClients,
      Parameters params, KeyManager *keyManager, Verifier *verifier,
      Partitioner *part, EndorsementClient *endorseClient) :
      PingInitiator(this, transport, clients_config->n),
      client_id(client_id), client_transport_id(client_transport_id), 
      transport(transport), config(config), clients_config(clients_config), 
      nshards(nshards), ngroups(ngroups),
      group(group), part(part), pingClients(pingClients), params(params),
      keyManager(keyManager), verifier(verifier), endorseClient(endorseClient) {
  
  // separate verifier from main client instance
  clients_verifier = new BasicVerifier(transport);

  valClient = new ValidationClient(client_id, nshards, ngroups, part); 
  valParseClient = new ValidationParseClient(10000); // TODO: pass arg for timeout length
  transport->Register(this, *clients_config, group, client_transport_id); 

  // assume these are somehow secretly shared before hand
  uint64_t idx = client_transport_id;
  for (uint64_t i = 0; i < clients_config->n; i++) {
    if (i > idx) {
      sessionKeys[i] = std::string(8, (char) idx + 0x30) + std::string(8, (char) i + 0x30);
    } else {
      sessionKeys[i] = std::string(8, (char) i + 0x30) + std::string(8, (char) idx + 0x30);
    }
  }

  for (size_t i = 0; i < params.sintr_params.maxValThreads; i++) {
    valThreads.push_back(new std::thread(&Client2Client::ValidationThreadFunction, this));
  }
}

Client2Client::~Client2Client() {
  for (auto t : valThreads) {
    t->join();
  }
  // valThread->join();
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
  else if (type == fwdReadResultMsg.GetTypeName()) {
    fwdReadResultMsg.ParseFromString(data);
    HandleForwardReadResultMessage(fwdReadResultMsg);
  }
  else if (type == finishValTxnMsg.GetTypeName()) {
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

void Client2Client::SendBeginValidateTxnMessage(uint64_t client_seq_num, const std::string &txnState, uint64_t txnStartTime) {
  this->client_seq_num = client_seq_num;

  sentBeginValTxnMsg = proto::BeginValidateTxnMessage();
  sentBeginValTxnMsg.set_client_id(client_id);
  sentBeginValTxnMsg.set_client_seq_num(client_seq_num);
  TxnState *protoTxnState = new TxnState();
  protoTxnState->ParseFromString(txnState);
  sentBeginValTxnMsg.set_allocated_txn_state(protoTxnState);
  sentBeginValTxnMsg.mutable_timestamp()->set_timestamp(txnStartTime);
  sentBeginValTxnMsg.mutable_timestamp()->set_id(client_id);

  Debug("SendToAll beginValTxnMsg");
  for (int i = 0; i < clients_config->n; i++) {
    beginValSent.insert(i);
  }
  transport->SendMessageToAll(this, sentBeginValTxnMsg);
}

void Client2Client::ForwardReadResultMessage(const std::string &key, const std::string &value, const Timestamp &ts,
    const proto::CommittedProof &proof, const std::string &serializedWrite, const std::string &serializedWriteTypeName, 
    const proto::Dependency &dep, bool hasDep, const EndorsementPolicy &policy) {
  
  EndorsementPolicy diffPolicy = endorseClient->UpdateRequirement(policy);
  if (diffPolicy > EndorsementPolicy()) {
    Debug("Initiating more beginValTxnMsg");
    // need to initiate more endorsements
    int numAdditional = static_cast<int>(diffPolicy.GetWeight());
    for (const auto &client_id : diffPolicy.GetAccessControlList()) {
      uint64_t transport_id = ClientIdToTransportId(client_id, params.sintr_params.clientThreadsPerProcess);
      beginValSent.insert(transport_id);
      numAdditional--;
      transport->SendMessageToReplica(this, transport_id, sentBeginValTxnMsg);
    }
    if (numAdditional > 0) {
      // send
    }
  }
  else {
    Debug("Received policy with weight %lu", policy.GetWeight());
  }

  proto::ForwardReadResultMessage fwdReadResultMsg = proto::ForwardReadResultMessage();
  fwdReadResultMsg.set_client_id(client_id);
  fwdReadResultMsg.set_client_seq_num(client_seq_num);
  proto::ForwardReadResult fwdReadResult = proto::ForwardReadResult();
  fwdReadResult.set_key(key);
  fwdReadResult.set_value(value);
  fwdReadResult.mutable_timestamp()->set_timestamp(ts.getTimestamp());
  fwdReadResult.mutable_timestamp()->set_id(ts.getID());

  if (params.sintr_params.signFwdReadResults) {
    proto::SignedMessage signedMsg;
    CreateHMACedMessage(fwdReadResult, signedMsg);
    *fwdReadResultMsg.mutable_signed_fwd_read_result() = signedMsg;
  }
  else {
    *fwdReadResultMsg.mutable_fwd_read_result() = fwdReadResult;
  }

  // this will contain the prepared txn dependency
  if (hasDep) {
    *fwdReadResultMsg.mutable_dep() = dep;
    // must be oneof write or signed write
    *fwdReadResultMsg.mutable_write() = proto::Write();
  }
  else {
    if (params.validateProofs) {
      if (proof.IsInitialized()) {
        *fwdReadResultMsg.mutable_proof() = proof;
      }
      // if no proof then it is possible the value is empty
    }

    // depending on if signatures are enabled and if the value is non empty
    proto::SignedMessage signedWrite;
    proto::Write write;
    if (serializedWriteTypeName == signedWrite.GetTypeName()) {
      signedWrite.ParseFromString(serializedWrite);
      *fwdReadResultMsg.mutable_signed_write() = signedWrite;
    }
    else if (serializedWriteTypeName == write.GetTypeName()) {
      write.ParseFromString(serializedWrite);
      *fwdReadResultMsg.mutable_write() = write;
    }
    else {
      // this should only happen if value is empty
      UW_ASSERT(value.length() == 0);
      *fwdReadResultMsg.mutable_write() = write;
    }
  }

  Debug(
    "SendToAll ForwardReadResult: client id %lu, seq num %lu, key %s, value %s",
    client_id,
    client_seq_num,
    BytesToHex(key, 16).c_str(),
    BytesToHex(value, 16).c_str()
  );
  transport->SendMessageToAll(this, fwdReadResultMsg);
}

void Client2Client::HandleBeginValidateTxnMessage(const TransportAddress &remote, 
    const proto::BeginValidateTxnMessage &beginValTxnMsg) {
  uint64_t curr_client_id = beginValTxnMsg.client_id();
  uint64_t curr_client_seq_num = beginValTxnMsg.client_seq_num();
  TxnState txnState = beginValTxnMsg.txn_state();
  Timestamp ts(beginValTxnMsg.timestamp());
  Debug(
    "HandleBeginValidateTxnMessage: from client id %lu, seq num %lu", 
    curr_client_id, 
    curr_client_seq_num
  );
  ValidationTransaction *valTxn = valParseClient->Parse(txnState);
  TransportAddress *remoteCopy = remote.clone();
  ValidationInfo *valInfo = new ValidationInfo(curr_client_id, curr_client_seq_num, ts, std::move(valTxn), std::move(remoteCopy));
  validationQueue.push(valInfo);
}

void Client2Client::HandleForwardReadResultMessage(const proto::ForwardReadResultMessage &fwdReadResultMsg) {
  uint64_t curr_client_id = fwdReadResultMsg.client_id();
  uint64_t curr_client_seq_num = fwdReadResultMsg.client_seq_num();
  proto::ForwardReadResult fwdReadResult;
  if (params.sintr_params.signFwdReadResults) {
    // first check client signature
    if (!fwdReadResultMsg.has_signed_fwd_read_result()) {
      Debug(
        "Missing client signature on forwarded read result from client id %lu, seq num %lu", 
        curr_client_id, 
        curr_client_seq_num
      );
      return;
    }
    std::string data;
    if (!ValidateHMACedMessage(fwdReadResultMsg.signed_fwd_read_result(), data)) {
      Debug(
        "Invalid client signature on forwarded read result from client id %lu, seq num %lu", 
        curr_client_id, 
        curr_client_seq_num
      );
      return;
    }
    fwdReadResult.ParseFromString(data);
  }
  else {
    fwdReadResult = fwdReadResultMsg.fwd_read_result();
  }

  proto::Write write;
  bool hasDep = false;
  proto::Dependency dep;
  // if has dependency, then this is based on a prepared txn
  if (fwdReadResultMsg.has_dep()) {
    if (params.validateProofs && params.signedMessages && params.verifyDeps) {
      if (!ValidateDependency(fwdReadResultMsg.dep(), config, params.readDepSize, 
          keyManager, verifier)) {
        Debug(
          "Invalid dependency on forwarded read result from client id %lu, seq num %lu",
          curr_client_id, 
          curr_client_seq_num
        );
        return;
      }
    }
    hasDep = true;
    dep = fwdReadResultMsg.dep();
    write = fwdReadResultMsg.dep().write();
  } 
  else {
    // otherwise can check committed proof and signature

    if (params.validateProofs && params.signedMessages) {
      // check server signature
      if (fwdReadResultMsg.has_signed_write()) {
        proto::SignedMessage signedWrite = fwdReadResultMsg.signed_write();
        if (!verifier->Verify(keyManager->GetPublicKey(signedWrite.process_id()),
            signedWrite.data(), signedWrite.signature())) {
          Debug(
            "Invalid server signature on forwarded read result from client id %lu, seq num %lu", 
            curr_client_id, 
            curr_client_seq_num
          );
          return;
        }

        write.ParseFromString(signedWrite.data());
      }
      else {
        if (fwdReadResultMsg.has_write() && fwdReadResultMsg.write().has_committed_value()) {
          Debug(
            "Missing server signature on forwarded read result with committed value from client id %lu, seq num %lu", 
            curr_client_id, 
            curr_client_seq_num
          );
          return;
        }

        write = fwdReadResultMsg.write();
      }
    }
    else {
      write = fwdReadResultMsg.write();
    }
      
    if (params.validateProofs) {
      // check committed proof
      if (write.has_committed_value() && write.has_committed_timestamp()) {
        if (!fwdReadResultMsg.has_proof()) {
          Debug(
            "Missing committed value proof for forwarded read result from client id %lu, seq num %lu",
            curr_client_id,
            curr_client_seq_num
          );
          return;
        }
        
        std::string committedTxnDigest = TransactionDigest(fwdReadResultMsg.proof().txn(), params.hashDigest);
        if (!ValidateTransactionWrite(fwdReadResultMsg.proof(), &committedTxnDigest,
            write.key(), write.committed_value(), write.committed_timestamp(),
            config, params.signedMessages, keyManager, verifier)) {
          Debug(
            "Failed to validate committed value for forwarded read result from client id %lu, seq num %lu",
            curr_client_id,
            curr_client_seq_num
          );
          return;
        }
      }
    }
  }

  std::string curr_key = fwdReadResult.key();
  std::string curr_value = fwdReadResult.value();

  // curr_key is essentially what the forwarding client is claiming is the key
  // write contains the server's claim as to what the key is
  // these two should match
  // also if value is empty, then no need to check since server makes no claims about it
  if (curr_value.length() > 0 && curr_key != write.key()) {
    Debug(
      "Mismatch in forwarded key and the server key: from client id %lu, seq num %lu, forwarded key %s, server key %s",
      curr_client_id, 
      curr_client_seq_num,
      BytesToHex(curr_key, 16).c_str(),
      BytesToHex(write.key(), 16).c_str()
    );
    return;
  }

  Debug(
    "HandleForwardReadResult: from client id %lu, seq num %lu, key %s, value %s", 
    curr_client_id, 
    curr_client_seq_num,
    BytesToHex(curr_key, 16).c_str(),
    BytesToHex(curr_value, 16).c_str()
  );
  // tell valClient about this forwardedReadResult
  valClient->ProcessForwardReadResult(curr_client_id, curr_client_seq_num, fwdReadResult, dep, hasDep);
}

void Client2Client::HandleFinishValidateTxnMessage(const proto::FinishValidateTxnMessage &finishValTxnMsg) {
  uint64_t peer_client_id = finishValTxnMsg.client_id();

  proto::SignedMessage signedMsg;
  proto::ValidationTxnDigest valTxnDigest;
  if (params.sintr_params.signFinishValidation) {
    // verify signature
    if (!finishValTxnMsg.has_signed_validation_txn_digest()) {
      Debug("Missing signed validation txn digest sent from client id %lu", peer_client_id);
      return;
    }
    signedMsg = finishValTxnMsg.signed_validation_txn_digest();
    if(!clients_verifier->Verify(keyManager->GetPublicKey(keyManager->GetClientKeyId(signedMsg.process_id())),
        signedMsg.data(), signedMsg.signature())) {
      Debug("Invalid signature on validation txn digest sent from client id %lu", peer_client_id);
      return;
    }
    if (!valTxnDigest.ParseFromString(signedMsg.data())) {
      Debug("Invalid serialization of validation txn digest sent from client id %lu", peer_client_id);
      return;
    }
  }
  else {
    valTxnDigest = finishValTxnMsg.validation_txn_digest();
  }

  uint64_t intended_client_id = valTxnDigest.client_id();
  if (intended_client_id != client_id) {
    Debug("Received unexpected FinishValidationTxnMessage. Intended for client id %lu, I am client id %lu", intended_client_id, client_id);
    return;
  }
  Debug("HandleFinishValidateTxnMessage: from client id %lu, for my seq num %lu", peer_client_id, valTxnDigest.client_seq_num());

  if (params.sintr_params.debugEndorseCheck) {
    endorseClient->DebugCheck(finishValTxnMsg.val_txn());
  }

  endorseClient->AddValidation(peer_client_id, valTxnDigest.digest(), signedMsg);
}

void Client2Client::ValidationThreadFunction() {
  ::SyncClient syncClient(valClient);
  for(;;) {
    ValidationInfo *valInfo;
    validationQueue.pop(valInfo);
    uint64_t curr_client_id = valInfo->txn_client_id;
    uint64_t curr_client_seq_num = valInfo->txn_client_seq_num;
    Timestamp curr_ts = valInfo->txn_ts;
    ValidationTransaction *valTxn = valInfo->valTxn;
    std::cerr << std::this_thread::get_id() << " will validate for client " << curr_client_id 
              << ", seq num " << curr_client_seq_num << std::endl;

    valClient->SetThreadValTxnId(curr_client_id, curr_client_seq_num);
    valClient->SetTxnTimestamp(curr_client_id, curr_client_seq_num, curr_ts);

    transaction_status_t result = valTxn->Validate(syncClient);

    if (result == COMMITTED) {
      Debug("Completed validation for client id %lu, seq num %lu", curr_client_id, curr_client_seq_num);
      proto::Transaction *txn = valClient->GetCompletedTxn(curr_client_id, curr_client_seq_num);

      // for consistent hashing results
      std::sort(txn->mutable_read_set()->begin(), txn->mutable_read_set()->end(), sortReadByKey);
      std::sort(txn->mutable_write_set()->begin(), txn->mutable_write_set()->end(), sortWriteByKey);
      std::sort(txn->mutable_involved_groups()->begin(), txn->mutable_involved_groups()->end());

      proto::FinishValidateTxnMessage finishValTxnMsg = proto::FinishValidateTxnMessage();
      finishValTxnMsg.set_client_id(client_id);

      // only send over digest, not actual contents
      std::string digest = TransactionDigest(*txn, params.hashDigest);
      proto::ValidationTxnDigest valTxnDigest = proto::ValidationTxnDigest(); 
      valTxnDigest.set_client_id(curr_client_id);
      valTxnDigest.set_client_seq_num(curr_client_seq_num);
      valTxnDigest.set_digest(digest);

      if (params.sintr_params.signFinishValidation) {
        // sign the digest
        proto::SignedMessage signedMessage;
        SignMessage(
          &valTxnDigest, 
          keyManager->GetPrivateKey(keyManager->GetClientKeyId(client_transport_id)), 
          client_transport_id, 
          &signedMessage
        );
        *finishValTxnMsg.mutable_signed_validation_txn_digest() = signedMessage;
      }
      else {
        *finishValTxnMsg.mutable_validation_txn_digest() = valTxnDigest;
      }

      if (params.sintr_params.debugEndorseCheck) {
        *finishValTxnMsg.mutable_val_txn() = *txn;
      }

      transport->SendMessage(this, *valInfo->remote, finishValTxnMsg);
      Debug("transport->SendMessage complete");
    }

    delete valInfo;
    Debug("thread exiting for validation for client id %lu, seq num %lu", curr_client_id, curr_client_seq_num);
  }
}

bool Client2Client::ValidateHMACedMessage(const proto::SignedMessage &signedMessage, std::string &data) {
  data = signedMessage.data();
  proto::HMACs hmacs;
  hmacs.ParseFromString(signedMessage.signature());
  return crypto::verifyHMAC(
    signedMessage.data(), 
    (*hmacs.mutable_hmacs())[client_transport_id], 
    sessionKeys[signedMessage.process_id() % clients_config->n]
  );
}

void Client2Client::CreateHMACedMessage(const ::google::protobuf::Message &msg, proto::SignedMessage& signedMessage) {
  std::string msgData = msg.SerializeAsString();
  signedMessage.set_data(msgData);
  signedMessage.set_process_id(client_transport_id);
  proto::HMACs hmacs;
  for (uint64_t i = 0; i < clients_config->n; i++) {
    (*hmacs.mutable_hmacs())[i] = crypto::HMAC(msgData, sessionKeys[i]);
  }
  signedMessage.set_signature(hmacs.SerializeAsString());
}

} // namespace sintrstore
