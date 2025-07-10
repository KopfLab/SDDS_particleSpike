#pragma once

#include "Particle.h"
#include "uTypedef.h"
#include "uCoreEnums.h"
#include "uPlainCommHandler.h"
#include "uParticleSystem.h"

TparticleSystem sddsParticleSystem;
TparticleVariableIntervals sddsParticleVariables;

class TparticleSpike{

	private:

		using TonOff = sdds::enums::OnOff;


		/*** particle serializer (to Variants)  ***/
		#pragma region serializer

		// holds the whole structure serialized into a variant to avoid redundant regeneration
		Variant FstructVar;

		/**
		 * @brief container for timed data in a burst (kept in system time until publishing)
		 */
		struct TburstData {
			system_tick_t Ftime; // in MS
			Variant Fdata; // data
		};

		/**
		 * @brief dataset in a burst from a single variable
		 */
		struct TvarBurstDataset {
			Tdescr* FdescrPtr; // pointer to Tdescr variable where the data is from
			std::vector<TburstData> Fdataset; // data bursts
			TvarBurstDataset(Tdescr* _ptr, TburstData _first) : FdescrPtr(_ptr) {
				Fdataset.push_back(_first);
			}
		};

		/**
		 * @brief class with static functions to serialize sdds structure to Variants
		 * FIXME: should this be a namespace instead?
		 */
		class TparticleSerializer{

			private:

				// keys for tree top level
				inline static const char* FtreeTypeKey = "t";
				inline static const char* FtreeVersionKey = "v";
				inline static const char* FtreeEnumKey = "e";
				inline static const char* FtreeStructKey = "s";

				// keys for tree Tdesc
				inline static const char* FdescTypeKey = "t";
				inline static const char* FdescOptKey = "o";
				inline static const char* FdescNameKey = "n";
				inline static const char* FdescEnumKey = "e";
				inline static const char* FdescSubStructKey = "s";

				// keys for bursts top level
				inline static const char* FburstDeviceNameKey = "name";
				inline static const char* FburstTimeBaseKey = "tb";
				inline static const char* FburstDataKey = "b";

				// keys for bursts data
				inline static const char* FburstTimeOffsetKey = "o";
				inline static const char* FburstNumValueKey = "v";
				inline static const char* FburstNumCountKey = "n";
				inline static const char* FburstNumSdevKey = "s";
				inline static const char* FburstTextValueKey = "c";
				inline static const char* FburstUnitsKey = "u";
				inline static const char* FburstSnapshotKey = "snap";

				// base64 charset
				inline static const char base64_chars[] =
					"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
					"abcdefghijklmnopqrstuvwxyz"
					"0123456789+/";


				/**
				 * @brief serialize structure enumerations
				 */
				static Variant serializeEnums(TmenuHandle* _struct) {
					Vector<sdds::metaTypes::TenumId> enumIds;
					Variant enums;
					serializeEnums(_struct, enums, enumIds);
					return(enums);
				}

				static void serializeEnums(TmenuHandle* _struct, Variant& _enums, Vector<sdds::metaTypes::TenumId>& _ids){
					for (auto it = _struct->iterator(); it.hasCurrent(); it.jumpToNext()){
						Tdescr* d = it.current();
						if (d->isStruct()){
							TmenuHandle* mh = static_cast<Tstruct*>(d)->value();
							if (mh)  serializeEnums(mh, _enums, _ids);
						}
						if (d->type() == sdds::Ttype::ENUM){
							TenumBase* en = static_cast<TenumBase*>(d);
							sdds::metaTypes::TenumId uid = en->enumInfo().id;
							for (size_t i = 0; i < _ids.size(); ++i) {
								if (uid == _ids[i]) return; // already serialized this enum
							}
							_ids.append(uid);
							// serialize this enumeration
							Variant enumeration;
							enumeration.append(uid);
							Variant values;
							for (auto it = en->enumInfo().iterator;;){
								values.append(it.next());
								if (!it.hasNext()) break;
							}
							enumeration.append(values);
							_enums.append(enumeration);
						}
					}
				}

				/**
				 * @brief serialize structure
				 */
				static Variant serializeStruct(TmenuHandle* _struct, bool _withKeys = true) {
					Variant var;
					auto it = _struct->iterator();
					for (auto it = _struct->iterator(); it.hasCurrent(); it.jumpToNext()){
						Tdescr *d = it.current();
						Variant item = serializeDescr(d, _withKeys);
						if (d->isStruct()) {
							TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
							Variant sub;
							if (mh) sub = serializeStruct(mh, _withKeys);
							(_withKeys) ?
								item.set(FdescSubStructKey, sub):
								item.append(sub);
						}
						var.append(item);
					}
					return var;
				}

				/**
				 * @brief Tdescr to variant (value not included)
				 */
				static Variant serializeDescr(Tdescr* _d, bool _withKeys = true){
					Variant var;
					if (_withKeys) {
						var.set(FdescTypeKey, _d->typeId());
						var.set(FdescOptKey, _d->option());
						var.set(FdescNameKey, _d->name());
					} else {
						var.append(_d->typeId());
						var.append(_d->option());
						var.append(_d->name());
					}

					// which enum?
					if (_d->type() == sdds::Ttype::ENUM) {
						TenumBase* en = static_cast<TenumBase*>(_d);
						(_withKeys) ?
							var.set(FdescEnumKey, en->enumInfo().id) :
							var.append(en->enumInfo().id);
					}
					return var;
				}

