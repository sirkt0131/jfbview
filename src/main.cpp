/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Copyright (C) 2012-2020 Chuan Ji                                         *
 *                                                                           *
 *  Licensed under the Apache License, Version 2.0 (the "License");          *
 *  you may not use this file except in compliance with the License.         *
 *  You may obtain a copy of the License at                                  *
 *                                                                           *
 *   http://www.apache.org/licenses/LICENSE-2.0                              *
 *                                                                           *
 *  Unless required by applicable law or agreed to in writing, software      *
 *  distributed under the License is distributed on an "AS IS" BASIS,        *
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. *
 *  See the License for the specific language governing permissions and      *
 *  limitations under the License.                                           *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// Main program file.

// Program name. May be overridden in the Makefile.
#ifndef JFBVIEW_PROGRAM_NAME
#define JFBVIEW_PROGRAM_NAME "jfbview"
#endif

// Binary program name. May be overridden in the Makefile.
#ifndef JFBVIEW_BINARY_NAME
#define JFBVIEW_BINARY_NAME "jfbview"
#endif

// Program version. May be overridden in the Makefile.
#ifndef JFBVIEW_VERSION
#define JFBVIEW_VERSION
#endif

#include <curses.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/vt.h>
#include <locale.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "command.hpp"
#include "cpp_compat.hpp"
#include "fitz_document.hpp"
#include "framebuffer.hpp"
#include "image_document.hpp"
#include "outline_view.hpp"
#include "pdf_document.hpp"
#include "search_view.hpp"
#include "viewer.hpp"

volatile sig_atomic_t e_flag = 0;
void reload_handler(int sig) {
  //printf("Signal Handled\r\n");
  e_flag = 1;
}

// Main program state.
struct State : public Viewer::State {
  // If true, just print debugging info and exit.
  bool PrintFBDebugInfoAndExit;
  // If true, exit main event loop.
  bool Exit;
  // If true (default), requires refresh after current command.
  bool Render;

  // The type of the displayed file.
  enum {
    AUTO_DETECT,
    PDF,
#ifndef JFBVIEW_NO_IMLIB2
    IMAGE,
#endif
  } DocumentType;
  // Viewer render cache size.
  int RenderCacheSize;
  // Input file.
  std::string FilePath;
  // Password for the input file. If no password is provided, this will be
  // nullptr.
  std::unique_ptr<std::string> FilePassword;
  // Framebuffer device.
  std::string FramebufferDevice;
  // Document instance.
  std::unique_ptr<Document> DocumentInst;
  // Outline view instance.
  std::unique_ptr<OutlineView> OutlineViewInst;
  // Search view instance.
  std::unique_ptr<SearchView> SearchViewInst;
  // Framebuffer instance.
  std::unique_ptr<Framebuffer> FramebufferInst;
  // Viewer instance.
  std::unique_ptr<Viewer> ViewerInst;

  // Default state.
  State()
      : Viewer::State(),
        PrintFBDebugInfoAndExit(false),
        Exit(false),
        Render(true),
        DocumentType(AUTO_DETECT),
        RenderCacheSize(Viewer::DEFAULT_RENDER_CACHE_SIZE),
        FilePath(""),
        FilePassword(),
        FramebufferDevice(Framebuffer::DEFAULT_FRAMEBUFFER_DEVICE),
        OutlineViewInst(nullptr),
        SearchViewInst(nullptr),
        FramebufferInst(nullptr),
        ViewerInst(nullptr) {}
};

// Returns the all lowercase version of a string.
static std::string ToLower(const std::string& s) {
  std::string r(s);
  std::transform(r.begin(), r.end(), r.begin(), &tolower);
  return r;
}

// Returns the file extension of a path, or the empty string. The extension is
// converted to lower case.
static std::string GetFileExtension(const std::string& path) {
  int path_len = path.length();
  if ((path_len >= 4) && (path[path_len - 4] == '.')) {
    return ToLower(path.substr(path_len - 3));
  }
  return std::string();
}

