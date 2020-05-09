#ifndef _STATS_H_
#define _STATS_H_

#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

class Stats {
 public:
  Stats();
  virtual ~Stats();

  void Increment(const std::string &key, int amount = 1);
  void Add(const std::string &key, int value);
  void AddList(const std::string &key, size_t idx, uint64_t value);

  void ExportJSON(std::ostream &os);
  void ExportJSON(const std::string &file);
  void Merge(const Stats &other);

 private:
  std::mutex mtx;
  std::unordered_map<std::string, int> statInts;
  std::unordered_map<std::string, std::vector<int>> statLists;
  std::unordered_map<std::string, std::vector<std::vector<uint64_t>>> statLoLs;
};
#endif /* _STATS_H_ */
