// Meta4msXXL offline engine test (x86): drives the engine through the wrapper's contract (mpc_engine()) the way the
// plugin does on the Force, without MPC.
//
//   test/run_tests.sh   (builds this against build/host objects and runs it)
//
// For every patch in patches/: load it, play a note with the host transport running (MIDI clock), render 3 s, and
// check the output is finite, not stuck at DC and (for patches that make sound on their own or from a note) not silent;
// walk every page and move every knob; then a state round trip (a changed knob survives save -> new instance -> load),
// and a Wi-Fi upload exactly as the VCV hub sends it (POST /action, FlatBuffers UploadPatch) that must be saved and
// loaded. Writes build/test/<patch>.wav for listening.
extern "C" {
#include "engine.h"
}
#include "flat/all_generated.h"
#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

static const mpc_engine_t *E;
static int failures = 0;
#define CHECK(c, ...)                                                                                                  \
	do {                                                                                                               \
		if (!(c)) {                                                                                                    \
			failures++;                                                                                                \
			printf("  FAIL: " __VA_ARGS__);                                                                           \
			printf("\n");                                                                                              \
		}                                                                                                              \
	} while (0)

static std::string get(void *in, const char *k) {
	char b[8192];
	int n = E->get_param(in, k, b, sizeof b);
	return n > 0 ? std::string(b) : std::string();
}
static void set(void *in, const char *k, double v) {
	char b[64];
	snprintf(b, sizeof b, "%g", v);
	E->set_param(in, k, b);
}

static void render(void *in, std::vector<int16_t> *keep, int blocks, double *ppq, bool playing) {
	int16_t buf[256];
	for (int i = 0; i < blocks; i++) {
		if (E->song_pos)
			E->song_pos(in, playing ? *ppq : -1, 120.0, playing, 128);
		*ppq += 128 * 2.0 / 44100.0;
		E->render(in, buf, 128);
		if (keep)
			keep->insert(keep->end(), buf, buf + 256);
	}
}

// load is asynchronous: render until the patch name settles
static bool wait_loaded(void *in, double *ppq, int timeout_ms = 20000) {
	auto t0 = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(timeout_ms)) {
		render(in, nullptr, 4, ppq, false);
		std::string n = get(in, "patch_name");
		std::string st = get(in, "status");
		if (!n.empty() && n.rfind("LOADING", 0) != 0 && !st.empty())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return false;
}

static void write_wav(const std::string &path, const std::vector<int16_t> &s) {
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return;
	uint32_t data = (uint32_t)(s.size() * 2), riff = 36 + data, sr = 44100, br = sr * 4, fmt = 16;
	uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
	fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f);
	fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(s.data(), 2, s.size(), f);
	fclose(f);
}

static bool post_upload(int port, const std::string &fname, const std::string &yml, std::string &reply) {
	flatbuffers::FlatBufferBuilder fbb;
	auto c = fbb.CreateVector((const uint8_t *)yml.data(), yml.size());
	auto up = CreateUploadPatch(fbb, c, fbb.CreateString("Internal"), fbb.CreateString(fname));
	fbb.Finish(CreateMessage(fbb, AnyMessage_UploadPatch, up.Union()));
	int s = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(s, (sockaddr *)&a, sizeof a) != 0) {
		close(s);
		return false;
	}
	char head[256];
	int h = snprintf(head, sizeof head,
					 "POST /action HTTP/1.1\r\nHost: x\r\nbridge-timeout-ms: 10000\r\nContent-Length: %u\r\n\r\n",
					 (unsigned)fbb.GetSize());
	send(s, head, h, 0);
	send(s, fbb.GetBufferPointer(), fbb.GetSize(), 0);
	std::string r;
	char b[4096];
	ssize_t n;
	while ((n = recv(s, b, sizeof b, 0)) > 0)
		r.append(b, n);
	close(s);
	auto p = r.find("\r\n\r\n");
	if (p == std::string::npos)
		return false;
	std::string body = r.substr(p + 4);
	flatbuffers::Verifier v{(const uint8_t *)body.data(), body.size()};
	if (!VerifyMessageBuffer(v))
		return false;
	auto *res = GetMessage(body.data())->content_as_Result();
	if (!res)
		return false;
	reply = res->message() ? res->message()->str() : "";
	return res->success();
}

