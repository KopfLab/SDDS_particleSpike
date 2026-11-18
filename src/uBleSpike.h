#pragma once

/**
 * @brief TbleSpike - access to the SDDS tree via Bluetooth Low Energy (BLE)
 *
 * The BLE equivalent of TserialSpike: it speaks the exact same line-based
 * plainComm protocol (see SDDS/src/protocol.txt), just over BLE instead of USB serial.
 * TparticleSpike creates and manages one automatically (see SYSTEM.bluetooth), so it
 * normally does not need to be instantiated directly.
 *
 * Transport: Nordic UART Service (NUS) - the de-facto standard "serial over BLE" service
 * which is supported by Web Bluetooth (Chrome desktop/Android, Bluefy on iOS), nRF Connect,
 * Bluefruit Connect, "Serial Bluetooth Terminal", etc.
 *   - service  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *   - RX       6E400002-...  (client writes commands here; WRITE / WRITE_WO_RSP)
 *   - TX       6E400003-...  (device notifies responses here; NOTIFY)
 *
 * Framing: messages are '\n' terminated in both directions (like serial). Writes from the
 * client may arrive in arbitrary fragments and are reassembled here. Responses are split
 * into notifications of at most (ATT MTU - 3) bytes and the client reassembles them up to
 * the '\n'. With the P2's max ATT MTU of 247 that is 244 bytes per notification
 * (20 bytes if the central does not negotiate a larger MTU).
 *
 * Client identification: BLE does not tell the peripheral who connected - only the peer's
 * address, which phones/computers randomize. A client can therefore announce itself by
 * sending a line "@client <description>" (e.g. "@client Chrome on macOS"), which is consumed
 * here (not passed to the SDDS tree) and reported via peer().
 *
 * Time sync: the P2 has no battery backed clock, so after a power up without a cloud
 * connection (e.g. local wifi only) the time stays invalid. A client can provide its time by
 * sending a line "@time <unix time in ms>" (the SDDS browser does this on connect), which
 * sets the clock ONLY if it is not yet valid - it never overrides a valid (e.g. cloud synced)
 * time, and a later cloud sync takes precedence. Also consumed here, not passed to the tree.
 *
 * Threading: Particle BLE callbacks run on the BLE/system thread, NOT in the application
 * loop. They only copy data into mutex-protected buffers / set atomic flags. All SDDS tree
 * access happens in the application thread from a Ttimer (the "pump").
 */

#include "Particle.h"
#include "uPlainCommHandler.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#if Wiring_BLE

namespace nus // Nordic UART Service
{
	constexpr const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
	constexpr const char *RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
	constexpr const char *TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
}

class TbleSpike
{
public:
	enum class Tstatus
	{
		off,
		discoverable,
		connected
	};

	// called (in the application thread) whenever status() or peer() changes
	using TstatusCallback = std::function<void(Tstatus _status, const char *_peer)>;
	// called (in the application thread) when the clock was set from a client's @time
	using TtimeCallback = std::function<void()>;
	// called (in the application thread) for a registered "@<name> [args]" client command (see onCommand)
	using TcommandCallback = std::function<void(const char *_args)>;

	// tuning parameters
	static constexpr size_t TX_BUFFER_MAX = 16 * 1024; // max pending outgoing bytes (messages beyond are dropped)
	static constexpr size_t RX_BUFFER_MAX = 1024;	   // max length of a single incoming command line
	static constexpr uint32_t PUMP_ACTIVE_MS = 10;	   // pump interval while connected
	static constexpr uint32_t PUMP_IDLE_MS = 100;	   // pump interval while not connected
	static constexpr uint8_t CHUNKS_PER_PUMP = 4;	   // max notifications per pump cycle
	static constexpr size_t MAX_NAME_LENGTH = 29;	   // 31 byte scan response - 2 byte AD header
	static constexpr const char *CLIENT_CMD = "@client ";
	static constexpr const char *TIME_CMD = "@time ";
	static constexpr uint64_t MIN_VALID_TIME_MS = 1704067200000ULL; // 2024-01-01, sanity check for @time

private:
	/**
	 * @brief stream that the plainCommHandler writes to; it appends to the spike's tx buffer
	 * flush() terminates a message with '\n'. Messages that would overflow the buffer
	 * (or that are written while nobody is connected) are dropped as a whole.
	 */
	class TbleStream : public Tstream
	{
		TbleSpike &Fspike;
		size_t FmsgStart = 0;
		bool FmsgStarted = false;
		bool Foverflow = false;

