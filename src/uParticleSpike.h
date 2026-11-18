#pragma once

#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uPlainCommHandler.h"
#include "uParticleSystem.h"
#include "uBleSpike.h"
#include "uRunningStats.h"

// particle spike class
class TparticleSpike
{

public:
	// define the publishing interval constants (not an sdds enum)
	enum publish
	{
		AVG_GLOBAL = -1, // average and publish at global interval (if publishing is on!)
		OFF = 0,		 // never publish
		EACH = 1,		 // publish each value change immediately (if publishing is on!)
		ALWAYS = 2,		 // publish each value change even if publishing is off
		MAX = ALWAYS	 // keep track of what the maximum defined value is
	};

private:
	// namespace
	using TonOff = sdds::enums::OnOff;

	// free RAM to keep in reserve: when free memory drops below this, the oldest queued bursts
	// (or, if none are queued yet, the oldest data of the current burst) are discarded
	// (override by defining SDDS_PARTICLE_RESERVE_RAM before including this header)
#ifdef SDDS_PARTICLE_RESERVE_RAM
	static constexpr uint32_t RESERVE_RAM = SDDS_PARTICLE_RESERVE_RAM;
#elif (PLATFORM_ID == PLATFORM_P2 || PLATFORM_ID == PLATFORM_MSOM)
	static constexpr uint32_t RESERVE_RAM = 256 * 1024; // of ~3 MB
#else
	static constexpr uint32_t RESERVE_RAM = 48 * 1024; // of ~80 KB (Argon, Boron)
#endif

	// max size of a single burst (= max cloud event size, 16 kB of CBOR)
	// https://docs.particle.io/reference/device-os/typed-publish/
	static constexpr size_t MAX_BURST_CBOR = 16 * particle::protocol::MAX_EVENT_DATA_LENGTH;

	// bursts with at most this many data points are serialized to check their size,
	// larger ones are split by time first (serializing a huge burst at once would need a lot of RAM)
	static constexpr size_t MAX_BURST_POINTS_TO_SERIALIZE = 512;

/*** particle serializer (to Variants)  ***/
#pragma region serializer

	/**
	 * @brief container for timed data in a burst (kept in system time until publishing)
	 */
	struct TburstData
	{
		system_tick_t Ftime; // in MS
		Variant Fdata;		 // data
	};

	/**
	 * @brief dataset in a burst from a single variable
	 */
	struct TvarBurstDataset
	{
		Tdescr *FdescrPtr;				  // pointer to Tdescr variable where the data is from
		std::vector<TburstData> Fdataset; // data bursts
		TvarBurstDataset(Tdescr *_ptr, TburstData _first) : FdescrPtr(_ptr)
		{
			Fdataset.push_back(_first);
		}
		explicit TvarBurstDataset(Tdescr *_ptr) : FdescrPtr(_ptr) {}
	};

	/**
	 * @brief class with static functions to serialize sdds structure to Variants
	 * FIXME: should this be a namespace instead?
	 */
	class TparticleSerializer
	{

	private:
		// keys for tree top level
		inline static const char *FtreeDeviceNameKey = "n";
		inline static const char *FtreeTypeKey = "t";
		inline static const char *FtreeVersionKey = "v";
		inline static const char *FtreeEnumKey = "e";
		inline static const char *FtreeStructKey = "s";

		// keys for values-only top level
		inline static const char *FvaluesDeviceNameKey = "n";
		inline static const char *FvaluesTypeKey = "t";
		inline static const char *FvaluesVersionKey = "v";
		inline static const char *FvaluesDataKey = "d";

		// keys for state (=snapshot) top level
		inline static const char *FstateDeviceNameKey = "n";
		inline static const char *FstateTimeBaseKey = "tb";
		inline static const char *FstateDataKey = "s";

		// keys for tree Tdesc
		inline static const char *FdescTypeKey = "t";
		inline static const char *FdescOptKey = "o";
		inline static const char *FdescNameKey = "n";
		inline static const char *FdescEnumKey = "e";
		inline static const char *FdescSubStructKey = "s";

		// keys for bursts top level
		inline static const char *FburstDeviceNameKey = "n";
		inline static const char *FburstTimeBaseKey = "tb";
		inline static const char *FburstDataKey = "b";

		// keys for bursts data
		inline static const char *FburstTimeOffsetKey = "o";
		inline static const char *FburstNumValueKey = "v";
		inline static const char *FburstNumCountKey = "n";
		inline static const char *FburstNumSdevKey = "s";
		inline static const char *FburstTextValueKey = "c";
		inline static const char *FburstUnitsKey = "u";

		// keys for var command log
		inline static const char *FvarLogTimeBaseKey = "tb";
		inline static const char *FvarLogCmdsLogKey = "l";