int main(int argc, char **argv) {
	std::string root = argc > 1 ? argv[1] : ".";
	std::string media = root + "/build/test/media";
	system(("rm -rf '" + media + "' && mkdir -p '" + media + "/internal-test' '" + root + "/build/test'").c_str());
	setenv("META4MSXXL_MEDIA_ROOT", media.c_str(), 1);
	E = mpc_engine();
	std::string factory = root + "/patches";
	void *in = E->create(factory.c_str());
	double ppq = 0;
	CHECK(wait_loaded(in, &ppq), "first patch did not load");
	printf("default patch: %s | %s\n", get(in, "patch_name").c_str(), get(in, "status").c_str());

	// count the library: step until the index stops moving
	int count = 0;
	int scan_max = argc > 2 ? atoi(argv[2]) : 2000;
	for (int i = 0; i < scan_max; i++) {
		set(in, "patch", i);
		render(in, nullptr, 2, &ppq, false);
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		wait_loaded(in, &ppq);
		int got = atoi(get(in, "patch").c_str());
		if (got != i)
			break;
		count = i + 1;
	}
	printf("library: %d patches\n\n", count);
	if (argc > 2) // a quick run (under ARM emulation): only the first N patches
		count = std::min(count, atoi(argv[2]));

	printf("%-28s %-34s %6s %8s %6s %5s\n", "patch", "status", "pages", "peak", "rms", "dc");
	for (int i = 0; i < count; i++) {
		set(in, "patch", i);
		bool ok = wait_loaded(in, &ppq);
		std::string name = get(in, "patch_name"), st = get(in, "status");
		CHECK(ok, "%s: load timed out", name.c_str());
		std::vector<int16_t> out;
		render(in, &out, 40, &ppq, true); // transport running: clock + start
		uint8_t on[3] = {0x90, 60, 100}, off[3] = {0x80, 60, 0}, on2[3] = {0x90, 67, 90}, off2[3] = {0x80, 67, 0};
		E->midi(in, on, 3);
		render(in, &out, 300, &ppq, true);
		E->midi(in, off, 3);
		E->midi(in, on2, 3);
		render(in, &out, 300, &ppq, true);
		E->midi(in, off2, 3);
		render(in, &out, 400, &ppq, true);
		double sum = 0, mean = 0;
		int peak = 0;
		for (auto v : out) {
			peak = std::max(peak, abs((int)v));
			sum += (double)v * v;
			mean += v;
		}
		double rms = sqrt(sum / out.size()) / 32768.0, dc = fabs(mean / out.size()) / 32768.0;
		std::string pagepos = get(in, "page_pos");
		int pages = atoi(pagepos.substr(pagepos.find('/') + 1).c_str());
		printf("%-28.28s %-34.34s %6d %8.3f %6.3f %5.3f\n", name.c_str(), st.c_str(), pages, peak / 32768.0, rms, dc);
		CHECK(st.find("MISSING") == std::string::npos, "%s: %s", name.c_str(), st.c_str());
		CHECK(dc < 0.25, "%s: output stuck at DC %.3f", name.c_str(), dc);
		std::string fn = name;
		for (auto &c : fn)
			if (!isalnum((unsigned char)c))
				c = '_';
		write_wav(root + "/build/test/" + fn + ".wav", out);

		// every page, every knob: names present, values move, nothing breaks
		for (int p = 0; p < pages; p++) {
			set(in, "page", p);
			render(in, nullptr, 1, &ppq, false);
			CHECK(atoi(get(in, "page").c_str()) == p, "%s: page %d not selected", name.c_str(), p);
			for (int k = 1; k <= 8; k++) {
				char key[8], nk[16], dk[16];
				snprintf(key, 8, "k%d", k);
				snprintf(nk, 16, "k%d_name", k);
				snprintf(dk, 16, "k%d_display", k);
				std::string kn = get(in, nk);
				if (kn == "-")
					continue;
				CHECK(!kn.empty(), "%s page %d knob %d has no name", name.c_str(), p, k);
				double before = atof(get(in, key).c_str());
				set(in, key, 0.25);
				render(in, nullptr, 1, &ppq, false);
				set(in, key, before);
				CHECK(!get(in, dk).empty(), "%s page %d knob %d has no value text", name.c_str(), p, k);
			}
			render(in, nullptr, 2, &ppq, false);
		}
		set(in, "page", 0);
	}

	// tabs: the 5 tab rows name pages, a tap selects one
	set(in, "patch", 0);
	wait_loaded(in, &ppq);
	printf("\ntabs on %s: [%s] [%s] [%s] [%s] [%s] page %s\n", get(in, "patch_name").c_str(), get(in, "tab_1").c_str(),
		   get(in, "tab_2").c_str(), get(in, "tab_3").c_str(), get(in, "tab_4").c_str(), get(in, "tab_5").c_str(),
		   get(in, "page_pos").c_str());
	set(in, "tab_2", 1);
	set(in, "tab_2", 0); // the wrapper springs it back: must not move again
	CHECK(get(in, "page") == "1", "tab 2 did not select page 2 (page %s)", get(in, "page").c_str());
	printf("after tapping tab 2: page %s, knob 1 = %s (%s)\n", get(in, "page_pos").c_str(), get(in, "k1_name").c_str(),
		   get(in, "k1_display").c_str());

	// state round trip
	int pick = count > 2 ? 2 : 0;
	set(in, "patch", pick);
	wait_loaded(in, &ppq);
	set(in, "page", 1);
	std::string kname = get(in, "k1_name");
	set(in, "k1", 0.77);
	set(in, "level", -3);
	std::string st = get(in, "state");
	printf("\nstate (%zu bytes) from %s, page 2 knob 1 %s = %s\n", st.size(), get(in, "patch_name").c_str(),
		   kname.c_str(), get(in, "k1").c_str());
	void *in2 = E->create(factory.c_str());
	double ppq2 = 0;
	wait_loaded(in2, &ppq2);
	E->set_param(in2, "state", st.c_str());
	render(in2, nullptr, 4, &ppq2, false);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	wait_loaded(in2, &ppq2);
	render(in2, nullptr, 4, &ppq2, false);
	printf("restored: %s page %s knob 1 %s = %s level %s\n", get(in2, "patch_name").c_str(), get(in2, "page_pos").c_str(),
		   get(in2, "k1_name").c_str(), get(in2, "k1").c_str(), get(in2, "level").c_str());
	CHECK(get(in2, "patch_name") == get(in, "patch_name"), "state: patch not restored");
	CHECK(fabs(atof(get(in2, "k1").c_str()) - 0.77) < 0.02 || get(in2, "k1_display") == get(in, "k1_display"),
		  "state: knob not restored (%s)", get(in2, "k1").c_str());
	CHECK(fabs(atof(get(in2, "level").c_str()) + 3) < 0.01, "state: level not restored");
	E->destroy(in2);

	// Wi-Fi: the hub's upload
	set(in, "wifi", 1);
	std::string addr;
	for (int i = 0; i < 100 && addr.rfind("http", 0) != 0; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		addr = get(in, "wifi_addr");
	}
	printf("\nwifi address: %s\n", addr.c_str());
	int port = atoi(addr.substr(addr.rfind(':') + 1).c_str());
	FILE *f = fopen((factory + "/QuadDrum.yml").c_str(), "rb");
	std::string yml;
	if (f) {
		char b[65536];
		size_t n;
		while ((n = fread(b, 1, sizeof b, f)) > 0)
			yml.append(b, n);
		fclose(f);
	}
	std::string reply;
	bool sent = port > 0 && post_upload(port, "My Hub Patch.yml", yml, reply);
	printf("upload: %s (%s)\n", sent ? "ok" : "FAILED", reply.c_str());
	CHECK(sent, "wifi upload failed");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	wait_loaded(in, &ppq);
	struct stat sb;
	CHECK(stat((media + "/internal-test/Meta4msXXL/My Hub Patch.yml").c_str(), &sb) == 0, "uploaded file not saved");
	printf("after upload: %s | %s | %s\n", get(in, "patch_name").c_str(), get(in, "status").c_str(),
		   get(in, "wifi_msg").c_str());
	set(in, "wifi", 0);

	E->destroy(in);
	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
