#include "store/mortystore/server.h"

#include <sstream>
#include <sys/time.h>

#include "store/mortystore/common.h"

#include <google/protobuf/util/message_differencer.h>

namespace mortystore {

Server::Server(const transport::Configuration &config, int groupIdx, int idx,
    Transport *transport, bool debugStats, uint64_t prepareBatchPeriod) : config(config),
    groupIdx(groupIdx), idx(idx), transport(transport), debugStats(debugStats),
    prepareBatchPeriod(prepareBatchPeriod) {
  transport->Register(this, config, groupIdx, idx);
  if (prepareBatchPeriod > 0) {
    transport->Timer(prepareBatchPeriod, std::bind(&Server::PrepareBatchTrigger, this));
  }
  _Latency_Init(&readWriteResp, "read_write_response");
}

Server::~Server() {
}

void Server::ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data, void *meta_data) {
  proto::Read read;
  proto::Write write;
  proto::Prepare prepare;
  proto::KO ko;
  proto::Commit commit;
  proto::Abort abort;

  if (type == read.GetTypeName()) {
    read.ParseFromString(data);

    if (Message_DebugEnabled(__FILE__)) {
      std::stringstream ss;
      ss << "Read: ";
      PrintBranch(read.branch(), ss);
      Debug("%s", ss.str().c_str());
    }

    if (debugStats) {
      struct timeval now;
      gettimeofday(&now, NULL);
      uint64_t diff = now.tv_usec - read.ts();
      stats.Add("recv_read_write" + std::to_string(read.branch().txn().id()),
          diff);
      Latency_Start(&readWriteResp);
    }

    HandleRead(remote, read);

    if (debugStats) {
      uint64_t ns = Latency_End(&readWriteResp);
      stats.Add("handle_read_write" + std::to_string(read.branch().txn().id()),
          ns);
    }
  } else if (type == write.GetTypeName()) {
    write.ParseFromString(data);

    if (Message_DebugEnabled(__FILE__)) {
      std::stringstream ss;
      ss << "Write: ";
      PrintBranch(write.branch(), ss);
      Debug("%s", ss.str().c_str());
    }

    if (debugStats) {
      struct timeval now;
      gettimeofday(&now, NULL);
      uint64_t diff = now.tv_usec - write.ts();
      stats.Add("recv_read_write" + std::to_string(write.branch().txn().id()),
          diff);

      Latency_Start(&readWriteResp);
    }

    HandleWrite(remote, write);

    if (debugStats) {
      uint64_t ns = Latency_End(&readWriteResp);
      stats.Add("handle_read_write" + std::to_string(write.branch().txn().id()),
          ns);
    }
  } else if (type == prepare.GetTypeName()) {
    prepare.ParseFromString(data);

    if (Message_DebugEnabled(__FILE__)) {
      std::stringstream ss;
      ss << "Prepare: ";
      PrintBranch(prepare.branch(), ss);
      Debug("%s", ss.str().c_str());
    }

    if (prepareBatchPeriod > 0 ){
      prepareBatch.push_back(std::make_pair(&remote, prepare));
    } else {
      HandlePrepare(remote, prepare);
    }
  } else if (type == ko.GetTypeName()) {
    ko.ParseFromString(data);

    if (Message_DebugEnabled(__FILE__)) {
      std::stringstream ss;
      ss << "KO: ";
      PrintBranch(ko.branch(), ss);
      Debug("%s", ss.str().c_str());
    }

    HandleKO(remote, ko);
  } else if (type == commit.GetTypeName()) {
    commit.ParseFromString(data);

    if (Message_DebugEnabled(__FILE__)) {
      std::stringstream ss;
      ss << "Commit: ";
      PrintBranch(commit.branch(), ss);
      Debug("%s", ss.str().c_str());
    }

    HandleCommit(remote, commit);
  } else if (type == abort.GetTypeName()) {
    abort.ParseFromString(data);

    if (Message_DebugEnabled(__FILE__)) {
      std::stringstream ss;
      ss << "Abort: ";
      PrintBranch(abort.branch(), ss);
      Debug("%s", ss.str().c_str());
    }

    HandleAbort(remote, abort);
  } else {
    Panic("Received unexpected message type: %s", type.c_str());
  }
}

