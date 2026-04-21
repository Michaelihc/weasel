#include "stdafx.h"
#include "WeaselServerApp.h"
#include <filesystem>

namespace {
class WinSparkleApi {
 public:
  WinSparkleApi() {
    const auto dll_path = (WeaselServerApp::install_dir() / L"WinSparkle.dll");
    module_ = LoadLibraryW(dll_path.c_str());
    if (!module_) {
      return;
    }

    init_ = load<init_fn>("win_sparkle_init");
    cleanup_ = load<cleanup_fn>("win_sparkle_cleanup");
    set_registry_path_ =
        load<set_registry_path_fn>("win_sparkle_set_registry_path");
    set_lang_ = load<set_lang_fn>("win_sparkle_set_lang");
    set_appcast_url_ =
        load<set_appcast_url_fn>("win_sparkle_set_appcast_url");
    check_update_with_ui_ =
        load<check_update_with_ui_fn>("win_sparkle_check_update_with_ui");

    if (!init_ || !cleanup_ || !set_registry_path_ || !set_lang_ ||
        !set_appcast_url_ || !check_update_with_ui_) {
      FreeLibrary(module_);
      module_ = nullptr;
    }
  }

  ~WinSparkleApi() {
    if (module_) {
      FreeLibrary(module_);
    }
  }

  void set_registry_path(const char* path) const {
    if (set_registry_path_) {
      set_registry_path_(path);
    }
  }

  void set_lang(const char* lang) const {
    if (set_lang_) {
      set_lang_(lang);
    }
  }

  void set_appcast_url(const char* url) const {
    if (set_appcast_url_) {
      set_appcast_url_(url);
    }
  }

  void init() const {
    if (init_) {
      init_();
    }
  }

  void cleanup() const {
    if (cleanup_) {
      cleanup_();
    }
  }

  void check_update_with_ui() const {
    if (check_update_with_ui_) {
      check_update_with_ui_();
    }
  }

 private:
  template <typename T>
  T load(const char* name) const {
    return reinterpret_cast<T>(GetProcAddress(module_, name));
  }

  using init_fn = void(__cdecl*)();
  using cleanup_fn = void(__cdecl*)();
  using set_registry_path_fn = void(__cdecl*)(const char*);
  using set_lang_fn = void(__cdecl*)(const char*);
  using set_appcast_url_fn = void(__cdecl*)(const char*);
  using check_update_with_ui_fn = void(__cdecl*)();

  HMODULE module_ = nullptr;
  init_fn init_ = nullptr;
  cleanup_fn cleanup_ = nullptr;
  set_registry_path_fn set_registry_path_ = nullptr;
  set_lang_fn set_lang_ = nullptr;
  set_appcast_url_fn set_appcast_url_ = nullptr;
  check_update_with_ui_fn check_update_with_ui_ = nullptr;
};

WinSparkleApi& win_sparkle() {
  static WinSparkleApi api;
  return api;
}
}  // namespace

WeaselServerApp::WeaselServerApp()
    : m_handler(std::make_unique<RimeWithWeaselHandler>(&m_ui)),
      tray_icon(m_ui) {
  // m_handler.reset(new RimeWithWeaselHandler(&m_ui));
  m_server.SetRequestHandler(m_handler.get());
  SetupMenuHandlers();
}

WeaselServerApp::~WeaselServerApp() {}

int WeaselServerApp::Run() {
  if (!m_server.Start())
    return -1;

  // win_sparkle_set_appcast_url("http://localhost:8000/weasel/update/appcast.xml");
  auto& sparkle = win_sparkle();
  sparkle.set_registry_path("Software\\Rime\\Weasel\\Updates");
  if (GetThreadUILanguage() ==
      MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL))
    sparkle.set_lang("zh-TW");
  else if (GetThreadUILanguage() ==
           MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED))
    sparkle.set_lang("zh-CN");
  else
    sparkle.set_lang("en");
  sparkle.init();
  m_ui.Create(m_server.GetHWnd());

  m_handler->Initialize();
  m_handler->OnUpdateUI([this]() { tray_icon.Refresh(); });

  tray_icon.Create(m_server.GetHWnd());
  tray_icon.Refresh();

  int ret = m_server.Run();

  m_handler->Finalize();
  m_ui.Destroy();
  tray_icon.RemoveIcon();
  sparkle.cleanup();

  return ret;
}

bool WeaselServerApp::check_update() {
  // when checked manually, show testing versions too
  std::string feed_url = GetCustomResource("ManualUpdateFeedURL", "APPCAST");
  std::wstring channel{};
  auto ret = RegGetStringValue(HKEY_CURRENT_USER, L"Software\\Rime\\Weasel",
                               L"UpdateChannel", channel);
  if (!ret && channel == L"testing") {
    feed_url = GetCustomResource("TestingManualUpdateFeedURL", "APPCAST");
  }
  if (!feed_url.empty()) {
    win_sparkle().set_appcast_url(feed_url.c_str());
  }
  win_sparkle().check_update_with_ui();
  return true;
}

void WeaselServerApp::SetupMenuHandlers() {
  std::filesystem::path dir = install_dir();
  m_server.AddMenuHandler(ID_WEASELTRAY_QUIT,
                          [this] { return m_server.Stop() == 0; });
  m_server.AddMenuHandler(ID_WEASELTRAY_DEPLOY,
                          std::bind(execute, dir / L"WeaselDeployer.exe",
                                    std::wstring(L"/deploy")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SETTINGS,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring()));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_DICT_MANAGEMENT,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/dict")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SYNC,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/sync")));
  m_server.AddMenuHandler(ID_WEASELTRAY_WIKI,
                          std::bind(open, L"https://rime.im/docs/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_HOMEPAGE,
                          std::bind(open, L"https://rime.im/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_FORUM,
                          std::bind(open, L"https://rime.im/discuss/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_CHECKUPDATE, check_update);
  m_server.AddMenuHandler(ID_WEASELTRAY_INSTALLDIR, std::bind(explore, dir));
  m_server.AddMenuHandler(ID_WEASELTRAY_USERCONFIG,
                          std::bind(explore, WeaselUserDataPath()));
  m_server.AddMenuHandler(ID_WEASELTRAY_LOGDIR,
                          std::bind(explore, WeaselLogPath()));
}