// Loads the file specified in a state. Returns true if the file has been
// loaded.
static bool LoadFile(State* state) {
#if !defined(JFBVIEW_ENABLE_LEGACY_PDF_IMPL) && \
    !defined(JFBVIEW_ENABLE_LEGACY_IMAGE_IMPL)
  Document* doc =
      FitzDocument::Open(state->FilePath, state->FilePassword.get());
#else
  if (state->DocumentType == State::AUTO_DETECT) {
    if (GetFileExtension(state->FilePath) == "pdf") {
      state->DocumentType = State::PDF;
    } else {
#ifndef JFBVIEW_NO_IMLIB2
      state->DocumentType = State::IMAGE;
#else
      fprintf(
          stderr,
          "Cannot detect file format. Plase specify a file format "
          "with --format. Try --help for help.\n");
      return false;
#endif
    }
  }
  Document* doc = nullptr;
  switch (state->DocumentType) {
    case State::PDF:
#ifdef JFBVIEW_ENABLE_LEGACY_PDF_IMPL
      doc = PDFDocument::Open(state->FilePath, state->FilePassword.get());
#else
      doc = FitzDocument::Open(state->FilePath, state->FilePassword.get());
#endif
      break;
#ifdef JFBVIEW_ENABLE_LEGACY_IMAGE_IMPL
#ifndef JFBVIEW_NO_IMLIB2
    case State::IMAGE:
      doc = ImageDocument::Open(state->FilePath);
      break;
#endif
#else
    case State::IMAGE:
      doc = FitzDocument::Open(state->FilePath, state->FilePassword.get());
      break;
#endif
    default:
      abort();
  }
#endif
  if (doc == nullptr) {
    fprintf(
        stderr, "Failed to open document \"%s\".\n", state->FilePath.c_str());
    return false;
  }
  state->DocumentInst.reset(doc);
  return true;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                 COMMANDS                                  *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

class ExitCommand : public Command {
 public:
  void Execute(int repeat, State* state) override { state->Exit = true; }
};

// Base class for move commands.
class MoveCommand : public Command {
 protected:
  // Returns how much to move by in a direction.
  int GetMoveSize(const State* state, bool horizontal) const {
    if (horizontal) {
      return state->ScreenWidth / 10;
    }
    return state->ScreenHeight / 10;
  }
};

class MoveDownCommand : public MoveCommand {
 public:
  void Execute(int repeat, State* state) override {
    state->YOffset += RepeatOrDefault(repeat, 1) * GetMoveSize(state, false);
    if (state->YOffset + state->ScreenHeight >=
        state->PageHeight - 1 + GetMoveSize(state, false)) {
      if (++(state->Page) < state->NumPages) {
        state->YOffset = 0;
      }
    }
  }
};

class MoveUpCommand : public MoveCommand {
 public:
  void Execute(int repeat, State* state) override {
    state->YOffset -= RepeatOrDefault(repeat, 1) * GetMoveSize(state, false);
    if (state->YOffset <= -GetMoveSize(state, false)) {
      if (--(state->Page) >= 0) {
        state->YOffset = INT_MAX;
      }
    }
  }
};

class MoveLeftCommand : public MoveCommand {
 public:
  void Execute(int repeat, State* state) override {
    state->XOffset -= RepeatOrDefault(repeat, 1) * GetMoveSize(state, true);
  }
};

class MoveRightCommand : public MoveCommand {
 public:
  void Execute(int repeat, State* state) override {
    state->XOffset += RepeatOrDefault(repeat, 1) * GetMoveSize(state, true);
  }
};

class ScreenDownCommand : public Command {
 public:
  void Execute(int repeat, State* state) override {
    state->YOffset += RepeatOrDefault(repeat, 1) * state->ScreenHeight;
    if (state->YOffset + state->ScreenHeight >=
        state->PageHeight - 1 + state->ScreenHeight) {
      if (++(state->Page) < state->NumPages) {
        state->YOffset = 0;
      }
    }
  }
};

class ScreenUpCommand : public Command {
 public:
  void Execute(int repeat, State* state) override {
    state->YOffset -= RepeatOrDefault(repeat, 1) * state->ScreenHeight;
    if (state->YOffset <= -state->ScreenHeight) {
      if (--(state->Page) >= 0) {
        state->YOffset = INT_MAX;
      }
    }
  }
};

class PageDownCommand : public Command {
 public:
  void Execute(int repeat, State* state) override {
    state->Page += RepeatOrDefault(repeat, 1);
  }
};

class PageUpCommand : public Command {
 public:
  void Execute(int repeat, State* state) override {
    state->Page -= RepeatOrDefault(repeat, 1);
  }
};

// Base class for zoom commands.
class ZoomCommand : public Command {
 protected:
  // How much to zoom in/out by each time.
  static const float ZOOM_COEFFICIENT;
  // Sets zoom, preserving original screen center.
  void SetZoom(float zoom, State* state) {
    // Position in page of screen center, as fraction of page size.
    const float center_ratio_x =
        static_cast<float>(state->XOffset + state->ScreenWidth / 2) /
        static_cast<float>(state->PageWidth);
    const float center_ratio_y =
        static_cast<float>(state->YOffset + state->ScreenHeight / 2) /
        static_cast<float>(state->PageHeight);
    // Bound zoom.
    zoom = std::max(Viewer::MIN_ZOOM, std::min(Viewer::MAX_ZOOM, zoom));
    // Quotient of new and old zoom ratios.
    const float q = zoom / state->ActualZoom;
    // New page size after zoom change.
    const float new_page_width = static_cast<float>(state->PageWidth) * q;
    const float new_page_height = static_cast<float>(state->PageHeight) * q;
    // New center position within page after zoom change.
    const float new_center_x = new_page_width * center_ratio_x;
    const float new_center_y = new_page_height * center_ratio_y;
    // New offsets.
    state->XOffset = static_cast<int>(new_center_x) - state->ScreenWidth / 2;
    state->YOffset = static_cast<int>(new_center_y) - state->ScreenHeight / 2;
    // New zoom.
    state->Zoom = zoom;
  }
};
const float ZoomCommand::ZOOM_COEFFICIENT = 1.2f;

class ZoomInCommand : public ZoomCommand {
 public:
  void Execute(int repeat, State* state) override {
    SetZoom(
        state->ActualZoom * RepeatOrDefault(repeat, 1) * ZOOM_COEFFICIENT,
        state);
  }
};

class ZoomOutCommand : public ZoomCommand {
 public:
  void Execute(int repeat, State* state) override {
    SetZoom(
        state->ActualZoom * RepeatOrDefault(repeat, 1) / ZOOM_COEFFICIENT,
        state);
  }
};

class SetZoomCommand : public ZoomCommand {
 public:
  void Execute(int repeat, State* state) override {
    SetZoom(static_cast<float>(RepeatOrDefault(repeat, 100)) / 100.0f, state);
  }
};

class SetRotationCommand : public Command {
 public:
  void Execute(int repeat, State* state) override {
    state->Rotation = RepeatOrDefault(repeat, 0);
  }
};

class RotateCommand : public Command {
 public:
  explicit RotateCommand(int increment) : _increment(increment) {}

  void Execute(int repeat, State* state) override {
    state->Rotation += RepeatOrDefault(repeat, 1) * _increment;
  }

 private:
  int _increment;
};

class ZoomToFitCommand : public Command {
 public:
  void Execute(int repeat, State* state) override {
    state->Zoom = Viewer::ZOOM_TO_FIT;
  }
};

class ZoomToWidthCommand : public ZoomCommand {
 public:
  void Execute(int repeat, State* state) override {
    // Estimate page width at 100%.
    const float orig_page_width =
        static_cast<float>(state->PageWidth) / state->ActualZoom;
    // Estimate actual zoom ratio with zoom to width.
    const float actual_zoom =
        static_cast<float>(state->ScreenWidth) / orig_page_width;
    // Set center according to estimated actual zoom.
    SetZoom(actual_zoom, state);
    // Actually set zoom to width.
    state->Zoom = Viewer::ZOOM_TO_WIDTH;
  }
};

class GoToPageCommand : public Command {
 public:
  explicit GoToPageCommand(int default_page) : _default_page(default_page) {}

  void Execute(int repeat, State* state) override {
    int page =
        (std::max(
            1, std::min(
                   state->NumPages, RepeatOrDefault(repeat, _default_page)))) -
        1;
    if (page != state->Page) {
      state->Page = page;
      state->XOffset = 0;
      state->YOffset = 0;
    }
  }

 private:
  int _default_page;
};

class ShowOutlineViewCommand : public Command {
 public:
  void Execute(int repeat, State* state) override {
    const Document::OutlineItem* dest = state->OutlineViewInst->Run();
    if (dest == nullptr) {
      return;
    }
    const int dest_page = state->DocumentInst->Lookup(dest);
    if (dest_page >= 0) {
      GoToPageCommand c(0);
      c.Execute(dest_page + 1, state);
    }
  }
};

class ShowSearchViewCommand : public Command {
 public:
  void Execute(int repeat, State* state) override {
    const int dest_page = state->SearchViewInst->Run();
    if (dest_page >= 0) {
      GoToPageCommand c(0);
      c.Execute(dest_page + 1, state);
    }
  }
};

// Base class for SaveStateCommand and RestoreStateCommand.
class StateCommand : public Command {
 protected:
  // A global map from register number to saved state.
  static std::map<int, Viewer::State> _saved_states;
};
std::map<int, Viewer::State> StateCommand::_saved_states;

class SaveStateCommand : public StateCommand {
 public:
  void Execute(int repeat, State* state) override {
    state->ViewerInst->GetState(&(_saved_states[RepeatOrDefault(repeat, 0)]));
    state->Render = false;
  }
};

class RestoreStateCommand : public StateCommand {
 public:
  void Execute(int repeat, State* state) override {
    const int n = RepeatOrDefault(repeat, 0);
    if (_saved_states.count(n)) {
      state->ViewerInst->SetState(_saved_states[n]);
      state->ViewerInst->GetState(state);
    }
  }
};

class ReloadCommand : public StateCommand {
 public:
  void Execute(int repeat, State* state) override {
    if (LoadFile(state)) {
      state->ViewerInst = std::make_unique<Viewer>(
          state->DocumentInst.get(), state->FramebufferInst.get(), *state,
          state->RenderCacheSize);
    } else {
      state->Exit = true;
    }
  }
};

class ToggleInvertedColorModeCommand : public Command {
 public:
  void Execute(int repeat, State* state) override {
    state->ColorMode = state->ColorMode == Viewer::ColorMode::INVERTED
                           ? Viewer::ColorMode::NORMAL
                           : Viewer::ColorMode::INVERTED;
  }
};

class ToggleSepiaColorModeCommand : public Command {
 public:
  void Execute(int repeat, State* state) override {
    state->ColorMode = state->ColorMode == Viewer::ColorMode::SEPIA
                           ? Viewer::ColorMode::NORMAL
                           : Viewer::ColorMode::SEPIA;
  }
};

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                               END COMMANDS                                *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                 GPIO                                      *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*
  GPIOをコントロールするクラス。
  一応、以下のBCM 16, 20, 21を使用する想定。
  デフォルトはPullDownとなっているが、起動時にPullUp設定にする。
  以下を参考に、GPIOとスイッチの間に1kΩを使用した。
  N.Cとしているため、通常はGPIOは1。スイッチを押すと0となる。
  https://qiita.com/maitech/items/d63a77dea72355f142b5
$ gpio readall
 +-----+-----+---------+------+---+---Pi 4B--+---+------+---------+-----+-----+
 | BCM | wPi |   Name  | Mode | V | Physical | V | Mode | Name    | wPi | BCM |
 +-----+-----+---------+------+---+----++----+---+------+---------+-----+-----+
 |     |     |    3.3v |      |   |  1 || 2  |   |      | 5v      |     |     |
 |   2 |   8 |   SDA.1 |   IN | 1 |  3 || 4  |   |      | 5v      |     |     |
 |   3 |   9 |   SCL.1 |   IN | 1 |  5 || 6  |   |      | 0v      |     |     |
 |   4 |   7 | GPIO. 7 |   IN | 1 |  7 || 8  | 1 | IN   | TxD     | 15  | 14  |
 |     |     |      0v |      |   |  9 || 10 | 1 | IN   | RxD     | 16  | 15  |
 |  17 |   0 | GPIO. 0 |   IN | 0 | 11 || 12 | 0 | IN   | GPIO. 1 | 1   | 18  |
 |  27 |   2 | GPIO. 2 |   IN | 0 | 13 || 14 |   |      | 0v      |     |     |
 |  22 |   3 | GPIO. 3 |   IN | 0 | 15 || 16 | 0 | IN   | GPIO. 4 | 4   | 23  |
 |     |     |    3.3v |      |   | 17 || 18 | 0 | IN   | GPIO. 5 | 5   | 24  |
 |  10 |  12 |    MOSI |   IN | 0 | 19 || 20 |   |      | 0v      |     |     |
 |   9 |  13 |    MISO |   IN | 0 | 21 || 22 | 0 | IN   | GPIO. 6 | 6   | 25  |
 |  11 |  14 |    SCLK |   IN | 0 | 23 || 24 | 1 | IN   | CE0     | 10  | 8   |
 |     |     |      0v |      |   | 25 || 26 | 1 | IN   | CE1     | 11  | 7   |
 |   0 |  30 |   SDA.0 |   IN | 1 | 27 || 28 | 1 | IN   | SCL.0   | 31  | 1   |
 |   5 |  21 | GPIO.21 |   IN | 1 | 29 || 30 |   |      | 0v      |     |     |
 |   6 |  22 | GPIO.22 |   IN | 1 | 31 || 32 | 0 | IN   | GPIO.26 | 26  | 12  |
 |  13 |  23 | GPIO.23 |   IN | 0 | 33 || 34 |   |      | 0v      |     |     |
 |  19 |  24 | GPIO.24 |   IN | 0 | 35 || 36 | 1 | IN   | GPIO.27 | 27  | 16  |
 |  26 |  25 | GPIO.25 |   IN | 0 | 37 || 38 | 1 | IN   | GPIO.28 | 28  | 20  |
 |     |     |      0v |      |   | 39 || 40 | 1 | IN   | GPIO.29 | 29  | 21  |
 +-----+-----+---------+------+---+----++----+---+------+---------+-----+-----+
 | BCM | wPi |   Name  | Mode | V | Physical | V | Mode | Name    | wPi | BCM |
 +-----+-----+---------+------+---+---Pi 4B--+---+------+---------+-----+-----+
*/


enum class GPIO_STATUS   { OFF   ,  ON      };
enum class GPIO_MODE     { PULLUP,  PULLDOWN};
enum class GPIO_DIRECTION{ INPUT ,  OUTPUT  };

class GPIO {
private:
  const int GPIO_WAITTIMER = 100; // [ms]
public:
  GPIO(const std::vector<std::tuple<int, GPIO_DIRECTION, GPIO_MODE> >& bcws)
  :bcws_(bcws)
  {
    for(const auto& i:bcws)
    {
      int bcw = std::get<0>(i);
      // Use
      use_gpio_port(bcw);
      // Direction
      GPIO_DIRECTION direction = std::get<1>(i);
      set_direction(bcw, direction);
      // Mode
      GPIO_MODE mode = std::get<2>(i);
      set_mode(bcw, mode);
    }
  }
  ~GPIO()
  {
    for(const auto& i:bcws_)
    {
      int bcw = std::get<0>(i);
      // Unuse
      unuse_gpio_port(bcw);
    }
  }

  std::vector<std::tuple<int, GPIO_STATUS>> get_buttons() const
  {
    std::vector<std::tuple<int, GPIO_STATUS>> result;
    for(const auto& i:bcws_)
    {
      int bcw = std::get<0>(i);
      GPIO_STATUS status = get_value(bcw);
      result.push_back(std::make_tuple(bcw, status));
    }  
    return result;
  }
private:
  void use_gpio_port(int bcw) const
  {
    //std::printf("use %d\n",bcw);
    FILE* fd = fopen("/sys/class/gpio/export", "w");
    if (fd == NULL) { 
      printf("cannot open gpio %d\n", bcw);
      return;
      //exit(EXIT_FAILURE);
    }
    std::string s_bcw = std::to_string(bcw);
    fprintf(fd, s_bcw.c_str());
    fclose(fd);
    usleep(GPIO_WAITTIMER*1000);
  }

  void unuse_gpio_port(int bcw) const
  {
    //std::printf("unuse %d\n",bcw);
    FILE* fd = fopen("/sys/class/gpio/unexport", "w");
    if (fd == NULL) { 
      printf("cannot open gpio %d\n", bcw);
      return;
      //exit(EXIT_FAILURE);
    }
    std::string s_bcw = std::to_string(bcw);
    fprintf(fd, s_bcw.c_str());
    fclose(fd);
    usleep(GPIO_WAITTIMER*1000);
  }

  void set_direction(int bcw, GPIO_DIRECTION direction) const
  {
    //std::printf("set_direction %d %s\n",bcw, (direction == GPIO_DIRECTION::OUTPUT ? "out" : "in"));
    char gpio_direction[128] = {};
    sprintf(gpio_direction, "/sys/class/gpio/gpio%d/direction", bcw);
    FILE* fp = fopen(gpio_direction, "w");
    if (fp == NULL) {
        printf("cannot open gpio direction %s\n",gpio_direction);
        return;
        //exit(EXIT_FAILURE);
    }
    fprintf(fp, (direction == GPIO_DIRECTION::OUTPUT ? "out" : "in"));
    fclose(fp);
    usleep(GPIO_WAITTIMER*1000);
  }

  void set_mode(int bcw, GPIO_MODE mode) const
  {
    char gpio_mode[128] = {};
    sprintf(gpio_mode,"raspi-gpio set %d %s", bcw, (mode == GPIO_MODE::PULLUP ? "pu" : "pd"));
    system(gpio_mode);
    usleep(GPIO_WAITTIMER*1000);
    return;
  }

  GPIO_STATUS get_value(int bcw) const
  {
    char gpio_value[128] = {};
    sprintf(gpio_value, "/sys/class/gpio/gpio%d/value", bcw);
    FILE* fp = fopen(gpio_value, "r");
    if (fp == NULL) {
        printf("cannot open gpio value %s\n",gpio_value);
        exit(EXIT_FAILURE);
    }
    char state;
    fread(&state, sizeof(char), 1, fp);
    fclose(fp);
    return (state == '1') ? GPIO_STATUS::OFF : GPIO_STATUS::ON;
  }
private:
  std::vector<std::tuple<int, GPIO_DIRECTION, GPIO_MODE> > bcws_;
};

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                END GPIO                                   *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// Help text printed by --help or -h.
static const char* HELP_STRING =
    "\n" JFBVIEW_PROGRAM_NAME " " JFBVIEW_VERSION
    "\n"
    "\n"
    "Usage: " JFBVIEW_BINARY_NAME
    " [OPTIONS] FILE\n"
    "\n"
    "Options:\n"
    "\t--help, -h            Show this message.\n"
    "\t--fb=/path/to/dev     Specify output framebuffer device.\n"
    "\t--password=xx, -P xx  Unlock PDF document with the given password.\n"
    "\t--page=N, -p N        Open page N on start up.\n"
    "\t--zoom=N, -z N        Set initial zoom to N. E.g., -z 150 sets \n"
    "\t                      zoom level to 150%.\n"
    "\t--zoom_to_fit         Start in automatic zoom-to-fit mode.\n"
    "\t--zoom_to_width       Start in automatic zoom-to-width mode.\n"
    "\t--rotation=N, -r N    Set initial rotation to N degrees clockwise.\n"
    "\t--color_mode=invert, -c invert\n"
    "\t                      Start in inverted color mode.\n"
    "\t--color_mode=sepia, -c sepia\n"
    "\t                      Start in sepia color mode.\n"
    "\t--interval=N, -i N    Set auto interval time in seconds \n"
    "\t--intervals=N, -j N,. Set auto intervals time in seconds \n"
    "\t--show_progress       Show progress circle \n"
    "\t--use_button          Use GPIO button \n"
#if defined(JFBVIEW_ENABLE_LEGACY_IMAGE_IMPL) && \
    defined(JFBVIEW_ENABLE_LEGACY_PDF_IMPL) && !defined(JFBVIEW_NO_IMLIB2)
    "\t--format=image, -f image\n"
    "\t                      Forces the program to treat the input file as an\n"
    "\t                      image.\n"
    "\t--format=pdf, -f pdf  Forces the program to treat the input file as a\n"
    "\t                      PDF document. Use this if your PDF file does not\n"
    "\t                      end in \".pdf\" (case is ignored).\n"
#endif
    "\t--cache_size=N        Cache at most N pages. If you have an older\n"
    "\t                      machine with limited RAM, or if you are loading\n"
    "\t                      huge documents, or if you just want to reduce\n"
    "\t                      memory usage, you might want to set this to a\n"
    "\t                      smaller number.\n"
    "\n"
    "jfbview home page: https://github.com/jichu4n/jfbview\n"
    "Bug reports & suggestions: https://github.com/jichu4n/jfbview/issues\n"
    "\n";

// Split string into token
std::vector<int> split_intervals(const std::string& string, const std::string& separator) {
  auto separator_length = separator.length(); // 区切り文字の長さ

  auto list = std::vector<int>();

  if (separator_length == 0) {
    list.emplace_back(0);
  } else {
    auto offset = std::string::size_type(0);
    while (true) {
      auto pos = string.find(separator, offset);
      if (pos == std::string::npos) {
        int val = std::stoi(string.substr(offset));
        list.emplace_back(val);
        break;
      }
      int val = std::stoi(string.substr(offset, pos - offset));
      list.emplace_back(val);
      offset = pos + separator_length;
    }
  }
  return list;
}

// Parses the command line, and stores settings in state. Crashes the
// program if the commnad line contains errors.
static void ParseCommandLine(int argc, char* argv[], State* state) {
  // Tags for long options that don't have short option chars.
  enum {
    RENDER_CACHE_SIZE = 0x1000,
    ZOOM_TO_WIDTH,
    ZOOM_TO_FIT,
    FB,
    PRINT_FB_DEBUG_INFO_AND_EXIT,
  };
  // Command line options.
  static const option LongFlags[] = {
      {"help", false, nullptr, 'h'},
      {"fb", true, nullptr, FB},
      {"password", true, nullptr, 'P'},
      {"page", true, nullptr, 'p'},
      {"zoom", true, nullptr, 'z'},
      {"zoom_to_width", false, nullptr, ZOOM_TO_WIDTH},
      {"zoom_to_fit", false, nullptr, ZOOM_TO_FIT},
      {"rotation", true, nullptr, 'r'},
      {"color_mode", true, nullptr, 'c'},
      {"interval", true, nullptr, 'i'},
      {"intervals", true, nullptr, 'j'},
      {"show_progress", false, nullptr, 's'},
      {"use_button", false, nullptr, 'b'},
      {"format", true, nullptr, 'f'},
      {"cache_size", true, nullptr, RENDER_CACHE_SIZE},
      {"fb_debug_info", false, nullptr, PRINT_FB_DEBUG_INFO_AND_EXIT},
      {0, 0, 0, 0},
  };
  static const char* ShortFlags = "hP:p:z:r:c:i:j:s:b:f:";

  for (;;) {
    int opt_char = getopt_long(argc, argv, ShortFlags, LongFlags, nullptr);
    if (opt_char == -1) {
      break;
    }
    switch (opt_char) {
      case 'h':
        fprintf(stdout, "%s", HELP_STRING);
        exit(EXIT_FAILURE);
      case FB:
        state->FramebufferDevice = optarg;
        break;
      case 'f':
        if (ToLower(optarg) == "pdf") {
          state->DocumentType = State::PDF;
#ifndef JFBVIEW_NO_IMLIB2
        } else if (ToLower(optarg) == "image") {
          state->DocumentType = State::IMAGE;
#endif
        } else {
          fprintf(stderr, "Invalid file format \"%s\"\n", optarg);
          exit(EXIT_FAILURE);
        }
        break;
      case 'P':
        state->FilePassword = std::make_unique<std::string>(optarg);
        break;
      case RENDER_CACHE_SIZE:
        if (sscanf(optarg, "%d", &(state->RenderCacheSize)) < 1) {
          fprintf(stderr, "Invalid render cache size \"%s\"\n", optarg);
          exit(EXIT_FAILURE);
        }
        state->RenderCacheSize = std::max(1, state->RenderCacheSize + 1);
        break;
      case 'p':
        if (sscanf(optarg, "%d", &(state->Page)) < 1) {
          fprintf(stderr, "Invalid page number \"%s\"\n", optarg);
          exit(EXIT_FAILURE);
        }
        --(state->Page);
        break;
      case 'z':
        if (sscanf(optarg, "%f", &(state->Zoom)) < 1) {
          fprintf(stderr, "Invalid zoom ratio \"%s\"\n", optarg);
          exit(EXIT_FAILURE);
        }
        state->Zoom /= 100.0f;
        break;
      case ZOOM_TO_WIDTH:
        state->Zoom = Viewer::ZOOM_TO_WIDTH;
        break;
      case ZOOM_TO_FIT:
        state->Zoom = Viewer::ZOOM_TO_FIT;
        break;
      case 'r':
        if (sscanf(optarg, "%d", &(state->Rotation)) < 1) {
          fprintf(stderr, "Invalid rotation degree \"%s\"\n", optarg);
          exit(EXIT_FAILURE);
        }
        break;
      case 'c': {
        const std::string arg = ToLower(optarg);
        if (arg == "normal" || arg == "") {
          state->ColorMode = Viewer::ColorMode::NORMAL;
        } else if (arg == "invert" || arg == "inverted") {
          state->ColorMode = Viewer::ColorMode::INVERTED;
        } else if (arg == "sepia") {
          state->ColorMode = Viewer::ColorMode::SEPIA;
        } else {
          fprintf(stderr, "Invalid color mode \"%s\"\n", optarg);
          exit(EXIT_FAILURE);
        }
        break;
      }
      case 'i':
        if (sscanf(optarg, "%d", &(state->Interval)) < 0) {
          fprintf(stderr, "Invalid interval \"%s\"\n", optarg);
          exit(EXIT_FAILURE);
        }
        state->Zoom = Viewer::ZOOM_TO_FIT;
        break;
      case 'j':
        {
          std::vector<int> intervals = split_intervals(optarg, ",");
          state->Intervals = intervals;
          //for_each(intervals.begin(), intervals.end(), [](int v){printf("%d\n",v);});
          state->Zoom = Viewer::ZOOM_TO_FIT;
        }
        break;
      case 's':
        state->ShowProgress = true;
        break;
      case 'b':
        state->UseButton = true;
        break;
      case PRINT_FB_DEBUG_INFO_AND_EXIT:
        state->PrintFBDebugInfoAndExit = true;
        break;
      default:
        fprintf(stderr, "Try \"-h\" for help.\n");
        exit(EXIT_FAILURE);
    }
  }
  if (optind == argc) {
    if (!state->PrintFBDebugInfoAndExit) {
      fprintf(stderr, "No file specified. Try \"-h\" for help.\n");
      exit(EXIT_FAILURE);
    }
  } else if (optind < argc - 1) {
    fprintf(
        stderr,
        "Please specify exactly one input file. Try \"-h\" for "
        "help.\n");
    exit(EXIT_FAILURE);
  } else {
    state->FilePath = argv[optind];
  }
}

// Constructs the command registry.
std::unique_ptr<Registry> BuildRegistry() {
  std::unique_ptr<Registry> registry = std::make_unique<Registry>();

  registry->Register('q', std::move(std::make_unique<ExitCommand>()));

  registry->Register('h', std::move(std::make_unique<MoveLeftCommand>()));
  registry->Register(KEY_LEFT, std::move(std::make_unique<MoveLeftCommand>()));
  registry->Register('j', std::move(std::make_unique<MoveDownCommand>()));
  registry->Register(KEY_DOWN, std::move(std::make_unique<MoveDownCommand>()));
  registry->Register('k', std::move(std::make_unique<MoveUpCommand>()));
  registry->Register(KEY_UP, std::move(std::make_unique<MoveUpCommand>()));
  registry->Register('l', std::move(std::make_unique<MoveRightCommand>()));
  registry->Register(
      KEY_RIGHT, std::move(std::make_unique<MoveRightCommand>()));
  registry->Register(' ', std::move(std::make_unique<ScreenDownCommand>()));
  registry->Register(
      6 /* CTRL-F */, std::move(std::make_unique<ScreenDownCommand>()));  // ^F
  registry->Register(
      2 /* CTRL-B */, std::move(std::make_unique<ScreenUpCommand>()));  // ^B
  registry->Register('J', std::move(std::make_unique<PageDownCommand>()));
  registry->Register(KEY_NPAGE, std::move(std::make_unique<PageDownCommand>()));
  registry->Register('K', std::move(std::make_unique<PageUpCommand>()));
  registry->Register(KEY_PPAGE, std::move(std::make_unique<PageUpCommand>()));

  registry->Register('=', std::move(std::make_unique<ZoomInCommand>()));
  registry->Register('+', std::move(std::make_unique<ZoomInCommand>()));
  registry->Register('-', std::move(std::make_unique<ZoomOutCommand>()));
  registry->Register('z', std::move(std::make_unique<SetZoomCommand>()));
  registry->Register('s', std::move(std::make_unique<ZoomToWidthCommand>()));
  registry->Register('a', std::move(std::make_unique<ZoomToFitCommand>()));

  registry->Register('r', std::move(std::make_unique<SetRotationCommand>()));
  registry->Register('>', std::move(std::make_unique<RotateCommand>(90)));
  registry->Register('.', std::move(std::make_unique<RotateCommand>(90)));
  registry->Register('<', std::move(std::make_unique<RotateCommand>(-90)));
  registry->Register(',', std::move(std::make_unique<RotateCommand>(-90)));

  registry->Register('g', std::move(std::make_unique<GoToPageCommand>(0)));
  registry->Register(KEY_HOME, std::move(std::make_unique<GoToPageCommand>(0)));
  registry->Register(
      'G', std::move(std::make_unique<GoToPageCommand>(INT_MAX)));
  registry->Register(
      KEY_END, std::move(std::make_unique<GoToPageCommand>(INT_MAX)));

  registry->Register(
      '\t', std::move(std::make_unique<ShowOutlineViewCommand>()));
  registry->Register('/', std::move(std::make_unique<ShowSearchViewCommand>()));

  registry->Register('m', std::move(std::make_unique<SaveStateCommand>()));
  registry->Register('`', std::move(std::make_unique<RestoreStateCommand>()));

  registry->Register('e', std::move(std::make_unique<ReloadCommand>()));

  registry->Register('I', std::make_unique<ToggleInvertedColorModeCommand>());
  registry->Register('S', std::make_unique<ToggleSepiaColorModeCommand>());

  return registry;
}

static void DetectVTChange(pid_t parent) {
  struct vt_event e;
  struct vt_stat s;

  int fd = open("/dev/tty", O_RDONLY);
  if (fd == -1) {
    return;
  }

  if (ioctl(fd, VT_GETSTATE, &s) == -1) {
    goto out;
  }
  for (;;) {
    if (ioctl(fd, VT_WAITEVENT, &e) == -1) {
      goto out;
    }
    if (e.newev == s.v_active) {
      if (ioctl(fd, VT_WAITACTIVE, static_cast<int>(s.v_active)) == -1) {
        goto out;
      }
      if (kill(parent, SIGWINCH)) {
        goto out;
        // I wanted to use SIGRTMIN, but getch was not interrupted.
        // So instead, I choiced SIGWINCH because getch already
        // recognises this (and returns KEY_RESIZE), and the program
        // should support SIGWINCH and perform the same action anyways.
      }
    }
  }

out:
  close(fd);
}

void PrintFBDebugInfo(Framebuffer* fb) {
  assert(fb != nullptr);
  fprintf(stdout, "%s", fb->GetDebugInfoString().c_str());
}

static const char* FRAMEBUFFER_ERROR_HELP_STR = R"(
Troubleshooting tips:

1. Try adding yourself to the "video" group, e.g.:

       sudo usermod -a -G video $USER

   You will typically need to log out and back in for this to take effect.

2. Alternatively, try running this command as root, e.g.:

       sudo jfbview <file>

3. Verify that the framebuffer device exists. If not, please supply the correct
   device with "--fb=<path to device>".
)";

