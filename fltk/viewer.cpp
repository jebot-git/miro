// SPDX-License-Identifier: AGPL-3.0-only
#include "viewer.h"
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <gio/gio.h>
#include <iomanip>
#include <iostream>
#include <png.h>
#include <sstream>
namespace {
double seconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}
std::string basename(const std::string &path) {
  auto n = path.find_last_of('/');
  return n == std::string::npos ? path : path.substr(n + 1);
}
std::string browser_text(std::string s) {
  std::replace(s.begin(), s.end(), '\n', ' ');
  std::replace(s.begin(), s.end(), '\t', ' ');
  if (s.size() > 300)
    s.resize(300);
  return s;
}
void external(const std::string &value) {
  const char *args[] = {"xdg-open", value.c_str(), nullptr};
  GError *error = nullptr;
  if (!g_spawn_async(nullptr, const_cast<char **>(args), nullptr,
                     G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, &error)) {
    fl_alert("%s", error->message);
    g_error_free(error);
  }
}
std::string key_name() {
  int key = Fl::event_key();
  std::string name;
  switch (key) {
  case FL_Escape:
    name = "Escape";
    break;
  case FL_Tab:
    name = "Tab";
    break;
  case FL_Enter:
  case FL_KP_Enter:
    name = "Enter";
    break;
  case FL_Left:
    name = "Left";
    break;
  case FL_Right:
    name = "Right";
    break;
  case FL_Up:
    name = "Up";
    break;
  case FL_Down:
    name = "Down";
    break;
  case FL_Home:
    name = "Home";
    break;
  case FL_End:
    name = "End";
    break;
  case FL_BackSpace:
    name = "Backspace";
    break;
  case FL_Delete:
    name = "Delete";
    break;
  case FL_Insert:
    name = "Insert";
    break;
  case FL_Page_Up:
    name = "PageUp";
    break;
  case FL_Page_Down:
    name = "PageDown";
    break;
  case ' ':
    name = "Space";
    break;
  default:
    if (key > FL_F && key <= FL_F + 35)
      name = "F" + std::to_string(key - FL_F);
    else if (key == '-' || key == FL_KP + '-')
      name = (!(Fl::event_state() & FL_CTRL) && Fl::event_length() == 1 &&
              Fl::event_text()[0] == '_')
                 ? "_"
                 : "Minus";
    else if (key == '+' || (key == '=' && (Fl::event_state() & FL_SHIFT)) ||
             key == FL_KP + '+')
      name = "Plus";
    else if (key >= 32 && key < 127) {
      name = std::string(1, static_cast<char>(key));
      if ((Fl::event_state() & FL_SHIFT) && key >= 'a' && key <= 'z')
        name[0] -= 32;
      else if (!(Fl::event_state() & FL_CTRL) && Fl::event_length() == 1 &&
               static_cast<unsigned char>(Fl::event_text()[0]) >= 32)
        name = Fl::event_text();
    }
  }
  if (name.empty())
    return name;
  bool letter =
      name.size() == 1 && std::isalpha(static_cast<unsigned char>(name[0]));
  bool shift = Fl::event_state() & FL_SHIFT;
  if ((Fl::event_state() & FL_CTRL) && letter) {
    name[0] = std::tolower(name[0]);
  }
  if (shift && ((!letter && (name.size() > 1 && name != "Plus")) ||
                (Fl::event_state() & FL_CTRL && letter)))
    name = "Shift+" + name;
  if (Fl::event_state() & FL_ALT)
    name = "Alt+" + name;
  if (Fl::event_state() & FL_CTRL)
    name = "Ctrl+" + name;
  return name;
}
int shortcut(const std::string &key) {
  int modifiers = 0;
  size_t start = 0;
  while (true) {
    auto pos = key.find('+', start);
    if (pos == std::string::npos)
      break;
    auto part = key.substr(start, pos - start);
    if (part == "Ctrl")
      modifiers |= FL_CTRL;
    else if (part == "Shift")
      modifiers |= FL_SHIFT;
    else if (part == "Alt")
      modifiers |= FL_ALT;
    else
      return 0;
    start = pos + 1;
  }
  auto name = key.substr(start);
  int value = 0;
  if (name.size() == 1)
    value = static_cast<unsigned char>(name[0]);
  else if (name == "Minus")
    value = '-';
  else if (name == "Plus")
    value = '+';
  else if (name == "Tab")
    value = FL_Tab;
  else if (name == "Escape")
    value = FL_Escape;
  else if (name == "Enter")
    value = FL_Enter;
  else if (name == "Space")
    value = ' ';
  else if (name == "Left")
    value = FL_Left;
  else if (name == "Right")
    value = FL_Right;
  else if (name == "Up")
    value = FL_Up;
  else if (name == "Down")
    value = FL_Down;
  else if (name == "PageUp")
    value = FL_Page_Up;
  else if (name == "PageDown")
    value = FL_Page_Down;
  else if (name.size() > 1 && name[0] == 'F') {
    try {
      value = FL_F + std::stoi(name.substr(1));
    } catch (...) {
      return 0;
    }
  }
  return value ? value | modifiers : 0;
}
std::string mouse_name(int button) {
  std::string key = button == 1   ? "MouseLeft"
                    : button == 2 ? "MouseMiddle"
                    : button == 3 ? "MouseRight"
                    : button == 8 ? "MouseBack"
                    : button == 9 ? "MouseForward"
                                  : "";
  if (Fl::event_state() & FL_SHIFT)
    key = "Shift+" + key;
  if (Fl::event_state() & FL_ALT)
    key = "Alt+" + key;
  if (Fl::event_state() & FL_CTRL)
    key = "Ctrl+" + key;
  return key;
}
void colour(Fl_Color c) {
  unsigned char r, g, b;
  Fl::get_color(c, r, g, b);
  glColor3ub(r, g, b);
}
void quad(double x, double y, double w, double h) {
  glBegin(GL_QUADS);
  glVertex2d(x, y);
  glVertex2d(x + w, y);
  glVertex2d(x + w, y + h);
  glVertex2d(x, y + h);
  glEnd();
}
Json state_call(Json request) {
  return Json::parse(miro_state(request.dump().c_str()));
}
} // namespace

