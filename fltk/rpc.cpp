// SPDX-License-Identifier: AGPL-3.0-only
#include "rpc.h"
#include <FL/Fl.H>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <stdexcept>
Rpc::Rpc(int port, bool lan) {
  GError *error = nullptr;
  listener = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
                          G_SOCKET_PROTOCOL_TCP, &error);
  if (!listener)
    throw std::runtime_error(error ? error->message
                                   : "Cannot create RPC socket");
  GInetAddress *ip =
      g_inet_address_new_from_string(lan ? "0.0.0.0" : "127.0.0.1");
  auto *address = g_inet_socket_address_new(ip, port);
  g_object_unref(ip);
  bool ok = g_socket_bind(listener, address, true, &error) &&
            g_socket_listen(listener, &error);
  g_object_unref(address);
  if (!ok) {
    std::string message = error ? error->message : "Cannot bind RPC socket";
    if (error)
      g_error_free(error);
    g_object_unref(listener);
    listener = nullptr;
    throw std::runtime_error(message);
  }
  g_socket_set_timeout(listener, 1);
  thread = std::thread([this] { run(); });
}
Rpc::~Rpc() {
  stopping = true;
  if (listener) {
    g_socket_close(listener, nullptr);
    thread.join();
    g_object_unref(listener);
  }
}
std::deque<Json> Rpc::drain() {
  std::lock_guard<std::mutex> lock(mutex);
  std::deque<Json> result;
  result.swap(pending);
  return result;
}
void Rpc::run() {
  while (!stopping) {
    GSocket *socket = g_socket_accept(listener, nullptr, nullptr);
    if (!socket)
      continue;
    g_socket_set_timeout(socket, 1);
    bool good = false;
    int code = 400;
    std::string body = "Bad request", request;
    size_t length = 0, header_end = std::string::npos;
    try {
      char buffer[4096];
      while (request.size() < 73728 && !stopping) {
        gssize count =
            g_socket_receive(socket, buffer, sizeof(buffer), nullptr, nullptr);
        if (count <= 0)
          break;
        request.append(buffer, count);
        if (header_end == std::string::npos) {
          header_end = request.find("\r\n\r\n");
          if (header_end == std::string::npos) {
            if (request.size() > 8192)
              break;
            continue;
          }
          if (request.rfind("POST / HTTP/1.", 0) != 0)
            throw std::runtime_error("Use POST /");
          std::string headers = request.substr(0, header_end);
          std::transform(headers.begin(), headers.end(), headers.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          if (headers.find("transfer-encoding:") != std::string::npos)
            throw std::runtime_error("Chunked requests are unsupported");
          auto pos = headers.find("\r\ncontent-length:");
          if (pos == std::string::npos)
            throw std::runtime_error("Content-Length required");
          size_t end = 0;
          auto value = headers.substr(pos + 17);
          length = std::stoul(value, &end);
          if (length > 65536 || length == 0)
            throw std::runtime_error("Invalid body length");
        }
        if (request.size() >= header_end + 4 + length) {
          auto json = Json::parse(request.substr(header_end + 4, length));
          auto message = json.at("message");
          auto type = message.at("type").get<std::string>();
          if (type != "OpenFile" && type != "CloseFile" &&
              type != "ToggleDarkModeUi" && type != "ToggleDarkModePdf")
            throw std::runtime_error("Unknown RPC message");
          if (type == "OpenFile" || type == "CloseFile") {
            auto path = message.at("data").at("path").get<std::string>();
            if (path.empty() || path.find('\0') != std::string::npos)
              throw std::runtime_error("Invalid path");
          }
          {
            std::lock_guard<std::mutex> lock(mutex);
            if (pending.size() >= 128)
              throw std::runtime_error("RPC queue is full");
            pending.push_back(std::move(message));
          }
          Fl::awake();
          good = true;
          code = 200;
          body = "{}";
          break;
        }
      }
    } catch (const std::exception &e) {
      body = e.what();
    }
    std::string response =
        "HTTP/1.1 " + std::to_string(code) + (good ? " OK" : " Bad Request") +
        "\r\nContent-Type: " + (good ? "application/json" : "text/plain") +
        "\r\nConnection: close\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body;
    size_t sent = 0;
    while (sent < response.size()) {
      auto count = g_socket_send(socket, response.data() + sent,
                                 response.size() - sent, nullptr, nullptr);
      if (count <= 0)
        break;
      sent += count;
    }
    g_socket_close(socket, nullptr);
    g_object_unref(socket);
  }
}