extern int JpdfgrepMain(int argc, char* argv[]);
extern int JpdfcatMain(int argc, char* argv[]);

int kbhit(void)
{
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }

    return 0;
}

void line_to(Framebuffer* fb, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b)
{
  int i;

  int dx = ( x1 > x0 ) ? x1 - x0 : x0 - x1;
  int dy = ( y1 > y0 ) ? y1 - y0 : y0 - y1;

  int sx = ( x1 > x0 ) ? 1 : -1;
  int sy = ( y1 > y0 ) ? 1 : -1;

  /* 傾きが1より小さい場合 */
  if ( dx > dy ) {
    int E = -dx;
    for ( i = 0 ; i <= dx ; ++i ) {
      fb->WritePixel(x0, y0, r, g, b);
      x0 += sx;
      E += 2 * dy;
      if ( E >= 0 ) {
        y0 += sy;
        E -= 2 * dx;
      }
    }
  /* 傾きが1以上の場合 */
  } else {
    int E = -dy;
    for ( i = 0 ; i <= dy ; ++i ) {
      fb->WritePixel(x0, y0, r, g, b);
      y0 += sy;
      E += 2 * dx;
      if ( E >= 0 ) {
        x0 += sx;
        E -= 2 * dy;
      }
    }
  }
}

int buttonHit(GPIO* gpio)
{
  if (gpio) {
    std::vector<std::tuple<int, GPIO_STATUS>> result = gpio->get_buttons();
    if (result.size() == 3) {
      int bcw1            = std::get<0>(result[0]);
      GPIO_STATUS status1 = std::get<1>(result[0]);
      int bcw2            = std::get<0>(result[1]);
      GPIO_STATUS status2 = std::get<1>(result[1]);
      int bcw3            = std::get<0>(result[2]);
      GPIO_STATUS status3 = std::get<1>(result[2]);

      GPIO_STATUS status16 = (bcw1 == 16) ? status1 : ((bcw2 == 16) ? status2 :((bcw3 == 16) ? status3 : GPIO_STATUS::OFF));
      GPIO_STATUS status20 = (bcw1 == 20) ? status1 : ((bcw2 == 20) ? status2 :((bcw3 == 20) ? status3 : GPIO_STATUS::OFF));
      GPIO_STATUS status21 = (bcw1 == 21) ? status1 : ((bcw2 == 21) ? status2 :((bcw3 == 21) ? status3 : GPIO_STATUS::OFF));

      // 16(Forward)のみON
      if ( (status16 == GPIO_STATUS::ON ) &&
           (status20 == GPIO_STATUS::OFF) && 
           (status21 == GPIO_STATUS::OFF) )
      {
        usleep(500*1000);
        return 'J';
      }else if // 20(Stop)のみON
          ((status16 == GPIO_STATUS::OFF) &&
           (status20 == GPIO_STATUS::ON ) && 
           (status21 == GPIO_STATUS::OFF) )
      {
        usleep(100*1000);
        return 'P';
      }else if // 21(Backward)のみON
          ((status16 == GPIO_STATUS::OFF ) &&
           (status20 == GPIO_STATUS::OFF) && 
           (status21 == GPIO_STATUS::ON ) )
      {
        usleep(500*1000);
        return 'K';
      }
    }
  }
  return 0;
}

