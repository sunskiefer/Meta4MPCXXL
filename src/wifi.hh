// Meta4msXXL: receive patches from VCV Rack over Wi-Fi.
//
// The MetaModule hub module in VCV Rack (4ms-vcv, src/hub/hub_medium.cc wifiSendPatchFile) sends a patch to the
// MetaModule's Wi-Fi expander as one HTTP request: POST <address>/action, the body a FlatBuffers "Message" holding an
// "UploadPatch" (file name, volume "Internal" / "USB" / "Card", the .yml contents); it expects a "Message" holding a
// "Result" (success, text) back. This answers that request, so typing the Force's address (shown on the SETUP page)
// into the hub sends patches straight to Meta4msXXL. The schema header is 4ms-vcv's own (third_party/4ms-vcv).
//
// Off unless switched on. Listens on port 8045 (the next free one up to 8052 when several instances have it on),
// handles one request at a time on a thread of its own, accepts only that upload, at most 1 MB, and writes only
// .yml files into a Meta4msXXL folder at the top of a drive.
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace Meta4ms
{

class WifiReceiver {
public:
	// called on the receiver's thread with the saved file's path
	using OnPatch = std::function<void(std::string const &path)>;

	explicit WifiReceiver(OnPatch cb);
	~WifiReceiver();

	void set_enabled(bool on);
	bool enabled() const {
		return on_.load();
	}
	// "http://192.168.1.40:8045", "NO NETWORK", "PORT BUSY", "" when off
	std::string address();
	std::string last_message();

private:
	OnPatch cb_;
	std::atomic<bool> on_{false};
	std::atomic<bool> quit_{false};
	std::thread thr_;
	int sock_ = -1;
	int port_ = 0;
	std::mutex mx_;
	std::string last_;

	void run();
	bool open_socket();
	void close_socket();
	void serve(int fd);
	std::string save(std::string const &filename, std::string const &volume, const uint8_t *data, size_t len,
					 std::string &err);
};

std::string local_ip();

} // namespace Meta4ms
