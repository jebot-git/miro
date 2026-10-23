// SPDX-License-Identifier: AGPL-3.0-only
#include "worker.h"
#include <FL/Fl.H>
#include <algorithm>
Worker::Worker() : thread([this] { run(); }) {}
Worker::~Worker() {
  {
    std::lock_guard<std::mutex> lock(mutex);
    stopping = true;
    queue.clear();
  }
  miro_cancel_pending();
  changed.notify_one();
  thread.join();
}
void Worker::submit(Job job) {
  std::lock_guard<std::mutex> lock(mutex);
  if (job.op == "search" || job.op == "close") {
    miro_cancel_pending();
    queue.erase(std::remove_if(queue.begin(), queue.end(),
                               [&](const Job &j) {
                                 return j.tab == job.tab &&
                                        (j.op == "search" || job.op == "close");
                               }),
                queue.end());
  }
  queue.push_back(std::move(job));
  changed.notify_one();
}
void Worker::discard_tiles(int tab, unsigned epoch) {
  std::lock_guard<std::mutex> lock(mutex);
  queue.erase(std::remove_if(queue.begin(), queue.end(),
                             [&](const Job &j) {
                               return j.tab == tab && j.op == "render" &&
                                      j.epoch != epoch;
                             }),
              queue.end());
}
std::vector<std::string>
Worker::retain_tiles(int tab, const std::set<std::string> &visible) {
  std::lock_guard<std::mutex> lock(mutex);
  std::vector<std::string> removed;
  queue.erase(std::remove_if(queue.begin(), queue.end(),
                             [&](const Job &j) {
                               if (j.tab == tab && j.op == "render" &&
                                   !visible.count(j.key)) {
                                 removed.push_back(j.key);
                                 return true;
                               }
                               return false;
                             }),
              queue.end());
  return removed;
}
std::deque<Result> Worker::drain() {
  std::lock_guard<std::mutex> lock(mutex);
  std::deque<Result> out;
  out.swap(results);
  return out;
}
void Worker::run() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex);
      changed.wait(lock, [&] { return stopping || !queue.empty(); });
      if (stopping)
        break;
      job = std::move(queue.front());
      queue.pop_front();
    }
    Result result;
    result.job = job;
    try {
      if (job.op == "open") {
        auto &doc = documents[job.tab];
        if (doc)
          miro_close(doc);
        std::string path = job.args.value("path", ""),
                    password = job.args.value("password", "");
        doc = miro_open(path.c_str(), password.c_str());
        result.data =
            Json::parse(doc ? miro_request(doc, "{\"op\":\"metadata\"}")
                            : miro_last_result());
        std::fill(password.begin(), password.end(), '\0');
        if (job.args.contains("password"))
          job.args.erase("password");
        result.job.args.erase("password");
      } else if (job.op == "close") {
        auto it = documents.find(job.tab);
        if (it != documents.end()) {
          miro_close(it->second);
          documents.erase(it);
        }
        continue;
      } else if (job.op == "state")
        result.data = Json::parse(miro_state(job.args.dump().c_str()));
      else {
        auto it = documents.find(job.tab);
        if (it == documents.end() || !it->second)
          continue;
        if (job.op == "render") {
          result.data =
              Json::parse(miro_render(it->second, job.args.dump().c_str()));
          if (!result.data.contains("error")) {
            size_t size = result.data.at("height").get<size_t>() *
                          result.data.at("stride").get<size_t>();
            const unsigned char *p = miro_pixels(it->second);
            result.pixels.assign(p, p + size);
            ++rendered;
          }
        } else
          result.data =
              Json::parse(miro_request(it->second, job.args.dump().c_str()));
      }
    } catch (const std::exception &e) {
      result.data = Json{{"error", e.what()}};
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      results.push_back(std::move(result));
    }
    Fl::awake();
  }
  for (auto &[id, doc] : documents)
    miro_close(doc);
}