int wait_timer(float sec, Framebuffer* fb, GPIO* gpio)
{
  const double PI = 3.141592653589;
  int msec = int(sec * 1000);
  int times = msec/10;

  int center_x = fb ? fb->GetSize().Width-(fb->GetSize().Width/48.0) : 0;
  int center_y = fb ? fb->GetSize().Height/(48.0*9.0/16.0) : 0;
  int length   = fb ? center_y*0.3 : 0;

  for (int i = 0; i < times; ++i){
    if (kbhit()) {
        int c = getchar();
        if (c == 'q' || c == 'r') return c;
    }
    if (e_flag == 1) {
      return 'r';
    }
    if (fb) {
      int x = center_x + length * cos(PI/2.0 + i*(2*PI)/times);
      int y = center_y - length * sin(PI/2.0 + i*(2*PI)/times);
      line_to(fb, center_x, center_y, x, y, 250, 0, 0);
    }
    usleep(1000*10); // 10[ms]
    // Watch button hit
    int c = buttonHit(gpio);
    while (c == 'P') {
      c = buttonHit(gpio);
    }
    if (c == 'K' || c == 'J') {
      return c;
    }
  }
  return 'J';
}

std::unique_ptr<GPIO> setup_gpio()
{
  std::tuple<int, GPIO_DIRECTION, GPIO_MODE> gpio16(16, GPIO_DIRECTION::INPUT, GPIO_MODE::PULLUP);
  std::tuple<int, GPIO_DIRECTION, GPIO_MODE> gpio20(20, GPIO_DIRECTION::INPUT, GPIO_MODE::PULLUP);
  std::tuple<int, GPIO_DIRECTION, GPIO_MODE> gpio21(21, GPIO_DIRECTION::INPUT, GPIO_MODE::PULLUP);
  std::vector<std::tuple<int, GPIO_DIRECTION, GPIO_MODE>>  bcws = {gpio16, gpio20, gpio21};
  
  return std::make_unique<GPIO>(bcws);
}