		// base64 charset
		inline static const char base64_chars[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz"
			"0123456789+/";

		/**
		 * @brief serialize structure enumerations
		 */
		static Variant serializeEnums(TmenuHandle *_struct)
		{
			Vector<sdds::metaTypes::TenumId> enumIds;
			Variant enums;
			serializeEnums(_struct, enums, enumIds);
			return (enums);
		}

		static void serializeEnums(TmenuHandle *_struct, Variant &_enums, Vector<sdds::metaTypes::TenumId> &_ids)
		{
			for (auto it = _struct->iterator(); it.hasCurrent(); it.jumpToNext())
			{
				Tdescr *d = it.current();
				if (d->isStruct())
				{
					// recursively serialize sub-structs
					TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
					if (mh)
						serializeEnums(mh, _enums, _ids);
				}
				else if (d->type() == sdds::Ttype::ENUM)
				{
					// check if this enumeration has already been serialized
					TenumBase *en = static_cast<TenumBase *>(d);
					sdds::metaTypes::TenumId uid = en->enumInfo().id;
					bool alreadySerialized = false;
					for (size_t i = 0; i < _ids.size(); ++i)
					{
						if (uid == _ids[i])
						{
							alreadySerialized = true;
							break;
						}
					}
					if (alreadySerialized)
						continue;

					// new enum --> remember and serialize
					_ids.append(uid);
					Variant enumeration;
					enumeration.append(uid);
					Variant values;
					for (auto it = en->enumInfo().iterator;;)
					{
						values.append(it.next());
						if (!it.hasNext())
							break;
					}
					enumeration.append(values);
					_enums.append(enumeration);
				}
			}
		}

		/**
		 * @brief serialize structure
		 */
		static Variant serializeStruct(TmenuHandle *_struct, bool _withKeys = true)
		{
			Variant var;
			auto it = _struct->iterator();
			for (auto it = _struct->iterator(); it.hasCurrent(); it.jumpToNext())
			{
				Tdescr *d = it.current();
				Variant item = serializeDescr(d, _withKeys);
				if (d->isStruct())
				{
					TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
					Variant sub;
					if (mh)
						sub = serializeStruct(mh, _withKeys);
					(_withKeys) ? item.set(FdescSubStructKey, sub) : item.append(sub);
				}
				var.append(item);
			}
			return var;
		}

		/**
		 * @brief Tdescr to variant (value not included)
		 */
		static Variant serializeDescr(Tdescr *_d, bool _withKeys = true)
		{
			Variant var;
			if (_withKeys)
			{
				var.set(FdescTypeKey, _d->typeId());
				var.set(FdescOptKey, _d->option());
				var.set(FdescNameKey, _d->name());
			}
			else
			{
				var.append(_d->typeId());
				var.append(_d->option());
				var.append(_d->name());
			}

			// which enum?
			if (_d->type() == sdds::Ttype::ENUM)
			{
				TenumBase *en = static_cast<TenumBase *>(_d);
				(_withKeys) ? var.set(FdescEnumKey, en->enumInfo().id) : var.append(en->enumInfo().id);
			}
			return var;
		}

		/**
		 * @brief return value in variant
		 */
		static Variant serializeValue(Tdescr *_d, bool _enumAsText = false)
		{
			auto dt = _d->type();
			if (dt == sdds::Ttype::UINT8)
			{
				return Variant(static_cast<Tuint8 *>(_d)->value());
			}
			else if (dt == sdds::Ttype::UINT16)
			{
				return Variant(static_cast<Tuint16 *>(_d)->value());
			}
			else if (dt == sdds::Ttype::UINT32)
			{
				return Variant(static_cast<Tuint32 *>(_d)->value());
			}
			else if (dt == sdds::Ttype::INT8)
			{
				return Variant(static_cast<Tint8 *>(_d)->value());
			}
			else if (dt == sdds::Ttype::INT16)
			{
				return Variant(static_cast<Tint16 *>(_d)->value());
			}
			else if (dt == sdds::Ttype::INT32)
			{
				return Variant(static_cast<Tint32 *>(_d)->value());
			}
			else if (dt == sdds::Ttype::FLOAT32)
			{
				Tfloat32 *val = static_cast<Tfloat32 *>(_d);
				if (!val->isNan())
					return Variant(val->value());
			}
			else if (dt == sdds::Ttype::FLOAT64)
			{
				Tfloat64 *val = static_cast<Tfloat64 *>(_d);
				if (!val->isNan())
					return Variant(val->value());
			}
			else if (dt == sdds::Ttype::ENUM && !_enumAsText)
			{
				dtypes::uint8 *value = static_cast<dtypes::uint8 *>(static_cast<TenumBase *>(_d)->pValue());
				return Variant(*value);
			}
			else if (dt == sdds::Ttype::STRING || dt == sdds::Ttype::TIME || (dt == sdds::Ttype::ENUM && _enumAsText))
			{
				return Variant(_d->to_string().c_str());
			}
			return Variant(); // NULL
		}

		/**
		 * @brief serialize a single burst dataset
		 */
		static Variant serializeBurstDataset(system_tick_t _refTime, TvarBurstDataset _dataset)
		{
			Variant data;
			for (size_t i = 0; i < _dataset.Fdataset.size(); ++i)
			{
				data.append(serializeBurstData(_refTime, _dataset.Fdataset[i]));
			}
			Variant var;
			String key = (_dataset.FdescrPtr->parent()) ? getVarPath(_dataset.FdescrPtr) : "";
			var.set(key, data);
			return (var);
		}

		/**
		 * @brief serialize burst data
		 */
		static Variant serializeBurstData(system_tick_t _refTime, TburstData _data)
		{
			Variant data = _data.Fdata;
			data.set(FburstTimeOffsetKey, _data.Ftime - _refTime);
			return (data);
		}

	public:
		// key of the time base in data bursts (ISO 8601 string)
		static const char *burstTimeBaseKey() { return FburstTimeBaseKey; }

		/**
		 * @brief format a unix time in milliseconds as an ISO8601 timestamp with
		 * millisecond precision (e.g. 2026-04-10T15:20:32.123Z)
		 * note: the per data point offsets ("o") are in milliseconds, so a time base
		 * that only resolves whole seconds would throw all of them off by up to a second
		 */
		static String formatTime_ms(dtypes::int64 _unix_ms)
		{
			// floored division so that the millisecond part is never negative
			dtypes::int64 sec = _unix_ms / 1000;
			dtypes::int32 ms = static_cast<dtypes::int32>(_unix_ms - sec * 1000);
			if (ms < 0)
			{
				ms += 1000;
				sec -= 1;
			}
			// splice the milliseconds in ahead of the time zone (Z for UTC) instead of
			// hardcoding the suffix so this keeps working if the time zone is ever changed
			time_t t = static_cast<time_t>(sec);
			char buf[48];
			snprintf(buf, sizeof(buf), "%s.%03d%s",
					 Time.format(t, "%Y-%m-%dT%H:%M:%S").c_str(), static_cast<int>(ms),
					 Time.format(t, "%z").c_str());
			return (String(buf));
		}

		/**
		 * @brief serialize tree as variant
		 * @param _withKeys whether to include the identifier keys or use unnamed entries
		 */
		static Variant serializeTree(TmenuHandle *_struct, bool _withKeys = true)
		{
			Variant var;
			var.set(FtreeEnumKey, serializeEnums(_struct));
			var.set(FtreeStructKey, serializeStruct(_struct, _withKeys));
			return (var);
		}

		/**
		 * @brief serialize with particle information added
		 */
		static Variant serializeParticleTree(TmenuHandle *_struct)
		{
			Variant var = serializeTree(_struct, false);
			var.set(FtreeTypeKey, particleSystem().type.c_str());
			var.set(FtreeVersionKey, particleSystem().version.value());
			var.set(FtreeDeviceNameKey, particleSystem().name.c_str());
			return var;
		}

		/**
		 * @brief serialize tree values as variant
		 * @param _withNameAsKey whether to include the name as key or just the value
		 * @param _optsFilter whether to filter for values with speicifc options
		 * @param _enumAsText whether to include enum values as text (instead of number)
		 * @param _exclude a menu item to exclude from the serialization if encoutnered
		 */
		static Variant serializeValues(TmenuHandle *_struct, bool _withNameAsKey = true, int _optsFilter = -1, bool _enumAsText = true, TmenuHandle *_exclude = nullptr)
		{
			Variant var;
			for (auto it = _struct->iterator(); it.hasCurrent(); it.jumpToNext())
			{
				Variant item; // default: NULL
				Tdescr *d = it.current();
				if (d->isStruct())
				{
					TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
					if (mh && (!_exclude || mh != _exclude))
						item = serializeValues(mh, _withNameAsKey, _optsFilter, _enumAsText, _exclude);
				}
				else
				{
					if (_optsFilter >= 0)
					{
						// opts filter is active, check if d has the requested option(s)
						if ((d->meta().option & _optsFilter) != _optsFilter)
							continue;
					}
					item = serializeValue(d, _enumAsText);
				}
				if (_withNameAsKey)
					var.set(d->name(), item);
				else
					var.append(item);
			}
			return var;
		}

		/**
		 * @brief serialize with particle information added
		 */
		static Variant serializeParticleValues(TmenuHandle *_struct)
		{
			Variant var;
			var.set(FvaluesTypeKey, particleSystem().type.c_str());
			var.set(FvaluesVersionKey, particleSystem().version.value());
			var.set(FvaluesDeviceNameKey, particleSystem().name.c_str());
			var.set(FvaluesDataKey, serializeValues(_struct, false, -1, false));
			return var;
		}

		/**
		 * @brief serialize state variable/value pairs (i.e. all elemens that can be stored in EEPROM)
		 */
		static Variant serializeParticleState(TmenuHandle *_struct)
		{
			Variant var;
			var.set(FstateDeviceNameKey, particleSystem().name.c_str());
			var.set(FstateTimeBaseKey, formatTime_ms(particleSystem().unixTime_ms()));
			var.set(FstateDataKey, serializeValues(_struct, true, sdds::opt::saveval, true));
			return var;
		}

		/**
		 * @brief serialize data for transmission in data bursts
		 */
		static Variant serializeData(dtypes::uint32 _n, dtypes::float64 _value, dtypes::float64 _sdev, Tdescr *_unit = nullptr)
		{
			Variant data;
			data.set(FburstNumValueKey, _value);
			data.set(FburstNumCountKey, _n);
			if (_n > 1)
			{
				// no point including sdev if there's only one data point
				data.set(FburstNumSdevKey, _sdev);
			}
			if (_unit && _unit->type() == sdds::Ttype::STRING)
			{
				// got a unit (double checking that it's string)
				data.set(FburstUnitsKey, _unit->to_string().c_str());
			}
			return data;
		}

		static Variant serializeData(dtypes::float64 _value, Tdescr *_unit = nullptr)
		{
			Variant data;
			data.set(FburstNumValueKey, _value);
			if (_unit && _unit->type() == sdds::Ttype::STRING)
			{
				// got a unit (double checking that it's string)
				data.set(FburstUnitsKey, _unit->to_string().c_str());
			}
			return data;
		}

		static Variant serializeData(const dtypes::string &_text)
		{
			Variant data;
			data.set(FburstTextValueKey, _text);
			return data;
		}

		static Variant serializeData(Tdescr *_d, Tdescr *_unit = nullptr)
		{
			Variant data;
			auto dt = _d->type();
			if (dt == sdds::Ttype::ENUM || dt == sdds::Ttype::STRING || dt == sdds::Ttype::TIME)
			{
				// text based value
				data.set(FburstTextValueKey, serializeValue(_d, true));
			}
			else
			{
				// numeric value
				data.set(FburstNumValueKey, serializeValue(_d));
			}
			if (_unit && _unit->type() == sdds::Ttype::STRING)
			{
				// got a unit (double checking that it's string)
				data.set(FburstUnitsKey, _unit->to_string().c_str());
			}
			return data;
		}

		/**
		 * @brief serialize a data burst
		 * @param _refTime the reference time to normalize the burst data against
		 */
		static Variant serializeBurst(system_tick_t _refTime, const std::vector<TvarBurstDataset> &_data)
		{
			if (_data.size() == 0)
				return (Variant());
			Variant burst;

			// device name (will be "" if not yet retrieved - always available if publishReady() was checked first)
			burst.set(FburstDeviceNameKey, particleSystem().name.c_str());

			// time base (if valid time, otherwise NULL and the offset is machine time in millis)
			if (Time.isValid())
			{
				// note: the elapsed time is kept in exact milliseconds (unsigned tick subtraction,
				// correct across the ~49.7 day millis() wrap around) instead of being rounded to
				// whole seconds, so that the time base carries the same resolution as the offsets
				burst.set(FburstTimeBaseKey, formatTime_ms(particleSystem().unixTime_ms(millis() - _refTime)));
			}
			else
			{
				// note: if readyTopublish is checked first, will not get to this!
				burst.set(FburstTimeBaseKey, Variant());
			}

			// burst data
			Variant burstData;
			for (size_t i = 0; i < _data.size(); ++i)
			{
				burstData.append(serializeBurstDataset((Time.isValid() ? _refTime : 0), _data[i]));
			}
			burst.set(FburstDataKey, burstData);
			return (burst);
		}

		/**
		 * @brief serialize a command with it's return code and message
		 * @param _time offset of the command from the log's time base [ms]
		 * note: the command log keeps its ticks in the 64bit System.millis() time base rather
		 * than the 32bit millis() one, see serializeCommandLog() for why
		 * @todo do we need a version with keys here? probably fine as array
		 */
		static Variant serializeCommand(dtypes::uint64 _time, const String &_cmd, int _code, const char *_msg)
		{
			Variant var;
			var.append(_time);
			var.append(_cmd);
			var.append(_code);
			// var.append(_msg);
			return (var);
		}

		/**
		 * @brief serialize command log
		 * @param _refTime the time base of all the commands in the provided log [System.millis() ticks]
		 * @param _timeShift how much to shift ("time rebase") the logs in addition [ms]
		 * note: unlike the data bursts, log entries are only ever evicted when the log buffer
		 * overflows, never by age, so an entry can outlive the ~49.7 day wrap around of the
		 * 32bit millis() counter. The command log therefore uses the 64bit System.millis()
		 * time base, which never wraps, so that the time base (tb) stays correct no matter
		 * how long the device has been up or how old the oldest retained entry is
		 */
		static Variant serializeCommandLog(dtypes::uint64 _refTime, dtypes::uint64 _timeShift, const Variant &_log)
		{
			// rebase the logs
			Variant rebasedLog;
			if (_log.isArray())
			{
				for (size_t i = 0; i < _log.size(); ++i)
				{
					Variant entry = _log.at(i);
					if (_timeShift > 0 && entry.isArray() && !entry.isEmpty())
					{
						// shift time
						dtypes::uint64 offset = entry[0].toUInt64();
						entry.removeAt(0);
						if (offset > _timeShift)
							entry.prepend(offset - _timeShift);
						else
							entry.prepend(0);
					}
					rebasedLog.append(entry);
				}
			}

			// whole log
			Variant var;

			// time base (if valid time, otherwise NULL)
			if (Time.isValid())
			{
				var.set(FvarLogTimeBaseKey, formatTime_ms(particleSystem().unixTime_ms(System.millis() - _refTime - _timeShift)));
			}
			else
			{
				var.set(FvarLogTimeBaseKey, Variant());
			}
			var.set(FvarLogCmdsLogKey, rebasedLog);
			return (var);
		}

		/**
		 * @brief get logs back out of string
		 */
		static Variant deserializeCommandLog(const char *_logString)
		{
			return Variant::fromJSON(_logString).get(FvarLogCmdsLogKey);
		}

		/**
		 * @brief get the path of the descriptor variable
		 */
		static String getVarPath(Tdescr *_d)
		{
			return getMenuPath(_d->parent(), _d->name());
		}

		static String getMenuPath(TmenuHandle *_d, String _path)
		{
			if (!_d || !_d->parent())
				return _path;
			return getMenuPath(_d->parent(), String(_d->name()) + String(".") + _path);
		}

		/**
		 * @brief convert variant to CBOR
		 * @note CBOR/binary cannot be transmitted via Particle.variable (UTF-8 only),
		 * base64 is the most compact printable format
		 */
		static String variantToCbor(const Variant &_var)
		{
			String cbor;
			OutputStringStream stream(cbor);
			if (encodeToCBOR(_var, stream) == 0)
			{
				return cbor;
				// Log.trace("CBOR stream (as hex):");
				// for (size_t i = 0; i < cbor.length(); ++i) Log.printf("%02x", cbor[i]); Log.print("\n");
				// Log.trace("CBOR stream (as base64):");
				// Log.print(encodeBinaryInBase64(cbor)); Log.print("\n");
			}
			else
			{
				Log.error("failed to convert Variant to CBOR");
			}
			return String();
		}

		/**
		 * @brief convert variant to Base64
		 */
		static String variantToBase64(const Variant &_var)
		{
			return encodeBinaryInBase64(variantToCbor(_var));
		}

		/**
		 * @brief encode binary data in base 64 for transmission in UTF-8 (e.g. via Particle.variable),
		 * see Base64 representation on https://docs.particle.io/tools/developer-tools/json/
		 * @note that the conversion of JSON --> CBOR --> base64 does not save that much space unless there's a lot of floats
		 */
		static String encodeBinaryInBase64(const String &_data)
		{

			String encoded;
			int val = 0, valb = -6;

			for (size_t i = 0; i < _data.length(); ++i)
			{
				val = (val << 8) + _data[i];
				valb += 8;
				while (valb >= 0)
				{
					encoded += base64_chars[(val >> valb) & 0x3F];
					valb -= 6;
				}
			}

			if (valb > -6)
			{
				encoded += base64_chars[((val << 8) >> (valb + 8)) & 0x3F];
			}

			while (encoded.length() % 4)
				encoded += '='; // padding

			return encoded;
		}

		/**
		 * @brief integer serializer for values up from 0 to 63
		 * returns '>' if value is too large
		 */
		static const char encodeIntToBase64(dtypes::uint8 _value)
		{
			if (_value > 63)
				return '>';
			return base64_chars[_value];
		}

		/**
		 * @brief turn char bask into integer
		 * returns -1 if value is not a valued base64 character
		 */
		static int decodeIntFromBase64(char _c)
		{
			if ('A' <= _c && _c <= 'Z')
				return _c - 'A'; // 0–25
			if ('a' <= _c && _c <= 'z')
				return _c - 'a' + 26; // 26–51
			if ('0' <= _c && _c <= '9')
				return _c - '0' + 52; // 52–61
			if (_c == '+')
				return 62;
			if (_c == '/')
				return 63;
			return -1; // invalid Base64 character
		}
	};

#pragma endregion

/*** particle variable response channels ***/
#pragma region response channels