		void begin()
		{
			if (!FmsgStarted)
			{
				FmsgStarted = true;
				FmsgStart = Fspike.Ftx.size();
				Foverflow = !Fspike.Fconnected;
			}
		}
		void append(const char *_str, size_t _len)
		{
			begin();
			if (Foverflow)
				return;
			if (Fspike.Ftx.size() - Fspike.FtxPos + _len > TbleSpike::TX_BUFFER_MAX)
			{
				Foverflow = true;
				return;
			}
			Fspike.Ftx.append(_str, _len);
		}

	public:
		TbleStream(TbleSpike &_spike) : Fspike(_spike) {}
		void write(const char *_str) override { append(_str, strlen(_str)); }
		void write(char _char) override { append(&_char, 1); }
		void flush() override
		{
			begin();
			if (Foverflow)
			{
				// drop the whole (partial) message so the client never sees a truncated line
				if (Fspike.Fconnected)
				{
					Log.warn("bleSpike: tx buffer full, dropping message");
					Fspike.FdroppedMsgs++;
				}
				Fspike.Ftx.resize(FmsgStart);
			}
			else
			{
				Fspike.Ftx.push_back('\n');
			}
			FmsgStarted = false;
			Foverflow = false;
		}
		void reset()
		{
			FmsgStarted = false;
			Foverflow = false;
		}
	};

	// sdds communication
	TbleStream Fstream;
	TplainCommHandler FcommHandler;
	Ttimer Fpump;

	// ble
	BleCharacteristic FtxChar;
	BleCharacteristic FrxChar;

	// tx (application thread only)
	std::string Ftx;
	size_t FtxPos = 0;

	// rx (written from BLE thread, read from application thread)
	std::mutex FrxMutex;
	std::string FrxIn;	  // raw incoming bytes (protected by FrxMutex)
	std::string FrxLine; // line being assembled (application thread only)
	bool FrxOverflow = false;

	// connection state (written from BLE thread)
	std::atomic<bool> FconnectEvent{false};
	std::atomic<bool> FdisconnectEvent{false};
	std::atomic<size_t> FattMtu{BLE_MIN_ATT_MTU_SIZE};
	std::mutex FpeerMutex;
	String FpeerAddress; // protected by FpeerMutex

	// application thread state
	bool Fbegun = false;
	bool Fenabled = true;
	bool Fconnected = false;
	Tstatus Fstatus = Tstatus::off;
	String Fpeer; // peer description (announced client name or address)
	TstatusCallback FstatusCallback;
	TtimeCallback FtimeCallback;
	std::vector<std::pair<String, TcommandCallback>> Fcommands; // registered @ commands

	// advertised name
	String FfallbackName = "SDDS";
	Tstring *FnameVar = nullptr;
	String FadvertisedName;

public:
	// diagnostics
	uint32_t FdroppedMsgs = 0;
	uint32_t FtxBytes = 0;
	uint32_t FrxBytes = 0;

	TbleSpike(TmenuHandle &_root)
		: Fstream(*this), FcommHandler(_root, Fstream)
	{
		on(Fpump)
		{
			pump();
			Fpump.start(Fconnected ? PUMP_ACTIVE_MS : PUMP_IDLE_MS);
		};
	}