int get_current_interval(const State& state)
{
  if (state.Interval != 0)
  {
    return state.Interval;
  }
  else if(state.Intervals.size() >= state.NumPages)
  {
    return state.Intervals[state.Page];
  }
  return 10;
}

int main(int argc, char* argv[]) {
  if ( signal(SIGINT, reload_handler) == SIG_ERR ) {
    exit(1);
  }
  // Dispatch to jpdfgrep and jpdfcat.
  const std::string argv0 = argv[0];
  const std::string basename = argv0.substr(argv0.find_last_of('/') + 1);
  if (basename == "jpdfgrep") {
    return JpdfgrepMain(argc, argv);
  } else if (basename == "jpdfcat") {
    return JpdfcatMain(argc, argv);
  }
  
  State prev_state;

  e_flag = 0;
  // Main program state.
  State state;
  // 1. Initialization.
  ParseCommandLine(argc, argv, &state);
  
  // Setup GPIO
  std::unique_ptr<GPIO> gpio;
  if (state.UseButton == true) {
    gpio = setup_gpio();
  }

  // restore
  if (prev_state.Exit) {
    state.Exit = false;
    state.Interval = prev_state.Interval;
    state.Intervals = prev_state.Intervals;
    state.Zoom = prev_state.Zoom;
    state.ShowProgress = prev_state.ShowProgress;
  }

  state.FramebufferInst.reset(Framebuffer::Open(state.FramebufferDevice));
  if (state.FramebufferInst == nullptr) {
    fprintf(stderr, "%s", FRAMEBUFFER_ERROR_HELP_STR);
    exit(EXIT_FAILURE);
  }

  if (state.PrintFBDebugInfoAndExit) {
    PrintFBDebugInfo(state.FramebufferInst.get());
    exit(EXIT_SUCCESS);
  }

  if (!LoadFile(&state)) {
    exit(EXIT_FAILURE);
  }

  setlocale(LC_ALL, "");
  initscr();
  start_color();
  keypad(stdscr, true);
  nonl();
  cbreak();
  noecho();
  curs_set(false);
  // This is necessary to prevent curses erasing the framebuffer on first call
  // to getch().
  refresh();

  state.ViewerInst = std::make_unique<Viewer>(
      state.DocumentInst.get(), state.FramebufferInst.get(), state,
      state.RenderCacheSize);
  std::unique_ptr<Registry> registry(BuildRegistry());

  state.OutlineViewInst =
      std::make_unique<OutlineView>(state.DocumentInst->GetOutline());
  state.SearchViewInst = std::make_unique<SearchView>(state.DocumentInst.get());

  pid_t parent = getpid();
  if (!fork()) {
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
      exit(EXIT_FAILURE);
    }
    // Possible race condition. Cannot be fixed by doing before
    // fork, because this is cleared at fork. Instead, we now
    // check that we have not been reparented. This should
    // nullify the race condition.
    if (getppid() != parent) {
      exit(EXIT_SUCCESS);
    }
    DetectVTChange(parent);
    exit(EXIT_FAILURE);
  }

  // 2. Main event loop.
  state.Render = true;
  int repeat = Command::NO_REPEAT;
