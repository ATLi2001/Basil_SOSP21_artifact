#include "store/benchmark/async/smallbank/bal.h"

#include "store/benchmark/async/smallbank/smallbank_transaction.h"
#include "store/benchmark/async/smallbank/utils.h"

namespace smallbank {

Bal::Bal(const std::string &cust, const uint32_t timeout)
    : SmallbankTransaction(BALANCE), cust(cust), timeout(timeout) {}

Bal::~Bal() {}

int Bal::Execute(SyncClient &client) {
  proto::AccountRow accountRow;
  proto::SavingRow savingRow;
  proto::CheckingRow checkingRow;
  client.Begin();
  Debug("Balance for customer %s", cust.c_str());
  if (!ReadAccountRow(client, cust, accountRow, timeout) ||
      !ReadSavingRow(client, accountRow.customer_id(), savingRow, timeout) ||
      !ReadCheckingRow(client, accountRow.customer_id(), checkingRow,
                       timeout)) {
    client.Abort(timeout);
    Debug("Aborted Balance");
    return 1;
  }
  int commitRes = client.Commit(timeout);
  Debug("Committed Balance %d",
        savingRow.saving_balance() + checkingRow.checking_balance());
  //std::pair<uint32_t, bool> res = std::make_pair(
  //    savingRow.saving_balance() + checkingRow.checking_balance(), true);
  return commitRes;
}

}  // namespace smallbank