	/**
	 * @brief configure the BLE service and (if _enabled) start advertising
	 * @param _fallbackName name advertised until _nameVar has a (non-empty) value
	 * @param _nameVar optional SDDS string variable to advertise as the name, e.g.
	 *        &particleSystem().name (the cloud device name, restored from EEPROM at startup and
	 *        updated when the cloud reports it). The advertised name follows all its changes.
	 * @param _enabled whether to turn bluetooth on right away (otherwise it stays off until enable())
	 */
	void begin(const char *_fallbackName, Tstring *_nameVar = nullptr, bool _enabled = true)
	{
		FfallbackName = _fallbackName;
		FnameVar = _nameVar;
		BleUuid svc(nus::SERVICE_UUID);
		FtxChar = BleCharacteristic("tx", BleCharacteristicProperty::NOTIFY, BleUuid(nus::TX_UUID), svc);
		FrxChar = BleCharacteristic("rx", BleCharacteristicProperty::WRITE | BleCharacteristicProperty::WRITE_WO_RSP,
									BleUuid(nus::RX_UUID), svc, &TbleSpike::onDataReceived, this);
		BLE.addCharacteristic(FtxChar);
		BLE.addCharacteristic(FrxChar);

		BLE.setDesiredAttMtu(BLE_MAX_ATT_MTU_SIZE);
		BLE.onConnected(&TbleSpike::onConnected, this);
		BLE.onDisconnected(&TbleSpike::onDisconnected, this);
		BLE.onAttMtuExchanged(&TbleSpike::onAttMtuExchanged, this);

		Fbegun = true;
		Fenabled = _enabled;
		FadvertisedName = currentName().substring(0, MAX_NAME_LENGTH);
		if (Fenabled)
			startAdvertising();
		else
			BLE.off();

		// follow changes of the name variable
		if (FnameVar)
		{
			Tstring &nameVar = *FnameVar; // on() expands to _var.callbacks() -> needs an lvalue, not *ptr
			on(nameVar)
			{
				setName(currentName().c_str());
			};
		}

		updateStatus();
		Fpump.start(PUMP_IDLE_MS);
	}

	/**
	 * @brief turn bluetooth on (if it was turned off) and start advertising again
	 */
	void enable()
	{
		if (!Fbegun || Fenabled)
			return;
		Log.info("bleSpike: turning bluetooth on");
		Fenabled = true;
		startAdvertising();
		updateStatus();
	}

	/**
	 * @brief disconnect any client, stop advertising and turn the BLE radio off
	 */
	void disable()
	{
		if (!Fbegun || !Fenabled)
			return;
		Log.info("bleSpike: turning bluetooth off");
		Fenabled = false;
		if (BLE.connected())
			BLE.disconnectAll();
		BLE.stopAdvertising();
		BLE.off();
		closeConnection();
		updateStatus();
	}

	/**
	 * @brief change the advertised name (scan response) and the GAP device name
	 * takes effect immediately if advertising, otherwise after the current client disconnects
	 */
	void setName(const char *_name)
	{
		String name = String(_name).substring(0, MAX_NAME_LENGTH);
		if (name == FadvertisedName)
			return;
		FadvertisedName = name;
		if (!Fbegun || !Fenabled)
			return; // applied when bluetooth is turned on
		applyAdvertisingData();
		if (!BLE.connected())
			BLE.advertise(); // (re)start advertising with the updated data
		Log.info("bleSpike: advertising as '%s'", name.c_str());
	}

	void onStatusChange(TstatusCallback _cb) { FstatusCallback = _cb; }
	void onTimeSet(TtimeCallback _cb) { FtimeCallback = _cb; }

	/**
	 * @brief register a handler for an out-of-band client command (not an SDDS command),
	 * e.g. onCommand("@data", ...) handles the lines "@data" and "@data <args>"
	 */
	void onCommand(const char *_cmd, TcommandCallback _cb) { Fcommands.push_back({String(_cmd), _cb}); }

	/**
	 * @brief free space in the tx buffer (0 if not connected)
	 */
	size_t txFree()
	{
		if (!Fconnected)
			return 0;
		size_t pending = Ftx.size() - FtxPos;
		return pending >= TX_BUFFER_MAX ? 0 : TX_BUFFER_MAX - pending;
	}

	/**
	 * @brief send a line (newline is appended) to the connected client if it fits into the tx buffer
	 * @return false if not connected or no room (nothing is sent)
	 */
	bool sendLine(const char *_str, size_t _len)
	{
		if (_len + 1 > txFree())
			return false;
		Ftx.append(_str, _len);
		Ftx.push_back('\n');
		return true;
	}
	bool sendLine(const String &_line) { return sendLine(_line.c_str(), _line.length()); }