#if 0
  do {
    // 2.1 Render.
    if (state.Render) {
      state.ViewerInst->SetState(state);
      state.ViewerInst->Render();
      state.ViewerInst->GetState(&state);
    }
    state.Render = true;

    // 2.2. Grab input.
    int c;
    while (isdigit(c = getch())) {
      if (repeat == Command::NO_REPEAT) {
        repeat = c - '0';
      } else {
        repeat = repeat * 10 + c - '0';
      }
    }
    if (c == KEY_RESIZE) {
      continue;
    }

    // 2.3. Run command.
    registry->Dispatch(c, repeat, &state);
    repeat = Command::NO_REPEAT;
  } while (!state.Exit);
#else  
  do {
    // 2.1 Render.
    if (state.Render) {
      state.ViewerInst->SetState(state);
      state.ViewerInst->Render();
      state.ViewerInst->GetState(&state);
    }
    state.Render = true;

    // Check PDF numpages and intervals count
    if (state.Intervals.size() != 0 and state.Intervals.size() < state.NumPages) {
      printf("PDF page count and intervals are mismatch!  Pages %d, Intervals %d\n", state.NumPages, int(state.Intervals.size()));
      state.Intervals.clear();
      state.Interval = 15; // default 15sec
    }

    // If not set auto pager interval
    if (state.Interval == 0 and state.Intervals.size()==0) {
      // 2.2. Grab input.
      int c;
      while (isdigit(c = getch())) {
        if (repeat == Command::NO_REPEAT) {
          repeat = c - '0';
        } else {
          repeat = repeat * 10 + c - '0';
        }
      }
      if (c == KEY_RESIZE) {
        continue;
      }

      // 2.3. Run command.
      registry->Dispatch(c, repeat, &state);
    }
    else {
      int c = 0;
      // 2.2 Grab input.
      int wait_result = wait_timer(get_current_interval(state), state.ShowProgress ? state.FramebufferInst.get(): nullptr, gpio.get());
      if (wait_result == 'q' || wait_result == 'r') {
        state.Exit = true;
          if (wait_result == 'q') {e_flag = 0;}
          else                    {e_flag = 1;}
      }
      else if (wait_result == 'J' || wait_result == 'K') {
        if (state.Page+1 == state.NumPages && wait_result == 'J') wait_result = 'g';
        if (state.Page+1 == 1              && wait_result == 'K') wait_result = 'G';
        c = wait_result;
      }
      // 2.3. Run command.
      registry->Dispatch(c, repeat, &state);
    }
    repeat = Command::NO_REPEAT;
  } while (!state.Exit);
#endif
  
  // 3. Clean up.
  state.OutlineViewInst.reset();
  // Hack alert: Calling endwin() immediately after the framebuffer destructor
  // (which clears the screen) appears to cause a race condition where the next
  // shell prompt after this program exits would also get erased. Adding a
  // short sleep appears to fix the issue.
  state.FramebufferInst.reset();
  usleep(100 * 1000);
  endwin();

  // backup interval
  prev_state.Interval = state.Interval;
  prev_state.Intervals = state.Intervals;
  prev_state.Exit = state.Exit;
  prev_state.Zoom = state.Zoom;
  prev_state.ShowProgress = state.ShowProgress;
  // printf("Backup %d %d %d\n", prev_state.Interval, (int)prev_state.Intervals.size(), e_flag);
  if (e_flag == 0) return EXIT_SUCCESS;
  else             return EXIT_FAILURE; // for reload
  
}

