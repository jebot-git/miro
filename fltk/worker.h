// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "core.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <thread>
#include <vector>
using Json = nlohmann::json;
struct Job {
  int tab = 0;
  unsigned epoch = 0;
  std::string op, key;
  Json args = Json::object();
};
struct Result {
  Job job;
  Json data;
  std::vector<unsigned char> pixels;
};
// MuPDF handles are created, used and destroyed exclusively by this thread.
class Worker {
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<Job> queue;
  std::deque<Result> results;
  std::map<int, DocumentHandle *> documents;
  std::thread thread;
  bool stopping = false;
  void run();

public:
  Worker();
  ~Worker();
  void submit(Job);
  void discard_tiles(int tab, unsigned epoch);
  std::vector<std::string> retain_tiles(int tab,
                                        const std::set<std::string> &visible);
  std::deque<Result> drain();
  std::atomic<unsigned long> rendered{0};
};
