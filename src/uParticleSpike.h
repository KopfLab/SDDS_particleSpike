#pragma once

#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uPlainCommHandler.h"
#include "uParticleSystem.h"
#include "uRunningStats.h"

// particle spike class
class TparticleSpike
{

public:
	// define the publishing interval constants
	enum publish
	{
		INHERIT = -1,
		OFF = 0,
		IMMEDIATELY = 1
	};

private:
	// namespace
	using TonOff = sdds::enums::OnOff;

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
		inline static const char *FburstsnapshotStateKey = "STATE";

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
					TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
					if (mh)
						serializeEnums(mh, _enums, _ids);
				}
				if (d->type() == sdds::Ttype::ENUM)
				{
					TenumBase *en = static_cast<TenumBase *>(d);
					sdds::metaTypes::TenumId uid = en->enumInfo().id;
					for (size_t i = 0; i < _ids.size(); ++i)
					{
						if (uid == _ids[i])
							return; // already serialized this enum
					}
					_ids.append(uid);
					// serialize this enumeration
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
			return Variant(); // null
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
		 */
		static Variant serializeValues(TmenuHandle *_struct, bool _withNameAsKey = true, int _optsFilter = -1, bool _enumAsText = true)
		{
			Variant var;
			for (auto it = _struct->iterator(); it.hasCurrent(); it.jumpToNext())
			{
				Variant item; // default: NULL
				Tdescr *d = it.current();
				if (d->isStruct())
				{
					TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
					if (mh)
						item = serializeValues(mh, _withNameAsKey, _optsFilter, _enumAsText);
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

		static Variant serializeValuesForsnapshotState(TmenuHandle *_struct)
		{
			Variant snapshotState;
			snapshotState.set(
				FburstsnapshotStateKey,
				serializeValues(_struct, true, sdds::opt::saveval, true));
			return (snapshotState);
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
				time_t timeBase = Time.now() - static_cast<time_t>(round(static_cast<dtypes::float64>(millis() - _refTime) / 1000));
				burst.set(FburstTimeBaseKey, Time.format(timeBase, TIME_FORMAT_ISO8601_FULL));
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
		 * @todo do we need a version with keys here? probably fine as array
		 */
		static Variant serializeCommand(system_tick_t _time, const String &_cmd, int _code, const char *_msg)
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
		 * @param _refTime the time base of all the commands in the provided log
		 * @param _timeShift how much to shift ("time rebase") the logs in addition
		 */
		static Variant serializeCommandLog(system_tick_t _refTime, system_tick_t _timeShift, const Variant &_log)
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
						system_tick_t offset = entry[0].toUInt();
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
				time_t timeBase = Time.now() - static_cast<time_t>(round(static_cast<dtypes::float64>(millis() - _refTime - _timeShift) / 1000));
				var.set(FvarLogTimeBaseKey, Time.format(timeBase, TIME_FORMAT_ISO8601_FULL));
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
			system_tick_t FlastUsed = 0;

		public:
			Tchannel() {}

			// management
			size_t getBytesRemaining()
			{
				return (FbytesStart < Fresponse.length()) ? Fresponse.length() - FbytesStart : 0;
			}
			bool idle() { return Fresponse.length() == 0; }
			bool neverUsed() { return FlastUsed == 0; }
			bool isOlder(system_tick_t _lastUsed)
			{
				return !neverUsed() && (_lastUsed == 0 || FlastUsed < _lastUsed);
			}
			system_tick_t lastUsed() { return FlastUsed; }
			void release() { assign(""); }
			void registerChannel(size_t _channel)
			{
				Fchannel = _channel;
				char variable[20];
				snprintf(variable, sizeof(variable), "sddsChannel%c", TparticleSerializer::encodeIntToBase64(_channel));
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
				FlastUsed = millis();
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
				FlastUsed = millis();
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
			system_tick_t oldestFreeLastUsed = 0;
			size_t oldestInUseChannel = 0;
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
				// check for oldest free channel
				if (Fchannels[i].idle() && Fchannels[i].isOlder(oldestFreeLastUsed))
				{
					oldestFreeChannel = i;
					oldestFreeLastUsed = Fchannels[i].lastUsed();
				}
				// check for oldest channel that's in use
				if (!Fchannels[i].idle() && Fchannels[i].isOlder(oldestInUseLastUsed))
				{
					oldestInUseChannel = i;
					oldestInUseLastUsed = Fchannels[i].lastUsed();
				}
			}
			// if no unsed channel, use the oldest free (if there are any), or the oldest in use
			if (!foundChannel)
			{
				channel = (oldestFreeLastUsed > 0) ? oldestFreeChannel : oldestInUseChannel;
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

		/**
		 * @brief check if we're ready to publish (need a name and valid time)
		 */
		bool readyToPublish()
		{
			return particleSystem().name != "" && Time.isValid();
		}

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
		// constructor + logic
		TparticlePublisher()
		{

			// data bursts
			on(FburstTimer)
			{
				Variant burst = TparticleSerializer::serializeBurst(FminTime, FburstData);
				if (readyToPublish())
				{
					queueData(burst);
					clearBurst();
				}
				else
				{
					// want to publish but not yet ready to
					// --> keep the burst going = restart the timer
					if (FnewBurstData)
					{
						// evrerytime there's new data, show it
						Log.trace("*** CONTINUING BURST, SO FAR: ***");
						printVariant(burst);
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
			if (!_always && particleSystem().publishing.publish != TonOff::ON)
				Log.trace("NOT adding to burst for %s: %s", TparticleSerializer::getVarPath(_d).c_str(), _data.toJSON().c_str());
			else
				Log.trace("ADDING to burst for %s: %s", TparticleSerializer::getVarPath(_d).c_str(), _data.toJSON().c_str());
#endif

			// publish needs to be active or these data need to be set to always
			if (!_always && particleSystem().publishing.publish != TonOff::ON)
				return;

			// keep track of min time
			if (FminTime == 0 || FminTime > _time)
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
		 * @brief queues Variant for publication, typically data is added through
		 * addToBurst but this function can be used to queue any data directly
		 * for publication
		 * @return true if successfully queued, false if too large to be queued
		 */
		bool queueData(const Variant &_data)
		{
			// safety check if data is small enough (less than 16kB)
			// https://docs.particle.io/reference/device-os/typed-publish/
			if (getCBORSize(_data) > 16 * particle::protocol::MAX_EVENT_DATA_LENGTH)
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
		virtual void clear() {}
		virtual void changeValue()
		{
			if (!FhasPreviousValue || isValueDifferent())
			{
				// by default, only update if the value is different
				FlastUpdateTime = millis();
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
					if (Fvalue == publish::IMMEDIATELY)
					{
						// publish current variable value immediately if it has changed
						if ((!FhasPreviousValue || isValueDifferent()) && Fpublisher)
						{
							Fpublisher->addToBurst(FvarOrigin, millis(), TparticleSerializer::serializeData(FvarOrigin, FlinkedUnit));
						}
					}
					else
					{
						// collect values
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

				// if Fvalue is set > IMMEDIATELY --> start own timer
				if (Fvalue > publish::IMMEDIATELY)
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
		void operator=(Tuint32::dtype _v) { __setValue(_v); }
		template <typename T>
		void operator=(T _val) { __setValue(_val); }

		// original sdds var access
		Tdescr *origin() { return FvarOrigin; }
		Tmeta meta() override { return Tmeta{Tuint32::TYPE_ID, sdds::opt::saveval, FvarOrigin->name()}; }

		/**
		 * @brief does this use the global publishing interval?
		 */
		bool usesGlobalPublishingInterval()
		{
			return (Fvalue == publish::INHERIT);
		}

		// methods implemented in derived classes
		/**
		 * @brief reset the data storage
		 */
		void reset()
		{
			FlastUpdateTime = 0; // reset to no value
			clear();
		}

		/**
		 * @brief adds to the burst of the publisher if any data is stored
		 */
		void publish()
		{
			if (FlastUpdateTime == 0)
				return; // has no recent value
			if (Fpublisher)
			{
				Fpublisher->addToBurst(FvarOrigin, getTimeForPublish(), getDataForPublish());
			}
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
		dtypes::TtickCount FstartTime = 0;	// start time of the averaged value
		dtypes::TtickCount FlatestTime = 0; // last time a value was received
		dtypes::float64 FlatestValue = 0;	// last value that was received

		/**
		 * @brief add latest value to the running average with the weight based on the time interval
		 * sets the new latest value/time to the current value/time
		 */
		void addLatest()
		{
			// do we already have a value? --> add it to the running stats
			if (FlatestTime > 0)
			{
				rs.add(FlatestValue, millis() - FlatestTime);
			}
			// store the latest time and value
			FlatestValue = static_cast<dtypes::float64>(this->originValue());
			FlatestTime = millis();
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

		void clear() override
		{
			// already have one data point stored in the running stats
			// --> start next stats with FlatestValue
			bool carryOver = (rs.count() > 0);
			rs.reset(); // restart running stats
			FlatestTime = 0;
			FhasFirstValue = false;
			if (carryOver)
				changeValue();
		}

		void changeValue() override
		{
			// always triggers update for continuously collected values even if value is the same
			this->FlastUpdateTime = millis();
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
				return (millis() + FstartTime) / 2;
			}
		}

		Variant getDataForPublish() override
		{
			if (rs.count() == 0)
			{
				// single point (i.e. no stats yet) -> return latest value
				return TparticleSerializer::serializeData(FlatestValue, this->FlinkedUnit);
			}
			else
			{
				// multi point -> add the value of the currently active data point before returning
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
		if (_default > 1 && _default < 1000)
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
	 * sdds::particle::publish::IMMEDIATELY -> publish every time the value is set
	 * < 1000 	-> not allowed
	 * 1000+ 	-> publish every 1000+ ms
	 * @return whether _var was found in any of the variable intervals' origin
	 */
	bool setVariableInterval(TmenuHandle *_vars, dtypes::int32 _interval, Tdescr *_var)
	{
		if (_interval > 1 && _interval < 1000)
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

	// plain comms handler to process sdds variable commands
	TplainCommHandler Fpch;

	// Timer for global publishing interval
	Ttimer FglobalPublishTimer;

	// error codes
	constexpr static int ERR_NO_CMD = -200;
	constexpr static int ERR_CMDS_MAX = -201;
	constexpr static int ERR_EVENT_SIZE_MAX = -202;

	// command log
	system_tick_t FcmdLogMinTime = 0; // earliest time in the log
	char FcmdLog[particle::protocol::MAX_FUNCTION_ARG_LENGTH] = "[]\0";

	void logCommand(system_tick_t _time, const String &_cmd, int _errCode, const char *_errText)
	{
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
			system_tick_t timeRebaseShift = 0;
			if (cmd_log[0].isArray() && !cmd_log[0].isEmpty())
			{
				// rebased based on first entry if there is one
				timeRebaseShift = cmd_log[0][0].toUInt();
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
		system_tick_t timestamp = millis();
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
	 * @brief Particle.function sddsPublishTree
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
	 * @brief Particle.variable sddsGetTree
	 */
	String getTree()
	{
		Variant tree = TparticleSerializer::serializeParticleTree(Froot);
		return FvarResp.queue(tree.toJSON());
	}

	/**
	 * @brief Particle.function sddsPublishValues
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
	 * @brief Particle.variable sddsGetValues
	 */
	String getValues()
	{
		Variant values = TparticleSerializer::serializeParticleValues(Froot);
		return FvarResp.queue(values.toJSON());
	}

#pragma endregion

/*** constructor and setup ***/
#pragma region constructor+setup

private:
	// keep track of publishing to detect when it switches from OFF to ON
	bool FisPublishing = particleSystem().publishing.publish == TonOff::ON;

public:
	/**
	 * @brief particle spike consturctor
	 * @param _root the sdds tree
	 * @param _type name of the structure type, i.e. what kind of device is this? ("pump", "mfc", etc.)
	 * @param _version version of the structure type to inform servers who have cached structure when the structure type has been updated
	 * @param _unit name of unit vars suffix for auto-detecting dynamic units (pass "" to turn auto-detection off)
	 */
	TparticleSpike(TmenuHandle &_root, const dtypes::string &_type, dtypes::uint16 _version, const dtypes::string &_unit) : Fpch(_root, nullptr)
	{
		Froot = _root;
		particleSystem().type = _type;
		particleSystem().version = _version;
		FunitVarSuffix = _unit;
		FunitAutoDetect = FunitVarSuffix != "";

		// custom system actions
		on(particleSystem().action)
		{
			if (particleSystem().action == TparticleSystem::Taction::snapshotState)
			{

				// get snapshotState
				Variant snapshotState = TparticleSerializer::serializeValuesForsnapshotState(Froot);

				if (Log.isTraceEnabled())
				{
					Log.trace("*** snapshotState ***");
					Log.print(snapshotState.toJSON().c_str());
					Log.print("\n");
				}

				Fpublisher.addToBurst(Froot, millis(), snapshotState);

				// add all variables to burst that are saveval
				particleSystem().action = TparticleSystem::Taction::___;
			}
		};

		// publishw now actions
		on(particleSystem().publishing.publishNow)
		{
			if (particleSystem().publishing.publishNow == TparticleSystem::Tpublishing::Taction::tree)
			{
				publishTree("");
				particleSystem().publishing.publishNow = TparticleSystem::Tpublishing::Taction::___;
			}
			else if (particleSystem().publishing.publishNow == TparticleSystem::Tpublishing::Taction::values)
			{
				publishValues("");
				particleSystem().publishing.publishNow = TparticleSystem::Tpublishing::Taction::___;
			}
			else if (particleSystem().publishing.publishNow == TparticleSystem::Tpublishing::Taction::bursts)
			{
				// FIXME: implement this
				particleSystem().publishing.publishNow = TparticleSystem::Tpublishing::Taction::___;
			}
		};

		// global publishing interval
		on(particleSystem().publishing.globalInterval_ms)
		{
			if (particleSystem().publishing.globalInterval_ms < 1000)
				particleSystem().publishing.globalInterval_ms = 1000; // don't allow publishing any faster than once a second
			FglobalPublishTimer.stop();
			resetGlobal();
			FglobalPublishTimer.start(particleSystem().publishing.globalInterval_ms);
		};
		FglobalPublishTimer.start(particleSystem().publishing.globalInterval_ms);

		// (re)start timer when publishing is turned on
		on(particleSystem().publishing.publish)
		{
			// triggers only if publishing is freshly turned on
			if (!FisPublishing && particleSystem().publishing.publish == TonOff::ON)
			{
				FglobalPublishTimer.stop();
				resetGlobal();
				FglobalPublishTimer.start(particleSystem().publishing.globalInterval_ms);
			}
			FisPublishing = particleSystem().publishing.publish == TonOff::ON;
		};

		// publish timer triggers
		on(FglobalPublishTimer)
		{
			publishGlobal(); // variables reset themselves after publish
			FglobalPublishTimer.start(particleSystem().publishing.globalInterval_ms);
		};

		// startup complete
		on(particleSystem().state.time)
		{
			// the state is loaded from EEPROM, this completes the startup
			if (particleSystem().startup != TparticleSystem::TstartupStatus::complete)
			{
				particleSystem().startup = TparticleSystem::TstartupStatus::complete;
				// --> always publish restart
				Fpublisher.addToBurst(&particleSystem().vitals.lastRestart, millis(), true);
			}
		};

		// system error
		on(particleSystem().state.error)
		{
			if (particleSystem().state.error != TparamError::___)
			{
				// encountered an error when loading system state from EEPROM
				// --> always publish this information
				Fpublisher.addToBurst(&particleSystem().state.error, millis(), true);
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
					{{publish::IMMEDIATELY, sdds::opt::saveval},
					 {publish::INHERIT, {sdds::Ttype::FLOAT32, sdds::Ttype::FLOAT64}}});
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
	TparticleSpike(TmenuHandle &_root, const dtypes::string &_type, dtypes::uint16 _version) : TparticleSpike(_root, _type, _version, "")
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

		// set defaults
		setupDefaults(_defaults);

		// process device reset information
		bool resetState = false; // whether to reset the state/EEPROM
		System.enableFeature(FEATURE_RESET_INFO);
		if (System.resetReason() == RESET_REASON_PANIC)
		{
			// uh oh - reset due to PANIC (e.g. segfault)
			// https://docs.particle.io/reference/cloud-apis/api/#spark-device-last_reset
			uint32_t panicCode = System.resetReasonData();
			Log.error("restarted due to PANIC, code: %lu", panicCode);
			particleSystem().vitals.lastRestart = TparticleSystem::TresetStatus::PANIC;
			// System.enterSafeMode(); // go straight to safe mode?
		}
		else if (System.resetReason() == RESET_REASON_WATCHDOG)
		{
			// hardware watchdog detected a timeout
			Log.warn("restarted due to watchdog (=timeout)");
			particleSystem().vitals.lastRestart = TparticleSystem::TresetStatus::watchdogTimeout;
		}
		else if (System.resetReason() == RESET_REASON_USER)
		{
			// software triggered resets
			uint32_t userReset = System.resetReasonData();
			if (userReset == static_cast<uint8_t>(TparticleSystem::TresetStatus::outOfMemory))
			{
				// low memory detected
				Log.warn("restarted due to low memory");
				particleSystem().vitals.lastRestart = TparticleSystem::TresetStatus::outOfMemory;
			}
			else if (userReset == static_cast<uint8_t>(TparticleSystem::TresetStatus::userRestart))
			{
				// user requested a restart
				Log.trace("restarted per user request");
				particleSystem().vitals.lastRestart = TparticleSystem::TresetStatus::userRestart;
			}
			else if (userReset == static_cast<uint8_t>(TparticleSystem::TresetStatus::userReset))
			{
				resetState = true;
				// user requested a restart
				Log.trace("restarted and resetting per user request");
				particleSystem().vitals.lastRestart = TparticleSystem::TresetStatus::userReset;
			}
		}
		else
		{
			// report any of the other reset reasons?
			// https://docs.particle.io/reference/cloud-apis/api/#spark-device-last_reset
		}

		// reset state params?
		if (resetState)
		{
			sdds::paramSave::Tstream::INIT();
			sdds::paramSave::TparamStreamer ps;
			sdds::paramSave::Tstream s;
			ps.save(Froot, &s);
		}

		// main particle functions to interact with the the self-describing data-structure
		Particle.function("sdds", &TparticleSpike::setVariables, this);

		// convenience particle functions to publish tree and values to event stream (also possible via sdds "SYSTEM.publishing.publishNow=tree/values")
		Particle.function("sddsPublishTree", &TparticleSpike::publishTree, this);
		Particle.function("sddsPublishValues", &TparticleSpike::publishValues, this);

		// backup particle variables to get tree/values if capturing publish events is not feasible or the structure is too big:
		Particle.variable("sddsGetTree", [this]()
						  { return this->getTree(); });
		Particle.variable("sddsGetValues", [this]()
						  { return this->getValues(); });
		FvarResp.registerChannels(); // return value channels

		// convenience particle variable to get the command log
		Particle.variable("sddsGetCommandLog", FcmdLog);
	}

#pragma endregion
};