void Server::Load(const std::string &key, const std::string &value,
    const Timestamp timestamp) {
}

/** State Machine Transitions **/

void Server::HandleRead(const TransportAddress &remote, const proto::Read &msg) {
  if (committed_txn_ids.find(msg.branch().txn().id()) != committed_txn_ids.end()) {
    // msg is for already committed txn
    generator.ClearActive(msg.branch().txn().id());
    return;
  }

  if (txn_coordinators.find(msg.branch().txn().id()) == txn_coordinators.end()) {
    txn_coordinators[msg.branch().txn().id()] = &remote;
  }
    
  generator.AddActive(msg.branch());
  SendBranchReplies(msg.branch(), proto::OperationType::READ, msg.key());
}

void Server::HandleWrite(const TransportAddress &remote, const proto::Write &msg) {
  if (committed_txn_ids.find(msg.branch().txn().id()) != committed_txn_ids.end()) {
    // msg is for already committed txn
    generator.ClearActive(msg.branch().txn().id());
    return;
  }

  if (txn_coordinators.find(msg.branch().txn().id()) == txn_coordinators.end()) {
    txn_coordinators[msg.branch().txn().id()] = &remote;
  }

  generator.AddActive(msg.branch());
  SendBranchReplies(msg.branch(), proto::OperationType::WRITE, msg.key());
}

void Server::HandlePrepare(const TransportAddress &remote, const proto::Prepare &msg) {
  if (committed_txn_ids.find(msg.branch().txn().id()) != committed_txn_ids.end()) {
    // msg is for already committed txn
    return;
  }

  if (!CheckBranch(remote, msg.branch())) {
    waiting.push_back(msg.branch());
  }
}

void Server::HandleKO(const TransportAddress &remote, const proto::KO &msg) {
  if (committed_txn_ids.find(msg.branch().txn().id()) != committed_txn_ids.end()) {
    // msg is for already committed txn
    return;
  }

  auto itr = std::find_if(prepared.begin(), prepared.end(), [&](const proto::Transaction &other) {
          return google::protobuf::util::MessageDifferencer::Equals(msg.branch().txn(), other);
        });
  auto jtr = prepared_txn_ids.find(msg.branch().txn().id());
  if (jtr != prepared_txn_ids.end()) {
    prepared_txn_ids.erase(jtr);
  }
  if (itr != prepared.end()) {
    prepared.erase(itr);
    ++itr;
    for (; itr != prepared.end(); ++itr) {
      // TODO: this is probably unsafe, unpreparing all transactions that prepared
      // after this koed txn
      prepared.erase(itr);
    }
  }
}

void Server::HandleCommit(const TransportAddress &remote, const proto::Commit &msg) {
  if (committed_txn_ids.find(msg.branch().txn().id()) != committed_txn_ids.end()) {
    // msg is for already committed txn
    return;
  }

  //committed.push_back(msg.branch().txn());
  store.ApplyTransaction(msg.branch().txn());
  // TODO: this doesn't seem safe. or is it?
  //    We only receive a commit message for this txn when all the participants have responded
  //    PrepareOK ==> participants only respond PrepareOK when theyve received Commit messages for
  //    dependencies
  //    
  //    So any conflicting transactions in prepared will be erased in dependency order
  prepared.erase(std::remove_if(prepared.begin(), prepared.end(),
      [&](const proto::Transaction &txn) {
        return txn.id() == msg.branch().txn().id();
      }), prepared.end());

  committed_txn_ids.insert(msg.branch().txn().id());
  
  generator.ClearActive(msg.branch().txn().id());

  /*auto jtr = txn_coordinators.find(msg.branch().txn().id());
  if (jtr != txn_coordinators.end()) {
    txn_coordinators.erase(jtr);
  }*/

  for (auto itr = waiting.begin(); itr != waiting.end(); ) {
    if (CheckBranch(*txn_coordinators[itr->txn().id()],
          *itr)) {
      waiting.erase(itr);
      for (auto shard : itr->shards()) {
        if (shard != static_cast<uint64_t>(groupIdx)) {
          transport->SendMessage(this, *shards[shard], msg);
        }
      }
    } else {
      ++itr;
    }
  }
}

