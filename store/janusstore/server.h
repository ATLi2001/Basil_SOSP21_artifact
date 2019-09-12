// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
#ifndef _JANUS_SERVER_H_
#define _JANUS_SERVER_H_

#include "lib/udptransport.h"
#include "replication/common/replica.h"

#include "store/server.h"
#include "store/janusstore/store.h"
#include "store/janusstore/transaction.h"
#include "store/common/timestamp.h"
#include "store/common/truetime.h"
#include "store/janusstore/janus-proto.pb.h"

namespace janusstore {

class Server : public TransportReceiver, public ::Server {
public:
    std::string server_ip;
    uint64_t server_port;
    Server(transport::Configuration config, int myIdx, Transport *transport);
    virtual ~Server();

    void ReceiveMessage(const TransportAddress &remote,
                        const std::string &type, const std::string &data);

    void HandlePreAccept(const TransportAddress &remote,
                         const proto::PreAcceptMessage &pa_msg);

    void HandleAccept(const TransportAddress &remote,
                      const proto::AcceptMessage &a_msg);

    void HandleCommit(const TransportAddress &remote,
                      const proto::CommitMessage c_msg);

private:
    // simple key-value store
    Store *store;

    transport::Configuration config;
    int myIdx;
    // Index of 'this' replica, and handle to transport layer.
    Transport *transport;

    // highest ballot accepted per txn id
    std::unordered_map<uint64_t, uint64_t> accepted_ballots;

    // maps Transaction ids in the graph to ancestor Transaction ids
    std::unordered_map<uint64_t, std::vector<uint64_t>> dep_map;

    // maps Transaction ids to Transcation objects
    std::unordered_map<uint64_t, Transaction> id_txn_map;

    // maps Txn to locally processed status
    std::unordered_map<uint64_t, bool> processed;

    // maps keys to transaction ids that read it
    // TODO ensure that this map is cleared per transaction
    std::unordered_map<std::string, std::vector<uint64_t>> read_key_txn_map;

    // maps keys to transaction ids that write to it
    // TODO ensure that this map is cleared per transaction
    std::unordered_map<std::string, std::vector<uint64_t>> write_key_txn_map;

    // maps txn_id -> list[other_ids] being blocked by txn_id
    std::unordered_map<uint64_t, std::vector<uint64_t>> blocking_ids;

    // functions to process shardclient requests
    // must take in a full Transaction object in order to correctly bookkeep and commit

    // returns the list of dependencies for given txn, NULL if PREACCEPT-NOT-OK
    std::vector<uint64_t> BuildDepList(Transaction txn, uint64_t ballot);

    // TODO figure out what T.abandon and T.result are
    void _HandleCommit(uint64_t txn_id, std::vector<uint64_t> deps);

    void _SendInquiry(uint64_t txn_id);

    std::unordered_map<std::string, std::string> WaitAndInquire(uint64_t txn_id);
    std::unordered_map<std::string, std::string> _ExecutePhase(uint64_t txn_id);
    std::vector<uint64_t> _StronglyConnectedComponent(uint64_t txn_id);
    bool _ReadyToProcess(Transaction txn);

    // TODO determine the return type??
    std::unordered_map<std::string, std::string> Execute(Transaction txn);
    // for cyclic dependency case, compute SCCs and execute in order
    // to be called during the Commit phase from HandleCommitJanusTxn()
    void ResolveContention(std::vector<uint64_t> scc);

    Stats stats;
    void Load(const std::string &key, const std::string &value, Timestamp timestamp);
    inline Stats &GetStats() { return stats; }
};
} // namespace janusstore

#endif /* _JANUS_SERVER_H_ */