	class TparticleVariableResponse
	{

	public:
		const static size_t RESPONSE_SIZE = 400;		   // FIXME particle::protocol::MAX_VARIABLE_VALUE_LENGTH;
		const static size_t DATA_SIZE = RESPONSE_SIZE - 1; // leave 1 char space for adding number of transmissions remaining (in base 64)
		const static size_t INITIAL_SIZE = DATA_SIZE - 1;  // leave an additional 1 char space for indicating the channel the data is stored in
		const static size_t N_CHANNELS = 4;				   // how many channels are available?

		// channel class
		class Tchannel
		{

			String Fresponse = "";
			size_t Fchannel = 0;
			size_t FbytesStart = 0;
			bool Fused = false; // explicit flag, a tick of 0 is a legitimate FlastUsed
			system_tick_t FlastUsed = 0;

			void markUsed()
			{
				Fused = true;
				FlastUsed = millis();
			}

		public:
			Tchannel() {}

			// management
			size_t getBytesRemaining()
			{
				return (FbytesStart < Fresponse.length()) ? Fresponse.length() - FbytesStart : 0;
			}
			bool idle() { return Fresponse.length() == 0; }
			bool neverUsed() { return !Fused; }
			bool isOlderThan(system_tick_t _lastUsed)
			{
				// note: signed tick difference instead of a plain < comparison of the absolute
				// tick values so that the least-recently-used selection stays correct across
				// the ~49.7 day millis() wrap around (otherwise a channel used right after the
				// wrap looks older than one used before it and gets evicted while still in use)
				return static_cast<dtypes::TtickCount_signed>(FlastUsed - _lastUsed) < 0;
			}
			system_tick_t lastUsed() { return FlastUsed; }
			void release() { assign(""); }
			void registerChannel(size_t _channel)
			{
				Fchannel = _channel;
				char variable[20];
				snprintf(variable, sizeof(variable), "getSddsCh%c", TparticleSerializer::encodeIntToBase64(_channel));
				Particle.variable(variable, [this]()
								  { return this->retrieve(); });
			}

			// functionality
			size_t getTransmissionsRemaining()
			{
				size_t remaining = getBytesRemaining();
				return remaining / DATA_SIZE + ((remaining % DATA_SIZE > 0) ? 1 : 0);
			}
			size_t assign(const String &_response)
			{
				Fresponse = _response;
				FbytesStart = 0;
				markUsed();
				if (_response.length() > 0)
				{
					Log.trace("assigned new value to channel '%c' (idx %d): %d bytes, %d transmissions: ",
							  TparticleSerializer::encodeIntToBase64(Fchannel), Fchannel,
							  getBytesRemaining(), getTransmissionsRemaining());
				}
				return (getTransmissionsRemaining());
			}
			String retrieve()
			{
				if (getBytesRemaining() == 0)
					return ("-");
				markUsed();
				size_t remaining = getTransmissionsRemaining();
				remaining = (remaining < 1) ? 0 : remaining - 1;
				// format: transmissions remaining + actual data
				String result = String(TparticleSerializer::encodeIntToBase64(remaining)) +
								Fresponse.substring(FbytesStart, FbytesStart + DATA_SIZE);
				if (remaining == 0)
					release();
				else
					FbytesStart += DATA_SIZE;
				return result;
			}
		};

		// array of channels (could be made a vector for flexible number of channels)
		Tchannel Fchannels[N_CHANNELS];

		// register channels
		void registerChannels()
		{
			for (size_t i = 0; i < N_CHANNELS; i++)
			{
				Fchannels[i].registerChannel(i);
			}
		}

		// queue a message
		String queue(const String &_response)
		{
			//  any message
			if (_response.length() == 0)
				return "";
			// message small enough for single transmission?
			if (_response.length() < DATA_SIZE)
			{
				return "-" + _response; // - is the channel marker, i.e. no channel used
			}
			// find channel to use
			size_t channel = 0;
			size_t oldestFreeChannel = 0;
			bool foundFreeChannel = false;
			system_tick_t oldestFreeLastUsed = 0;
			size_t oldestInUseChannel = 0;
			bool foundInUseChannel = false;
			system_tick_t oldestInUseLastUsed = 0;
			bool foundChannel = false;
			for (size_t i = 0; i < N_CHANNELS; i++)
			{
				// if any never used channels --> use them first
				if (Fchannels[i].idle() && Fchannels[i].neverUsed())
				{
					channel = i;
					foundChannel = true;
					break;
				}
				// check for oldest free channel (all idle channels reaching here have been used)
				if (Fchannels[i].idle())
				{
					if (!foundFreeChannel || Fchannels[i].isOlderThan(oldestFreeLastUsed))
					{
						oldestFreeChannel = i;
						oldestFreeLastUsed = Fchannels[i].lastUsed();
						foundFreeChannel = true;
					}
				}
				// check for oldest channel that's in use
				else if (!foundInUseChannel || Fchannels[i].isOlderThan(oldestInUseLastUsed))
				{
					oldestInUseChannel = i;
					oldestInUseLastUsed = Fchannels[i].lastUsed();
					foundInUseChannel = true;
				}
			}
			// if no unsed channel, use the oldest free (if there are any), or the oldest in use
			if (!foundChannel)
			{
				channel = (foundFreeChannel) ? oldestFreeChannel : oldestInUseChannel;
			}
			// assign to channel and return initial message
			// format: base64 channel ID + base64 remaining transmissions + actual data
			size_t remaining = Fchannels[channel].assign(_response.substring(INITIAL_SIZE));
			String initial = String(TparticleSerializer::encodeIntToBase64(channel)) +
							 String(TparticleSerializer::encodeIntToBase64(remaining)) +
							 _response.substring(0, INITIAL_SIZE);
			return initial;
		}

	}
	// particle variable response object
	FvarResp;

#pragma endregion

/*** particle publishing ***/
#pragma region publishing

	class TparticlePublisher
	{

	private:
		// burst & publish management
		bool FnewBurstData = false;
		system_tick_t FminTime = 0;						 // smallest burst data timestamp (to normalize against)
		std::vector<TvarBurstDataset> FburstData;		 // data in current burst
		Ttimer FburstTimer;								 // timer keeping track of bursts
		CloudEvent Fevent;								 // cloud event
		VariantArray FeventData;						 // current event data
		VariantArray FqueuedBursts;						 // stack of data ready for publishing
		const system_tick_t FpublishcheckInterval = 200; // publish status check timer [ms]
		Ttimer FpublishCheckTimer;						 // publish check timer
		system_tick_t FlastMemoryCheck = 0;				 // last free memory check when adding burst data

		void printVariant(const Variant &data)
		{
			if (Log.isTraceEnabled())
			{
				Log.print(data.toJSON().c_str());
				Log.print("\n");
			}
		}

		/**
		 * @brief clear burst
		 */
		void clearBurst()
		{
			FnewBurstData = false;
			FburstData.clear();
			FminTime = 0;
		}

	public:
		/**
		 * @brief check if we're ready to publish (need a name and valid time)
		 * note: after a cold power up the RTC starts at 2000-01-01 and Time.isValid() stays
		 * false until the cloud syncs the clock, so anything that carries an absolute time
		 * base has to wait for this before it can be serialized
		 */
		bool readyToPublish()
		{
			return particleSystem().name != "" && Time.isValid();
		}

		// constructor + logic
		TparticlePublisher()
		{

			// data bursts
			on(FburstTimer)
			{
				if (readyToPublish())
				{
					// split into bursts that fit into a cloud event if necessary
					// (e.g. data collected for a long time while the time was not valid yet)
					queueBurst(std::move(FburstData));
					clearBurst();
				}
				else
				{
					// want to publish but not yet ready to
					// --> keep the burst going = restart the timer
					if (FnewBurstData)
					{
						// evrerytime there's new data, show it (only serialized if tracing, the burst can get large)
						if (Log.isTraceEnabled())
						{
							Log.trace("*** CONTINUING BURST, SO FAR: ***");
							printVariant(TparticleSerializer::serializeBurst(FminTime, FburstData));
						}
						FnewBurstData = false;
					}
					FburstTimer.start(particleSystem().publishing.bursts.timer_ms);
				}
			};

			// data publishing
			on(FpublishCheckTimer)
			{

				// check ongoing cloud event
				if (!Fevent.isNew() && !Fevent.isSending())
				{
					if (Fevent.isSent())
					{
						Log.trace("publish succeeded");
						particleSystem().publishing.bursts.sent += particleSystem().publishing.bursts.sending;
					}
					else if (!Fevent.isValid())
					{
						Log.trace("publish failed, invalid (error %d, discarding)", Fevent.error());
						particleSystem().publishing.bursts.invalid += particleSystem().publishing.bursts.sending;
					}
					else if (!Fevent.isOk())
					{
						Log.error("publish failed, recoverable (error %d, re-queuing)", Fevent.error());
						particleSystem().publishing.bursts.failed += particleSystem().publishing.bursts.sending;
						for (auto var : FeventData)
						{
							FqueuedBursts.append(var); // add what is in eventData back at the end of the queue
							particleSystem().publishing.bursts.queued++;
						}
					}
					Fevent.clear();
					particleSystem().publishing.bursts.sending = 0;
				}

				// check if new cloud event can be sent
				if (!FqueuedBursts.isEmpty() && Particle.connected() && !Fevent.isSending())
				{
					FeventData = VariantArray();
					size_t cborSize = 0;
					// pack as much of the queued data into the event as is possible with the 16kb limit and what can currently be published given what's in flight
					// https://docs.particle.io/reference/device-os/typed-publish/
					while (!FqueuedBursts.isEmpty() && cborSize <= 16 * particle::protocol::MAX_EVENT_DATA_LENGTH && Fevent.canPublish(cborSize))
					{
						cborSize += getCBORSize(FqueuedBursts[0]);
						if (cborSize <= 16 * particle::protocol::MAX_EVENT_DATA_LENGTH)
						{
							// add to event data and remove from queue
							FeventData.append(FqueuedBursts[0]);
							FqueuedBursts.removeAt(0);
						}
					}
					// finalize event (if it holds any data)
					if (FeventData.size() > 0)
					{
						Fevent.name(particleSystem().publishing.event);
						Fevent.data(FeventData);
						particleSystem().publishing.bursts.sending = FeventData.size();
						particleSystem().publishing.bursts.queued -= particleSystem().publishing.bursts.sending;
						// try to publish publish
						if (!Particle.publish(Fevent))
						{
							Log.error("published failed immediately, discarding");
							Fevent.clear();
							particleSystem().publishing.bursts.invalid += particleSystem().publishing.bursts.sending;
							particleSystem().publishing.bursts.sending = 0;
						}
					}
				}

				// check in again?
				if (Fevent.isSending() || particleSystem().publishing.bursts.queued > 0)
					FpublishCheckTimer.start(FpublishcheckInterval);
			};
		}