				/**
				 * @brief return value in variant
				 */
				static Variant serializeValue(Tdescr *_d, bool _enumAsText = false) {
					auto dt = _d->type();
					if (dt == sdds::Ttype::UINT8) {
						return Variant(static_cast<Tuint8 *> (_d)->value());
					} else if (dt == sdds::Ttype::UINT16) {
						return Variant(static_cast<Tuint16 *> (_d)->value());
					} else if (dt == sdds::Ttype::UINT32) {
						return Variant(static_cast<Tuint32 *> (_d)->value());
					} else if (dt == sdds::Ttype::INT8) {
						return Variant(static_cast<Tint8 *> (_d)->value());
					} else if (dt == sdds::Ttype::INT16) {
						return Variant(static_cast<Tint16 *> (_d)->value());
					} else if (dt == sdds::Ttype::INT32) {
						return Variant(static_cast<Tint32 *> (_d)->value());
					} else if (dt == sdds::Ttype::FLOAT32) {
						Tfloat32 *val = static_cast<Tfloat32 *>(_d);
						if (!val->isNan()) return Variant(val->value());
					} else if (dt == sdds::Ttype::ENUM && !_enumAsText) {
						dtypes::uint8* value = static_cast<dtypes::uint8*>(static_cast<TenumBase*> (_d)->pValue());
						return Variant(*value);
					} else if (dt == sdds::Ttype::STRING || dt == sdds::Ttype::TIME || (dt == sdds::Ttype::ENUM && _enumAsText)) {
						return Variant(_d->to_string().c_str());
					}
					return Variant(); // null
				}

				/**
				 * @brief serialize a single burst dataset
				 */
				static Variant serializeBurstDataset(system_tick_t _refTime, TvarBurstDataset _dataset) {
					Variant data;
					for (size_t i = 0; i < _dataset.Fdataset.size(); ++i) {
						data.append(serializeBurstData(_refTime, _dataset.Fdataset[i]));
					}
					Variant var;
					String key = (_dataset.FdescrPtr->parent()) ? getVarPath(_dataset.FdescrPtr) : "";
					var.set(key, data);
					return(var);
				}

				/**
				 * @brief serialize burst data
				 */
				static Variant serializeBurstData(system_tick_t _refTime, TburstData _data) {
					Variant data = _data.Fdata;
                	data.set(FburstTimeOffsetKey, _data.Ftime - _refTime);
					return(data);
				}


			public:

				/**
				 * @brief serialize tree as variant
				 * @param _withKeys whether to include the identifier keys or use unnamed entries
				 */
				static Variant serializeTree(TmenuHandle* _struct, bool _withKeys = true) {
					Variant var;
					var.set(FtreeEnumKey, serializeEnums(_struct));
					var.set(FtreeStructKey, serializeStruct(_struct, _withKeys));
					return(var);
				}

				static Variant serializeParticleTree(TmenuHandle* _struct) {
					Variant var = serializeTree(_struct, false);
					var.set(FtreeTypeKey, sddsParticleSystem.type.c_str());
					var.set(FtreeVersionKey, sddsParticleSystem.version.value());
					return var;
				}

				/**
				 * @brief serialize tree values as variant
				 * @param _withNameAsKey whether to include the name as key or just the value
				 * @param _optsFilter whether to filter for values with speicifc options
				 * @param _enumAsText whether to include enum values as text (instead of number)
				 */
				static Variant serializeValues(TmenuHandle* _struct, bool _withNameAsKey = true, int _optsFilter = -1, bool _enumAsText = true) {
					Variant var;
					for (auto it=_struct->iterator(); it.hasCurrent(); it.jumpToNext()){
						Variant item; // default: NULL
						Tdescr *d = it.current();
						if (d->isStruct()) {
							TmenuHandle *mh = static_cast<Tstruct *>(d)->value();
							if (mh) item = serializeValues(mh, _withNameAsKey, _optsFilter, _enumAsText);
						} else {
							if (_optsFilter >= 0) {
								// opts filter is active, check if d has the requested option(s)
								if ((d->meta().option & _optsFilter) != _optsFilter) continue;
							}
							item = serializeValue(d, _enumAsText);
						}
						if (_withNameAsKey) var.set(d->name(), item);
						else var.append(item);
					}
					return var;
				}

				static Variant serializeValuesOnly(TmenuHandle* _struct) {
					return serializeValues(_struct, false, -1, false);
				}

				static Variant serializeValuesForSnapshot(TmenuHandle* _struct) {
					Variant snapshot;
					snapshot.set(
						FburstSnapshotKey,
						serializeValues(_struct, true, sdds::opt::saveval, true)
					);
					return(snapshot);
				}

				/**
				 * @brief serialize data for transmission in data bursts
				 */
				static Variant serializeData(dtypes::uint32 _n, dtypes::float32 _value, dtypes::float32 _sdev, Tdescr* _unit = nullptr) {
					Variant data;
					data.set(FburstNumValueKey, _value);
					data.set(FburstNumCountKey, _n);
					if (_n > 1) {
						// no point including sdev if there's only one data point
						data.set(FburstNumSdevKey, _sdev);
					}
					if (_unit && _unit->type() == sdds::Ttype::STRING) {
						// got a unit (double checking that it's string)
						data.set(FburstUnitsKey, _unit->to_string().c_str());
					}
					return data;
				}

				static Variant serializeData(const dtypes::string& _text) {
					Variant data;
					data.set(FburstTextValueKey, _text);
					return data;
				}

				static Variant serializeData(Tdescr *_d, Tdescr* _unit = nullptr) {
					Variant data;
					auto dt = _d->type();
					if (dt == sdds::Ttype::ENUM || dt == sdds::Ttype::STRING || dt == sdds::Ttype::TIME) {
						// text based value
						data.set(FburstTextValueKey, serializeValue(_d, true));
					} else {
						// numeric value
						data.set(FburstNumValueKey, serializeValue(_d));
					}
					if (_unit && _unit->type() == sdds::Ttype::STRING) {
						// got a unit (double checking that it's string)
						data.set(FburstUnitsKey, _unit->to_string().c_str());
					}
					return data;
				}