void Server::HandleAbort(const TransportAddress &remote, const proto::Abort &msg) {
  generator.ClearActive(msg.branch().txn().id());
}

void Server::PrepareBatchTrigger() {
  std::sort(prepareBatch.begin(), prepareBatch.end(), [](const PrepareBatchItem &a,
      const PrepareBatchItem &b) {
    return a.second.branch().txn().id() < b.second.branch().txn().id();
  });
  for (const auto &pbi : prepareBatch) {
    HandlePrepare(*(pbi.first), pbi.second);
  }
  prepareBatch.clear();
  transport->Timer(prepareBatchPeriod, std::bind(&Server::PrepareBatchTrigger, this));
}

/** End State Machine Transitions **/

/** State Machine Helper Functions **/

bool Server::CheckBranch(const TransportAddress &addr, const proto::Branch &branch) {
  if (prepared_txn_ids.find(branch.txn().id()) != prepared_txn_ids.end()) {
    // cannot prepare more than one branch for the same txn
    Debug("Transaction %lu already prepared on different branch.", branch.txn().id());
    proto::PrepareKO reply;
    *reply.mutable_branch() = branch;
    transport->SendMessage(this, addr, reply);
    return true;
  } else if (CommitCompatible(branch, store, prepared, prepared_txn_ids)) {
    prepared.push_back(branch.txn());
    prepared_txn_ids.insert(branch.txn().id());
    proto::PrepareOK reply;
    *reply.mutable_branch() = branch;
    transport->SendMessage(this, addr, reply);
    return true;
  } else if (!WaitCompatible(branch, store, prepared)) {
    if (Message_DebugEnabled(__FILE__)) {
      std::stringstream ss;
      ss << "Branch not compatible with prepared." << std::endl;
      ss << "Branch: " << std::endl;
      PrintBranch(branch, ss);
      ss << std::endl << "Prepared: " << std::endl;
      PrintTransactionList(prepared, ss);
      Debug("%s", ss.str().c_str());
    }

    proto::PrepareKO reply;
    *reply.mutable_branch() = branch;
    transport->SendMessage(this, addr, reply);
    return true;
  } else {
    return false;
  }
}

void Server::SendBranchReplies(const proto::Branch &init,
    proto::OperationType type, const std::string &key) {
  std::vector<proto::Branch> generated_branches;

  uint64_t ns = generator.GenerateBranches(init, type, key, store, generated_branches);
  if (debugStats) {
    stats.Add("generate_branches" + std::to_string(init.txn().id()), ns);
  }
  for (const proto::Branch &branch : generated_branches) {
    const proto::Operation &op = branch.txn().ops()[branch.txn().ops().size() - 1];
    if (op.type() == proto::OperationType::READ) {
      std::string val;
      ValueOnBranch(branch, op.key(), val);
      proto::ReadReply reply;

      struct timeval now;
      gettimeofday(&now, NULL);
      reply.set_ts(now.tv_usec);

      *reply.mutable_branch() =  branch;
      reply.set_key(op.key());
      reply.set_value(val);
      transport->SendMessage(this, *txn_coordinators[branch.txn().id()], reply);
    } else {
      proto::WriteReply reply;
      
      struct timeval now;
      gettimeofday(&now, NULL);
      reply.set_ts(now.tv_usec);

      *reply.mutable_branch() = branch;
      reply.set_key(op.key());
      reply.set_value(op.val());
      transport->SendMessage(this, *txn_coordinators[branch.txn().id()], reply);
    }
  }
}

bool Server::IsStaleMessage(uint64_t txn_id) const {
  return committed_txn_ids.find(txn_id) != committed_txn_ids.end() ||
    aborted_txn_ids.find(txn_id) != aborted_txn_ids.end();
}

/** End State Machine Helper Functions **/

} // namespace mortystore