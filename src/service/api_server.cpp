// httplib must come before windows.h: it pulls in winsock2.h, which
// conflicts with the older winsock.h that windows.h includes by default.
#include <httplib.h>

#include "disktree/service/api_server.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

#include <fmt/format.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace disktree::service {

std::vector<std::string> default_allowed_origins() {
    return {
        "http://tauri.localhost",   // Tauri 2 on Windows
        "https://tauri.localhost",
        "tauri://localhost",        // Tauri on macOS / Linux
        "http://localhost:1420",    // Vite dev server
        "http://127.0.0.1:1420",
    };
}

namespace {

void send_json(httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

void send_error(httplib::Response& res, int status, const std::string& code,
                const std::string& message) {
    send_json(res, {{"error", code}, {"message", message}}, status);
}

json body_json(const httplib::Request& req) {
    if (req.body.empty()) return json::object();
    try {
        return json::parse(req.body);
    } catch (const json::exception&) {
        throw BadRequestError("Request body is not valid JSON");
    }
}

size_t param_size(const httplib::Request& req, const char* name, size_t def) {
    if (!req.has_param(name)) return def;
    try { return size_t(std::max<long long>(0, std::stoll(req.get_param_value(name)))); }
    catch (...) { throw BadRequestError(std::string("Invalid number for ") + name); }
}

double param_double(const httplib::Request& req, const char* name, double def) {
    if (!req.has_param(name)) return def;
    try { return std::stod(req.get_param_value(name)); }
    catch (...) { throw BadRequestError(std::string("Invalid number for ") + name); }
}

// Runs a handler and maps engine exceptions to HTTP errors
template <typename F>
httplib::Server::Handler guarded(F f) {
    return [f](const httplib::Request& req, httplib::Response& res) {
        try {
            f(req, res);
        } catch (const NoScanError& e) {
            send_error(res, 409, "no_scan", e.what());
        } catch (const NotFoundError& e) {
            send_error(res, 404, "not_found", e.what());
        } catch (const BadRequestError& e) {
            send_error(res, 400, "bad_request", e.what());
        } catch (const std::exception& e) {
            send_error(res, 500, "internal", e.what());
        }
    };
}

bool reveal_in_explorer(const std::string& path) {
#ifdef _WIN32
    std::wstring wpath = fs::path(std::u8string(
        reinterpret_cast<const char8_t*>(path.data()), path.size())).wstring();
    std::wstring args = L"/select,\"" + wpath + L"\"";
    auto rc = reinterpret_cast<intptr_t>(
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL));
    return rc > 32;
#else
    (void)path;
    return false;
#endif
}

uint32_t current_pid() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return uint32_t(getpid());
#endif
}

