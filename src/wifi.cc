#include "wifi.hh"
#include "library.hh"
#include <algorithm>
#include "flat/all_generated.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace Meta4ms
{

namespace
{

constexpr int FirstPort = 8045, LastPort = 8052;
constexpr size_t MaxBody = 1024 * 1024 + 4096;
constexpr int IoTimeoutMs = 5000;

bool read_some(int fd, std::string &buf, size_t want_total) {
	char tmp[8192];
	while (buf.size() < want_total) {
		pollfd p{fd, POLLIN, 0};
		if (poll(&p, 1, IoTimeoutMs) <= 0)
			return false;
		ssize_t n = recv(fd, tmp, sizeof tmp, 0);
		if (n <= 0)
			return false;
		buf.append(tmp, (size_t)n);
	}
	return true;
}

void write_all(int fd, const char *p, size_t n) {
	while (n) {
		pollfd q{fd, POLLOUT, 0};
		if (poll(&q, 1, IoTimeoutMs) <= 0)
			return;
		ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
		if (w <= 0)
			return;
		p += w;
		n -= (size_t)w;
	}
}

std::vector<uint8_t> result_message(bool ok, std::string const &text) {
	flatbuffers::FlatBufferBuilder fbb;
	auto msg = fbb.CreateString(text);
	auto res = CreateResult(fbb, ok, msg);
	auto m = CreateMessage(fbb, AnyMessage_Result, res.Union());
	fbb.Finish(m);
	auto span = fbb.GetBufferSpan();
	return {span.begin(), span.end()};
}

void respond(int fd, int code, const char *status, const char *type, const void *body, size_t len) {
	char head[256];
	int h = std::snprintf(head,
						  sizeof head,
						  "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
						  code,
						  status,
						  type,
						  len);
	write_all(fd, head, (size_t)h);
	write_all(fd, (const char *)body, len);
}

std::string lower(std::string s) {
	for (auto &c : s)
		c = (char)std::tolower((unsigned char)c);
	return s;
}

bool is_dir(std::string const &p) {
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// file name only, letters/digits/space/-_.() kept, ends in .yml
std::string clean_name(std::string const &in) {
	std::string base = in;
	auto slash = base.find_last_of("/\\");
	if (slash != std::string::npos)
		base = base.substr(slash + 1);
	std::string out;
	for (char c : base) {
		if (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')')
			out += c;
	}
	while (!out.empty() && (out[0] == '.' || out[0] == ' '))
		out.erase(0, 1);
	if (out.size() > 80)
		out.resize(80);
	if (out.size() < 4 || lower(out.substr(out.size() - 4)) != ".yml")
		out += ".yml";
	if (out == ".yml")
		out = "patch.yml";
	return out;
}

} // namespace

std::string local_ip() {
	ifaddrs *ifs = nullptr;
	if (getifaddrs(&ifs) != 0)
		return "";
	std::string best, any;
	for (auto *i = ifs; i; i = i->ifa_next) {
		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP))
			continue;
		char buf[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &((sockaddr_in *)i->ifa_addr)->sin_addr, buf, sizeof buf);
		if (std::strncmp(i->ifa_name, "wlan", 4) == 0 && best.empty())
			best = buf;
		if (any.empty())
			any = buf;
	}
	freeifaddrs(ifs);
	return best.empty() ? any : best;
}

WifiReceiver::WifiReceiver(OnPatch cb)
	: cb_{std::move(cb)} {
	thr_ = std::thread([this] { run(); });
}

WifiReceiver::~WifiReceiver() {
	quit_.store(true);
	if (thr_.joinable())
		thr_.join();
	close_socket();
}

void WifiReceiver::set_enabled(bool on) {
	on_.store(on);
}

std::string WifiReceiver::address() {
	if (!on_.load())
		return "";
	if (sock_ < 0 || port_ == 0)
		return port_ < 0 ? "PORT BUSY" : "STARTING";
	std::string ip = local_ip();
	if (ip.empty())
		return "NO NETWORK";
	return "http://" + ip + ":" + std::to_string(port_);
}

std::string WifiReceiver::last_message() {
	std::lock_guard<std::mutex> g{mx_};
	return last_;
}

bool WifiReceiver::open_socket() {
	for (int p = FirstPort; p <= LastPort; p++) {
		int s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (s < 0)
			return false;
		int one = 1;
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		sockaddr_in a{};
		a.sin_family = AF_INET;
		a.sin_port = htons((uint16_t)p);
		a.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(s, (sockaddr *)&a, sizeof a) == 0 && listen(s, 4) == 0) {
			sock_ = s;
			port_ = p;
			return true;
		}
		close(s);
	}
	port_ = -1;
	return false;
}

void WifiReceiver::close_socket() {
	if (sock_ >= 0)
		close(sock_);
	sock_ = -1;
	if (port_ > 0)
		port_ = 0;
}

