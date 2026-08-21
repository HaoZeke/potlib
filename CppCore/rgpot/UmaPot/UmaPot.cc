// MIT License — persistent fairchem helper frontend

#include "rgpot/UmaPot/UmaPot.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace rgpot {
namespace {

#ifdef RGPOT_UMA_HELPER_SOURCE
constexpr const char *kHelperSource = RGPOT_UMA_HELPER_SOURCE;
#else
constexpr const char *kHelperSource = "";
#endif
#ifdef RGPOT_UMA_HELPER_INSTALLED
constexpr const char *kHelperInstalled = RGPOT_UMA_HELPER_INSTALLED;
#else
constexpr const char *kHelperInstalled = "";
#endif

void appendDoubles(std::ostringstream &os, const double *v, size_t n) {
  os << '[';
  os.setf(std::ios::fmtflags(0), std::ios::floatfield);
  os.precision(17);
  for (size_t i = 0; i < n; ++i) {
    if (i != 0) {
      os << ',';
    }
    os << v[i];
  }
  os << ']';
}

void appendInts(std::ostringstream &os, const int *v, size_t n) {
  os << '[';
  for (size_t i = 0; i < n; ++i) {
    if (i != 0) {
      os << ',';
    }
    os << v[i];
  }
  os << ']';
}

bool extractBool(const std::string &s, const char *key, bool *out) {
  const std::string pat = std::string("\"") + key + "\":";
  auto pos = s.find(pat);
  if (pos == std::string::npos) {
    return false;
  }
  pos += pat.size();
  while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) {
    ++pos;
  }
  if (s.compare(pos, 4, "true") == 0) {
    *out = true;
    return true;
  }
  if (s.compare(pos, 5, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

bool extractString(const std::string &s, const char *key, std::string *out) {
  const std::string pat = std::string("\"") + key + "\":";
  auto pos = s.find(pat);
  if (pos == std::string::npos) {
    return false;
  }
  pos += pat.size();
  while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) {
    ++pos;
  }
  if (pos >= s.size() || s[pos] != '"') {
    return false;
  }
  ++pos;
  std::string acc;
  while (pos < s.size() && s[pos] != '"') {
    if (s[pos] == '\\' && pos + 1 < s.size()) {
      acc += s[pos + 1];
      pos += 2;
      continue;
    }
    acc += s[pos++];
  }
  *out = acc;
  return true;
}

bool extractDouble(const std::string &s, const char *key, double *out) {
  const std::string pat = std::string("\"") + key + "\":";
  auto pos = s.find(pat);
  if (pos == std::string::npos) {
    return false;
  }
  pos += pat.size();
  char *end = nullptr;
  const double v = std::strtod(s.c_str() + pos, &end);
  if (end == s.c_str() + pos) {
    return false;
  }
  *out = v;
  return true;
}

bool extractDoubleArray(const std::string &s, const char *key,
                        std::vector<double> *out) {
  const std::string pat = std::string("\"") + key + "\":";
  auto pos = s.find(pat);
  if (pos == std::string::npos) {
    return false;
  }
  pos += pat.size();
  while (pos < s.size() && s[pos] != '[') {
    ++pos;
  }
  if (pos >= s.size()) {
    return false;
  }
  ++pos;
  out->clear();
  while (pos < s.size() && s[pos] != ']') {
    while (pos < s.size() &&
           (s[pos] == ' ' || s[pos] == ',' || s[pos] == '\t')) {
      ++pos;
    }
    if (pos >= s.size() || s[pos] == ']') {
      break;
    }
    char *end = nullptr;
    const double v = std::strtod(s.c_str() + pos, &end);
    if (end == s.c_str() + pos) {
      return false;
    }
    out->push_back(v);
    pos = static_cast<size_t>(end - s.c_str());
  }
  return true;
}

#ifndef _WIN32
bool readableFile(const std::string &path) {
  return !path.empty() && ::access(path.c_str(), R_OK) == 0;
}
#endif

std::string resolveHelper(const UmaConfig &cfg) {
#ifdef _WIN32
  throw std::runtime_error("UmaPot requires a POSIX helper process");
#else
  auto require_readable = [](const std::string &path) {
    if (!readableFile(path)) {
      throw std::runtime_error("UmaPot: uma_helper.py not found at " + path);
    }
    return path;
  };
  if (!cfg.helper_path.empty()) {
    return require_readable(cfg.helper_path);
  }
  if (const char *env = std::getenv("RGPOT_UMA_HELPER")) {
    if (env[0] != '\0') {
      return require_readable(env);
    }
  }
  if (readableFile(kHelperSource)) {
    return kHelperSource;
  }
  if (readableFile(kHelperInstalled)) {
    return kHelperInstalled;
  }
  throw std::runtime_error(
      "UmaPot: uma_helper.py not found (set UmaConfig.helper_path or "
      "RGPOT_UMA_HELPER)");
#endif
}

std::string resolvePython(const UmaConfig &cfg) {
  if (!cfg.python.empty()) {
    return cfg.python;
  }
  if (const char *env = std::getenv("RGPOT_UMA_PYTHON")) {
    if (env[0] != '\0') {
      return env;
    }
  }
  return "python3";
}

} // namespace

struct UmaPot::Helper {
#ifndef _WIN32
  pid_t pid = -1;
  FILE *to_child = nullptr;
  FILE *from_child = nullptr;
  int err_fd = -1;