		/**
		 * @brief add variant data to the current burst
		 * @param _always whether to add to the burst even if publishing is NOT active
		 */
		void addToBurst(Tdescr *_d, system_tick_t _time, const Variant &_data, bool _always = false)
		{
			// safety check
			if (_d == nullptr)
				return;

			// need to complete startup
			if (particleSystem().startup != TparticleSystem::TstartupStatus::complete)
				return;

// debug info
#ifdef SDDS_PARTICLE_DEBUG
			if (!_always && particleSystem().publishing.record != TonOff::ON)
				Log.trace("NOT adding to burst for %s: %s", TparticleSerializer::getVarPath(_d).c_str(), _data.toJSON().c_str());
			else
				Log.trace("ADDING to burst for %s: %s", TparticleSerializer::getVarPath(_d).c_str(), _data.toJSON().c_str());
#endif

			// publish needs to be active or these data need to be set to always
			if (!_always && particleSystem().publishing.record != TonOff::ON)
				return;

			// make room if RAM is running low (checked at most every 250 ms, System.freeMemory() walks the heap)
			if (millis() - FlastMemoryCheck >= 250)
			{
				FlastMemoryCheck = millis();
				ensureFreeMemory();
			}

			// keep track of min time
			// note: FburstData being empty (rather than FminTime being 0) is the "no reference
			// time yet" flag so that a legitimate tick of 0 does not permanently pin the burst
			// time base to boot time, and the comparison goes through a signed tick difference
			// so it stays correct across the ~49.7 day millis() wrap around
			if (FburstData.empty() || static_cast<dtypes::TtickCount_signed>(_time - FminTime) < 0)
				FminTime = _time;

			// add to existing burst data for this variable
			FnewBurstData = true;
			for (size_t i = 0; i < FburstData.size(); ++i)
			{
				if (FburstData[i].FdescrPtr == _d)
				{
					FburstData[i].Fdataset.push_back({_time, _data});
					return;
				}
			}

			// first burst data for this variable
			FburstData.push_back({_d, {_time, _data}});

			// start burst timer if it's not already running
			if (!FburstTimer.running())
				FburstTimer.start(particleSystem().publishing.bursts.timer_ms);
		}

		/**
		 * @brief burst publish the current value of Tdescr
		 */
		void addToBurst(Tdescr *_d, system_tick_t _time, bool _always = false)
		{
			addToBurst(_d, _time, TparticleSerializer::serializeData(_d), _always);
		}

		/**
		 * @brief discard the oldest queued bursts (or, if nothing is queued, the oldest data
		 * points of the current burst) until at least RESERVE_RAM of free memory is left
		 */
		void ensureFreeMemory()
		{
			if (System.freeMemory() >= RESERVE_RAM)
				return;
			uint32_t bursts = 0, points = 0;
			while (System.freeMemory() < RESERVE_RAM)
			{
				if (!FqueuedBursts.isEmpty())
				{
					FqueuedBursts.removeAt(0);
					particleSystem().publishing.bursts.queued--;
					particleSystem().publishing.bursts.discarded++;
					bursts++;
				}
				else if (dropOldestBurstPoint())
					points++;
				else
					break; // nothing left to free
			}
			if (bursts > 0)
				Log.warn("low memory (%lu bytes free): discarded the %lu oldest queued burst(s)", System.freeMemory(), bursts);
			if (points > 0)
				Log.warn("low memory (%lu bytes free): discarded the %lu oldest data point(s) of the current burst", System.freeMemory(), points);
		}

		/**
		 * @brief remove the oldest data point of the current burst
		 * @return false if there is nothing to remove
		 */
		bool dropOldestBurstPoint()
		{
			int oldest = -1;
			for (size_t i = 0; i < FburstData.size(); ++i)
			{
				if (FburstData[i].Fdataset.empty())
					continue;
				if (oldest < 0 || static_cast<dtypes::TtickCount_signed>(FburstData[i].Fdataset[0].Ftime - FburstData[oldest].Fdataset[0].Ftime) < 0)
					oldest = i;
			}
			if (oldest < 0)
				return false;
			auto &set = FburstData[oldest].Fdataset;
			set.erase(set.begin());
			if (set.empty())
				FburstData.erase(FburstData.begin() + oldest);
			return true;
		}

		/**
		 * @brief earliest time stamp and number of data points of burst data
		 */
		static size_t burstPoints(const std::vector<TvarBurstDataset> &_data, system_tick_t *_minTime = nullptr)
		{
			size_t n = 0;
			for (const auto &set : _data)
			{
				for (const auto &point : set.Fdataset)
				{
					if (_minTime && (n == 0 || static_cast<dtypes::TtickCount_signed>(point.Ftime - *_minTime) < 0))
						*_minTime = point.Ftime;
					n++;
				}
			}
			return n;
		}

		/**
		 * @brief queue burst data for publication, split into several bursts (by time)
		 * if it does not fit into a single cloud event
		 */
		void queueBurst(std::vector<TvarBurstDataset> _data)
		{
			system_tick_t minTime = 0;
			size_t n = burstPoints(_data, &minTime);
			if (n == 0)
				return;

			// small enough to try in one piece?
			if (n <= MAX_BURST_POINTS_TO_SERIALIZE)
			{
				Variant burst = TparticleSerializer::serializeBurst(minTime, _data);
				// a single data point that is too large is discarded by queueData
				if (n == 1 || getCBORSize(burst) <= MAX_BURST_CBOR)
				{
					queueData(burst);
					return;
				}
			}

			// split in two: by time (earlier/later half of the time range), data points are moved not copied
			uint32_t maxOffset = 0;
			for (const auto &set : _data)
				for (const auto &point : set.Fdataset)
					maxOffset = std::max<uint32_t>(maxOffset, point.Ftime - minTime);
			std::vector<TvarBurstDataset> early, late;
			for (size_t i = 0; i < _data.size(); ++i)
			{
				TvarBurstDataset e(_data[i].FdescrPtr), l(_data[i].FdescrPtr);
				auto &points = _data[i].Fdataset;
				for (size_t j = 0; j < points.size(); ++j)
				{
					// all at the same time (maxOffset 0): split by position instead
					bool isEarly = (maxOffset > 0) ? (points[j].Ftime - minTime <= maxOffset / 2)
												   : ((_data.size() > 1) ? (i < _data.size() / 2) : (j < points.size() / 2));
					(isEarly ? e : l).Fdataset.push_back(std::move(points[j]));
				}
				if (!e.Fdataset.empty())
					early.push_back(std::move(e));
				if (!l.Fdataset.empty())
					late.push_back(std::move(l));
			}
			_data.clear();
			_data.shrink_to_fit();
			queueBurst(std::move(early));
			queueBurst(std::move(late));
		}

		/**
		 * @brief the earliest queued data burst with a time base later than _after (read-only access
		 * for the bluetooth data transfer, the queue is not changed). Time bases are ISO 8601 strings
		 * in the same format and time zone, so they sort as strings. Pass "" to get the earliest one.
		 * @return nullptr if there is none
		 */
		const Variant *nextQueuedBurst(const String &_after)
		{
			const Variant *next = nullptr;
			String nextTb;
			for (int i = 0; i < FqueuedBursts.size(); i++)
			{
				const Variant &burst = FqueuedBursts[i];
				if (!burst.isMap() || !burst.has(TparticleSerializer::burstTimeBaseKey()))
					continue; // not a data burst (e.g. a queued tree/state snapshot)
				String tb = burst.get(TparticleSerializer::burstTimeBaseKey()).asString();
				if (tb.length() == 0 || (_after.length() > 0 && tb.compareTo(_after) <= 0))
					continue;
				if (!next || tb.compareTo(nextTb) < 0)
				{
					next = &burst;
					nextTb = tb;
				}
			}
			return next;
		}

		/**
		 * @brief number of queued data bursts with a time base later than _after ("" = all)
		 */
		size_t countQueuedBursts(const String &_after)
		{
			size_t n = 0;
			for (int i = 0; i < FqueuedBursts.size(); i++)
			{
				const Variant &burst = FqueuedBursts[i];
				if (!burst.isMap() || !burst.has(TparticleSerializer::burstTimeBaseKey()))
					continue;
				String tb = burst.get(TparticleSerializer::burstTimeBaseKey()).asString();
				if (tb.length() > 0 && (_after.length() == 0 || tb.compareTo(_after) > 0))
					n++;
			}
			return n;
		}

		/**
		 * @brief queues Variant for publication, typically data is added through
		 * addToBurst but this function can be used to queue any data directly
		 * for publication
		 * @return true if successfully queued, false if too large to be queued
		 */
		bool queueData(const Variant &_data)
		{
			// safety check if data is small enough (less than 16kB)
			// https://docs.particle.io/reference/device-os/typed-publish/
			if (getCBORSize(_data) > MAX_BURST_CBOR)
			{
				// burst too large!
				Log.error("cannot queue data because it exceeds the 16kB cloud event limit");
				// FIXME: should this be reported as an error in some additional way?
				particleSystem().publishing.bursts.discarded++;
				printVariant(_data);
				return false;
			}

			// add to event data queue stack
			Log.trace("*** QUEUEING DATA: ***");
			FqueuedBursts.append(_data);
			printVariant(_data);
			particleSystem().publishing.bursts.queued++;
			ensureFreeMemory();
			if (!FpublishCheckTimer.running())
				FpublishCheckTimer.start(0);
			return true;
		}

	}
	// publisher object
	Fpublisher;

#pragma endregion

/*** automatic publishing of sdds variables  ***/
#pragma region variables publish

	// auto-detection of unit vars
	bool FunitAutoDetect;
	dtypes::string FunitVarSuffix;

	/**
	 * @brief the container for the variables; interval menu
	 */
	class TparticleVariableIntervals : public TmenuHandle
	{
	private:
		Tmeta meta() override { return Tmeta{TYPE_ID, 0, "varIntervals_ms"}; }

	public:
		TparticleVariableIntervals() {}
	} sddsParticleVariables;

	/**
	 * @brief menu handle with provided name
	 */
	class TnamedMenuHandle : public TmenuHandle
	{
		dtypes::string Fname;
		Tmeta meta() override { return Tmeta{TYPE_ID, 0, Fname.c_str()}; }

	public:
		TnamedMenuHandle(const dtypes::string _name) : Fname(_name) {};
	};

	/**
	 * @brief keeps track of the publish intervals (see setVariableInterval), provides access to the original sdds var, and defines the reset() and publish() methods
	 */
	class TparticleVarWrapper : public Tint32
	{
	private:
		// timer and callback wrappers
		Ttimer Ftimer;
		TcallbackWrapper FintervalCbw{this};
		TcallbackWrapper ForiginCbw{this};

	protected:
		// origin
		Tdescr *FvarOrigin = nullptr;
		Tdescr *FlinkedUnit = nullptr;

		// previous value
		bool FhasPreviousValue = false;
		virtual void storePreviousValue() = 0;
		virtual bool isValueDifferent() = 0;

		// publishing
		TparticlePublisher *Fpublisher = nullptr;
		system_tick_t FlastUpdateTime = 0;
		bool FhasUpdate = false; // explicit flag, a tick of 0 is a legitimate FlastUpdateTime
		void markUpdated()
		{
			FlastUpdateTime = millis();
			FhasUpdate = true;
		}
		virtual void clear() {}
		virtual void changeValue()
		{
			if (!FhasPreviousValue || isValueDifferent())
			{
				// by default, only update if the value is different
				markUpdated();
			}
		}
		virtual system_tick_t getTimeForPublish()
		{
			// default is the last update time
			return FlastUpdateTime;
		}
		virtual Variant getDataForPublish()
		{
			// default is just the value of the variable
			return TparticleSerializer::serializeData(FvarOrigin, FlinkedUnit);
		}

		// averaging suspended? (see suspendAveraging)
		bool Fsuspended = false;
		virtual void onSuspend() {}
		virtual void onResume() {}
		bool isAveraging() { return Fvalue == publish::AVG_GLOBAL || Fvalue > publish::MAX; }

