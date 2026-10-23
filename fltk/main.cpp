// SPDX-License-Identifier: AGPL-3.0-only
#include "viewer.h"
#include <FL/Fl.H>
#include <fstream>
#include <iostream>
#include <unistd.h>
int main(int argc, char **argv) {
  Options options;
  std::string stdin_file;
  try {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      auto value = [&]() {
        if (i + 1 >= argc)
          throw std::runtime_error("Missing value for " + arg);
        return std::string(argv[++i]);
      };
      if (arg == "--help" || arg == "-h") {
        std::cout << "Miro FLTK PDF viewer\nUsage: miro-pdf [options] [PDF "
                     "...]\n  --url URL    Open an HTTP(S) PDF\n  -f, "
                     "--fullscreen\n  -p, --presentation\n  --page N     "
                     "Initial page (1-based)\n  --config FILE\n  --version\n";
        return 0;
      }
      if (arg == "--version") {
        std::cout << "Miro 0.12.0-dome.1 (FLTK; upstream d35322c)\n";
        return 0;
      }
      if (arg == "-f" || arg == "--fullscreen")
        options.fullscreen = true;
      else if (arg == "-p" || arg == "--presentation")
        options.presentation = true;
      else if (arg == "--url")
        options.paths.push_back(value());
      else if (arg == "--page")
        options.page = std::max(0, std::stoi(value()) - 1);
      else if (arg == "--config")
        options.config = value();
      else if (arg == "--report")
        options.report = value();
      else if (arg == "--capture")
        options.capture = value();
      else if (arg == "--script")
        options.script = value();
      else if (arg == "--quit-after-ms")
        options.quit_after = std::stoi(value());
      else if (arg == "--") {
        while (++i < argc)
          options.paths.emplace_back(argv[i]);
        break;
      } else if (arg.size() > 1 && arg[0] == '-')
        throw std::runtime_error("Unknown option: " + arg);
      else
        options.paths.push_back(arg);
    }
    // Read a pipe only when stdin has data. Preserve the source until the
    // viewer exits.
    if (options.paths.empty() && !isatty(STDIN_FILENO)) {
      std::cin.peek();
      if (!std::cin.eof()) {
        char name[] = "/tmp/miro-stdin-XXXXXX";
        int fd = mkstemp(name);
        if (fd < 0)
          throw std::runtime_error("Cannot create PDF input file");
        stdin_file = name;
        char buffer[65536];
        size_t total = 0;
        while (std::cin) {
          std::cin.read(buffer, sizeof(buffer));
          auto count = std::cin.gcount();
          total += count;
          if (total > 512 * 1024 * 1024)
            throw std::runtime_error("PDF input exceeds 512 MiB");
          for (std::streamsize offset = 0; offset < count;) {
            ssize_t n = write(fd, buffer + offset, count - offset);
            if (n <= 0)
              throw std::runtime_error("Cannot write PDF input");
            offset += n;
          }
        }
        close(fd);
        options.paths.push_back(stdin_file);
      }
    }
    dx::init();
    Fl::lock();
    {
      Viewer viewer(std::move(options));
      viewer.show();
      if (viewer.options.fullscreen || viewer.options.presentation)
        viewer.fullscreen();
      Fl::run();
    }
    if (!stdin_file.empty())
      unlink(stdin_file.c_str());
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "miro-pdf: " << e.what() << '\n';
    if (!stdin_file.empty())
      unlink(stdin_file.c_str());
    return 1;
  }
}