	Tstatus status() { return Fstatus; }
	const char *peer() { return Fpeer.c_str(); }
	const char *name() { return FadvertisedName.c_str(); }
	bool enabled() { return Fenabled; }
	bool connected() { return Fconnected; }
	size_t attMtu() { return FattMtu; }

private:
	String currentName()
	{
		if (FnameVar)
		{
			String val = static_cast<Tdescr *>(FnameVar)->to_string().c_str(); // to_string is only public via Tdescr
			if (val.length() > 0)
				return val;
		}
		return FfallbackName;
	}

	// advertising packet: just the service UUID (18 bytes + 3 bytes flags = 21 of 31 bytes),
	// the name goes into the scan response; also sets the GAP device name
	void applyAdvertisingData()
	{
		BleAdvertisingData adv;
		adv.appendServiceUUID(BleUuid(nus::SERVICE_UUID));
		BLE.setAdvertisingData(&adv);
		BLE.setDeviceName(FadvertisedName);
		BleAdvertisingData scan;
		scan.appendLocalName(FadvertisedName);
		BLE.setScanResponseData(&scan);
	}

	// turn the radio on and (re)apply the advertising data (which may not survive BLE.off())
	void startAdvertising()
	{
		(BLE.on)(); // parenthesized: SDDS defines an on() macro
		applyAdvertisingData();
		if (!BLE.connected())
			BLE.advertise();
		Log.info("bleSpike: advertising as '%s'", FadvertisedName.c_str());
	}

	/*** BLE thread callbacks (no SDDS access here!) ***/

	static void onDataReceived(const uint8_t *_data, size_t _len, const BlePeerDevice &_peer, void *_ctx)
	{
		TbleSpike *self = static_cast<TbleSpike *>(_ctx);
		std::lock_guard<std::mutex> lock(self->FrxMutex);
		if (self->FrxIn.size() + _len <= 4 * RX_BUFFER_MAX)
			self->FrxIn.append(reinterpret_cast<const char *>(_data), _len);
	}

	static void onConnected(const BlePeerDevice &_peer, void *_ctx)
	{
		TbleSpike *self = static_cast<TbleSpike *>(_ctx);
		ssize_t mtu = BLE.getCurrentAttMtu(_peer);
		if (mtu > 0)
			self->FattMtu = mtu;
		{
			std::lock_guard<std::mutex> lock(self->FpeerMutex);
			self->FpeerAddress = _peer.address().toString();
		}
		self->FconnectEvent = true;
	}

	static void onDisconnected(const BlePeerDevice &_peer, void *_ctx)
	{
		TbleSpike *self = static_cast<TbleSpike *>(_ctx);
		self->FattMtu = BLE_MIN_ATT_MTU_SIZE;
		self->FdisconnectEvent = true;
	}

	static void onAttMtuExchanged(const BlePeerDevice &_peer, size_t _mtu, void *_ctx)
	{
		static_cast<TbleSpike *>(_ctx)->FattMtu = _mtu;
	}

	/*** application thread ***/

	void updateStatus()
	{
		Tstatus status = !Fenabled ? Tstatus::off : (Fconnected ? Tstatus::connected : Tstatus::discoverable);
		if (status != Tstatus::connected)
			Fpeer = "";
		if (status != Fstatus)
		{
			Fstatus = status;
			if (FstatusCallback)
				FstatusCallback(Fstatus, Fpeer.c_str());
		}
	}

	void setPeer(const String &_peer)
	{
		if (_peer == Fpeer)
			return;
		Fpeer = _peer;
		Log.info("bleSpike: client is '%s'", Fpeer.c_str());
		if (FstatusCallback)
			FstatusCallback(Fstatus, Fpeer.c_str());
	}

	void resetBuffers()
	{
		Ftx.clear();
		FtxPos = 0;
		Fstream.reset();
		FrxLine.clear();
		FrxOverflow = false;
		std::lock_guard<std::mutex> lock(FrxMutex);
		FrxIn.clear();
	}

	void closeConnection()
	{
		Fconnected = false;
		FconnectEvent = false;
		FdisconnectEvent = false;
		// drop all subscriptions of this client, otherwise they keep filling the tx buffer
		FcommHandler.shutdown();
		resetBuffers();
	}