Viewer::Viewer(Options opts)
    : dx::GpuWindow(1100, 800, "Miro"), options(std::move(opts)) {
  start = seconds();
  config = Json::parse(
      miro_config(options.config.empty() ? nullptr : options.config.c_str()));
  if (config.contains("error"))
    throw std::runtime_error(config["error"].get<std::string>());
  for (const auto &error : config["errors"])
    std::cerr << "miro-pdf configuration: " << error.get<std::string>() << '\n';
  store = state_call({{"op", "load"}});
  auto preferences = store["preferences"];
  size(std::clamp(preferences.value("width", 1100), 480, 3840),
       std::clamp(preferences.value("height", 800), 320, 2160));
  if (!options.script.empty()) {
    std::ifstream file(options.script);
    file >> script_actions;
    if (!script_actions.is_array())
      throw std::runtime_error("Script must be an array");
  }
  auto &settings = config["settings"];
  sidebar_visible = settings.value("OpenSidebar", false);
  borders = settings.value("PageBorders", true);
  presentation =
      options.presentation || settings.value("OpenPresentation", false);
  options.presentation = presentation;
  options.fullscreen =
      options.fullscreen || settings.value("OpenFullscreen", false);
  regex = settings.value("DefaultSearchMethod", std::string("PlainText")) ==
          "Regex";
  if (settings.value("Rpc", false)) {
    try {
      rpc = std::make_unique<Rpc>(settings.value("RpcPort", 7890),
                                  settings.value("RpcAllowLan", false));
    } catch (const std::exception &e) {
      std::cerr << "miro-pdf RPC: " << e.what() << '\n';
      config["errors"].push_back(e.what());
    }
  }
  xclass("miro-pdf");
  size_range(480, 320);
  menu = new Fl_Menu_Bar(0, 0, w(), 30);
  menu->user_data(this);
  tab_choice = new Fl_Choice(8, 34, 300, 30);
  tab_choice->callback(choice_cb, this);
  open_button = new Fl_Button(314, 34, 70, 30, "Open");
  open_button->callback(
      [](Fl_Widget *, void *p) {
        static_cast<Viewer *>(p)->action("OpenFileFinder");
      },
      this);
  previous_button = new Fl_Button(390, 34, 44, 30, "<");
  previous_button->callback(
      [](Fl_Widget *, void *p) {
        static_cast<Viewer *>(p)->action("PreviousPage");
      },
      this);
  page_input = new Fl_Input(438, 34, 80, 30);
  page_input->when(FL_WHEN_ENTER_KEY_ALWAYS);
  page_input->callback(
      [](Fl_Widget *, void *p) {
        auto &a = *static_cast<Viewer *>(p);
        try {
          a.go(std::stoi(a.page_input->value()) - 1);
        } catch (...) {
          a.update_title();
        }
      },
      this);
  next_button = new Fl_Button(524, 34, 44, 30, ">");
  next_button->callback(
      [](Fl_Widget *, void *p) {
        static_cast<Viewer *>(p)->action("NextPage");
      },
      this);
  fit_button = new Fl_Button(574, 34, 80, 30, "Fit");
  fit_button->callback(
      [](Fl_Widget *, void *p) { static_cast<Viewer *>(p)->action("ZoomFit"); },
      this);
  side_button = new Fl_Button(660, 34, 100, 30, "Sidebar");
  side_button->callback(
      [](Fl_Widget *, void *p) {
        static_cast<Viewer *>(p)->action("ToggleSidebar");
      },
      this);
  search_input = new Fl_Input(70, 70, 400, 30, "Find:");
  search_input->when(FL_WHEN_ENTER_KEY_ALWAYS);
  search_input->callback(search_cb, this);
  regex_button = new Fl_Check_Button(480, 70, 100, 30, "Regex");
  regex_button->value(regex);
  regex_button->callback(
      [](Fl_Widget *, void *p) {
        auto &a = *static_cast<Viewer *>(p);
        a.regex = a.regex_button->value();
        search_cb(a.search_input, p);
      },
      this);
  side_choice = new Fl_Choice(8, 105, 230, 30);
  side_choice->add("Contents|Bookmarks|Search|Comments");
  side_choice->callback(
      [](Fl_Widget *, void *p) { static_cast<Viewer *>(p)->sidebar(); }, this);
  side_browser = new Fl_Hold_Browser(8, 140, 230, 600);
  side_browser->format_char(0);
  side_browser->callback(side_cb, this);
  canvas = new Canvas(246, 105, w() - 246, h() - 135, *this);
  status = new Fl_Box(8, h() - 28, w() - 16, 24);
  status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  end();
  resizable(canvas);
  dx::style(this);
  font_revision = dx::font_revision;
  last_light = light();
  rebuild_menus();
  layout_widgets();
  callback(
      [](Fl_Widget *, void *p) { static_cast<Viewer *>(p)->action("Exit"); },
      this);
  for (const auto &path : options.paths)
    open(path);
  if (tabs.empty())
    message("Open a PDF with Ctrl+O");
  Fl::add_timeout(.02, tick_cb, this);
}
Viewer::~Viewer() {
  Fl::remove_timeout(tick_cb, this);
  persist();
  report();
  rpc.reset();
  desktop_menu.reset();
}
Tab *Viewer::tab() const {
  return active >= 0 && active < (int)tabs.size() ? tabs[active].get()
                                                  : nullptr;
}
bool Viewer::light() const {
  unsigned char r, g, b;
  Fl::get_color(FL_BACKGROUND_COLOR, r, g, b);
  return int(r) + g + b > 384;
}
std::string Viewer::mode_name() const {
  return presentation ? "Presentation" : overview ? "Overview" : "Normal";
}
void Viewer::message(const std::string &text) {
  status->copy_label(text.c_str());
  status->redraw();
}
void Viewer::open(const std::string &input, const std::string &password,
                  int retry) {
  std::string path = input;
  if (path.rfind("file://", 0) == 0) {
    GError *e = nullptr;
    char *p = g_filename_from_uri(path.c_str(), nullptr, &e);
    if (p) {
      path = p;
      g_free(p);
    } else {
      message(e ? e->message : "Invalid file URL");
      if (e)
        g_error_free(e);
      return;
    }
  }
  if (path.rfind("http://", 0) != 0 && path.rfind("https://", 0) != 0) {
    std::error_code ec;
    auto canonical = std::filesystem::canonical(path, ec);
    if (!ec)
      path = canonical.string();
  }
  if (retry < 0)
    for (size_t i = 0; i < tabs.size(); ++i)
      if (tabs[i]->path == path) {
        set_active(i);
        return;
      }
  Tab *t = nullptr;
  if (retry >= 0) {
    for (auto &v : tabs)
      if (v->id == retry)
        t = v.get();
    if (!t)
      return;
  } else {
    auto value = std::make_unique<Tab>();
    value->id = next_id++;
    value->path = path;
    value->name = basename(path);
    value->page = std::max(0, options.page);
    value->dark = !light();
    auto &prefs = store["preferences"];
    if (prefs.contains("documents") && prefs["documents"].contains(path)) {
      auto p = prefs["documents"][path];
      if (options.page < 0)
        value->page = p.value("page", value->page);
      value->layout = p.value("layout", 0);
      value->scale = p.value("scale", 1.0);
    }
    t = value.get();
    tabs.push_back(std::move(value));
    active = tabs.size() - 1;
  }
  t->error.clear();
  t->ready = false;
  worker.submit(
      {t->id, 0, "open", "", {{"path", path}, {"password", password}}});
  update_title();
  message("Opening " + t->name);
  canvas->redraw();
}
void Viewer::set_active(int index) {
  if (index < 0 || index >= (int)tabs.size())
    return;
  active = index;
  sidebar();
  update_title();
  rebuild_menus();
  canvas->redraw();
}
void Viewer::update_title() {
  auto *t = tab();
  std::string title = t ? t->name + " — Miro" : "Miro";
  copy_label(title.c_str());
  tab_choice->clear();
  for (auto &v : tabs) {
    std::string name = v->name;
    std::replace(name.begin(), name.end(), '/', '_');
    std::replace(name.begin(), name.end(), '|', '_');
    tab_choice->add(name.c_str());
  }
  tab_choice->value(active);
  if (t && t->ready) {
    page_input->value(std::to_string(t->page + 1).c_str());
    std::ostringstream text;
    text << t->page + 1 << " / " << t->meta["page_count"] << "    "
         << std::lround(t->scale * 100) << "%";
    if (presentation)
      text << "    Presentation";
    else if (overview)
      text << "    Overview";
    message(text.str());
  } else
    page_input->value("");
}
void Viewer::layout_widgets() {
  int row = std::max(30, dx::size + 14), top = 0;
  fl_font(dx::font, dx::size);
  std::vector<Fl_Widget *> controls = {open_button, previous_button,
                                       page_input,  next_button,
                                       fit_button,  side_button};
  std::vector<int> widths;
  int occupied = 16 + 6 * 6;
  for (auto *widget : controls) {
    int width =
        widget == page_input
            ? row * 2
            : std::max(row + 8, int(std::ceil(fl_width(
                                    widget->label() ? widget->label() : ""))) +
                                    20);
    widths.push_back(width);
    occupied += width;
  }
  int minimum = std::max(480, 130 + occupied);
  size_range(minimum, std::max(320, row * 6));
  if (w() < minimum) {
    size(minimum, h());
    return;
  }
  bool universal = desktop_menu && desktop_menu->attached() && dx::dome();
  if (presentation) {
    menu->hide();
    tab_choice->hide();
    open_button->hide();
    previous_button->hide();
    next_button->hide();
    page_input->hide();
    fit_button->hide();
    side_button->hide();
  } else {
    if (universal)
      menu->hide();
    else {
      menu->show();
      menu->resize(0, top, w(), row);
      top += row;
    }
    tab_choice->show();
    open_button->show();
    previous_button->show();
    next_button->show();
    page_input->show();
    fit_button->show();
    side_button->show();
    int gap = 6, x = 8, tw = std::max(130, w() - occupied);
    tab_choice->resize(x, top + 4, tw, row);
    x += tw + gap;
    for (size_t i = 0; i < controls.size(); ++i) {
      controls[i]->resize(x, top + 4, widths[i], row);
      x += widths[i] + gap;
    }
    top += row + 8;
  }
  if (search_visible && !presentation) {
    search_input->show();
    regex_button->show();
    int label_width = std::max(65, int(std::ceil(fl_width("Find:"))) + 16);
    int regex_width = std::max(125, int(std::ceil(fl_width("Regex"))) + row);
    search_input->resize(label_width, top + 4,
                         std::max(100, w() - label_width - regex_width - 24),
                         row);
    regex_button->resize(w() - regex_width - 8, top + 4, regex_width, row);
    top += row + 8;
  } else {
    search_input->hide();
    regex_button->hide();
  }
  int bottom = presentation ? 0 : row;
  status->resize(8, h() - row, w() - 16, row);
  if (presentation)
    status->hide();
  else
    status->show();
  int side =
      sidebar_visible && !presentation ? std::clamp(w() / 4, 180, 340) : 0;
  if (side) {
    side_choice->show();
    side_browser->show();
    side_choice->resize(8, top + 4, side - 16, row);
    side_browser->resize(8, top + row + 8, side - 16,
                         std::max(1, h() - top - row - bottom - 12));
  } else {
    side_choice->hide();
    side_browser->hide();
  }
  canvas->resize(side, top, w() - side, std::max(1, h() - top - bottom));
  if (auto *t = tab(); t && t->ready && t->fit)
    action("ZoomFit");
  redraw();
}
void Viewer::resize(int X, int Y, int W, int H) {
  dx::GpuWindow::resize(X, Y, W, H);
  if (canvas)
    layout_widgets();
}
void Viewer::invalidate() {
  if (auto *t = tab()) {
    ++t->epoch;
    worker.discard_tiles(t->id, t->epoch);
    canvas->invalidate(t->id);
  }
  canvas->redraw();
  update_title();
}
void Viewer::request_page(int page) {
  auto *t = tab();
  if (!t || !t->ready || t->pages.count(page) || t->pending_pages.count(page))
    return;
  t->pending_pages.insert(page);
  worker.submit(
      {t->id, t->epoch, "page", "", {{"op", "page"}, {"page", page}}});
}
void Viewer::go(int page, bool remember) {
  auto *t = tab();
  if (!t || !t->ready)
    return;
  page = std::clamp(page, 0, t->meta["page_count"].get<int>() - 1);
  if (remember && (t->history.empty() || t->history.back() != t->page)) {
    t->history.resize(t->history_index + 1);
    t->history.push_back(t->page);
    t->history_index = t->history.size() - 1;
  }
  t->page = page;
  if (presentation) {
    t->fit = true;
    action("ZoomFit");
  } else {
    for (auto p : canvas->placements())
      if (p.page == page) {
        t->pan_y += p.y - 20;
        break;
      }
  }
  request_page(page);
  sidebar();
  update_title();
  canvas->redraw();
}
void Viewer::choice_cb(Fl_Widget *, void *p) {
  auto &a = *static_cast<Viewer *>(p);
  a.set_active(a.tab_choice->value());
}
void Viewer::search_cb(Fl_Widget *, void *p) {
  auto &a = *static_cast<Viewer *>(p);
  auto *t = a.tab();
  if (!t || !t->ready)
    return;
  ++t->search_revision;
  t->matches = Json::array();
  t->match = -1;
  std::string needle = a.search_input->value();
  if (needle.empty()) {
    a.canvas->redraw();
    return;
  }
  a.message("Searching…");
  a.worker.submit({t->id,
                   t->search_revision,
                   "search",
                   "",
                   {{"op", "search"}, {"needle", needle}, {"regex", a.regex}}});
}
void Viewer::sidebar() {
  side_browser->clear();
  sidebar_rows.clear();
  auto *t = tab();
  if (!t || !t->ready)
    return;
  auto add = [&](std::string title, Json data) {
    side_browser->add(browser_text(title).c_str());
    sidebar_rows.push_back(std::move(data));
  };
  switch (side_choice->value()) {
  case 0: {
    std::function<void(Json, int)> visit = [&](Json nodes, int depth) {
      for (auto &n : nodes) {
        add(std::string(depth * 2, ' ') + n.value("title", "Untitled"), n);
        if (n.contains("children"))
          visit(n["children"], depth + 1);
      }
    };
    visit(t->meta["outline"], 0);
    break;
  }
  case 1:
    if (store["bookmarks"]["sets"].is_array())
      for (auto &s : store["bookmarks"]["sets"])
        if (s["path"] == t->path)
          for (auto &m : s["marks"])
            add(m.value("name", "Bookmark") + " (" +
                    std::to_string(m.value("page", 0) + 1) + ")",
                m);
    break;
  case 2:
    for (auto &m : t->matches)
      add(std::to_string(m.value("page", 0) + 1) + ": " +
              m.value("snippet", "Match"),
          m);
    break;
  case 3:
    request_page(t->page);
    if (t->pages.count(t->page))
      for (auto &c : t->pages[t->page]["comments"])
        add(c.value("author", "") + ": " + c.value("text", ""), c);
    break;
  }
}
void Viewer::side_cb(Fl_Widget *, void *p) {
  auto &a = *static_cast<Viewer *>(p);
  int n = a.side_browser->value() - 1;
  if (n < 0 || n >= (int)a.sidebar_rows.size())
    return;
  auto row = a.sidebar_rows[n];
  if (row.contains("page") && row["page"].is_number())
    a.go(row["page"].get<int>());
  else if (row.contains("text")) {
    auto *window = new Fl_Double_Window(620, 420, "PDF comment");
    auto *display = new Fl_Text_Display(12, 12, 596, 396);
    auto *buffer = new Fl_Text_Buffer();
    buffer->text(row["text"].get<std::string>().c_str());
    display->buffer(buffer);
    window->resizable(display);
    window->end();
    dx::style(window);
    window->callback(
        [](Fl_Widget *w, void *p) {
          auto *d = static_cast<Fl_Text_Display *>(p);
          auto *b = d->buffer();
          d->buffer(nullptr);
          delete b;
          Fl::delete_widget(w);
        },
        display);
    window->show();
  }
}
void Viewer::rebuild_menus() {
  menu->clear();
  menu_actions.clear();
  auto add = [&](const char *path, const char *action, int flags = 0) {
    menu_actions.emplace_back(action);
    int hotkey = 0;
    for (auto it = config["bindings"].rbegin(); it != config["bindings"].rend();
         ++it)
      if ((*it)["action"] == action && (*it)["mode"] == "Normal" &&
          (*it)["keys"].size() == 1) {
        hotkey = shortcut((*it)["keys"][0]);
        break;
      }
    const std::set<std::string> always = {"OpenFileFinder",
                                          "Exit",
                                          "NextTab",
                                          "PreviousTab",
                                          "ToggleFullscreen",
                                          "TogglePresentationMode",
                                          "ToggleOverviewMode",
                                          "ToggleSidebar",
                                          "OpenSearch",
                                          "Help",
                                          "About",
                                          "ShowBookmarks"};
    if ((!tab() || !tab()->ready) && !always.count(action) &&
        std::string(action).rfind("Recent:", 0) != 0)
      flags |= FL_MENU_INACTIVE;
    menu->add(path, hotkey, menu_cb,
              reinterpret_cast<void *>(menu_actions.size()), flags);
  };
  add("&File/&Open…", "OpenFileFinder");
  if (store["recent"]["files"].is_array())
    for (auto &path : store["recent"]["files"]) {
      auto name = basename(path.get<std::string>());
      std::replace(name.begin(), name.end(), '/', '_');
      add(("&File/Recent/" + name).c_str(),
          ("Recent:" + path.get<std::string>()).c_str());
    }
  add("&File/Save &Copy…", "SaveCopy");
  add("&File/&Reload", "Reload");
  add("&File/&Print…", "PrintPdf", FL_MENU_DIVIDER);
  add("&File/&Close Tab", "CloseTab");
  add("&File/&Quit", "Exit");
  add("&Edit/&Copy", "Copy");
  add("&Edit/Select &All", "SelectAll", FL_MENU_DIVIDER);
  add("&Edit/&Find…", "OpenSearch");
  add("&Edit/Find &Next", "NextSearchResult");
  add("&Edit/Find &Previous", "PreviousSearchResult");
  add("&View/&Sidebar", "ToggleSidebar",
      FL_MENU_TOGGLE | (sidebar_visible ? FL_MENU_VALUE : 0));
  add("&View/Zoom &In", "ZoomIn");
  add("&View/Zoom &Out", "ZoomOut");
  add("&View/&Actual Size", "ZoomHome");
  add("&View/&Fit Page", "ZoomFit");
  add("&View/Fit &Width", "ZoomFitWidth", FL_MENU_DIVIDER);
  add("&View/&Fullscreen", "ToggleFullscreen",
      FL_MENU_TOGGLE | (fullscreen_active() ? FL_MENU_VALUE : 0));
  add("&View/&Presentation", "TogglePresentationMode",
      FL_MENU_TOGGLE | (presentation ? FL_MENU_VALUE : 0));
  add("&View/&Overview", "ToggleOverviewMode",
      FL_MENU_TOGGLE | (overview ? FL_MENU_VALUE : 0));
  add("&View/Dark &Document", "ToggleDarkModePdf",
      FL_MENU_TOGGLE | ((tab() ? tab()->dark : !light()) ? FL_MENU_VALUE : 0));
  add("&View/Page &Borders", "TogglePageBorders",
      FL_MENU_TOGGLE | (borders ? FL_MENU_VALUE : 0));
  add("&View/Link &Hitboxes", "ToggleLinkHitboxes",
      FL_MENU_TOGGLE | (link_boxes ? FL_MENU_VALUE : 0));
  add("&Layout/&Single Page", "SinglePageLayout",
      FL_MENU_RADIO | ((!tab() || tab()->layout == 0) ? FL_MENU_VALUE : 0));
  add("&Layout/&Two Pages", "DoublePageLayout",
      FL_MENU_RADIO | ((tab() && tab()->layout == 1) ? FL_MENU_VALUE : 0));
  add("&Layout/Two Pages with &Title", "DoublePageTitlePageLayout",
      FL_MENU_RADIO | ((tab() && tab()->layout == 2) ? FL_MENU_VALUE : 0));
  add("&Layout/Rotate Page &Clockwise", "RotatePageClockwise");
  add("&Layout/Rotate Page &Counterclockwise", "RotatePageCounterClockwise");
  add("&Layout/Rotate &All Clockwise", "RotateAllPagesClockwise");
  add("&Layout/Rotate All C&ounterclockwise", "RotateAllPagesCounterClockwise");
  add("&Go/&Previous Page", "PreviousPage");
  add("&Go/&Next Page", "NextPage");
  add("&Go/&Go to Page…", "GoToPage");
  add("&Go/&Back", "JumpBack");
  add("&Go/&Forward", "JumpForward");
  add("&Go/Next &Tab", "NextTab");
  add("&Go/Previous T&ab", "PreviousTab");
  add("&Bookmarks/&Add Bookmark…", "AddBookmark");
  add("&Bookmarks/&Show Bookmarks", "ShowBookmarks");
  add("&Bookmarks/&Remove Selected Bookmark", "DeleteBookmark");
  add("&Help/&Keyboard and Mouse", "Help");
  add("&Help/Document &Properties", "Properties");
  add("&Help/&About Miro", "About");
  dx::style(menu);
  if (desktop_menu)
    desktop_menu->refresh();
}
void Viewer::menu_cb(Fl_Widget *w, void *p) {
  auto *a = static_cast<Viewer *>(w->user_data());
  size_t i = reinterpret_cast<size_t>(p);
  if (a && i > 0 && i <= a->menu_actions.size()) {
    std::string command = a->menu_actions[i - 1];
    a->action(command);
  }
}
std::optional<std::string> Viewer::binding(const std::string &key, bool mouse) {
  if (key.empty())
    return std::nullopt;
  std::string candidate = key;
  if (!mouse) {
    if (seconds() - chord_time > 1)
      chord.clear();
    if (!chord.empty())
      candidate = chord + " " + key;
  }
  auto find = [&](const std::string &value) -> std::optional<std::string> {
    bool prefix = false;
    auto &list = config[mouse ? "mouse" : "bindings"];
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
      auto &b = *it;
      if (b.value("mode", "Normal") != mode_name())
        continue;
      std::string sequence;
      for (auto &k : b["keys"]) {
        if (!sequence.empty())
          sequence += ' ';
        sequence += k.get<std::string>();
      }
      if (sequence == value) {
        if (!mouse)
          chord.clear();
        return b["action"].get<std::string>();
      }
      if (sequence.rfind(value + " ", 0) == 0)
        prefix = true;
    }
    if (prefix && !mouse) {
      chord = value;
      chord_time = seconds();
      return std::string("Chord");
    }
    return std::nullopt;
  };
  auto result = find(candidate);
  if (!result && candidate != key) {
    chord.clear();
    result = find(key);
  }
  return result;
}
int Viewer::handle(int event) {
  if ((event == FL_KEYDOWN || event == FL_SHORTCUT) &&
      (Fl::event_state() & FL_META))
    return 0;
  if (event == FL_KEYDOWN || event == FL_SHORTCUT) {
    Fl_Widget *focus = Fl::focus();
    bool editing = focus && (dynamic_cast<Fl_Input_ *>(focus) ||
                             dynamic_cast<Fl_Text_Display *>(focus));
    std::string key = key_name();
    if (editing) {
      if (key == "Escape") {
        if (search_visible) {
          action("CloseSearch");
          return 1;
        }
        canvas->take_focus();
        return 1;
      }
      if (key == "Ctrl+f") {
        action("OpenSearch");
        return 1;
      }
      if (key == "F3" || key == "Shift+F3") {
        action(key == "F3" ? "NextSearchResult" : "PreviousSearchResult");
        return 1;
      }
      if (event == FL_KEYDOWN)
        return dx::GpuWindow::handle(event);
      // Unhandled typing must never activate Vim commands while an input owns
      // focus.
      return 0;
    }
    auto command = binding(key);
    if (command) {
      action(*command);
      return 1;
    }
  }
  return dx::GpuWindow::handle(event);
}
void Viewer::action(const std::string &command) {
  Tab *t = tab();
  if (command == "Chord")
    return;
  if (command.rfind("Recent:", 0) == 0) {
    open(command.substr(7));
    return;
  }
  if (command == "OpenFileFinder") {
    Fl_Native_File_Chooser chooser;
    chooser.title("Open PDF");
    chooser.type(Fl_Native_File_Chooser::BROWSE_MULTI_FILE);
    chooser.filter("PDF documents\t*.pdf\nAll files\t*");
    if (chooser.show() == 0)
      for (int i = 0; i < chooser.count(); ++i)
        open(chooser.filename(i));
    return;
  }
  if (command == "Exit") {
    closing = true;
    persist();
    report();
    std::vector<Fl_Window *> windows;
    for (auto *w = Fl::first_window(); w; w = Fl::next_window(w))
      windows.push_back(w);
    for (auto *w : windows)
      w->hide();
    return;
  }
  if (command == "NextTab" || command == "PreviousTab") {
    if (!tabs.empty())
      set_active((active + (command == "NextTab" ? 1 : tabs.size() - 1)) %
                 tabs.size());
    return;
  }
  if (command == "CloseTab" || command == "CloseActiveTab") {
    if (!t)
      return;
    persist();
    int id = t->id;
    worker.submit({id, 0, "close", "", Json::object()});
    canvas->invalidate(id);
    tabs.erase(tabs.begin() + active);
    active = tabs.empty() ? -1 : std::min(active, (int)tabs.size() - 1);
    sidebar();
    update_title();
    rebuild_menus();
    canvas->redraw();
    return;
  }
  if (command == "ToggleFullscreen") {
    if (fullscreen_active())
      fullscreen_off();
    else
      fullscreen();
    rebuild_menus();
    return;
  }
  if (command == "TogglePresentationMode") {
    presentation = !presentation;
    overview = false;
    layout_widgets();
    if (t && t->ready) {
      t->fit = true;
      action("ZoomFit");
    }
    rebuild_menus();
    return;
  }
  if (command == "ToggleOverviewMode") {
    overview = !overview;
    presentation = false;
    if (t) {
      t->pan_y = 0;
      t->pan_x = 0;
    }
    invalidate();
    layout_widgets();
    rebuild_menus();
    return;
  }
  if (command == "OverviewSelect") {
    overview = false;
    invalidate();
    go(t ? t->page : 0, false);
    rebuild_menus();
    return;
  }
  if (command == "ToggleSidebar" || command == "ShowBookmarks") {
    sidebar_visible = command == "ShowBookmarks" ? true : !sidebar_visible;
    if (command == "ShowBookmarks")
      side_choice->value(1);
    sidebar();
    layout_widgets();
    rebuild_menus();
    return;
  }
  if (command == "OpenSearch") {
    search_visible = true;
    layout_widgets();
    search_input->take_focus();
    search_input->insert_position(0, strlen(search_input->value()));
    return;
  }
  if (command == "CloseSearch") {
    search_visible = false;
    if (t) {
      ++t->search_revision;
      t->matches = Json::array();
      t->match = -1;
    }
    miro_cancel_pending();
    layout_widgets();
    canvas->take_focus();
    return;
  }
  if (command == "ToggleSearchMethod") {
    regex = !regex;
    regex_button->value(regex);
    search_cb(search_input, this);
    return;
  }
  if (command == "ToggleDarkModeUi") {
    if (dx::dome()) {
      message("Use DomeD’s Appearance menu to change the interface theme");
      return;
    }
    bool value = !light();
    Fl::background(value ? 229 : 31, value ? 233 : 37, value ? 239 : 45);
    Fl::background2(value ? 255 : 22, value ? 255 : 28, value ? 255 : 35);
    Fl::foreground(value ? 20 : 230, value ? 20 : 235, value ? 20 : 240);
    Fl::redraw();
    return;
  }
  if (command == "Help" || command == "About") {
    std::string text =
        command == "About"
            ? "Miro FLTK / DomeD\nUpstream: vincent-uden/miro d35322c "
              "(2026-10-19)\nNative interface: jebot-git/miro\nAGPL-3.0; MuPDF "
              "document engine\nInterface: raw BDF fonts through FreeType and "
              "Fontconfig.\n\nhttps://github.com/jebot-git/miro"
            : "Ctrl+O  Open    Ctrl+W  Close tab    Ctrl+Q  Quit\nCtrl+Tab / "
              "Ctrl+Shift+Tab  Switch tabs\nCtrl+F  Find    Enter  Search    "
              "F3 / Shift+F3  Next / previous\nCtrl+G  Go to page    PgUp / "
              "PgDn  Scroll\nCtrl+8  Fit page    Ctrl+9  Fit width    Ctrl+0  "
              "Actual size\nCtrl++ / Ctrl+-  Zoom    F11  Fullscreen    F10  "
              "Presentation\nEnter  Overview    Ctrl+B  Bookmark    "
              "Ctrl+Shift+B  Sidebar\nCtrl+1 / 2 / 3  Single / double / title "
              "layouts\n[ / ]  Rotate page    { / }  Rotate all pages\nLeft "
              "drag: pan    Shift+left drag: select text    Ctrl+C: "
              "copy\nCtrl+wheel: zoom    Click a link: follow\n\nVim keys and "
              "custom bindings: ~/.config/miro-pdf/miro.conf\nSuper+X and all "
              "Super shortcuts belong to the compositor.";
    fl_message("%s", text.c_str());
    return;
  }
  if (!t || !t->ready) {
    if (t && !t->error.empty())
      message(t->error);
    return;
  }
  if (command == "NextPage" || command == "PreviousPage") {
    go(t->page + (command == "NextPage" ? 1 : -1));
    return;
  }
  if (command == "GoToPage") {
    const char *input =
        fl_input("Page (1–%d):", std::to_string(t->page + 1).c_str(),
                 t->meta["page_count"].get<int>());
    if (input)
      try {
        go(std::stoi(input) - 1);
      } catch (...) {
        message("Enter a page number");
      }
    return;
  }
  if (command == "JumpBack" || command == "JumpForward") {
    if (command == "JumpBack") {
      if (t->history_index < 0)
        return;
      if (t->history_index == (int)t->history.size() - 1) {
        t->history.push_back(t->page);
      }
      int page = t->history[t->history_index];
      --t->history_index;
      go(page, false);
    } else if (t->history_index + 2 < (int)t->history.size()) {
      t->history_index += 2;
      int page = t->history[t->history_index];
      --t->history_index;
      go(page, false);
    }
    return;
  }
  if (command == "MoveUp" || command == "MoveDown" || command == "MoveLeft" ||
      command == "MoveRight" || command == "PageUp" || command == "PageDown" ||
      command == "HalfPageUp" || command == "HalfPageDown") {
    double amount = (command == "PageUp" || command == "PageDown")
                        ? canvas->h() * .9
                    : (command == "HalfPageUp" || command == "HalfPageDown")
                        ? canvas->h() * .5
                        : 40;
    if (command == "MoveLeft")
      t->pan_x -= amount;
    else if (command == "MoveRight")
      t->pan_x += amount;
    else if (command == "MoveUp" || command == "PageUp" ||
             command == "HalfPageUp")
      t->pan_y -= amount;
    else
      t->pan_y += amount;
    t->pan_y = std::max(-canvas->h() * .5, t->pan_y);
    canvas->redraw();
    return;
  }
  if (command == "ZoomIn" || command == "ZoomOut" || command == "ZoomHome" ||
      command == "ZoomFit" || command == "ZoomFitWidth") {
    double old = t->scale, scale = old;
    double pwidth = t->meta["bounds"][t->page][2].get<double>() -
                    t->meta["bounds"][t->page][0].get<double>(),
           pheight = t->meta["bounds"][t->page][3].get<double>() -
                     t->meta["bounds"][t->page][1].get<double>();
    if (t->rotations[t->page] % 2)
      std::swap(pwidth, pheight);
    if (command == "ZoomIn")
      scale *= 1.2;
    else if (command == "ZoomOut")
      scale /= 1.2;
    else if (command == "ZoomHome")
      scale = 1;
    else {
      if (t->layout != 0 && !presentation)
        pwidth *= 2;
      scale = (canvas->w() - 40) / std::max(1., pwidth);
      if (command == "ZoomFit")
        scale = std::min(scale, (canvas->h() - 40) / std::max(1., pheight));
    }
    scale = std::clamp(scale, .05, 16.);
    t->fit = command == "ZoomFit";
    t->scale = scale;
    t->pan_y = (t->pan_y + canvas->h() / 2.) * scale / old - canvas->h() / 2.;
    t->pan_x *= scale / old;
    invalidate();
    if (t->fit) {
      for (auto p : canvas->placements())
        if (p.page == t->page) {
          t->pan_y += p.y - std::max(20., (canvas->h() - p.h) / 2.);
          break;
        }
    }
    canvas->redraw();
    return;
  }
  if (command == "SinglePageLayout" || command == "DoublePageLayout" ||
      command == "DoublePageTitlePageLayout") {
    t->layout = command == "SinglePageLayout"   ? 0
                : command == "DoublePageLayout" ? 1
                                                : 2;
    invalidate();
    if (t->fit)
      action("ZoomFit");
    go(t->page, false);
    rebuild_menus();
    return;
  }
  if (command.rfind("Rotate", 0) == 0) {
    int delta = command.find("Counter") != std::string::npos ? 3 : 1;
    if (command.find("All") != std::string::npos)
      for (int &r : t->rotations)
        r = (r + delta) % 4;
    else
      t->rotations[t->page] = (t->rotations[t->page] + delta) % 4;
    invalidate();
    if (t->fit)
      action("ZoomFit");
    return;
  }
  if (command == "ToggleDarkModePdf") {
    t->dark = !t->dark;
    t->dark_override = true;
    invalidate();
    rebuild_menus();
    return;
  }
  if (command == "TogglePageBorders") {
    borders = !borders;
    canvas->redraw();
    rebuild_menus();
    return;
  }
  if (command == "ToggleLinkHitboxes") {
    link_boxes = !link_boxes;
    request_page(t->page);
    canvas->redraw();
    rebuild_menus();
    return;
  }
  if (command == "Reload") {
    int id = t->id;
    ++t->epoch;
    t->pages.clear();
    t->pending_pages.clear();
    t->matches = Json::array();
    t->selection.clear();
    canvas->invalidate(id);
    open(t->path, "", id);
    return;
  }
  if (command == "Copy" || command == "SelectAll") {
    canvas->copy_selection(command == "SelectAll");
    return;
  }
  if (command == "NextSearchResult" || command == "PreviousSearchResult") {
    if (t->matches.empty()) {
      message("No search results");
      return;
    }
    t->match = (t->match +
                (command == "NextSearchResult" ? 1 : t->matches.size() - 1) +
                t->matches.size()) %
               t->matches.size();
    go(t->matches[t->match]["page"].get<int>());
    auto &match = t->matches[t->match];
    if (match["rects"].is_array() && !match["rects"].empty()) {
      auto r = match["rects"][0]["rect"];
      t->pan_y += r[1].get<double>() * t->scale - canvas->h() * .35;
    }
    message(std::to_string(t->match + 1) + " / " +
            std::to_string(t->matches.size()) + " matches");
    canvas->redraw();
    return;
  }
  if (command == "AddBookmark") {
    const char *name = fl_input(
        "Bookmark name:", ("Page " + std::to_string(t->page + 1)).c_str());
    if (name) {
      worker.submit({t->id,
                     0,
                     "state",
                     "bookmark",
                     {{"op", "bookmark_add"},
                      {"path", t->path},
                      {"page", t->page},
                      {"name", name}}});
    }
    return;
  }
  if (command == "DeleteBookmark") {
    int row = side_browser->value() - 1;
    if (side_choice->value() != 1 || row < 0)
      return;
    worker.submit(
        {t->id,
         0,
         "state",
         "bookmark",
         {{"op", "bookmark_delete"}, {"path", t->path}, {"index", row}}});
    return;
  }
  if (command == "SaveCopy") {
    Fl_Native_File_Chooser chooser;
    chooser.title("Save a copy");
    chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
    chooser.preset_file(t->name.c_str());
    chooser.filter("PDF\t*.pdf");
    if (chooser.show() == 0)
      worker.submit({t->id,
                     0,
                     "save_copy",
                     "",
                     {{"op", "save_copy"}, {"path", chooser.filename()}}});
    return;
  }
  if (command == "PrintPdf") {
    if (t->path.rfind("http", 0) == 0) {
      message("Save a local copy before printing");
      return;
    }
    if (fl_choice("Print this PDF with the system print service?", "Cancel",
                  "Print", nullptr) == 1) {
      const char *args[] = {"lp", "--", t->path.c_str(), nullptr};
      GError *error = nullptr;
      if (!g_spawn_async(nullptr, const_cast<char **>(args), nullptr,
                         G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr,
                         &error)) {
        message(error->message);
        g_error_free(error);
      } else
        message("Sent to the system print service");
    }
    return;
  }
  if (command == "Properties") {
    std::ostringstream text;
    text << t->path << "\nTitle: " << t->meta.value("title", "")
         << "\nAuthor: " << t->meta.value("author", "")
         << "\nPages: " << t->meta["page_count"];
    fl_message("%s", text.str().c_str());
    return;
  }
  message("Unknown command: " + command);
}
void Viewer::process(Result result) {
  auto &job = result.job;
  Tab *t = nullptr;
  for (auto &v : tabs)
    if (v->id == job.tab)
      t = v.get();
  if (job.op == "state") {
    if (result.data.contains("error"))
      message(result.data["error"]);
    else if (job.key == "bookmark") {
      store["bookmarks"] = result.data;
      sidebar();
      message("Bookmarks saved");
    }
    return;
  }
  if (!t)
    return;
  if (result.data.contains("error")) {
    std::string error = result.data["error"];
    if (job.op == "open") {
      t->error = error;
      if (error.find("Password") != std::string::npos ||
          error.find("password") != std::string::npos) {
        const char *password =
            fl_password("Password for %s:", "", t->name.c_str());
        if (password) {
          open(t->path, password, t->id);
          return;
        }
      }
    }
    if (job.op == "render")
      canvas->receive(std::move(result));
    else if (job.op != "search" || job.epoch == t->search_revision)
      message(error);
    return;
  }
  if (job.op == "open") {
    t->meta = std::move(result.data);
    t->ready = true;
    t->error.clear();
    int count = t->meta["page_count"].get<int>();
    t->page = std::clamp(t->page, 0, count - 1);
    t->rotations.resize(count, 0);
    std::error_code ec;
    t->modified = std::filesystem::last_write_time(t->path, ec);
    if (t == tab()) {
      action("ZoomFit");
      go(t->page, false);
      sidebar();
      update_title();
      canvas->take_focus();
      rebuild_menus();
    }
    if (t->path.rfind("http", 0) != 0 &&
        t->path.rfind("/tmp/miro-stdin-", 0) != 0) {
      auto recent = state_call({{"op", "recent"}, {"path", t->path}});
      if (!recent.contains("error")) {
        store["recent"] = recent;
        rebuild_menus();
      }
    }
  } else if (job.op == "render")
    canvas->receive(std::move(result));
  else if (job.op == "page") {
    int page = job.args.value("page", 0);
    t->pending_pages.erase(page);
    if (t->ready)
      t->pages[page] = std::move(result.data);
    if (t == tab()) {
      sidebar();
      canvas->redraw();
    }
  } else if (job.op == "search") {
    if (job.epoch != t->search_revision)
      return;
    t->matches = result.data.value("matches", Json::array());
    t->match = -1;
    if (t == tab()) {
      sidebar();
      if (!t->matches.empty())
        action("NextSearchResult");
      else
        message("No matches");
    }
  } else if (job.op == "save_copy")
    message("Copy saved");
}
void Viewer::persist() {
  Json docs = store["preferences"].value("documents", Json::object());
  for (auto &t : tabs)
    if (t->ready && t->path.rfind("/tmp/miro-stdin-", 0) != 0)
      docs[t->path] = {
          {"page", t->page}, {"scale", t->scale}, {"layout", t->layout}};
  auto result = state_call(
      {{"op", "preferences"},
       {"value", {{"documents", docs}, {"width", w()}, {"height", h()}}}});
  if (result.contains("error"))
    std::cerr << "miro-pdf: " << result["error"] << '\n';
}
void Viewer::report() {
  if (options.report.empty())
    return;
  Json report = {
      {"upstream", "d35322c47d53903638558162a5e3153f32dfdc37"},
      {"toolkit", "FLTK"},
      {"font_file", dx::selected_font.file},
      {"font", dx::font_name},
      {"font_size", dx::size},
      {"font_revision", dx::font_revision},
      {"light", light()},
      {"mode", mode_name()},
      {"active", active},
      {"rendered_tiles", worker.rendered.load()},
      {"uploaded_tiles", canvas->uploaded},
      {"menu_attached", desktop_menu && desktop_menu->attached()},
      {"menu_service", desktop_menu ? desktop_menu->serviceName() : ""},
      {"menu_path", desktop_menu ? desktop_menu->objectPath() : ""},
      {"config_errors", config["errors"]},
      {"tabs", Json::array()}};
  for (auto &t : tabs)
    report["tabs"].push_back(
        {{"id", t->id},
         {"path", t->path},
         {"ready", t->ready},
         {"error", t->error},
         {"page", t->page},
         {"layout", t->layout},
         {"scale", t->scale},
         {"dark", t->dark},
         {"matches", t->matches.size()},
         {"selection", t->selection},
         {"page_count", t->ready ? t->meta["page_count"].get<int>() : 0},
         {"epoch", t->epoch},
         {"rotations", t->rotations}});
  std::ofstream file(options.report);
  file << report.dump(2) << '\n';
}
void Viewer::tick_cb(void *p) {
  auto *a = static_cast<Viewer *>(p);
  try {
    a->tick();
  } catch (const std::exception &e) {
    a->message(e.what());
    std::cerr << "miro-pdf: " << e.what() << '\n';
  }
  if (!a->closing)
    Fl::repeat_timeout(.025, tick_cb, p);
}
void Viewer::tick() {
  if (!desktop_menu && shown()) {
    desktop_menu = std::make_unique<DesktopMenu>(*this, *menu);
    layout_widgets();
  }
  bool was_hidden = !menu->visible();
  bool should_hide =
      presentation || (desktop_menu && desktop_menu->attached() && dx::dome());
  if (was_hidden != should_hide)
    layout_widgets();
  for (auto &result : worker.drain())
    process(std::move(result));
  if (rpc)
    for (auto &command : rpc->drain()) {
      auto type = command["type"].get<std::string>();
      if (type == "OpenFile")
        open(command["data"]["path"]);
      else if (type == "CloseFile") {
        std::string path = command["data"]["path"];
        std::error_code ec;
        auto canonical = std::filesystem::canonical(path, ec);
        if (!ec)
          path = canonical.string();
        for (size_t i = 0; i < tabs.size(); ++i)
          if (tabs[i]->path == path) {
            set_active(i);
            action("CloseTab");
            break;
          }
      } else
        action(type);
    }
  bool theme = light();
  if (font_revision != dx::font_revision || theme != last_light) {
    font_revision = dx::font_revision;
    last_light = theme;
    dx::style(this);
    for (auto &t : tabs)
      if (!t->dark_override) {
        t->dark = !theme;
        ++t->epoch;
        canvas->invalidate(t->id);
      }
    layout_widgets();
    rebuild_menus();
  }
  double now = seconds();
  if (now - last_watch > 1) {
    last_watch = now;
    for (auto &t : tabs)
      if (t->ready && t->path.rfind("http", 0) != 0) {
        std::error_code ec;
        auto modified = std::filesystem::last_write_time(t->path, ec);
        if (!ec && modified != t->modified) {
          t->modified = modified;
          if (t.get() == tab())
            action("Reload");
          else {
            t->pages.clear();
            ++t->epoch;
            open(t->path, "", t->id);
          }
        }
      }
    report();
  }
  if (!script_actions.is_null() && script_index < (int)script_actions.size() &&
      now >= script_next) {
    bool all_ready = !tabs.empty();
    for (auto &t : tabs)
      all_ready &= t->ready;
    if (all_ready) {
      auto step = script_actions[script_index++];
      if (step.contains("action"))
        action(step["action"]);
      if (step.contains("open"))
        open(step["open"]);
      if (step.contains("page"))
        go(step["page"].get<int>() - 1);
      if (step.contains("search")) {
        search_input->value(step["search"].get<std::string>().c_str());
        search_cb(search_input, this);
      }
      if (step.contains("active"))
        set_active(step["active"]);
      if (step.contains("bookmark")) {
        auto *t = tab();
        store["bookmarks"] = state_call({{"op", "bookmark_add"},
                                         {"path", t->path},
                                         {"page", t->page},
                                         {"name", step["bookmark"]}});
        sidebar();
      }
      script_next = now + step.value("wait_ms", 300) / 1000.;
    }
  }
  bool script_complete =
      script_actions.is_null() || script_index >= (int)script_actions.size();
  if (!options.capture.empty() && !captured && tab() && tab()->ready &&
      canvas->uploaded > 0 && script_complete && now > script_next + .3) {
    canvas->capture(options.capture);
    captured = true;
  }
  if (options.quit_after > 0 && (now - start) * 1000 >= options.quit_after)
    action("Exit");
}

