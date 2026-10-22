#pragma once
#include "util/static_string.hh"
#include <cstdint>

namespace MetaModule::System
{

// The USB-C port's current data role combined with the active class.
//   "Host..."   = the MetaModule is the USB host; a peripheral is attached to it.
//   "Device..." = the MetaModule is a USB device plugged into a host (computer).
enum class UsbConnectionType : uint16_t {
	None,			   // Not attached
	HostSearching,	   // Host: powered, no device class active yet
	HostMidiDevice,	   // Host: a USB MIDI device is attached
	HostUsbDrive,	   // Host: a USB mass-storage drive is attached
	DeviceWaiting,	   // Device: not yet enumerated by a host
	DeviceMidiHost,	   // Device: enumerated as a USB-MIDI device by a host
	DeviceVideoHost,   // Device: enumerated as a UVC video device by a host
	DeviceConsoleHost, // Device: enumerated as a CDC serial console by a host

	// Forced to device role and idle, but a downstream device (e.g. a USB drive)
	// was sensed on the port -- unusable until the USB Mode is set to Auto or Host.
	DeviceModePeripheralIgnored,
};

// Snapshot of the current USB connection.
//
// The device fields (vid/pid/manufacturer/product/counts) are populated only when
// the MetaModule is acting as a USB *host* with a peripheral attached. In device
// mode (plugged into a computer) there is no peripheral descriptor to report, so
// those fields are zero/empty and only `connection` is meaningful.
struct UsbConnectionStatus {
	UsbConnectionType connection = UsbConnectionType::None;

	// How many USB-MIDI cables the attached device has in each direction.
	// "rx"/"tx" are relative to the MetaModule: rx cables carry messages to us,
	// tx cables carry messages to the device
	uint8_t num_midi_rx_cables = 0;
	uint8_t num_midi_tx_cables = 0;

	// Number of USB MIDI Jacks declared (low-level descriptor access)
	uint8_t num_midi_in_jacks = 0;
	uint8_t num_midi_out_jacks = 0;

	uint16_t vid = 0; // attached device's idVendor (host mode)
	uint16_t pid = 0; // attached device's idProduct (host mode)
};

struct UsbDeviceName {
	StaticString<63> manufacturer; // iManufacturer string (may be empty)
	StaticString<63> product;	   // iProduct string (may be empty)
};

// Information about a USB-MIDI Cable
struct UsbMidiCableInfo {
	StaticString<31> name; // the corresponding jack's iJack string descriptor (may be empty)
	uint8_t cable_num = 0; // USB-MIDI cable number: only meaningful if valid = true
	bool valid = false;	   // false if no such cable
};

// Information about a single USB-MIDI jack (a "port" of the attached device).
// A USB-MIDI device declares one jack descriptor per port in each direction.
struct UsbMidiJackInfo {
	StaticString<31> name;	  // the jack's iJack string descriptor (may be empty)
	uint8_t jack_id = 0;	  // bJackID: the device's own ID for this jack
	uint8_t cable_num = 0;	  // USB-MIDI cable number; only meaningful if has_cable
	bool has_cable = false;	  // true if this jack is addressable by a cable number
	bool is_embedded = false; // true: Embedded jack (a real port); false: External
	bool valid = false;		  // false if `num` was out of range / no such jack
};

// Most jacks the snapshot can hold per direction.
inline constexpr unsigned MaxMidiJacks = 16;

// Most cables a device can have per direction (the cable number field in a
// USB-MIDI packet is 4 bits wide, so 16 is also the protocol's own limit).
inline constexpr unsigned MaxMidiCables = 16;

// Returns the current USB connection status
UsbConnectionStatus get_usb_connection_status();
UsbDeviceName get_usb_device_name();

// Returns info about one USB-MIDI cable of the attached device, by cable number.
// A cable is a MIDI stream you can filter on: `cable_num` matches
// MidiMessage::usb_hdr.cable_num, and `name` is the port name the device
// reports (what a computer would show in its MIDI port list).
//
// "rx" cables carry messages the device sends to the MetaModule -- filter these
// against messages from MidiInput::pop_message().
// "tx" cables are the ones the MetaModule can send messages to.
//
// Valid cable numbers run 0 .. num_midi_rx_cables-1 (or num_midi_tx_cables-1);
// out of range returns an entry whose `valid` is false.
UsbMidiCableInfo get_usb_midi_rx_cable(unsigned cable_num);
UsbMidiCableInfo get_usb_midi_tx_cable(unsigned cable_num);

// Returns info about a MIDI IN/OUT jack of the attached device.
// The returned info's `valid` field is false if `num` is out of range.
//
// Note these are the device's *jack descriptors*, which is a lower-level view
// than most modules need: External jacks (the device's physical DIN sockets)
// appear here but carry no cable number, and the device's MIDI OUT jacks are
// the ones that send messages *to* us. To pick a MIDI stream to listen to, use
// get_usb_midi_rx_cable() below instead.
UsbMidiJackInfo get_usb_midi_in_jack_info(unsigned num);
UsbMidiJackInfo get_usb_midi_out_jack_info(unsigned num);

} // namespace MetaModule::System
