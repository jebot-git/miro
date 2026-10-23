// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "worker.h"
#include <gio/gio.h>
class Rpc {
  GSocket *listener = nullptr;
  std::atomic<bool> stopping{false};
  std::thread thread;
  std::mutex mutex;
  std::deque<Json> pending;
  void run();

public:
  Rpc(int port, bool lan);
  ~Rpc();
  std::deque<Json> drain();
};