	public:
		TparticleVarWrapper(Tdescr *_voi, TparticlePublisher *_pub, Tdescr *_unit) : FvarOrigin(_voi), Fpublisher(_pub), FlinkedUnit(_unit)
		{
			Fvalue = publish::OFF;

			// call back for origin value change
			ForiginCbw = [this](void *_ctx)
			{
				// only start collecting values once startup is complete
				if (particleSystem().startup == TparticleSystem::TstartupStatus::complete)
				{
					if (Fvalue == publish::EACH || Fvalue == publish::ALWAYS)
					{
						// publish current variable value immediately (but only if it has changed!)
						if (!FhasPreviousValue || isValueDifferent())
							publish();
					}
					else if (!Fsuspended)
					{
						// collect values (not while averaging is suspended)
						changeValue();
					}
				}
				// keep track of previous value
				storePreviousValue();
				if (!FhasPreviousValue)
					FhasPreviousValue = true;
			};

			// call back for the publish interval changing
			FintervalCbw = [this](void *_ctx)
			{
				// reset callback, timer and values
				FvarOrigin->callbacks()->remove(&ForiginCbw);
				Ftimer.stop();
				reset();

				// no publishing for this variable
				if (Fvalue == publish::OFF)
					return;

				// add publish callback
				FvarOrigin->callbacks()->push_first(&ForiginCbw);

				// if Fvalue is set > MAX --> start own timer
				if (Fvalue > publish::MAX)
				{
					if (Fvalue < 1000)
						Fvalue = 1000; // min interval is 1 second
					Ftimer.start(Fvalue);
				}
			};
			callbacks()->addCbw(FintervalCbw);

			// trigger publish
			on(Ftimer)
			{
				// publish (only does anything if there's data)
				publish();
				// restart timer
				Ftimer.start(Fvalue);
			};
		}

		// setting the interval values
		void operator=(Tint32::dtype _v) { __setValue(_v); }
		template <typename T>
		void operator=(T _val) { __setValue(_val); }

		// original sdds var access
		Tdescr *origin() { return FvarOrigin; }
		Tmeta meta() override { return Tmeta{Tint32::TYPE_ID, sdds::opt::saveval, FvarOrigin->name()}; }

		/**
		 * @brief suspend / resume averaging (only affects variables that are averaged, i.e. AVG_GLOBAL
		 * or a custom interval): while suspended, value changes are ignored and the time while suspended
		 * does not count towards the time-weighted average. The data collected up to the suspension is
		 * kept (and published at the regular time, even if it's only a single value).
		 */
		void suspendAveraging(bool _suspend)
		{
			if (Fsuspended == _suspend)
				return;
			Fsuspended = _suspend;
			if (!isAveraging() || particleSystem().startup != TparticleSystem::TstartupStatus::complete)
				return;
			_suspend ? onSuspend() : onResume();
		}
		bool averagingSuspended() { return Fsuspended; }

		/**
		 * @brief does this use the global publishing interval?
		 */
		bool usesGlobalPublishingInterval()
		{
			return (Fvalue == publish::AVG_GLOBAL);
		}

		// methods implemented in derived classes
		/**
		 * @brief reset the data storage
		 */
		void reset()
		{
			FlastUpdateTime = 0; // reset to no value
			FhasUpdate = false;
			clear();
		}

		/**
		 * @brief adds to the burst of the publisher
		 */
		void publish()
		{
			// is publishing off for this variable? --> stop
			if (Fvalue == publish::OFF)
				return;

			// is publishing immediate or always? --> publish current value (whatever it is)
			if (Fvalue == publish::EACH || Fvalue == publish::ALWAYS)
			{
				if (Fpublisher)
					Fpublisher->addToBurst(FvarOrigin, millis(), TparticleSerializer::serializeData(FvarOrigin, FlinkedUnit), Fvalue == publish::ALWAYS);
				return;
			}

			// otherwise we're publishing as INHERIT --> check first if there is a recent value
			if (!FhasUpdate)
				return;

			// okay publishing the collected data
			if (Fpublisher)
				Fpublisher->addToBurst(FvarOrigin, getTimeForPublish(), getDataForPublish(), Fvalue == publish::ALWAYS);
			reset();
		}
	};

	/**
	 * @brief wrapper for string SDDS vars
	 */
	class TparticleStringVarWrapper : public TparticleVarWrapper
	{

	private:
		dtypes::string FpreviousValue;

	protected:
		dtypes::string originValue()
		{
			return (static_cast<Tstring *>(origin())->Fvalue);
		}

		void storePreviousValue() override
		{
			FpreviousValue = originValue();
		}

		bool isValueDifferent() override
		{
			return (FpreviousValue != originValue());
		}

	public:
		TparticleStringVarWrapper(Tdescr *_voi, TparticlePublisher *_pub, Tdescr *_unit) : TparticleVarWrapper(_voi, _pub, _unit)
		{
		}
	};

	/**
	 * @brief wrapper for enum SDDS vars
	 */
	class TparticleEnumVarWrapper : public TparticleVarWrapper
	{

	private:
		dtypes::uint8 FpreviousValue;

	protected:
		dtypes::uint8 originValue()
		{
			return (*static_cast<dtypes::uint8 *>(static_cast<TenumBase *>(origin())->pValue()));
		}

		void storePreviousValue() override
		{
			FpreviousValue = originValue();
		}

		bool isValueDifferent() override
		{
			return (FpreviousValue != originValue());
		}

	public:
		TparticleEnumVarWrapper(Tdescr *_voi, TparticlePublisher *_pub, Tdescr *_unit) : TparticleVarWrapper(_voi, _pub, _unit)
		{
		}
	};

	/**
	 * @brief wrapper for numeric SDDS vars (includes averaging if not set to immediate publish)
	 * note that this still publishes the original numeric data format (e.g. int) if it's
	 * immediate publish (=1) but any averaging even of ints will turn them to doubles
	 * for numerical accuracy - in the end it doesn't really make a difference in the
	 * resulting JSON though
	 */
	template <class sdds_dtype>
	class TparticleNumericVarWrapper : public TparticleVarWrapper
	{

	private:
		// keep track of data
		TrunningStats rs;

		// keeping track of time
		bool FhasFirstValue = false;
		bool FhasLatestValue = false;		// explicit flag, a tick of 0 is a legitimate FlatestTime
		dtypes::TtickCount FstartTime = 0;	// start time of the averaged value
		dtypes::TtickCount FlatestTime = 0; // last time a value was received
		dtypes::TtickCount FsuspendTime = 0; // when averaging was suspended
		dtypes::float64 FlatestValue = 0;	// last value that was received

		/**
		 * @brief add latest value to the running average with the weight based on the time interval
		 * sets the new latest value/time to the current value/time
		 */
		void addLatest()
		{
			// do we already have a value? --> add it to the running stats
			if (FhasLatestValue)
			{
				// note: unsigned tick subtraction, correct across the ~49.7 day millis() wrap around
				rs.add(FlatestValue, millis() - FlatestTime);
			}
			// store the latest time and value
			FlatestValue = static_cast<dtypes::float64>(this->originValue());
			FlatestTime = millis();
			FhasLatestValue = true;
		}

	protected:
		// origin value changes
		typedef typename sdds_dtype::dtype value_dtype;
		value_dtype FpreviousValue;
		sdds_dtype *typedOrigin() { return static_cast<sdds_dtype *>(origin()); }
		value_dtype originValue() { return typedOrigin()->Fvalue; }

		void storePreviousValue() override
		{
			FpreviousValue = originValue();
		}

		bool isValueDifferent() override
		{
			return (FpreviousValue != originValue());
		}

		void onSuspend() override
		{
			// close out the current value (weighted until now), nothing is added while suspended
			if (FhasLatestValue)
			{
				rs.add(FlatestValue, millis() - FlatestTime);
				FhasLatestValue = false;
			}
			FsuspendTime = millis();
		}

		void onResume() override
		{
			// continue with the current value from now on (the suspended time carries no weight)
			FlatestValue = static_cast<dtypes::float64>(this->originValue());
			FlatestTime = millis();
			FhasLatestValue = true;
			this->markUpdated();
			if (!FhasFirstValue)
			{
				FhasFirstValue = true;
				FstartTime = this->FlastUpdateTime;
			}
		}

		void clear() override
		{
			// already have one data point stored in the running stats
			// --> start next stats with FlatestValue (unless suspended, resuming starts the next stats)
			bool carryOver = (rs.count() > 0) && !this->Fsuspended;
			rs.reset(); // restart running stats
			FlatestTime = 0;
			FhasLatestValue = false;
			FhasFirstValue = false;
			if (carryOver)
				changeValue();
		}

		void changeValue() override
		{
			// always triggers update for continuously collected values even if value is the same
			this->markUpdated();
			// first value?
			if (!FhasFirstValue)
			{
				FhasFirstValue = true;
				FstartTime = this->FlastUpdateTime;
			}
			addLatest();
		}

		system_tick_t getTimeForPublish() override
		{
			if (rs.count() == 0)
			{
				// single point (i.e. no stats yet) -> return single time
				return FstartTime;
			}
			else
			{
				// multi point -> return middle of time interval
				// note: must be calculated as start + elapsed/2 rather than (now + start)/2
				// because the latter overflows the 32bit unsigned tick counter as soon as
				// millis() + FstartTime exceeds 2^32 (i.e. after ~24.9 days of uptime) and
				// is also incorrect across the ~49.7 day millis() wrap around, both of which
				// lead to a corrupted burst time base (tb)
				// (while suspended, the interval ends when the averaging was suspended)
				system_tick_t end = this->Fsuspended ? FsuspendTime : millis();
				return FstartTime + (static_cast<system_tick_t>(end - FstartTime) / 2);
			}
		}

		Variant getDataForPublish() override
		{
			if (rs.count() == 0)
			{
				// single point (i.e. no stats yet) -> return latest value, still with n = 1 (no sdev)
				// so it is not mistaken for a discrete value change downstream
				return TparticleSerializer::serializeData(1, FlatestValue, 0, this->FlinkedUnit);
			}
			else
			{
				// multi point -> add the value of the currently active data point before returning
				// (not while suspended: the last value was already closed out when suspending)
				if (!this->Fsuspended)
					addLatest();
				return TparticleSerializer::serializeData(rs.count(), rs.mean(), rs.stdDev(), this->FlinkedUnit);
			}
		}

	public:
		TparticleNumericVarWrapper(Tdescr *_voi, TparticlePublisher *_pub, Tdescr *_unit) : TparticleVarWrapper(_voi, _pub, _unit)
		{
		}
	};

	/**
	 * @brief create tree for the variable intervals
	 */
	void createVariableIntervalsTree(TmenuHandle *_src, TmenuHandle *_dst)
	{
		for (auto it = _src->iterator(); it.hasCurrent(); it.jumpToNext())
		{
			auto d = it.current();
			if (!d)
				continue;

			// is there a linked unit? (i.e. next variable named the same but with the unit suffix)
			Tdescr *linkedUnit = nullptr;
			if (FunitAutoDetect && d->next() && d->next()->type() == sdds::Ttype::STRING && d->next()->name() == (d->name() + FunitVarSuffix))
			{
				linkedUnit = d->next();
			}

			auto dt = d->type();
			// string wrapper
			if (dt == sdds::Ttype::STRING)
				_dst->addDescr(new TparticleStringVarWrapper(d, &Fpublisher, linkedUnit));
			// enum wrapper
			else if (dt == sdds::Ttype::ENUM)
				_dst->addDescr(new TparticleEnumVarWrapper(d, &Fpublisher, linkedUnit));
			// numeric wrappers
			else if (dt == sdds::Ttype::UINT8)
				_dst->addDescr(new TparticleNumericVarWrapper<Tuint8>(d, &Fpublisher, linkedUnit));
			else if (dt == sdds::Ttype::UINT16)
				_dst->addDescr(new TparticleNumericVarWrapper<Tuint16>(d, &Fpublisher, linkedUnit));
			else if (dt == sdds::Ttype::UINT32)
				_dst->addDescr(new TparticleNumericVarWrapper<Tuint32>(d, &Fpublisher, linkedUnit));
			else if (dt == sdds::Ttype::INT8)
				_dst->addDescr(new TparticleNumericVarWrapper<Tint8>(d, &Fpublisher, linkedUnit));
			else if (dt == sdds::Ttype::INT16)
				_dst->addDescr(new TparticleNumericVarWrapper<Tint16>(d, &Fpublisher, linkedUnit));
			else if (dt == sdds::Ttype::INT32)
				_dst->addDescr(new TparticleNumericVarWrapper<Tint32>(d, &Fpublisher, linkedUnit));
			else if (dt == sdds::Ttype::FLOAT32)
				_dst->addDescr(new TparticleNumericVarWrapper<Tfloat32>(d, &Fpublisher, linkedUnit));
			else if (dt == sdds::Ttype::FLOAT64)
				_dst->addDescr(new TparticleNumericVarWrapper<Tfloat64>(d, &Fpublisher, linkedUnit));
			// recursive through structure
			else if (dt == sdds::Ttype::STRUCT)
			{
				TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
				if (mh)
				{
					TmenuHandle *nextLevel = new TnamedMenuHandle(d->name());
					_dst->addDescr(nextLevel);
					createVariableIntervalsTree(mh, nextLevel);
				}
			}
			else
			{
				// FIXME: is anything else supported?
			}
		}
	}
	void createVariableIntervalsTree(TmenuHandle *_src)
	{
		createVariableIntervalsTree(_src, &sddsParticleVariables);
		particleSystem().publishing.addDescr(&sddsParticleVariables);
	}