Canvas::Canvas(int X, int Y, int W, int H, Viewer &a)
    : Fl_Gl_Window(X, Y, W, H), app(a) {
  mode(FL_RGB | FL_ALPHA | FL_DOUBLE);
  end();
}
Canvas::~Canvas() {
  if (context()) {
    make_current();
    for (auto &[key, t] : tiles)
      glDeleteTextures(1, &t.texture);
  }
}
std::vector<Placement> Canvas::placements() const {
  std::vector<Placement> result;
  auto *t = app.tab();
  if (!t || !t->ready)
    return result;
  int count = t->meta["page_count"].get<int>();
  double gap = 20, y = 20 - t->pan_y;
  auto size = [&](int p, double scale) {
    auto b = t->meta["bounds"][p];
    double width = b[2].get<double>() - b[0].get<double>(),
           height = b[3].get<double>() - b[1].get<double>();
    if (t->rotations[p] % 2)
      std::swap(width, height);
    return std::pair<double, double>(width * scale, height * scale);
  };
  if (app.presentation) {
    auto [pw, ph] = size(t->page, t->scale);
    result.push_back({t->page, (w() - pw) / 2. - t->pan_x, (h() - ph) / 2., pw,
                      ph, t->scale});
    return result;
  }
  for (int p = 0; p < count;) {
    int columns =
        app.overview ? std::max(1, w() / 220) : (t->layout == 0 ? 1 : 2);
    if (t->layout == 2 && p == 0 && !app.overview)
      columns = 1;
    columns = std::min(columns, count - p);
    double scale = t->scale;
    if (app.overview) {
      auto [pw, ph] = size(p, 1);
      scale = std::min((w() / columns - gap * 2) / std::max(1., pw),
                       240. / std::max(1., ph));
    }
    std::vector<std::pair<double, double>> sizes;
    double row_width = 0, row_height = 0;
    for (int col = 0; col < columns; ++col) {
      auto dims = size(p + col, scale);
      sizes.push_back(dims);
      row_width += dims.first + (col ? gap : 0);
      row_height = std::max(row_height, dims.second);
    }
    double x = (w() - row_width) / 2. - t->pan_x;
    for (int col = 0; col < columns; ++col) {
      result.push_back(
          {p + col, x, y, sizes[col].first, sizes[col].second, scale});
      x += sizes[col].first + gap;
    }
    y += row_height + gap;
    p += columns;
  }
  return result;
}
void Canvas::receive(Result result) {
  pending.erase(result.job.key);
  auto *t = app.tab();
  if (t && t->id == result.job.tab && t->epoch == result.job.epoch &&
      !result.data.contains("error")) {
    uploads.push_back(std::move(result));
    redraw();
  }
}
void Canvas::invalidate(int tab) {
  std::string prefix = std::to_string(tab) + ":";
  for (auto it = pending.begin(); it != pending.end();)
    if (it->rfind(prefix, 0) == 0)
      it = pending.erase(it);
    else
      ++it;
  redraw();
}
void Canvas::draw() {
  if (!context_valid()) {
    tiles.clear();
    pending.clear();
  }
  glViewport(0, 0, pixel_w(), pixel_h());
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, w(), h(), 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_TEXTURE_2D);
  unsigned char r, g, b;
  Fl::get_color(FL_BACKGROUND2_COLOR, r, g, b);
  glClearColor(r / 255.f, g / 255.f, b / 255.f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  auto *t = app.tab();
  if (!t || !t->ready) {
    gl_color(FL_FOREGROUND_COLOR);
    gl_font(dx::font, dx::size);
    std::string text = t ? (t->error.empty() ? "Opening PDF…" : t->error)
                         : "Open a PDF with Ctrl+O";
    gl_draw(text.c_str(), 20, h() / 2);
    return;
  }
  for (auto &result : uploads) {
    if (result.job.tab != t->id || result.job.epoch != t->epoch)
      continue;
    auto &tile = tiles[result.job.key];
    if (!tile.texture)
      glGenTextures(1, &tile.texture);
    tile.w = result.data["width"];
    tile.h = result.data["height"];
    tile.use = ++clock;
    glBindTexture(GL_TEXTURE_2D, tile.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tile.w, tile.h, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, result.pixels.data());
    ++uploaded;
  }
  uploads.clear();
  while (tiles.size() > 160) {
    auto it =
        std::min_element(tiles.begin(), tiles.end(), [](auto &a, auto &b) {
          return a.second.use < b.second.use;
        });
    glDeleteTextures(1, &it->second.texture);
    tiles.erase(it);
  }
  auto positions = placements();
  std::set<std::string> visible_tiles;
  double ratio = pixel_w() / double(std::max(1, w()));
  int current = t->page;
  double best = 1e30;
  for (auto p : positions) {
    if (p.y + p.h < 0 || p.y > h() || p.x + p.w < 0 || p.x > w())
      continue;
    double distance = std::abs(p.y + p.h / 2. - h() / 2.);
    if (distance < best) {
      best = distance;
      current = p.page;
    }
    colour(t->dark ? fl_rgb_color(0, 0, 0) : FL_WHITE);
    quad(p.x, p.y, p.w, p.h);
    if (app.borders || app.overview) {
      colour(p.page == t->page ? FL_SELECTION_COLOR : FL_FOREGROUND_COLOR);
      glBegin(GL_LINE_LOOP);
      glVertex2d(p.x - 1, p.y - 1);
      glVertex2d(p.x + p.w + 1, p.y - 1);
      glVertex2d(p.x + p.w + 1, p.y + p.h + 1);
      glVertex2d(p.x - 1, p.y + p.h + 1);
      glEnd();
    }
    double scale = p.scale * ratio;
    int width = std::ceil(p.w * ratio), height = std::ceil(p.h * ratio);
    int left = std::max(0, int(std::floor(-p.x * ratio / 512)) * 512),
        top = std::max(0, int(std::floor(-p.y * ratio / 512)) * 512);
    int right = std::min(width, int(std::ceil((w() - p.x) * ratio))),
        bottom = std::min(height, int(std::ceil((h() - p.y) * ratio)));
    for (int ty = top; ty < bottom; ty += 512)
      for (int tx = left; tx < right; tx += 512) {
        int tw = std::min(512, width - tx), th = std::min(512, height - ty);
        std::ostringstream key;
        key << t->id << ':' << t->epoch << ':' << p.page << ':' << std::fixed
            << std::setprecision(5) << scale << ':' << tx << ':' << ty;
        std::string name = key.str();
        visible_tiles.insert(name);
        auto it = tiles.find(name);
        if (it != tiles.end()) {
          auto &tile = it->second;
          tile.use = ++clock;
          glEnable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, tile.texture);
          glColor3ub(255, 255, 255);
          double x = p.x + tx / ratio, y = p.y + ty / ratio;
          glBegin(GL_QUADS);
          glTexCoord2f(0, 0);
          glVertex2d(x, y);
          glTexCoord2f(1, 0);
          glVertex2d(x + tile.w / ratio, y);
          glTexCoord2f(1, 1);
          glVertex2d(x + tile.w / ratio, y + tile.h / ratio);
          glTexCoord2f(0, 1);
          glVertex2d(x, y + tile.h / ratio);
          glEnd();
          glDisable(GL_TEXTURE_2D);
        } else if (!pending.count(name)) {
          auto b = t->meta["bounds"][p.page];
          double x0 = b[0], y0 = b[1], x1 = b[2], y1 = b[3];
          std::vector<double> m;
          switch (t->rotations[p.page]) {
          case 1:
            m = {0, scale, -scale, 0, y1 * scale, -x0 * scale};
            break;
          case 2:
            m = {-scale, 0, 0, -scale, x1 * scale, y1 * scale};
            break;
          case 3:
            m = {0, -scale, scale, 0, -y0 * scale, x1 * scale};
            break;
          default:
            m = {scale, 0, 0, scale, -x0 * scale, -y0 * scale};
          }
          pending.insert(name);
          app.worker.submit({t->id,
                             t->epoch,
                             "render",
                             name,
                             {{"page", p.page},
                              {"matrix", m},
                              {"clip", {tx, ty, tx + tw, ty + th}},
                              {"dark", t->dark}}});
        }
      }
    // Map original PDF coordinates to the rotated, scaled page, for links and
    // text overlays.
    auto transform = [&](double x, double y) {
      auto b = t->meta["bounds"][p.page];
      double xx = x - b[0].get<double>(), yy = y - b[1].get<double>(),
             pw = b[2].get<double>() - b[0].get<double>(),
             ph = b[3].get<double>() - b[1].get<double>();
      switch (t->rotations[p.page]) {
      case 1:
        return std::pair<double, double>(p.x + (ph - yy) * p.scale,
                                         p.y + xx * p.scale);
      case 2:
        return std::pair<double, double>(p.x + (pw - xx) * p.scale,
                                         p.y + (ph - yy) * p.scale);
      case 3:
        return std::pair<double, double>(p.x + yy * p.scale,
                                         p.y + (pw - xx) * p.scale);
      default:
        return std::pair<double, double>(p.x + xx * p.scale,
                                         p.y + yy * p.scale);
      }
    };
    auto rectangle = [&](Json box) {
      auto a = transform(box[0], box[1]), b = transform(box[2], box[3]);
      quad(std::min(a.first, b.first), std::min(a.second, b.second),
           std::abs(b.first - a.first), std::abs(b.second - a.second));
    };
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, .75, 0, .35);
    for (auto &match : t->matches)
      for (auto &box : match["rects"])
        if (box["page"] == p.page)
          rectangle(box["rect"]);
    if (app.link_boxes && t->pages.count(p.page)) {
      glColor4f(.2, .6, 1, .3);
      for (auto &link : t->pages[p.page]["links"])
        rectangle(link["rect"]);
    }
    if (selecting && selection_page == p.page) {
      glColor4f(.2, .55, 1, .35);
      rectangle(
          Json::array({std::min(select_x, end_x), std::min(select_y, end_y),
                       std::max(select_x, end_x), std::max(select_y, end_y)}));
    }
    glDisable(GL_BLEND);
    if (app.overview) {
      gl_color(FL_FOREGROUND_COLOR);
      gl_font(dx::font, dx::size);
      gl_draw(std::to_string(p.page + 1).c_str(), int(p.x),
              int(p.y + p.h + dx::size));
    }
  }
  for (const auto &key : app.worker.retain_tiles(t->id, visible_tiles))
    pending.erase(key);
  if (!app.presentation && !app.overview && current != t->page) {
    t->page = current;
    app.request_page(current);
    app.update_title();
  }
}
int Canvas::handle(int event) {
  if ((Fl::event_state() & FL_META) &&
      (event == FL_KEYDOWN || event == FL_SHORTCUT || event == FL_PUSH ||
       event == FL_MOUSEWHEEL))
    return 0;
  auto *t = app.tab();
  if (event == FL_FOCUS || event == FL_UNFOCUS)
    return 1;
  if (!t || !t->ready)
    return Fl_Gl_Window::handle(event);
  int px = Fl::event_x() - x(), py = Fl::event_y() - y();
  auto hit =
      [&](int X,
          int Y) -> std::optional<std::tuple<Placement, double, double>> {
    for (auto p : placements())
      if (X >= p.x && X <= p.x + p.w && Y >= p.y && Y <= p.y + p.h) {
        auto b = t->meta["bounds"][p.page];
        double xx = (X - p.x) / p.scale, yy = (Y - p.y) / p.scale,
               pw = b[2].get<double>() - b[0].get<double>(),
               ph = b[3].get<double>() - b[1].get<double>();
        double a = xx, c = yy;
        switch (t->rotations[p.page]) {
        case 1:
          a = yy;
          c = ph - xx;
          break;
        case 2:
          a = pw - xx;
          c = ph - yy;
          break;
        case 3:
          a = pw - yy;
          c = xx;
          break;
        }
        return std::make_tuple(p, a + b[0].get<double>(),
                               c + b[1].get<double>());
      }
    return std::nullopt;
  };
  if (event == FL_MOUSEWHEEL) {
    std::string key = Fl::event_dy() < 0 ? "ScrollUp" : "ScrollDown";
    if (Fl::event_state() & FL_SHIFT)
      key = "Shift+" + key;
    if (Fl::event_state() & FL_CTRL)
      key = "Ctrl+" + key;
    auto command = app.binding(key, true);
    if (command) {
      if (*command == "MoveUp" || *command == "MoveDown") {
        t->pan_y += Fl::event_dy() * 40 *
                    app.config["settings"].value("TrackpadSensitivity", 1.);
        t->pan_x += Fl::event_dx() * 40;
        redraw();
      } else
        app.action(*command);
      return 1;
    }
    if (Fl::event_dx()) {
      t->pan_x += Fl::event_dx() * 40;
      redraw();
      return 1;
    }
  }
  if (event == FL_PUSH) {
    take_focus();
    pressed = Fl::event_button();
    press_x = last_x = px;
    press_y = last_y = py;
    moved = false;
    selecting = false;
    auto command = app.binding(mouse_name(pressed), true);
    if (command && (*command == "Panning" || *command == "Selection")) {
      selecting = *command == "Selection";
      if (auto h = hit(px, py)) {
        auto [p, a, b] = *h;
        selection_page = p.page;
        select_x = end_x = a;
        select_y = end_y = b;
        app.request_page(p.page);
        t->page = p.page;
      } else
        selection_page = -1;
      return 1;
    }
    if (app.overview) {
      if (auto h = hit(px, py))
        t->page = std::get<0>(*h).page;
    }
    if (command) {
      app.action(*command);
      pressed = 0;
      return 1;
    }
    pressed = 0;
    return 0;
  }
  if (event == FL_DRAG && pressed) {
    moved |= std::abs(px - press_x) + std::abs(py - press_y) > 4;
    if (selecting) {
      if (auto h = hit(px, py); h && std::get<0>(*h).page == selection_page) {
        end_x = std::get<1>(*h);
        end_y = std::get<2>(*h);
      }
    } else {
      t->pan_x -= px - last_x;
      t->pan_y -= py - last_y;
    }
    last_x = px;
    last_y = py;
    redraw();
    return 1;
  }
  if (event == FL_RELEASE && pressed) {
    pressed = 0;
    if (selecting) {
      copy_selection(false);
      redraw();
      return 1;
    }
    if (!moved) {
      if (auto h = hit(px, py)) {
        auto [p, a, b] = *h;
        app.request_page(p.page);
        if (t->pages.count(p.page))
          for (auto &link : t->pages[p.page]["links"]) {
            auto r = link["rect"];
            if (a >= r[0].get<double>() && a <= r[2].get<double>() &&
                b >= r[1].get<double>() && b <= r[3].get<double>()) {
              if (link["page"].is_number())
                app.go(link["page"]);
              else {
                std::string uri = link.value("uri", "");
                if (uri.rfind("https://", 0) == 0 ||
                    uri.rfind("http://", 0) == 0 ||
                    uri.rfind("mailto:", 0) == 0)
                  external(uri);
                else
                  app.message("Unsupported link: " + uri);
              }
              break;
            }
          }
      }
    }
    return 1;
  }
  if (event == FL_MOVE) {
    bool link_hit = false;
    if (auto h = hit(px, py)) {
      auto [p, a, b] = *h;
      app.request_page(p.page);
      if (t->pages.count(p.page))
        for (auto &l : t->pages[p.page]["links"]) {
          auto r = l["rect"];
          link_hit |= a >= r[0].get<double>() && a <= r[2].get<double>() &&
                      b >= r[1].get<double>() && b <= r[3].get<double>();
        }
    }
    cursor(link_hit ? FL_CURSOR_HAND : FL_CURSOR_DEFAULT);
    return 1;
  }
  return Fl_Gl_Window::handle(event);
}
void Canvas::copy_selection(bool all) {
  auto *t = app.tab();
  if (!t || !t->ready)
    return;
  int page = all ? t->page : selection_page;
  if (page < 0) {
    if (!t->selection.empty())
      Fl::copy(t->selection.c_str(), t->selection.size(), 1);
    else
      app.message("Select text with Shift+drag");
    return;
  }
  app.request_page(page);
  if (!t->pages.count(page)) {
    app.message("Page text is loading; try again");
    return;
  }
  if (all)
    t->selection = t->pages[page].value("text", "");
  else if (selecting) {
    t->selection.clear();
    int block = -1, line = -1;
    for (auto &word : t->pages[page]["words"]) {
      auto r = word["rect"];
      double midx = (r[0].get<double>() + r[2].get<double>()) / 2.,
             midy = (r[1].get<double>() + r[3].get<double>()) / 2.;
      if (midx < std::min(select_x, end_x) ||
          midx > std::max(select_x, end_x) ||
          midy < std::min(select_y, end_y) || midy > std::max(select_y, end_y))
        continue;
      int b = word["block"], l = word["line"];
      if (!t->selection.empty())
        t->selection += (b != block || l != line) ? "\n" : " ";
      t->selection += word["text"].get<std::string>();
      block = b;
      line = l;
    }
  }
  if (!t->selection.empty()) {
    Fl::copy(t->selection.c_str(), t->selection.size(), 1);
    Fl::copy(t->selection.c_str(), t->selection.size(), 0);
    app.message(std::to_string(t->selection.size()) + " bytes copied");
  } else
    app.message("No selectable text in this area");
}
void Canvas::capture(const std::string &path) {
  // Explicit diagnostic capture only; normal presentation never reads the GPU
  // back.
  make_current();
  draw();
  glFinish();
  int W = pixel_w(), H = pixel_h();
  std::vector<unsigned char> pixels(size_t(W) * H * 3), flipped(pixels.size());
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  for (int y = 0; y < H; ++y)
    std::memcpy(flipped.data() + size_t(y) * W * 3,
                pixels.data() + size_t(H - y - 1) * W * 3, size_t(W) * 3);
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = W;
  image.height = H;
  image.format = PNG_FORMAT_RGB;
  if (!png_image_write_to_file(&image, path.c_str(), 0, flipped.data(), 0,
                               nullptr))
    app.message(image.message);
  png_image_free(&image);
}