				/**
				 * @brief serialize a data burst
				 * @param _refTime the reference time to normalize the burst data against
				 */
				static Variant serializeBurst(system_tick_t _refTime, const std::vector<TvarBurstDataset>& _data) {
					if (_data.size() == 0) return(Variant());
					Variant burst;

					// device name (if available, otherwise NULL)
					if (sddsParticleSystem.name != "") {
						burst.set(FburstDeviceNameKey, 
							sddsParticleSystem.name.c_str());
					} else {
						// note: if readyToPublish is checked first, will not get to this!
						burst.set(FburstDeviceNameKey, Variant());
					}

					// time base (if valid time, otherwise NULL and the offset is machine time in millis)
					if (Time.isValid()) {
						time_t timeBase = Time.now() - static_cast<time_t>(round(static_cast<dtypes::float32>(_refTime)/1000));
						burst.set(FburstTimeBaseKey, Time.format(timeBase, TIME_FORMAT_ISO8601_FULL));
					} else {
						// note: if readyTopublish is checked first, will not get to this!
						burst.set(FburstTimeBaseKey, Variant());
					}

					// burst data
					Variant burstData;
					for (size_t i = 0; i < _data.size(); ++i) {
						burstData.append(serializeBurstDataset((Time.isValid() ? _refTime : 0), _data[i]));
					}
					burst.set(FburstDataKey, burstData);
					return(burst);
				}

				
				/**
				 * @brief get the path of the descriptor variable
				 */
				static String getVarPath(Tdescr* _d) {
					return getMenuPath(_d->parent(), _d->name());
				}

				static String getMenuPath(TmenuHandle* _d, String _path) {
					if (!_d || !_d->parent()) return _path;
					return getMenuPath(_d->parent(), String(_d->name()) + String(".") + _path);
				}

				/**
				 * @brief convert variant to CBOR
				 * @note CBOR/binary cannot be transmitted via Particle.variable (UTF-8 only), 
				 * base64 is the most compact printable format 
				 */
				static String variantToCbor(const Variant& _var) {
					String cbor;
					OutputStringStream stream(cbor);
					if (encodeToCBOR(_var, stream) == 0) {
						return cbor;
						//Log.trace("CBOR stream (as hex):");
						//for (size_t i = 0; i < cbor.length(); ++i) Log.printf("%02x", cbor[i]); Log.print("\n");
						//Log.trace("CBOR stream (as base64):");
						//Log.print(encodeBinaryInBase64(cbor)); Log.print("\n");
					} else {
						Log.error("failed to convert Variant to CBOR");
					}
					return String();
				}

				/**
				 * @brief convert variant to Base64
				 */
				static String variantToBase64(const Variant& _var) {
					return encodeBinaryInBase64(variantToCbor(_var));
				}