// Stops the server when the parent process exits. `done` is set once
// the server has stopped for any other reason, which ends the watcher.
std::thread watch_parent(uint32_t pid, httplib::Server& srv, std::atomic<bool>& done) {
    return std::thread([pid, &srv, &done] {
#ifdef _WIN32
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
        bool parent_gone = (h == nullptr);
        while (!parent_gone && !done) {
            if (WaitForSingleObject(h, 250) == WAIT_OBJECT_0) parent_gone = true;
        }
        if (h) CloseHandle(h);
#else
        bool parent_gone = false;
        while (!done && !(parent_gone = (kill(pid_t(pid), 0) != 0)))
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
#endif
        if (!parent_gone) return;
        // listen() may not have started yet: keep stopping until it has ended
        while (!done) {
            srv.stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}

} // namespace

int run_api_server(Engine& engine, const ServerOptions& opts) {
    httplib::Server srv;
    const auto origins = opts.allowed_origins;
    const auto token   = opts.token;

#ifdef _WIN32
    // Exclusive bind: never share a port with another process
    srv.set_socket_options([](socket_t sock) {
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&yes), sizeof(yes));
    });
#endif

    // ── Auth + CORS ───────────────────────────────────────────────
    srv.set_pre_routing_handler([origins, token](const httplib::Request& req,
                                                 httplib::Response& res) {
        auto origin = req.get_header_value("Origin");
        bool origin_ok = !origin.empty() &&
                         std::find(origins.begin(), origins.end(), origin) != origins.end();
        if (origin_ok) {
            res.set_header("Access-Control-Allow-Origin", origin);
            res.set_header("Vary", "Origin");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, X-DiskTree-Token");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        }
        if (req.method == "OPTIONS") {
            res.status = origin_ok ? 204 : 403;
            return httplib::Server::HandlerResponse::Handled;
        }
        // A browser page from any other origin is refused outright
        if (!origin.empty() && !origin_ok) {
            send_error(res, 403, "forbidden_origin", "Origin not allowed");
            return httplib::Server::HandlerResponse::Handled;
        }
        if (!token.empty()) {
            auto given = req.get_header_value("X-DiskTree-Token");
            if (given.empty() && req.has_param("token")) given = req.get_param_value("token");
            if (given != token) {
                send_error(res, 401, "unauthorized", "Missing or wrong token");
                return httplib::Server::HandlerResponse::Handled;
            }
        }
        res.set_header("Cache-Control", "no-store");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // ── Routes ────────────────────────────────────────────────────
    srv.Get("/api/health", guarded([&](auto&, auto& res) { send_json(res, engine.health()); }));
    srv.Get("/api/drives", guarded([&](auto&, auto& res) { send_json(res, engine.drives()); }));
    srv.Get("/api/places", guarded([&](auto&, auto& res) { send_json(res, engine.places()); }));

    srv.Post("/api/scan", guarded([&](const httplib::Request& req, httplib::Response& res) {
        json b = body_json(req);
        Engine::ScanRequest r;
        r.path             = b.value("path", "");
        r.default_excludes = b.value("default_excludes", true);
        r.threads          = b.value("threads", size_t(0));
        r.whole_pc         = b.value("whole_pc", false);
        if (b.contains("excludes") && b["excludes"].is_array())
            for (const auto& e : b["excludes"])
                if (e.is_string()) r.excludes.push_back(e.get<std::string>());
        engine.start_scan(r);
        send_json(res, engine.status(), 202);
    }));
    srv.Post("/api/scan/cancel", guarded([&](auto&, auto& res) {
        engine.cancel_scan();
        send_json(res, engine.status());
    }));
    srv.Get("/api/scan/status", guarded([&](auto&, auto& res) { send_json(res, engine.status()); }));

    srv.Get("/api/node", guarded([&](const httplib::Request& req, httplib::Response& res) {
        send_json(res, engine.node(req.get_param_value("path"),
                                   int(param_size(req, "depth", 1)),
                                   param_size(req, "max_children", 200),
                                   param_double(req, "min_fraction", 0.0)));
    }));
    srv.Get("/api/top-files", guarded([&](const httplib::Request& req, httplib::Response& res) {
        send_json(res, engine.top_files(param_size(req, "n", 50)));
    }));
    srv.Get("/api/top-dirs", guarded([&](const httplib::Request& req, httplib::Response& res) {
        send_json(res, engine.top_dirs(param_size(req, "n", 50)));
    }));
    srv.Get("/api/extensions", guarded([&](const httplib::Request& req, httplib::Response& res) {
        send_json(res, engine.extensions(param_size(req, "n", 50)));
    }));
    srv.Get("/api/search", guarded([&](const httplib::Request& req, httplib::Response& res) {
        send_json(res, engine.search(req.get_param_value("q"), param_size(req, "limit", 100)));
    }));

    srv.Get("/api/snapshots", guarded([&](auto&, auto& res) { send_json(res, engine.list_snapshots()); }));
    srv.Post("/api/snapshots", guarded([&](const httplib::Request& req, httplib::Response& res) {
        send_json(res, engine.save_snapshot(body_json(req).value("label", "")), 201);
    }));
    srv.Delete(R"(/api/snapshots/(\d+))", guarded([&](const httplib::Request& req,
                                                      httplib::Response& res) {
        engine.delete_snapshot(std::stoll(req.matches[1]));
        send_json(res, {{"deleted", true}});
    }));
    srv.Get("/api/diff", guarded([&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("snapshot")) throw BadRequestError("snapshot is required");
        int64_t id = 0;
        try { id = std::stoll(req.get_param_value("snapshot")); }
        catch (...) { throw BadRequestError("Invalid snapshot id"); }
        send_json(res, engine.diff(id, param_size(req, "limit", 200)));
    }));

    srv.Post("/api/reveal", guarded([&](const httplib::Request& req, httplib::Response& res) {
        std::string path = body_json(req).value("path", "");
        std::error_code ec;
        fs::path p(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
        if (path.empty() || !fs::exists(p, ec)) throw NotFoundError("Path no longer exists");
        send_json(res, {{"opened", reveal_in_explorer(path)}});
    }));

    srv.Post("/api/shutdown", guarded([&](auto&, auto& res) {
        send_json(res, {{"stopping", true}});
        std::thread([&srv] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            srv.stop();
        }).detach();
    }));

    // ── Bind, announce, serve ─────────────────────────────────────
    int port = opts.port == 0 ? srv.bind_to_any_port("127.0.0.1")
                              : (srv.bind_to_port("127.0.0.1", opts.port) ? opts.port : -1);
    if (port <= 0) {
        std::fprintf(stderr, "{\"event\":\"error\",\"message\":\"could not bind a port\"}\n");
        return 1;
    }
    if (opts.on_ready) opts.on_ready(port);
    if (opts.print_ready) {
        std::printf("{\"event\":\"ready\",\"port\":%d,\"pid\":%u}\n", port, current_pid());
        std::fflush(stdout);
    }

    std::atomic<bool> done{false};
    std::thread watcher;
    if (opts.parent_pid) watcher = watch_parent(opts.parent_pid, srv, done);

    bool ok = srv.listen_after_bind();
    done = true;
    engine.cancel_scan();
    if (watcher.joinable()) watcher.join();
    return ok ? 0 : 1;
}

} // namespace disktree::service
