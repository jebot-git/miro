// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "desktop-menu.h"
#include "gpu-window.h"
#include "rpc.h"
#include "worker.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <filesystem>
#include <map>
#include <optional>
#include <set>

struct Options {
  std::vector<std::string> paths;
  std::string config, report, capture, script;
  bool fullscreen = false, presentation = false;
  int quit_after = 0, page = -1;
};
struct Tab {
  int id = 0, page = 0, layout = 0;
  unsigned epoch = 1, search_revision = 0;
  std::string path, name, error;
  Json meta, matches = Json::array();
  std::map<int, Json> pages;
  std::set<int> pending_pages;
  std::vector<int> rotations, history;
  int history_index = -1, match = -1;
  double scale = 1, pan_x = 0, pan_y = 0;
  bool ready = false, dark = true, dark_override = false, fit = true;
  std::filesystem::file_time_type modified{};
  std::string selection;
};
struct Placement {
  int page;
  double x, y, w, h, scale;
};
class Viewer;
class Canvas : public Fl_Gl_Window {
  Viewer &app;
  struct Tile {
    GLuint texture = 0;
    int w = 0, h = 0;
    unsigned long use = 0;
  };
  std::map<std::string, Tile> tiles;
  std::set<std::string> pending;
  std::vector<Result> uploads;
  unsigned long clock = 0;
  int pressed = 0, press_x = 0, press_y = 0, last_x = 0, last_y = 0;
  bool selecting = false, moved = false;
  int selection_page = -1;
  double select_x = 0, select_y = 0, end_x = 0, end_y = 0;
  void draw() override;
  int handle(int) override;

public:
  Canvas(int, int, int, int, Viewer &);
  ~Canvas() override;
  std::vector<Placement> placements() const;
  void receive(Result result);
  void invalidate(int tab);
  void copy_selection(bool all = false);
  void capture(const std::string &);
  unsigned long uploaded = 0;
};
class Viewer : public dx::GpuWindow {
  static void tick_cb(void *);
  static void menu_cb(Fl_Widget *, void *);
  static void search_cb(Fl_Widget *, void *);
  static void choice_cb(Fl_Widget *, void *);
  static void side_cb(Fl_Widget *, void *);
  double start = 0, last_watch = 0;
  uint64_t font_revision = 0;
  bool last_light = false, closing = false, script_done = false,
       captured = false;
  std::string chord;
  double chord_time = 0;
  std::vector<std::string> menu_actions;
  std::vector<Json> sidebar_rows;
  Json store, script_actions;
  int script_index = 0;
  double script_next = 0;
  unsigned next_id = 1;
  void rebuild_menus();
  void sidebar();
  void persist();
  void report();
  void process(Result);
  void tick();
  std::optional<std::string> binding(const std::string &, bool mouse = false);

public:
  Options options;
  Worker worker;
  Json config;
  std::vector<std::unique_ptr<Tab>> tabs;
  int active = -1;
  bool presentation = false, overview = false, sidebar_visible = false,
       search_visible = false, borders = true, link_boxes = false,
       regex = false;
  Fl_Menu_Bar *menu;
  Fl_Choice *tab_choice, *side_choice;
  Fl_Button *open_button, *previous_button, *next_button, *fit_button,
      *side_button;
  Fl_Input *page_input, *search_input;
  Fl_Check_Button *regex_button;
  Fl_Hold_Browser *side_browser;
  Fl_Box *status;
  Canvas *canvas = nullptr;
  std::unique_ptr<DesktopMenu> desktop_menu;
  std::unique_ptr<Rpc> rpc;
  explicit Viewer(Options);
  ~Viewer() override;
  Tab *tab() const;
  void open(const std::string &, const std::string &password = "",
            int retry = -1);
  void action(const std::string &);
  void go(int page, bool remember = true);
  void invalidate();
  void request_page(int);
  void message(const std::string &);
  void layout_widgets();
  void resize(int, int, int, int) override;
  int handle(int) override;
  std::string mode_name() const;
  bool light() const;
  void set_active(int);
  void update_title();
  friend class Canvas;
};