  ~Helper() { close(); }

  void close() {
    if (to_child != nullptr) {
      std::fputs("{\"op\":\"shutdown\"}\n", to_child);
      std::fflush(to_child);
      std::fclose(to_child);
      to_child = nullptr;
    }
    if (from_child != nullptr) {
      std::fclose(from_child);
      from_child = nullptr;
    }
    if (err_fd >= 0) {
      ::close(err_fd);
      err_fd = -1;
    }
    if (pid > 0) {
      int status = 0;
      ::waitpid(pid, &status, 0);
      pid = -1;
    }
  }

  std::string drainStderr() const {
    if (err_fd < 0) {
      return {};
    }
    std::string acc;
    char buf[512];
    ssize_t n = 0;
    while ((n = ::read(err_fd, buf, sizeof(buf))) > 0) {
      acc.append(buf, static_cast<size_t>(n));
      if (acc.size() > 4096) {
        break;
      }
    }
    return acc;
  }

  std::string readline() {
    char *line = nullptr;
    size_t cap = 0;
    const ssize_t n = ::getline(&line, &cap, from_child);
    if (n <= 0) {
      std::free(line);
      const std::string err = drainStderr();
      throw std::runtime_error("UmaPot: helper closed stdout" +
                               (err.empty() ? std::string() : (": " + err)));
    }
    std::string out(line, static_cast<size_t>(n));
    std::free(line);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
      out.pop_back();
    }
    return out;
  }

  void writeLine(const std::string &s) {
    if (std::fputs(s.c_str(), to_child) == EOF ||
        std::fputc('\n', to_child) == EOF || std::fflush(to_child) != 0) {
      throw std::runtime_error("UmaPot: failed to write helper stdin");
    }
  }
#endif
};

void UmaPot::recomputeParamsKey() {
  Fnv1a fp;
  fp.u64(kKernelVersion);
  fp.str(m_config.model);
  fp.str(m_config.task_name);
  fp.str(m_config.device);
  fp.i64(m_config.charge);
  fp.i64(m_config.spin);
  m_paramsKey = fp.h;
}

UmaPot::UmaPot(const UmaConfig &config)
    : Potential(PotType::Uma), m_config(config) {
  recomputeParamsKey();
#ifdef _WIN32
  throw std::runtime_error("UmaPot requires a POSIX helper process");
#else
  ensureHelper();
#endif
}

UmaPot::~UmaPot() = default;