	/**
	 * @brief set defaults for variables intervals tree
	 * @param _default value to set
	 * @param _optsFilter options filter
	 * @param _dtypes data types filter
	 */
	void setVariableIntervalsDefault(TmenuHandle *_vars, dtypes::int32 _default, int _optsFilter, const std::vector<sdds::Ttype> &_dtypes)
	{
		if (_default > publish::MAX && _default < 1000)
			_default = 1000; // min interval is 1 second
		for (auto it = _vars->iterator(); it.hasCurrent(); it.jumpToNext())
		{
			auto d = it.current();
			if (!d)
				continue;
			if (d->type() == sdds::Ttype::STRUCT)
			{
				TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
				if (mh)
				{
					setVariableIntervalsDefault(mh, _default, _optsFilter, _dtypes);
				}
			}
			else
			{
				TparticleVarWrapper *pvw = static_cast<TparticleVarWrapper *>(d);
				if (_optsFilter >= 0)
				{
					// opts filter is active, check if d has the requested option(s)
					if ((pvw->origin()->meta().option & _optsFilter) != _optsFilter)
						continue;
				}
				if (!_dtypes.empty())
				{
					// dtypes are provided, make sure it's one of them
					bool is_in_dtypes = false;
					for (size_t i = 0; i < _dtypes.size(); ++i)
					{
						if (pvw->origin()->type() == _dtypes[i])
						{
							is_in_dtypes = true;
							break;
						}
					}
					if (!is_in_dtypes)
						continue;
				}
				// opts filter and dtypes filter didn't throw us out --> set interval to the provided default
				pvw->__setValue(_default);
			}
		}
	}

	/**
	 * @brief set variable interval for a specific variable
	 * @param _var variable pointer
	 * @param _interval interval value to set
	 * sdds::particle::publish::INHERIT		-> use the globalPublishInterval
	 * sdds::particle::publish::OFF 		-> no publishes (initial default)
	 * sdds::particle::publish::IMMEDIATELY -> publish every time the value is set if publish is on
	 * sdds::particle::publish::ALWAYS -> publish every time the value is set even if publish is not on
	 * < 1000 	-> not allowed
	 * 1000+ 	-> publish every 1000+ ms
	 * @return whether _var was found in any of the variable intervals' origin
	 */
	/**
	 * @brief find the publishing wrapper of a variable
	 */
	TparticleVarWrapper *findVariableWrapper(TmenuHandle *_vars, Tdescr *_var)
	{
		for (auto it = _vars->iterator(); it.hasCurrent(); it.jumpToNext())
		{
			auto d = it.current();
			if (!d)
				continue;
			if (d->type() == sdds::Ttype::STRUCT)
			{
				TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
				TparticleVarWrapper *found = mh ? findVariableWrapper(mh, _var) : nullptr;
				if (found)
					return found;
			}
			else if (static_cast<TparticleVarWrapper *>(d)->origin() == _var)
				return static_cast<TparticleVarWrapper *>(d);
		}
		return nullptr;
	}