void WifiReceiver::run() {
	unsigned retry = 0;
	while (!quit_.load()) {
		if (!on_.load()) {
			close_socket();
			port_ = 0;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}
		if (sock_ < 0) {
			if (retry) {
				retry--;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}
			if (!open_socket()) {
				retry = 30; // try again in ~3 s
				continue;
			}
		}
		pollfd p{sock_, POLLIN, 0};
		if (poll(&p, 1, 100) <= 0)
			continue;
		int fd = accept4(sock_, nullptr, nullptr, SOCK_CLOEXEC);
		if (fd < 0)
			continue;
		serve(fd);
		close(fd);
	}
}

void WifiReceiver::serve(int fd) {
	std::string buf;
	// headers
	size_t hdr_end = std::string::npos;
	while (hdr_end == std::string::npos) {
		if (buf.size() > 16384 || !read_some(fd, buf, buf.size() + 1))
			return;
		hdr_end = buf.find("\r\n\r\n");
	}
	std::string head = buf.substr(0, hdr_end);
	std::string body = buf.substr(hdr_end + 4);
	std::string first = head.substr(0, head.find("\r\n"));
	std::string lh = lower(head);

	if (first.rfind("GET ", 0) == 0) {
		static const char page[] = "Meta4msXXL: send patches from the MetaModule hub in VCV Rack to this address.\n";
		respond(fd, 200, "OK", "text/plain", page, sizeof page - 1);
		return;
	}
	if (first.rfind("POST /action", 0) != 0) {
		respond(fd, 404, "Not Found", "text/plain", "", 0);
		return;
	}
	size_t len = 0;
	auto cl = lh.find("content-length:");
	if (cl != std::string::npos)
		len = (size_t)std::strtoul(lh.c_str() + cl + 15, nullptr, 10);
	if (len == 0 || len > MaxBody) {
		auto r = result_message(false, "File too large: max is 1MB");
		respond(fd, 200, "OK", "application/octet-stream", r.data(), r.size());
		return;
	}
	if (lh.find("expect: 100-continue") != std::string::npos)
		write_all(fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);
	if (!read_some(fd, body, len))
		return;
	body.resize(len);

	auto *data = (const uint8_t *)body.data();
	flatbuffers::Verifier v{data, body.size()};
	std::string err = "Not a patch upload";
	std::string saved;
	if (VerifyMessageBuffer(v)) {
		auto *msg = GetMessage(data);
		if (auto *up = msg->content_as_UploadPatch()) {
			std::string fname = up->filename() ? up->filename()->str() : "patch.yml";
			std::string vol = up->volume() ? up->volume()->str() : "Internal";
			if (up->content() && up->content()->size() > 0)
				saved = save(fname, vol, up->content()->data(), up->content()->size(), err);
			else
				err = "Empty patch";
		}
	}
	auto r = saved.empty() ? result_message(false, err) : result_message(true, "Loaded on Meta4msXXL");
	respond(fd, 200, "OK", "application/octet-stream", r.data(), r.size());
	{
		std::lock_guard<std::mutex> g{mx_};
		last_ = saved.empty() ? "RECEIVE FAILED: " + err : "RECEIVED " + clean_name(saved.substr(saved.rfind('/') + 1));
	}
	if (!saved.empty() && cb_)
		cb_(saved);
}

std::string WifiReceiver::save(
	std::string const &filename, std::string const &volume, const uint8_t *data, size_t len, std::string &err) {
	// pick the drive: "Internal" -> the Force's internal storage, "USB" / "Card" -> another drive if one is there
	std::string root = media_root();
	std::vector<std::string> drives;
	if (DIR *d = opendir(root.c_str())) {
		while (auto *e = readdir(d))
			if (e->d_name[0] != '.' && is_dir(root + "/" + e->d_name))
				drives.emplace_back(e->d_name);
		closedir(d);
	}
	std::sort(drives.begin(), drives.end());
	std::string internal, other;
	for (auto &d : drives) {
		std::string ld = lower(d);
		if (ld.find("internal") != std::string::npos) {
			if (internal.empty())
				internal = d;
		} else if (other.empty()) {
			other = d;
		}
	}
	std::string drive = lower(volume) == "internal" ? (internal.empty() ? other : internal)
													: (other.empty() ? internal : other);
	if (drive.empty()) {
		err = "No drive to save to";
		return "";
	}
	std::string dir = root + "/" + drive + "/Meta4msXXL";
	mkdir(dir.c_str(), 0775);
	if (!is_dir(dir)) {
		err = "Cannot create folder Meta4msXXL";
		return "";
	}
	std::string path = dir + "/" + clean_name(filename);
	std::string tmp = path + ".part";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) {
		err = "Cannot write file";
		return "";
	}
	bool ok = fwrite(data, 1, len, f) == len;
	ok = (fclose(f) == 0) && ok;
	if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
		unlink(tmp.c_str());
		err = "Cannot write file";
		return "";
	}
	return path;
}

} // namespace Meta4ms