	void pump()
	{
		// connection changes
		if (FdisconnectEvent.exchange(false))
		{
			Log.info("bleSpike: disconnected");
			closeConnection();
			// the device os restarts advertising after a disconnect, unless bluetooth is off
			updateStatus();
		}
		if (FconnectEvent.exchange(false))
		{
			resetBuffers();
			if (!Fenabled)
			{
				// connection slipped in while turning off
				BLE.disconnectAll();
				return;
			}
			Fconnected = true;
			String address;
			{
				std::lock_guard<std::mutex> lock(FpeerMutex);
				address = FpeerAddress;
			}
			Log.info("bleSpike: connected to %s (ATT MTU %u)", address.c_str(), (unsigned)FattMtu);
			Fpeer = address; // until the client announces itself (see CLIENT_CMD)
			updateStatus();
		}
		if (!Fconnected)
			return;

		processRx();
		processTx();
	}

	void processLine()
	{
		while (!FrxLine.empty() && isspace((unsigned char)FrxLine.back()))
			FrxLine.pop_back();
		if (FrxLine.empty())
			return;
		// client self-identification (not an SDDS command)
		size_t n = strlen(CLIENT_CMD);
		if (FrxLine.compare(0, n, CLIENT_CMD) == 0)
		{
			String address;
			{
				std::lock_guard<std::mutex> lock(FpeerMutex);
				address = FpeerAddress;
			}
			setPeer(String(FrxLine.c_str() + n) + " (" + address + ")");
			return;
		}
		// client time (sets the clock only if it is not valid yet)
		n = strlen(TIME_CMD);
		if (FrxLine.compare(0, n, TIME_CMD) == 0)
		{
			syncTime(FrxLine.c_str() + n);
			return;
		}
		// registered out-of-band commands
		for (auto &cmd : Fcommands)
		{
			n = cmd.first.length();
			if (FrxLine.compare(0, n, cmd.first.c_str()) == 0 && (FrxLine.size() == n || FrxLine[n] == ' '))
			{
				const char *args = FrxLine.c_str() + n;
				while (*args == ' ')
					args++;
				cmd.second(args);
				return;
			}
		}
		FcommHandler.handleMessage(FrxLine.c_str());
	}

	void syncTime(const char *_value)
	{
		char *end = nullptr;
		uint64_t ms = strtoull(_value, &end, 10);
		if (end == _value || ms < MIN_VALID_TIME_MS)
		{
			Log.warn("bleSpike: ignoring invalid client time '%s'", _value);
			return;
		}
		if (Time.isValid())
		{
			Log.trace("bleSpike: time already valid, ignoring client time");
			return;
		}
		Time.setTime((time_t)((ms + 500) / 1000)); // whole seconds, rounded
		Log.info("bleSpike: time set from bluetooth client: %s", Time.format(Time.now(), TIME_FORMAT_ISO8601_FULL).c_str());
		if (FtimeCallback)
			FtimeCallback();
	}

	void processRx()
	{
		std::string in;
		{
			std::lock_guard<std::mutex> lock(FrxMutex);
			in.swap(FrxIn);
		}
		FrxBytes += in.size();
		for (char c : in)
		{
			if (c == '\n')
			{
				if (FrxOverflow)
					Log.warn("bleSpike: rx line too long, ignored");
				else
					processLine();
				FrxLine.clear();
				FrxOverflow = false;
			}
			else if (FrxLine.size() < RX_BUFFER_MAX)
				FrxLine.push_back(c);
			else
				FrxOverflow = true;
		}
	}

	void processTx()
	{
		size_t chunk = FattMtu - 3; // ATT notification header = opcode (1) + handle (2)
		for (uint8_t i = 0; i < CHUNKS_PER_PUMP && FtxPos < Ftx.size(); i++)
		{
			size_t len = std::min(chunk, Ftx.size() - FtxPos);
			ssize_t res = FtxChar.setValue(reinterpret_cast<const uint8_t *>(Ftx.data() + FtxPos), len, BleTxRxType::NACK);
			if (res < 0)
				break; // stack busy - retry on next pump
			FtxPos += len;
			FtxBytes += len;
		}
		// compact the buffer
		if (FtxPos >= Ftx.size())
		{
			Ftx.clear();
			FtxPos = 0;
		}
		else if (FtxPos > 2048)
		{
			Ftx.erase(0, FtxPos);
			FtxPos = 0;
		}
	}
};

#endif // Wiring_BLE