	bool setVariableInterval(TmenuHandle *_vars, dtypes::int32 _interval, Tdescr *_var)
	{
		if (_interval > publish::MAX && _interval < 1000)
			_interval = 1000; // min interval is 1 second
		for (auto it = _vars->iterator(); it.hasCurrent(); it.jumpToNext())
		{
			auto d = it.current();
			if (!d)
				continue;
			if (d->type() == sdds::Ttype::STRUCT)
			{
				TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
				if (mh)
				{
					if (setVariableInterval(mh, _interval, _var))
						return true;
				}
			}
			else
			{
				TparticleVarWrapper *pvw = static_cast<TparticleVarWrapper *>(d);
				if (pvw->origin() == _var)
				{
					// found the matching wrapper
					pvw->__setValue(_interval);
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * @brief publish the current value of a specific variable
	 * @param _var variable pointer
	 * @return whether _var was found in any of the variable intervals' origin
	 */
	bool publishVariable(Tdescr *_var)
	{
		return publishVariable(&sddsParticleVariables, _var);
	}

	// actual function
	bool publishVariable(TmenuHandle *_vars, Tdescr *_var)
	{
		for (auto it = _vars->iterator(); it.hasCurrent(); it.jumpToNext())
		{
			auto d = it.current();
			if (!d)
				continue;
			if (d->type() == sdds::Ttype::STRUCT)
			{
				TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
				if (mh)
				{
					if (publishVariable(mh, _var))
						return true;
				}
			}
			else
			{
				TparticleVarWrapper *pvw = static_cast<TparticleVarWrapper *>(d);
				if (pvw->origin() == _var)
				{
					// found the matching wrapper
					pvw->publish();
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * @brief publishing interval default
	 */
	struct TintervalDefault
	{
		dtypes::int32 Finterval;
		Tdescr *Fvar = nullptr;
		dtypes::int16 FoptsFilter = -1;
		std::vector<sdds::Ttype> FdtypeFilter = {};
		TintervalDefault(dtypes::int32 _interval, Tdescr *_var) : Finterval(_interval), Fvar(_var) {}
		TintervalDefault(dtypes::int32 _interval, const std::vector<sdds::Ttype> &_dtypes) : Finterval(_interval), FdtypeFilter(_dtypes) {}
		TintervalDefault(dtypes::int32 _interval, dtypes::int16 _opts) : Finterval(_interval), FoptsFilter(_opts) {}
		TintervalDefault(dtypes::int32 _interval, dtypes::int16 _opts, const std::vector<sdds::Ttype> &_dtypes) : Finterval(_interval), FoptsFilter(_opts), FdtypeFilter(_dtypes) {}
	};

	/**
	 * @brief default during setup
	 */
	void setupDefaults(const std::vector<TintervalDefault> &_defaults)
	{
		// set defaults
		for (size_t i = 0; i < _defaults.size(); ++i)
		{
			if (_defaults[i].Fvar)
				// single variable default
				setVariableInterval(&sddsParticleVariables, _defaults[i].Finterval, _defaults[i].Fvar);
			else
				// default by opts/type filter
				setVariableIntervalsDefault(&sddsParticleVariables, _defaults[i].Finterval, _defaults[i].FoptsFilter, _defaults[i].FdtypeFilter);
		}
	}

	/**
	 * @brief reset values of global publish vars
	 */
	void resetGlobal()
	{
		resetGlobal(&sddsParticleVariables);
	}
	void resetGlobal(TmenuHandle *_vars)
	{
		for (auto it = _vars->iterator(); it.hasCurrent(); it.jumpToNext())
		{
			auto d = it.current();
			if (!d)
				continue;
			if (d->type() == sdds::Ttype::STRUCT)
			{
				TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
				if (mh)
					resetGlobal(mh);
			}
			else
			{
				TparticleVarWrapper *pvw = static_cast<TparticleVarWrapper *>(d);
				if (pvw->usesGlobalPublishingInterval())
					pvw->reset();
			}
		}
	}

	/**
	 * @brief publish values of global publish vars
	 */
	void publishGlobal()
	{
		publishGlobal(&sddsParticleVariables);
	}
	void publishGlobal(TmenuHandle *_dst)
	{
		for (auto it = _dst->iterator(); it.hasCurrent(); it.jumpToNext())
		{
			auto d = it.current();
			if (!d)
				continue;
			if (d->type() == sdds::Ttype::STRUCT)
			{
				TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
				if (mh)
					publishGlobal(mh);
			}
			else
			{
				TparticleVarWrapper *pvw = static_cast<TparticleVarWrapper *>(d);
				if (pvw->usesGlobalPublishingInterval())
					pvw->publish();
			}
		}
	}

#pragma endregion

private:
/*** particle variables/functions ***/
#pragma region particle vars/funcs

	// sdds structure
	TmenuHandle *Froot = nullptr;

	// whether to reset the state/EEPROM
	bool FresetState = false;

	// plain comms handler to process sdds variable commands
	TplainCommHandler Fpch;

#if Wiring_BLE
	// bluetooth spike: sdds tree access via BLE (status in SYSTEM.bluetooth)
	TbleSpike Fble;

	// bluetooth data transfer: copies of the queued data bursts (the queue itself is never changed)
	static constexpr size_t BLE_DATA_CHUNK = 1024; // max JSON characters per line
	Ttimer FbleDataTimer;						   // sends the next lines while the tx buffer has room
	bool FbleDataActive = false;
	String FbleDataCursor; // time base of the last burst that was sent
	String FbleDataJson;   // JSON of the burst currently being sent
	size_t FbleDataPos = 0;
	uint32_t FbleDataSent = 0;
#endif

	// Timer for global publishing interval
	Ttimer FglobalPublishTimer;
	Ttimer FglobalPublishInfoTimer;

	// error codes
	constexpr static int ERR_NO_CMD = -200;
	constexpr static int ERR_CMDS_MAX = -201;
	constexpr static int ERR_EVENT_SIZE_MAX = -202;
	constexpr static int ERR_NOT_READY = -203;

	// deferred startup state snapshot
	// a state snapshot carries an absolute time base (tb) instead of offsets, but on a cold
	// power up the RTC only becomes valid once the cloud syncs the clock, which is several
	// seconds after sdds::setup() runs. Publishing the snapshot is thus deferred until the device
	// is actually ready to publish.
	bool FstartupStatePending = false;
	Ttimer FstartupStateTimer;
	const system_tick_t FstartupStateCheckInterval = 1000; // [ms]

	// command log
	// note: 64bit System.millis() ticks rather than 32bit millis() ones because log entries
	// are only evicted on buffer overflow and can therefore outlive the ~49.7 day wrap around
	dtypes::uint64 FcmdLogMinTime = 0; // earliest time in the log
	char FcmdLog[particle::protocol::MAX_FUNCTION_ARG_LENGTH] = "[]\0";

	/**
	 * @brief mask secrets (any variable called "password") in commands before they are logged
	 */
	static String maskSecrets(const String &_cmd)
	{
		int eq = _cmd.indexOf('=');
		if (eq < 0)
			return _cmd;
		String path = _cmd.substring(0, eq);
		if (path == "password" || path.endsWith(".password"))
			return path + "=***";
		return _cmd;
	}

	void logCommand(dtypes::uint64 _time, const String &_cmdRaw, int _errCode, const char *_errText)
	{
		String _cmd = maskSecrets(_cmdRaw);
		// append to sddslog cloud var Variant array
		Variant cmd_log = TparticleSerializer::deserializeCommandLog(FcmdLog);
		Variant new_cmd = TparticleSerializer::serializeCommand(_time - FcmdLogMinTime, _cmd, _errCode, _errText);
		cmd_log.append(new_cmd);
		size_t msg_log_size = cmd_log.toJSON().length(); // reserve 40 chars for timebase
		while (msg_log_size >= (particle::protocol::MAX_FUNCTION_ARG_LENGTH - 40) && !cmd_log.isEmpty())
		{
			// remove the oldest entries until they fit
			cmd_log.removeAt(0);
			msg_log_size = cmd_log.toJSON().length();
		}
		// time offset
		if (!cmd_log.isEmpty())
		{
			// store back in the cloud log particle variable
			dtypes::uint64 timeRebaseShift = 0;
			if (cmd_log[0].isArray() && !cmd_log[0].isEmpty())
			{
				// rebased based on first entry if there is one
				timeRebaseShift = cmd_log[0][0].toUInt64();
			}
			Variant log = TparticleSerializer::serializeCommandLog(FcmdLogMinTime, timeRebaseShift, cmd_log);
			snprintf(FcmdLog, particle::protocol::MAX_FUNCTION_ARG_LENGTH, "%s", log.toJSON().c_str());
			FcmdLogMinTime = FcmdLogMinTime + timeRebaseShift;
		}
		else
		{
			// nothing to store
			FcmdLogMinTime = 0;
			snprintf(FcmdLog, particle::protocol::MAX_FUNCTION_ARG_LENGTH, "[]");
		}
	}

	/**
	 * @brief Particle.function sddsSetVariables
	 */
	int setVariables(String _cmd)
	{
		// parse _cmds into individual commands
		std::vector<String> cmds;
		dtypes::uint64 timestamp = System.millis();
		size_t start = 0;
		for (size_t i = 0; i < _cmd.length(); ++i)
		{
			char c = _cmd[i];
			if (c == ' ')
			{
				if (start == i)
				{
					// nope, we're still at the start, skip the whitespace
					start = i + 1;
					continue;
				}
				else
				{
					// finished a command
					cmds.push_back(_cmd.substring(start, i));
					start = i + 1;
				}
			}
		}
		// any leftover to add?
		if (start < _cmd.length())
		{
			cmds.push_back(_cmd.substring(start, _cmd.length()));
		}

		// got anything?
		if (cmds.empty())
			return (ERR_NO_CMD);

		// got too many?
		// cannot store the true/false return values for more
		// than 31 commands in the 32-bit int return value
		if (cmds.size() > 31)
			return (ERR_CMDS_MAX);

		// process commands
		int returnValue = 0;
		for (size_t i = 0; i < cmds.size(); ++i)
		{
			Fpch.resetLastError();
			Fpch.handleMessage(cmds[i]);
			if (Fpch.lastError() == plainComm::Terror::e::___)
			{
				// success
				logCommand(timestamp, cmds[i], 0, "");
			}
			else
			{
				// fail
				returnValue |= (1 << i);
				logCommand(timestamp, cmds[i],
						   plainComm::Terror::toInt(Fpch.lastError()),
						   plainComm::Terror::c_str(Fpch.lastError()));
				if (cmds.size() == 1)
				{
					// single command --> return its code as negative
					return -plainComm::Terror::toInt(Fpch.lastError());
				}
			}
		}
		return returnValue;
	}

	/**
	 * @brief Particle.function sendSdds
	 */
	int publishTree(String _cmd)
	{
		Variant tree = TparticleSerializer::serializeParticleTree(Froot);
		if (!Fpublisher.queueData(tree))
			// the structure is too large to publish, must use Particle.variable sddsGetTree instead
			return ERR_EVENT_SIZE_MAX;

		// succesfully queued tree for cloud event
		return 0;
	}

	/**
	 * @brief Particle.variable getSdds
	 */
	String getTree()
	{
		Variant tree = TparticleSerializer::serializeParticleTree(Froot);
		return FvarResp.queue(tree.toJSON());
	}

	/**
	 * @brief Particle.function sendSddsValues
	 */
	int publishValues(String _cmd)
	{
		Variant values = TparticleSerializer::serializeParticleValues(Froot);
		if (!Fpublisher.queueData(values))
			// the values are too large to publish, must use sddsGetValues instead
			return ERR_EVENT_SIZE_MAX;

		// succesfully queued values for cloud event
		return 0;
	}

	/**
	 * @brief Particle.variable getSddsValues
	 */
	String getValues()
	{
		Variant values = TparticleSerializer::serializeParticleValues(Froot);
		return FvarResp.queue(values.toJSON());
	}

	/**
	 * @brief Particle.function sendSddsState
	 */
	int publishState(String _cmd)
	{
		// the snapshot's time base (tb) is an absolute timestamp rather than an offset, so
		// refuse to serialize it before the wall clock is valid and the name is known
		if (!Fpublisher.readyToPublish())
			return ERR_NOT_READY;

		Variant state = TparticleSerializer::serializeParticleState(Froot);
		if (!Fpublisher.queueData(state))
			// the state is oo large to publish, must use sddsGetValues instead
			return ERR_EVENT_SIZE_MAX;

		// succesfully queued values for cloud event
		return 0;
	}

	/**
	 * @brief Particle.variable getSddsSystem
	 */
	String getSystem()
	{
		// serialize the SYSTEM submenu except for the individual variable interval
		Variant system = TparticleSerializer::serializeValues(particleSystem(), true, -1, true, &sddsParticleVariables);
		return system.toJSON();
	}

#pragma endregion

/*** constructor and setup ***/
#pragma region constructor+setup

private:
	// keep track of publishing to detect when it switches from OFF to ON
	bool FisPublishing = particleSystem().publishing.record == TonOff::ON;
	system_tick_t FnextGlobalPublish = 0;

	void startGlobalPublishTimer()
	{
		FnextGlobalPublish = millis() + particleSystem().publishing.globalInterval_ms.value();
		FglobalPublishTimer.start(particleSystem().publishing.globalInterval_ms);
		FglobalPublishInfoTimer.start(0);
	}

public:
	/**
	 * @brief particle spike consturctor
	 * @param _root the sdds tree
	 * @param _type name of the structure type, i.e. what kind of device is this? ("pump", "mfc", etc.)
	 * @param _dataEvent default name of the data event (e.g. sddsData, the default in the alternative constructor)
	 * @param _version version of the structure type to inform servers who have cached structure when the structure type has been updated
	 * @param _unit name of unit vars suffix for auto-detecting dynamic units (pass "" to turn auto-detection off)
	 */
	TparticleSpike(TmenuHandle &_root, const dtypes::string &_type, const dtypes::string &_dataEvent, dtypes::uint16 _version, const dtypes::string &_unit) : Fpch(_root, nullptr)
#if Wiring_BLE
		, Fble(_root)
#endif
	{
		Froot = _root;
		particleSystem().type = _type;
		particleSystem().version = _version;
		particleSystem().publishing.event = _dataEvent;
		FunitVarSuffix = _unit;
		FunitAutoDetect = FunitVarSuffix != "";

		// custom system actions
		on(particleSystem().action)
		{
			if (particleSystem().action == TparticleSystem::Taction::___)
			{
				// do nothing
				return;
			}
			else if (particleSystem().action == TparticleSystem::Taction::sendSdds)
			{

				publishTree("");
				particleSystem().action = TparticleSystem::Taction::___;
			}
			else if (particleSystem().action == TparticleSystem::Taction::sendSddsValues)
			{
				publishValues("");
				particleSystem().action = TparticleSystem::Taction::___;
			}
			else if (particleSystem().action == TparticleSystem::Taction::sendSddsState)
			{
				publishState("");
				particleSystem().action = TparticleSystem::Taction::___;
			}
			// not implementing size based bursts for now, see https://github.com/KopfLab/SDDS_particleSpike/issues/4
			// else if (particleSystem().action == TparticleSystem::Taction::sendBurstData)
			// {
			// 	particleSystem().action = TparticleSystem::Taction::___;
			// }
		};

#if Wiring_BLE
		// bluetooth data transfer (see startBleData)
		on(FbleDataTimer)
		{
			sendBleData();
		};

		// bluetooth on/off (ignored until the ble spike is started after the state is loaded, see on(sdds::setup()))
		on(particleSystem().bluetooth.state)
		{
			(particleSystem().bluetooth.state == TonOff::ON) ? Fble.enable() : Fble.disable();
		};
#endif

		// global publishing interval
		on(particleSystem().publishing.globalInterval_ms)
		{
			if (particleSystem().publishing.globalInterval_ms < 1000)
				particleSystem().publishing.globalInterval_ms = 1000; // don't allow publishing any faster than once a second
			FglobalPublishTimer.stop();
			resetGlobal();
			startGlobalPublishTimer();
		};
		startGlobalPublishTimer();

		// (re)start timer when publishing is turned on
		on(particleSystem().publishing.record)
		{
			// triggers only if publishing is freshly turned on
			if (!FisPublishing && particleSystem().publishing.record == TonOff::ON)
			{
				FglobalPublishTimer.stop();
				resetGlobal();
				startGlobalPublishTimer();
			}
			FisPublishing = particleSystem().publishing.record == TonOff::ON;
		};

		// publish timer triggers
		on(FglobalPublishTimer)
		{
			publishGlobal(); // variables reset themselves after publish
			startGlobalPublishTimer();
		};

		// publishing info update timer
		on(FglobalPublishInfoTimer)
		{
			if (particleSystem().publishing.record != TonOff::ON)
			{
				particleSystem().publishing.nextGlobalPublish = "off";
			}
			// note: signed tick difference instead of a plain > comparison so this stays
			// correct once millis() + globalInterval_ms wraps the 32bit tick counter
			else if (static_cast<dtypes::TtickCount_signed>(FnextGlobalPublish - millis()) > 0)
			{
				char buf[20]; // snprintf buffer
				dtypes::uint32 diff = static_cast<dtypes::uint32>(round((FnextGlobalPublish - millis()) / 1000.));
				if (diff > 3600)
					snprintf(buf, sizeof(buf), "%dh%dm", diff / 3600, (diff % 3600) / 60);
				else if (diff > 60)
					snprintf(buf, sizeof(buf), "%dm%ds", diff / 60, diff % 60);
				else
					snprintf(buf, sizeof(buf), "%ds", diff);
				particleSystem().publishing.nextGlobalPublish = buf;
			}
			else
			{
				particleSystem().publishing.nextGlobalPublish = "now";
			}
			if (particleSystem().publishing.record == TonOff::ON)
				FglobalPublishInfoTimer.start(1000); // restart
		};

		// deferred startup state snapshot (waits for the wall clock, see FstartupStatePending)
		on(FstartupStateTimer)
		{
			if (!FstartupStatePending)
				return;
			if (particleSystem().publishing.record != TonOff::ON)
			{
				// recording was turned off while waiting --> don't send the snapshot after all
				FstartupStatePending = false;
				return;
			}
			if (!Fpublisher.readyToPublish())
			{
				// no valid wall clock and/or no device name yet --> check back later
				FstartupStateTimer.start(FstartupStateCheckInterval);
				return;
			}
			FstartupStatePending = false;
			publishState("");
		};

		// startup complete
		on(sdds::setup())
		{

			// initialize state stream
			sdds::paramSave::Tstream::INIT();

			// resetting or loading state?
			(FresetState) ? particleSystem().saveState(true) : particleSystem().loadState();

			// start the wifi/cloud connection now that the saved state (wifi on/off) is loaded
			particleSystem().startConnection();

#if Wiring_BLE
			// start bluetooth now that the saved state (on/off and device name) is loaded
			// advertises the cloud device name (falls back to the device type until it is known)
			Fble.begin(particleSystem().type, &particleSystem().name, particleSystem().bluetooth.state == TonOff::ON);
#endif

			// mark startup complete now that state is loaded
			particleSystem().startup = TparticleSystem::TstartupStatus::complete;

			// publish startup info (will publish no matter what if these vars are set to ALWAYS)
			publishVariable(&particleSystem().vitals.lastRestart);
			publishVariable(&particleSystem().state.status);

			// publish state if we're publishing and autosend on startup is on
			// note: not published right here - the wall clock is typically not yet synced at
			// this point after a cold boot, see FstartupStatePending
			if (particleSystem().publishing.record == TonOff::ON && particleSystem().state.autoSendOnStartup == TonOff::ON)
			{
				FstartupStatePending = true;
				FstartupStateTimer.start(FstartupStateCheckInterval);
			}
		};

// debug actions
#ifdef SDDS_PARTICLE_DEBUG
		on(particleSystem().debug)
		{
			if (particleSystem().debug == TparticleSystem::TdebugAction::getValues)
			{
				Log.trace("*** VALUES ***");
				Log.print(TparticleSerializer::serializeParticleValues(Froot).toJSON().c_str());
				Log.print("\n");
			}
			else if (particleSystem().debug == TparticleSystem::TdebugAction::getTree)
			{
				Log.trace("*** TREE ***");
				Variant tree = TparticleSerializer::serializeParticleTree(Froot);
				Log.print(tree.toJSON().c_str());
				Log.print("\n");
				// String base64 = TvariantSerializer::variantToBase64(FstructVar);
				// Log.trace("\nCBOR in base64 (size %d): ", base64.length());
				// Log.print(base64);
				// Log.print("\n");
			}
			else if (particleSystem().debug == TparticleSystem::TdebugAction::setVars)
			{
				Log.trace("*** SET VARS: %s ***", particleSystem().command.c_str());
				Log.trace("retval: %d", setVariables(String(particleSystem().command.c_str())));
				Log.print("\n");
			}
			else if (particleSystem().debug == TparticleSystem::TdebugAction::setDefaults)
			{
				setupDefaults(
					{{publish::EACH, sdds::opt::saveval},
					 {publish::AVG_GLOBAL, {sdds::Ttype::FLOAT32, sdds::Ttype::FLOAT64}}});
			}
			if (particleSystem().debug == TparticleSystem::TdebugAction::getCommandLog ||
				particleSystem().debug == TparticleSystem::TdebugAction::setVars)
			{
				Log.trace("*** CMD LOG ***");
				Log.print(FcmdLog);
				Log.print("\n");
			}
			if (particleSystem().debug != TparticleSystem::TdebugAction::___)
			{
				particleSystem().debug = TparticleSystem::TdebugAction::___;
			}
		};
#endif
	}
	TparticleSpike(TmenuHandle &_root, const dtypes::string &_type, dtypes::uint16 _version, const dtypes::string &_unit) : TparticleSpike(_root, _type, "sddsData", _version, _unit)
	{
	}
	TparticleSpike(TmenuHandle &_root, const dtypes::string &_type, const dtypes::string &_dataEvent, dtypes::uint16 _version) : TparticleSpike(_root, _type, _dataEvent, _version, "")
	{
	}
	TparticleSpike(TmenuHandle &_root, const dtypes::string &_type, dtypes::uint16 _version) : TparticleSpike(_root, _type, "sddsData", _version, "")
	{
	}

	/**
	 * @brief call during setup() to initialize the SYSTEM menu and all particle variables and functions
	 * @note this runs BEFORE any on(sdds::setup())
	 */
	void setup(const std::vector<TintervalDefault> &_defaults = {})
	{
		// add SYSTEM menu
		Froot->addDescr(&particleSystem(), 0);

		// generate publishing intervals tree for all variables
		createVariableIntervalsTree(Froot);

		// set default defaults
		setupDefaults(
			{// --> device EEPROM status change should always be reported
			 {publish::ALWAYS, &particleSystem().state.status},
			 // --> device restart should be always published
			 {publish::ALWAYS, &particleSystem().vitals.lastRestart}});

		// set defaults
		setupDefaults(_defaults);

		// process device reset information
		System.enableFeature(FEATURE_RESET_INFO);
		if (System.resetReason() == RESET_REASON_PANIC)
		{
			// uh oh - reset due to PANIC (e.g. segfault)
			// https://docs.particle.io/reference/cloud-apis/api/#spark-device-last_reset
			uint32_t panicCode = System.resetReasonData();
			Log.error("restarted due to PANIC, code: %lu", panicCode);
			particleSystem().vitals.lastRestart = TparticleSystem::TrestartStatus::PANIC;
			// System.enterSafeMode(); // go straight to safe mode?
		}
		else if (System.resetReason() == RESET_REASON_WATCHDOG)
		{
			// hardware watchdog detected a timeout
			Log.warn("restarted due to watchdog (=timeout)");
			particleSystem().vitals.lastRestart = TparticleSystem::TrestartStatus::watchdogTimeout;
		}
		else if (System.resetReason() == RESET_REASON_USER)
		{
			// software triggered resets
			uint32_t userReset = System.resetReasonData();
			if (userReset == static_cast<uint8_t>(TparticleSystem::TrestartStatus::outOfMemory))
			{
				// low memory detected
				Log.warn("restarted due to low memory");
				particleSystem().vitals.lastRestart = TparticleSystem::TrestartStatus::outOfMemory;
			}
			else if (userReset == static_cast<uint8_t>(TparticleSystem::TrestartStatus::userRestart))
			{
				// user requested a restart
				Log.trace("restarted per user request");
				particleSystem().vitals.lastRestart = TparticleSystem::TrestartStatus::userRestart;
			}
			else if (userReset == static_cast<uint8_t>(TparticleSystem::TrestartStatus::userReset))
			{
				FresetState = true;
				// user requested a restart
				Log.trace("restarted and resetting per user request");
				particleSystem().vitals.lastRestart = TparticleSystem::TrestartStatus::userReset;
			}
		}
		else
		{
			// report any of the other reset reasons?
			// https://docs.particle.io/reference/cloud-apis/api/#spark-device-last_reset
		}

// DEBUG option: force state reset
#ifdef SDDS_PARTICLE_RESET
		resetState = true;
#endif

		// main particle functions to interact with the the self-describing data-structure
		Particle.function("sdds", &TparticleSpike::setVariables, this);

		// particle functions to publish tree, values, and state to event stream (also possible via sdds "SYSTEM.action=sendSdds/sendSddsValues/sendSddsState")
		Particle.function("sendSdds", &TparticleSpike::publishTree, this);
		Particle.function("sendSddsValues", &TparticleSpike::publishValues, this);
		Particle.function("sendSddsState", &TparticleSpike::publishState, this);

		// get SDDS sytem tree (minus the variable publish intervas), i.e. just the core tree which is small enough to fit into 1kB variable
		Particle.variable("getSddsSystem", [this]()
						  { return this->getSystem(); });

		// backup particle variables to get tree/values if capturing publish events is not feasible or the structure is too big:
		Particle.variable("getSdds", [this]()
						  { return this->getTree(); });
		Particle.variable("getSddsValues", [this]()
						  { return this->getValues(); });
		FvarResp.registerChannels(); // return value channels

		// convenience particle variable to get the command log
		Particle.variable("getSddsCommandLog", FcmdLog);

#if Wiring_BLE
		// bluetooth: mirror the BLE status into SYSTEM.bluetooth.status / .connection
		// (the ble spike itself is started in on(sdds::setup()) once the saved state is loaded)
		Fble.onStatusChange([](TbleSpike::Tstatus _status, const char *_peer)
							{
			auto &bt = particleSystem().bluetooth;
			using Tstatus = TparticleSystem::Tbluetooth::Tstatus;
			Tstatus::e status = (_status == TbleSpike::Tstatus::off) ? Tstatus::off
							 : (_status == TbleSpike::Tstatus::connected) ? Tstatus::connected
																			 : Tstatus::discoverable;
			if (bt.status != status)
				bt.status = status;
			if (strcmp(bt.connection, _peer) != 0)
				bt.connection = _peer; });
		Fble.onTimeSet([]()
					   { particleSystem().vitals.timeSource = TparticleSystem::TtimeSource::bluetooth; });
		Fble.onCommand("@data", [this](const char *_args)
					   { startBleData(_args); });
#endif
	}

#if Wiring_BLE
	/**
	 * @brief "@data [time base]": send copies of all queued data bursts with a time base later than
	 * the given one (all if omitted) to the bluetooth client, oldest first. The queue is NOT changed
	 * (the bursts are still published to the cloud once the device is online).
	 * Response lines:
	 *   "@data begin <n>"   number of bursts that will be sent
	 *   "@d+ <json part>"   part of a burst's JSON (more parts follow)
	 *   "@d <json part>"    last part of a burst's JSON (concatenate the parts to get the burst)
	 *   "@data end <n>"     number of bursts sent (can differ from begin if the queue changed meanwhile)
	 */
	void startBleData(const char *_after)
	{
		FbleDataCursor = _after;
		FbleDataCursor.trim();
		FbleDataJson = "";
		FbleDataPos = 0;
		FbleDataSent = 0;
		FbleDataActive = true;
		Log.info("bluetooth data transfer: sending queued bursts after '%s'", FbleDataCursor.c_str());
		Fble.sendLine(String("@data begin ") + String((unsigned long)Fpublisher.countQueuedBursts(FbleDataCursor)));
		FbleDataTimer.start(0);
	}

	void sendBleData()
	{
		if (!FbleDataActive)
			return;
		if (!Fble.connected())
		{
			Log.info("bluetooth data transfer: client disconnected after %lu bursts", (unsigned long)FbleDataSent);
			FbleDataActive = false;
			FbleDataJson = "";
			return;
		}
		// fill the tx buffer as far as possible, then wait for it to drain
		while (Fble.txFree() > BLE_DATA_CHUNK + 16)
		{
			if (FbleDataPos >= FbleDataJson.length())
			{
				// next burst (looked up by time base each time, so queue changes in between are fine)
				const Variant *burst = Fpublisher.nextQueuedBurst(FbleDataCursor);
				if (!burst)
				{
					Fble.sendLine(String("@data end ") + String((unsigned long)FbleDataSent));
					Log.info("bluetooth data transfer: sent %lu bursts", (unsigned long)FbleDataSent);
					FbleDataActive = false;
					FbleDataJson = "";
					return;
				}
				FbleDataCursor = burst->get(TparticleSerializer::burstTimeBaseKey()).asString();
				FbleDataJson = burst->toJSON();
				FbleDataPos = 0;
				FbleDataSent++;
			}
			// next part (never split a multi-byte UTF-8 character)
			size_t len = std::min(BLE_DATA_CHUNK, (size_t)FbleDataJson.length() - FbleDataPos);
			while (len > 1 && FbleDataPos + len < FbleDataJson.length() && ((uint8_t)FbleDataJson[FbleDataPos + len] & 0xC0) == 0x80)
				len--;
			bool last = FbleDataPos + len >= FbleDataJson.length();
			String line = String(last ? "@d " : "@d+ ") + FbleDataJson.substring(FbleDataPos, FbleDataPos + len);
			if (!Fble.sendLine(line))
				break;
			FbleDataPos += len;
		}
		FbleDataTimer.start(20);
	}
#endif

#if Wiring_BLE
	/**
	 * @brief access to the bluetooth spike (e.g. for diagnostics)
	 */
	TbleSpike &ble() { return Fble; }
#endif

	/**
	 * @brief suspend / resume the averaging of a published variable (e.g. a stirrer speed while the
	 * stirrer is paused for a read): while suspended, value changes are not collected and the
	 * suspended time does not count towards the time-weighted average. Data collected before the
	 * suspension is kept and published at the regular time. The live value of the variable is not
	 * affected. Only applies to averaged variables (AVG_GLOBAL or custom intervals).
	 * @return false if the variable is not part of the published tree
	 */
	bool suspendAveraging(Tdescr &_var, bool _suspend)
	{
		TparticleVarWrapper *w = findVariableWrapper(&sddsParticleVariables, &_var);
		if (!w)
			return false;
		w->suspendAveraging(_suspend);
		return true;
	}

#pragma endregion
};