				/**
				 * @brief encode binary data in base 64 for transmission in UTF-8 (e.g. via Particle.variable),
				 * see Base64 representation on https://docs.particle.io/tools/developer-tools/json/
				 * @note that the conversion of JSON --> CBOR --> base64 does not save that much space unless there's a lot of floats
				 */
				static String encodeBinaryInBase64(const String &_data) {

					String encoded;
					int val = 0, valb = -6;

					for (size_t i = 0; i < _data.length(); ++i) {
						val = (val << 8) + _data[i];
						valb += 8;
						while (valb >= 0) {
							encoded += base64_chars[(val >> valb) & 0x3F];
							valb -= 6;
						}
					}

					if (valb > -6) {
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
				static const char encodeIntToBase64(dtypes::uint8 _value) {
					if (_value > 63) return '>';
					return base64_chars[_value];
				}

				/**
				 * @brief turn char bask into integer
				 * returns -1 if value is not a valued base64 character
				 */
				static int decodeIntFromBase64(char _c) {
					if ('A' <= _c && _c <= 'Z') return _c - 'A';           // 0–25
					if ('a' <= _c && _c <= 'z') return _c - 'a' + 26;      // 26–51
					if ('0' <= _c && _c <= '9') return _c - '0' + 52;      // 52–61
					if (_c == '+') return 62;
					if (_c == '/') return 63;
					return -1; // invalid Base64 character
				}
		};

		#pragma endregion

		/*** particle variable response channels ***/
		#pragma region response channels
		
		class TparticleVariableResponse {

			public: 

			const static size_t RESPONSE_SIZE = 400; // FIXME particle::protocol::MAX_VARIABLE_VALUE_LENGTH;
			const static size_t DATA_SIZE = RESPONSE_SIZE - 1; // leave 1 char space for adding number of transmissions remaining (in base 64)
			const static size_t INITIAL_SIZE = DATA_SIZE - 1; // leave an additional 1 char space for indicating the channel the data is stored in
			const static size_t N_CHANNELS = 4; // how many channels are available?
			
			// channel class
			class Tchannel{

				String Fresponse = "";
				size_t Fchannel = 0;
				size_t FbytesStart = 0;
				system_tick_t FlastUsed = 0;

				public:
					Tchannel() {}

					// management
					size_t getBytesRemaining() { 
						return (FbytesStart < Fresponse.length()) ? Fresponse.length() - FbytesStart : 0; 
					}
					bool idle(){ return Fresponse.length() == 0; }
					bool neverUsed() { return FlastUsed == 0; }
					bool isOlder(system_tick_t _lastUsed) {
						return !neverUsed() && (_lastUsed == 0 || FlastUsed < _lastUsed);
					}
					system_tick_t lastUsed() { return FlastUsed; }
					void release() { assign(""); }
					void registerChannel(size_t _channel) {
						Fchannel = _channel;
						char variable[20];
						snprintf(variable, sizeof(variable), "sddsChannel%c", TparticleSerializer::encodeIntToBase64(_channel));
						Particle.variable(variable, [this](){ return this->retrieve(); });
					}

					// functionality
					size_t getTransmissionsRemaining() { 
						size_t remaining = getBytesRemaining();
						return remaining / DATA_SIZE + ((remaining % DATA_SIZE > 0) ? 1 : 0);
					}
					size_t assign(const String& _response) {
						Fresponse = _response;
						FbytesStart = 0;
						FlastUsed = millis();
						if (_response.length() > 0) {
							Log.trace("assigned new value to channel '%c' (idx %d): %d bytes, %d transmissions: ", 
								TparticleSerializer::encodeIntToBase64(Fchannel), Fchannel, 
								getBytesRemaining(), getTransmissionsRemaining());
						}
						return(getTransmissionsRemaining());
					}
					String retrieve() {
						if (getBytesRemaining() == 0) return("-");
						FlastUsed = millis();
						size_t remaining = getTransmissionsRemaining();
						remaining = (remaining < 1) ? 0 : remaining - 1;
						// format: transmissions remaining + actual data
						String result = String(TparticleSerializer::encodeIntToBase64(remaining)) + 
							Fresponse.substring(FbytesStart, FbytesStart + DATA_SIZE);
						if (remaining == 0) release();
						else FbytesStart += DATA_SIZE;
						return result;
					}
			};

			// array of channels (could be made a vector for flexible number of channels)
			Tchannel Fchannels[N_CHANNELS];

			// register channels
			void registerChannels(){
				for (size_t i = 0; i<N_CHANNELS; i++){
					Fchannels[i].registerChannel(i);
				}
			}

			// release channel
			bool releaseChannel(char _c) {
				int channel = TparticleSerializer::decodeIntFromBase64(_c);
				if (channel < 0 || channel > (N_CHANNELS - 1)) {
					Log.error("invalid channel '%c'", _c);
					return false;
				}
				Log.trace("releasing channel '%c' (idx %d)", _c, channel);
				Fchannels[channel].release();
				return true;
			}

			// queue a message
			String queue(const String& _response) {
				// any message
				if (_response.length() == 0) return "";
				// message small enough for single transmission?
				if (_response.length() < DATA_SIZE) {
					return "-" + _response; // - is the channel marker, i.e. no channel used
				}
				// find channel to use
				size_t channel = 0;
				size_t oldestFreeChannel = 0;
				system_tick_t oldestFreeLastUsed = 0;
				size_t oldestInUseChannel = 0;
				system_tick_t oldestInUseLastUsed = 0;
				bool foundChannel = false;
				for (size_t i = 0; i < N_CHANNELS; i++){
					// if any never used channels --> use them first
					if (Fchannels[i].idle() && Fchannels[i].neverUsed()){
						channel = i;
						foundChannel = true;
						break;
					}
					// check for oldest free channel
					if (Fchannels[i].idle() && Fchannels[i].isOlder(oldestFreeLastUsed) ) {
						oldestFreeChannel = i;
						oldestFreeLastUsed = Fchannels[i].lastUsed();
					}
					// check for oldest channel that's in use
					if (!Fchannels[i].idle() && Fchannels[i].isOlder(oldestInUseLastUsed) ) {
						oldestInUseChannel = i;
						oldestInUseLastUsed = Fchannels[i].lastUsed();
					}
				}
				// if no unsed channel, use the oldest free (if there are any), or the oldest in use
				if (!foundChannel) {
					channel = (oldestFreeLastUsed > 0) ? oldestFreeChannel : oldestInUseChannel;
				}
				// assign to channel and return initial message
				// format: base64 channel ID, base64 remaining transmissions, actual data
				size_t remaining = Fchannels[channel].assign(_response.substring(INITIAL_SIZE));
				String initial = String(TparticleSerializer::encodeIntToBase64(channel)) + 
					String(TparticleSerializer::encodeIntToBase64(remaining)) + 
					_response.substring(0, INITIAL_SIZE);
				Log.trace("INITIAL (with %d remaining in channel %d):", remaining, channel);
				return initial;
			}

		} 
		// particle variable response object
		FvarResp;

		#pragma endregion

		/*** particle publishing ***/
		#pragma region publishing

		class TparticlePublisher {
			
			private:
				
				// burst & publish management 
        		system_tick_t FminTime = 0; // smallest burst data timestamp (to normalize against)
				std::vector<TvarBurstDataset> FburstData; // data in current burst
				Ttimer FburstTimer; // timer keeping track of bursts
				CloudEvent Fevent; // cloud event
				VariantArray FeventData; // current event data
				VariantArray FqueuedBursts; // stack of data ready for publishing
				const system_tick_t FpublishcheckInterval = 200; // publish status check timer [ms]
				Ttimer FpublishCheckTimer; // publish check timer

				
			public:

				// constructor + logic
				TparticlePublisher() {

					// data bursts
					on(FburstTimer) {
						Variant burst = TparticleSerializer::serializeBurst(FminTime, FburstData);
						bool clear = true;
						if (sddsParticleSystem.publishing.publish == TonOff::e::ON) {
							if (getCBORSize(burst) > 16 * particle::protocol::MAX_EVENT_DATA_LENGTH) {
								// too large!
								Log.error("have to discard a large data burst because it exceeded the 16kB cloud event limit");
							} else if (readyToPublish()) {
								// add to event data stack
								Log.trace("*** QUEUING BURST ***");
								FqueuedBursts.append(burst);
								sddsParticleSystem.publishing.bursts.queued++;
								if (!FpublishCheckTimer.running()) FpublishCheckTimer.start(0);
							} else {
								// want to publish but not yet ready to, keep the burst going! --> restart the timer
								clear = false;
								FburstTimer.start(sddsParticleSystem.publishing.bursts.timerMS);
							}
						} else {	
							// publishing is OFF --> discard
							Log.trace("*** DISCARDING BURST ***\n --> because publishing is OFF");
						}

						// clear?
						if (clear) {
							if (Log.isTraceEnabled()) {
								Log.print(burst.toJSON().c_str());
								Log.print("\n");
							}
							FburstData.clear();
							FminTime = 0;
						}
					};

					// data publishing
					on(FpublishCheckTimer) {

						// check ongoing cloud event
						if (!Fevent.isNew() && !Fevent.isSending()) {
							if (Fevent.isSent()) {
								Log.trace("publish succeeded");
								sddsParticleSystem.publishing.bursts.sent += sddsParticleSystem.publishing.bursts.sending;
							} else if (!Fevent.isValid()) {
								Log.trace("publish failed, invalid (error %d, discarding)", Fevent.error());
								sddsParticleSystem.publishing.bursts.invalid += sddsParticleSystem.publishing.bursts.sending;
							} else if (!Fevent.isOk()) {
								Log.error("publish failed, recoverable (error %d, re-queuing)", Fevent.error());
								sddsParticleSystem.publishing.bursts.failed += sddsParticleSystem.publishing.bursts.sending;
								for (auto var : FeventData) {
									FqueuedBursts.append(var); // add what is in eventData back at the end of the queue
									sddsParticleSystem.publishing.bursts.queued++;
								}
							}
							Fevent.clear();
							sddsParticleSystem.publishing.bursts.sending = 0;
						}

						//  check if new cloud event can be sent
						if (!FqueuedBursts.isEmpty() && Particle.connected() && !Fevent.isSending()) {
							FeventData = VariantArray();
							size_t cborSize = 0; 
							// pack as much of the queued data into the event as is possible with the 1kb limit
							while(!FqueuedBursts.isEmpty() && cborSize < particle::protocol::MAX_EVENT_DATA_LENGTH) {
								cborSize += getCBORSize(FqueuedBursts[0]);
								if (cborSize < particle::protocol::MAX_EVENT_DATA_LENGTH || FeventData.isEmpty()) {
									// add to event data and remove from queue
									FeventData.append(FqueuedBursts[0]);
									FqueuedBursts.removeAt(0);
								}
							}
							// finalize event info
							Fevent.name(sddsParticleSystem.publishing.event);
							Fevent.data(FeventData);
							sddsParticleSystem.publishing.bursts.sending = FeventData.size();
							sddsParticleSystem.publishing.bursts.queued -= sddsParticleSystem.publishing.bursts.sending;
							// try to publish publish
							if (!Particle.publish(Fevent)) {
								Log.error("published failed immediately, discarding");
								Fevent.clear();
								sddsParticleSystem.publishing.bursts.invalid += sddsParticleSystem.publishing.bursts.sending;
								sddsParticleSystem.publishing.bursts.sending = 0;
							}
						}

						// check in again?
						if(Fevent.isSending() || sddsParticleSystem.publishing.bursts.queued > 0) 
							FpublishCheckTimer.start(FpublishcheckInterval);
					};

				}

				/**
				 * @brief add variant data to the current burst
				 */
				void addToBurst(Tdescr* _d, system_tick_t _time, const Variant& _data) {
					// safety check
					if (_d == nullptr) return; 
					
					// need to complete startup
					if (sddsParticleSystem.startup != TstartuStatus::e::complete) return;

					// keep track of min time
					if (FminTime == 0 || FminTime > _time) FminTime = _time;

					// add to existing burst data for this variable
					for (size_t i = 0; i < FburstData.size(); ++i) {
						if (FburstData[i].FdescrPtr == _d){
							FburstData[i].Fdataset.push_back({_time, _data});
							return;
						}
					}

					// first burst data for this variable
					FburstData.push_back({_d, {_time, _data}});

					// start burst timer if it's not already running
					if (!FburstTimer.running()) 
						FburstTimer.start(sddsParticleSystem.publishing.bursts.timerMS);
				}

				/**
				 * @brief burst publish the current value of Tdescr
				 */
				void addToBurst(Tdescr* _d, system_tick_t _time) {
					addToBurst(_d, _time, TparticleSerializer::serializeData(_d));
				}

				/**
				 * @brief check if we're ready to publish (need a name and valid time)
				 */
				bool readyToPublish() {
					return sddsParticleSystem.name != "" && Time.isValid();
				}

		} 
		// publisher object
		Fpublisher;

		#pragma endregion

		/*** automatic publishing of sdds variables  ***/
		#pragma region variables publish

		// auto-detection of unit vars
		bool FunitAutoDetect;
		dtypes::string FunitVarName;

		/**
		 * @brief menu handle with provided name
		 */
		class TnamedMenuHandle : public TmenuHandle{
			dtypes::string Fname;
			Tmeta meta() override { return Tmeta{TYPE_ID,0,Fname.c_str()}; }
			public:
				TnamedMenuHandle(const dtypes::string _name) : Fname(_name) {};
		};

		/**
		 * @brief keeps track of the publish intervals
		 * -1		-> use the globalPublishInterval
		 * 0 		-> no publishes (default)
		 * 1 		-> publish on change
		 * 0-999 	-> not allowed
		 * 1000+ 	-> publish every 1000+ ms
		 * 
		 */
		class TparticleVarWrapper : public Tint32 {
			private:
				Ttimer Ftimer;
				TcallbackWrapper FintervalCbw{this};
				TcallbackWrapper ForiginCbw{this};
			protected:
				Tdescr* FvarOrigin = nullptr;
				Tdescr* FlinkedUnit = nullptr;
				TparticlePublisher* Fpublisher = nullptr;
				system_tick_t FlastUpdateTime = 0;
				// override in derived classes
				virtual void clear() {}
				virtual void changeValue(){}
				virtual system_tick_t getTimeForPublish() {
					// default is the last update time
					return FlastUpdateTime;
				}
				virtual Variant getDataForPublish() {
					// default is just the value of the variable
					return TparticleSerializer::serializeData(FvarOrigin, FlinkedUnit);	
				}
			public:
				TparticleVarWrapper(Tdescr* _voi, TparticlePublisher* _pub, Tdescr* _unit) : FvarOrigin(_voi), Fpublisher(_pub), FlinkedUnit(_unit) {
					Fvalue = 0;

					// call back for origin value change
					ForiginCbw = [this](void* _ctx){ 
						// only start collecting values once startup is complete
						if (sddsParticleSystem.startup == TstartuStatus::e::complete) {
							if (Fvalue == 1) {
								// publish current variable value immediately
								if (Fpublisher) {
									Fpublisher->addToBurst(FvarOrigin, millis(), TparticleSerializer::serializeData(FvarOrigin, FlinkedUnit));
								}
							} else {
								// collect values
								FlastUpdateTime = millis();
								changeValue(); // note: always triggers, even if value hasn't actually changed (i.e. set to the same it already is)
							}
						}
					};

					// call back for the publish interval changing
					FintervalCbw = [this](void* _ctx){ 
						Log.trace("new publish interval value for %s: %lu", 
							TparticleSerializer::getVarPath(FvarOrigin).c_str(), Fvalue);

						// reset callback, timer and values
						FvarOrigin->callbacks()->remove(&ForiginCbw);
						Ftimer.stop();
						reset();
						
						// no publishing
						if (Fvalue == 0) return;
						
						// add publich callback
						FvarOrigin->callbacks()->addCbw(ForiginCbw);

						// if Fvalue is set (not -1) --> start own timer
						// (if Fvalue is -1, the publish trigger comes from the global)
						if (Fvalue > 0) {
							if (Fvalue > 1 && Fvalue < 1000) Fvalue = 1000; // min interval is 1 second
							Ftimer.start(Fvalue);
						}
					};
					callbacks()->addCbw(FintervalCbw);
					
					// trigger publish
					on(Ftimer){ 
						// publish (only does anything if there's data)
						publish(); 
						// restart timer
						Ftimer.start(Fvalue);
					};
				}
				Tdescr* origin(){ return FvarOrigin; } 
				Tmeta meta() override { return Tmeta{Tuint32::TYPE_ID, sdds::opt::saveval, FvarOrigin->name()}; }
				void operator=(Tuint32::dtype _v) { __setValue(_v); }
				template <typename T>
				void operator=(T _val) { __setValue(_val); }
				
				/**
				 * @brief reset the data storage
				 */
				void reset() {
					FlastUpdateTime = 0; // reset to no value
					clear();
				}

				/**
				 * @brief does this use the global publishing interval?
				 */
				bool usesGlobalPublishingInterval() {
					return (Fvalue == -1);
				}

				/**
				 * @brief adds to the burst of the publisher if any data is stored
				 */
				void publish(){
					if (FlastUpdateTime == 0) return; // has no recent value
					if (Fpublisher) {
						Fpublisher->addToBurst(FvarOrigin, getTimeForPublish(), getDataForPublish());
					}
					reset();
				}
		};

		/**
		 * @brief variable wrapper for averaging numeric data
		 * note that this still publishes the original numeric data format (e.g. int) if it's 
		 * immediate publish (=1) but any averaging even of ints will turn them to floats
		 * in the end it doesn't really make a difference in the resulting JSON though
		 */
		template<class sdds_dtype>
		class TparticleAveragingVarWrapper : public TparticleVarWrapper {
			private:
				// running stats for value averaging
				dtypes::uint32 FsumCnt = 0;
				dtypes::float32 Favg = 0;
				dtypes::float32 Fm2 = 0;
				dtypes::float32 Ftime = 0;
			
				void add(dtypes::float32 x) {
					FsumCnt++;
					if (FsumCnt > 1) {
						// value
						dtypes::float32 delta = x - Favg;
						Favg += delta / FsumCnt;
						Fm2 += delta * (x - Favg);
						// time
						delta = static_cast<dtypes::float32>(millis()) - Ftime;
						Ftime += delta / FsumCnt;
					} else {
						// n = 1
						Favg = x;
						Ftime = static_cast<dtypes::float32>(millis());
					}
				}

				dtypes::float32 variance() {
					// variance implemented based on Welford's algorithm
					return ( (FsumCnt > 1) ? Fm2 / (FsumCnt - 1) : std::numeric_limits<dtypes::float32>::quiet_NaN());
				}

				dtypes::float32 stdDev() {
					return sqrt(variance());
				}

			protected:
				sdds_dtype* typedVoi(){ return static_cast<sdds_dtype*>(this->FvarOrigin); } 

				void clear () override {
					FsumCnt = 0;
					Favg = 0;
					Fm2 = 0;
					Ftime = 0;
				}

				void changeValue() override{
					add(static_cast<dtypes::float32>(typedVoi()->Fvalue));
				}

				system_tick_t getTimeForPublish() override {
					return Ftime;
				}
				
				Variant getDataForPublish() override {
					return TparticleSerializer::serializeData(FsumCnt, Favg, stdDev(), FlinkedUnit);	
				}

			public:
				TparticleAveragingVarWrapper(Tdescr* _voi, TparticlePublisher* _pub, Tdescr* _unit) : TparticleVarWrapper(_voi, _pub, _unit) {
				}
		};

		/**
		 * @brief create tree for the variable intervals
		 */
		void createVariableIntervalsTree(TmenuHandle* _src, TmenuHandle* _dst){
			for (auto it=_src->iterator(); it.hasCurrent(); it.jumpToNext()){
				auto d = it.current();
				if (!d) continue;

				// is there a linked unit?
				Tdescr* linkedUnit = nullptr;
				if (FunitAutoDetect && d->next() && d->next()->type() == sdds::Ttype::STRING && d->next()->name() == FunitVarName) {
					linkedUnit = d->next();
				}
	
				// averaging wrappers for the numeric data types
				auto dt = d->type();
				if (dt == sdds::Ttype::UINT8)
					_dst->addDescr(new TparticleAveragingVarWrapper<Tuint8>(d, &Fpublisher, linkedUnit));
				else if (dt == sdds::Ttype::UINT16)
					_dst->addDescr(new TparticleAveragingVarWrapper<Tuint16>(d, &Fpublisher, linkedUnit));
				else if (dt == sdds::Ttype::UINT32)
					_dst->addDescr(new TparticleAveragingVarWrapper<Tuint32>(d, &Fpublisher, linkedUnit));
				else if (dt == sdds::Ttype::INT8)
					_dst->addDescr(new TparticleAveragingVarWrapper<Tint8>(d, &Fpublisher, linkedUnit));
				else if (dt == sdds::Ttype::INT16)
					_dst->addDescr(new TparticleAveragingVarWrapper<Tint16>(d, &Fpublisher, linkedUnit));
				else if (dt == sdds::Ttype::INT32)
					_dst->addDescr(new TparticleAveragingVarWrapper<Tint32>(d, &Fpublisher, linkedUnit));
				else if (dt == sdds::Ttype::FLOAT32)
					_dst->addDescr(new TparticleAveragingVarWrapper<Tfloat32>(d, &Fpublisher, linkedUnit));
				else if (dt == sdds::Ttype::STRUCT){
					// recursive structure
					TmenuHandle* mh = static_cast<Tstruct*>(d)->value();
					if (mh){
						TmenuHandle* nextLevel = new TnamedMenuHandle(d->name());
						_dst->addDescr(nextLevel);
						createVariableIntervalsTree(mh,nextLevel);
					} 
				} else {
					// non-averaging for everything else (enum, string)
					_dst->addDescr(new TparticleVarWrapper(d, &Fpublisher, linkedUnit));
				}

			}
		}

		/**
		 * @brief reset values of global publish vars
		 */
		void resetGlobal(TmenuHandle* _dst) {
			for (auto it=_dst->iterator(); it.hasCurrent(); it.jumpToNext()){
				auto d = it.current();
				if (!d) continue;
				if (d->type() == sdds::Ttype::STRUCT){
					TmenuHandle* mh = static_cast<Tstruct*>(d)->value();
					if (mh) resetGlobal(mh);
				} else {
					TparticleVarWrapper* pvw = static_cast<TparticleVarWrapper*>(d);
					pvw->reset();
				}
			}
		}
		void resetGlobal() {
			if (sddsParticleSystem.startup == TstartuStatus::e::complete) {
				// if we're fully started up, reset the variables (otherwise they might not yet be created)
				resetGlobal(&sddsParticleVariables);
			}
		}

		/**
		 * @brief publish values of global publish vars
		 */
		void publishGlobal(TmenuHandle* _dst) {
			for (auto it=_dst->iterator(); it.hasCurrent(); it.jumpToNext()){
				auto d = it.current();
				if (!d) continue;
				if (d->type() == sdds::Ttype::STRUCT){
					TmenuHandle* mh = static_cast<Tstruct*>(d)->value();
					if (mh) publishGlobal(mh);
				} else {
					TparticleVarWrapper* pvw = static_cast<TparticleVarWrapper*>(d);
					pvw->publish();
				}
			}
		}
		void publishGlobal() {
			if (sddsParticleSystem.startup == TstartuStatus::e::complete) {
				// if we're fully started up, publish the variables (otherwise they might not yet be created)
				publishGlobal(&sddsParticleVariables);
			}
		}

		#pragma endregion

	private:

		/*** particle variables/functions ***/
		#pragma region particle vars/funcs

		// sdds structure
		TmenuHandle* Froot = nullptr; 

		// plain comms handler to process sdds variable commands
		TplainCommHandler Fpch;

		// Timer for global publishing interval
		Ttimer FglobalPublishTimer;

		// error codes
		constexpr static int ERR_INV_CH = -200;

		/**
		 * @brief Particle.function sddsSetVariables
		 */
		int setVariables(String _cmd){
			// FIXME: split _cmd and process each command in it individually
			// FIXME: return a negative (-) bitwise addition of whether each command
			// succeeded or failed (Q: is that enough? limits to 31 commands at once but since the yhave to fit into a 1024 char that should suffice)
			// i.e. return 0 if all succeeded
			// store each command along with the time base, offset, command text, return code (and error description if there is one) in sddsCmdLog (latest however many fit into it)
			// --> retrießve with the getCommandLog call, doesn't have to be a call, could also just be a fixed variable
			Fpch.resetLastError();
			Fpch.handleMessage(_cmd);
			return -TplainCommHandler::Terror::toInt(Fpch.lastError());
		}

		/**
		 * @brief Particle.variable sddsGetTree
		 */
		String getTree() {
			return FvarResp.queue(FstructVar.toJSON());
		}

		/**
		 * @brief Particle.variable sddsGetValues
		 */
		String getValues() {
			Variant values = TparticleSerializer::serializeValuesOnly(Froot);
			return FvarResp.queue(values.toJSON());
		}

		/**
		 * @brief Particel variable sddsGetCommandLog
		 */
		String getCommandLog() {

			return "";
		}

		#pragma endregion

	public:

		/*** constructor and setup ***/
		#pragma region constructor+setup

		/**
		 * @brief particle spike consturctor
		 * @param _root the sdds tree
		 * @param _type name of the structure type, i.e. what kind of device is this? ("pump", "mfc", etc.)
		 * @param _version version of the structure type to inform servers who have cached structure when the structure type has been updated
		 * @param _unit name of unit vars for auto-detecting units (pass "" to turn auto-detection off)
		 */
		TparticleSpike(TmenuHandle& _root, const dtypes::string& _type, dtypes::uint16 _version, const dtypes::string& _unit): Fpch(_root, nullptr) {
			Froot = _root;
			sddsParticleSystem.type = _type;
			sddsParticleSystem.version = _version;
			FunitVarName = _unit;
			FunitAutoDetect = FunitVarName != "";

			// custom system actions
			on(sddsParticleSystem.action) {
				if (sddsParticleSystem.action==TsystemAction::e::snapshot){
					
					// get snapshot
					Variant snapshot = TparticleSerializer::serializeValuesForSnapshot(Froot);
					
					if (Log.isTraceEnabled()) {
						Log.trace("*** SNAPSHOT ***");
						Log.print(snapshot.toJSON().c_str());
						Log.print("\n");
					}

					Fpublisher.addToBurst(Froot, millis(), snapshot);

					// add all variables to burst that are saveval
					sddsParticleSystem.action = TsystemAction::e::___;
				}
			};

			// global publishing interval
            on(sddsParticleSystem.publishing.globalIntervalMS) {
                if (sddsParticleSystem.publishing.globalIntervalMS < 1000) 
                    sddsParticleSystem.publishing.globalIntervalMS = 1000; // don't allow publishing any faster than once a second
				FglobalPublishTimer.stop();
				resetGlobal();
				FglobalPublishTimer.start(sddsParticleSystem.publishing.globalIntervalMS);		
            };
			FglobalPublishTimer.start(sddsParticleSystem.publishing.globalIntervalMS);

			// (re)start timer when publishing is turned on
			on(sddsParticleSystem.publishing.publish) {
				// Q: should this only trigger if it is _changed_ to ON?
				// (currently this triggers every time it's set even if it's already ON)
				if (sddsParticleSystem.publishing.publish == TonOff::e::ON) {
					FglobalPublishTimer.stop();
					resetGlobal();
					FglobalPublishTimer.start(sddsParticleSystem.publishing.globalIntervalMS);
				}
			};

			// publish timer triggers
			on(FglobalPublishTimer) {
				Log.trace("*** GLOBAL PUBLISH TIMER ***");
				publishGlobal(); // variables reset themselves
				FglobalPublishTimer.start(sddsParticleSystem.publishing.globalIntervalMS);
			};

			// startup complete
			on(sddsParticleSystem.state.time) {
				// the state is loaded from EEPROM, this completes the startup
				if (sddsParticleSystem.state.time > 0 && sddsParticleSystem.startup != TstartuStatus::e::complete) {
					sddsParticleSystem.startup = TstartuStatus::e::complete;
					// note: same as with state.error below, this is NOT recorded if it's after a user reset
					// because the device goes back to default publish (NO)
					Fpublisher.addToBurst(&sddsParticleSystem.vitals.lastRestart, millis());
				}
			};

			// system error
			on(sddsParticleSystem.state.error) {
				if (sddsParticleSystem.state.error != TparamError::e::___) {
					// encountered an error when loading system state from EEPROM
					// FIXME: how should this be handled? if state didn't load the device has no
					// way of knowing whether it should publish this issue, worst case scenario
					// noone realizes it had a restart that lead to a state loading problem
					// MAYBE: force a publish of this issue? i.e. allow flagging a burst for
					// publish irrespective of publishing::publish value?
				}
			};

			// debug actions
			#ifdef SDDS_PARTICLE_DEBUG
			on(sddsParticleSystem.debug) {
				if (sddsParticleSystem.debug == TdebugAction::e::getValues) {
					Log.trace("*** VALUES ***");
					Log.print(TparticleSerializer::serializeValuesOnly(Froot).toJSON().c_str());
					Log.print("\n");
				} else if (sddsParticleSystem.debug == TdebugAction::e::getTree) {
					Log.trace("*** TREE ***");
					Log.print(FstructVar.toJSON().c_str());
					Log.print("\n");
					//String base64 = TvariantSerializer::variantToBase64(FstructVar);
					//Log.trace("\nCBOR in base64 (size %d): ", base64.length());
					//Log.print(base64);
					//Log.print("\n");
				}
				if (sddsParticleSystem.debug != TdebugAction::e::___) {
					sddsParticleSystem.debug = TdebugAction::e::___;
				}
			};
			#endif
		}

		/**
		 * @brief call during setup() to initialize the SYSTEM menu and all particle variables and functions
		 * @note this runs BEFORE any on(sdds::setup())
		 */
		void setup(){
			// add SYSTEM menu
			Froot->addDescr(&sddsParticleSystem, 0);
			
			// generate publishing intervals tree for all variables
			createVariableIntervalsTree(Froot, &sddsParticleVariables);
			sddsParticleSystem.publishing.addDescr(sddsParticleVariables);
			
			// store complete SDDS structure in FstructVar
			FstructVar = TparticleSerializer::serializeParticleTree(Froot);
			
            // process device reset information
			System.enableFeature(FEATURE_RESET_INFO);
            if (System.resetReason() == RESET_REASON_PANIC) {
                // uh oh - reset due to PANIC (e.g. segfault)
                // https://docs.particle.io/reference/cloud-apis/api/#spark-device-last_reset
                uint32_t panicCode = System.resetReasonData();
                Log.error("restarted due to PANIC, code: %lu", panicCode);
                sddsParticleSystem.vitals.lastRestart = TresetStatus::e::PANIC;
                //System.enterSafeMode(); // go straight to safe mode?
            } else if (System.resetReason() == RESET_REASON_WATCHDOG) {
                // hardware watchdog detected a timeout
                Log.warn("restarted due to watchdog (=timeout)");
                sddsParticleSystem.vitals.lastRestart = TresetStatus::e::watchdogTimeout;
            } else if (System.resetReason() == RESET_REASON_USER) {
                // software triggered resets
                uint32_t userReset = System.resetReasonData();
                if (userReset == static_cast<uint8_t>(TresetStatus::e::outOfMemory)) {
                    // low memory detected
                    Log.warn("restarted due to low memory");
                    sddsParticleSystem.vitals.lastRestart = TresetStatus::e::outOfMemory;
                } else if (userReset == static_cast<uint8_t>(TresetStatus::e::userRestart)) {
                    // user requested a restart
                    Log.trace("restarted per user request");
                    sddsParticleSystem.vitals.lastRestart = TresetStatus::e::userRestart;
                } else if (userReset == static_cast<uint8_t>(TresetStatus::e::userReset)) {
                    // user requested a restart
                    Log.trace("restarted and resetting per user request");
                    sddsParticleSystem.vitals.lastRestart = TresetStatus::e::userReset;
					sdds::paramSave::Tstream::INIT();
					sdds::paramSave::TparamStreamer ps;
					sdds::paramSave::Tstream s;
					ps.save(Froot,&s);
                } 
            } else {
                // report any of the other reset reasons? 
                // https://docs.particle.io/reference/cloud-apis/api/#spark-device-last_reset
            }

			// particles variables to retrieve tree and values
			Particle.variable("sddsGetTree", [this](){ return this->getTree(); });
			Particle.variable("sddsGetValues", [this](){ return this->getValues(); });
			FvarResp.registerChannels(); // return value channels

			// particle functions to set variable and variable for logs
			Particle.function("sddsSetVariables", &TparticleSpike::setVariables,this);
			Particle.variable("sddsGetCommandLog", [this](){ return this->getCommandLog(); });
		}

		#pragma endregion
};
