// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
#ifndef _JANUS_SERVER_H_
#define _JANUS_SERVER_H_

#include "replication/common/replica.h"
// #include "store/server.h"

#include "store/janusstore/store.h"
#include "store/janusstore/transaction.h"
#include "store/janusstore/janus-proto.pb.h"

namespace janusstore {

class Server : public replication::common::AppReplica {
public:
    Server();
    virtual ~Server();

    // Invoke callback on the leader, with the option to replicate on success 
    void LeaderUpcall(opnum_t opnum, const string &str1, bool &replicate, string &str2) { return; };

    // Invoke callback on all replicas
    void ReplicaUpcall(opnum_t opnum, const string &str1, string &str2) { return; };

    // Invoke call back for unreplicated operations run on only one replica
    // This will match on a RequestMessage and call a private handler function
    void UnloggedUpcall(const string &str1, string &str2) { };

    // TODO only need this if we are extending store/server.h, but i dont think
    // this is necessary
    // void Load(const string &key, const string &value, const Timestamp timestamp);

private:
    // simple key-value store
    Store *store;

    // highest ballot accepted
    uint64_t accepted_ballot;

    // maps Transactions in the graph to ancestor Transactions
    std::unordered_map<Transaction, std::list<Transaction>> dep_map;

    // functions to process shardclient requests
    // must take in a full Transaction object in order to correctly bookkeep
    // and commit
    void HandlePreAccept(Transaction txn, uint64_t ballot);
    void HandleAccept(Transaction txn, std::vector<std::string> deps, uint64_t ballot);
    void HandleCommitJanusTxn(Transaction txn, std::vector<std::string> deps);

    // for cyclic dependency case, compute SCCs and execute in order
    // to be called during the Commit phase from HandleCommitJanusTxn()
    void ResolveContention();
};
} // namespace janusstore

#endif /* _JANUS_SERVER_H_ */