void UmaPot::setChargeSpin(int charge, int spin) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_config.charge = charge;
  m_config.spin = spin;
  recomputeParamsKey();
}

void UmaPot::ensureHelper() const {
#ifdef _WIN32
  throw std::runtime_error("UmaPot requires a POSIX helper process");
#else
  if (m_helper && m_helper->pid > 0) {
    return;
  }
  const std::string helper = resolveHelper(m_config);
  const std::string python = resolvePython(m_config);

  int in_pipe[2];
  int out_pipe[2];
  int err_pipe[2];
  if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
    throw std::runtime_error("UmaPot: pipe() failed");
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    throw std::runtime_error("UmaPot: fork() failed");
  }
  if (pid == 0) {
    ::dup2(in_pipe[0], STDIN_FILENO);
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::dup2(err_pipe[1], STDERR_FILENO);
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    ::close(err_pipe[0]);
    ::close(err_pipe[1]);
    const char *argv[] = {python.c_str(),
                          helper.c_str(),
                          "--model",
                          m_config.model.c_str(),
                          "--task-name",
                          m_config.task_name.c_str(),
                          "--device",
                          m_config.device.c_str(),
                          nullptr};
    ::execvp(python.c_str(), const_cast<char **>(argv));
    std::fprintf(stderr, "UmaPot: execvp(%s) failed: %s\n", python.c_str(),
                 std::strerror(errno));
    ::_exit(127);
  }

  ::close(in_pipe[0]);
  ::close(out_pipe[1]);
  ::close(err_pipe[1]);
  int flags = ::fcntl(err_pipe[0], F_GETFL, 0);
  if (flags >= 0) {
    ::fcntl(err_pipe[0], F_SETFL, flags | O_NONBLOCK);
  }

  auto helper_state = std::make_unique<Helper>();
  helper_state->pid = pid;
  helper_state->to_child = ::fdopen(in_pipe[1], "w");
  helper_state->from_child = ::fdopen(out_pipe[0], "r");
  helper_state->err_fd = err_pipe[0];
  if (helper_state->to_child == nullptr || helper_state->from_child == nullptr) {
    throw std::runtime_error("UmaPot: fdopen failed");
  }

  const std::string ready = helper_state->readline();
  bool ok = false;
  extractBool(ready, "ok", &ok);
  if (!ok) {
    std::string err;
    extractString(ready, "error", &err);
    throw std::runtime_error("UmaPot: helper handshake failed: " +
                             (err.empty() ? ready : err));
  }
  extractString(ready, "backend", &m_backend);
  m_helper = std::move(helper_state);
#endif
}

void UmaPot::forceImpl(const ForceInput &in, ForceOut *out) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  ensureHelper();

  std::ostringstream req;
  req << "{\"op\":\"eval\",\"pos\":";
  appendDoubles(req, in.pos, in.nAtoms * 3);
  req << ",\"atmnrs\":";
  appendInts(req, in.atmnrs, in.nAtoms);
  req << ",\"box\":";
  appendDoubles(req, in.box, 9);
  req << ",\"charge\":" << m_config.charge << ",\"spin\":" << m_config.spin
      << '}';

  m_helper->writeLine(req.str());
  const std::string resp = m_helper->readline();
  bool ok = false;
  extractBool(resp, "ok", &ok);
  if (!ok) {
    std::string err;
    extractString(resp, "error", &err);
    throw std::runtime_error("UmaPot: eval failed: " +
                             (err.empty() ? resp : err));
  }
  if (!extractDouble(resp, "energy", &out->energy)) {
    throw std::runtime_error("UmaPot: helper response missing energy");
  }
  std::vector<double> forces;
  if (!extractDoubleArray(resp, "forces", &forces) ||
      forces.size() != in.nAtoms * 3) {
    throw std::runtime_error("UmaPot: helper response has wrong force size");
  }
  std::memcpy(out->F, forces.data(), forces.size() * sizeof(double));
  out->variance = 0.0;
}

} // namespace rgpot
