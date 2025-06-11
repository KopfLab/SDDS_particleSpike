#ifndef UPARTICLESPIKE_H
#define UPARTICLESPIKE_H

#if MARKI_DEBUG_PLATFORM
	#include "uParticleEmulation.h"
#endif

#include "uTypedef.h"
#include "uCoreEnums.h"

/**
 * @brief Declare PARTICLE_IO menu
 */
class TparticleGlobal : public TmenuHandle, public Tthread{
	private:
		using TonOff = sdds::enums::OnOff;
		Tevent FevPublishPending;

		Tmeta meta() override { return Tmeta{TYPE_ID,0,"PARTICLE_IO"}; }
	public:
		sdds_var(TonOff,publish,sdds::opt::saveval,TonOff::e::OFF) 								// global on/off for publishing to the cloud
		sdds_var(TmenuHandle,variables)
#if MARKI_DEBUG_PLATFORM
		sdds_var(Tstring,func)
		sdds_var(Tstring,param)
		sdds_var(Tint32,funcRes)
		sdds_var(Tint32,logChs)
#endif
		TparticleGlobal() : FevPublishPending(this){

		}

		void setPublishPending(){ FevPublishPending.signal(); }

		void execute(Tevent* _ev){

		}

} sddsParticleInst;

class TparticleSpike{
	private:
		//Tmeta meta() override { return Tmeta{TYPE_ID,0,"PARTICLE_IO"}; }
		TplainCommHandler Fpch;

		/**
		 * @brief streamClass to be used with TplainCommHandler
		 * It manages the channels 0..19 and fills them on the fly
		 */
		class TparticleStream : public Tstream{
			constexpr static int CH_BUF_SIZE = 1024;
			constexpr static int N_CHANNELS = 20;
			
			class Tchannel{
				Ttimer timer;
				public:
					int FbytesInBuffer; 
					char Fbuffer[CH_BUF_SIZE];
					bool append(char _c){
						if (FbytesInBuffer >= CH_BUF_SIZE-1) return false;
						Fbuffer[FbytesInBuffer++] = _c;
						return true;
					}
					bool idle(){ return FbytesInBuffer == 0; }
					void release() { FbytesInBuffer = 0; }
					void init() { 
						memset(Fbuffer, 0, CH_BUF_SIZE);
						FbytesInBuffer = 0; 
					}
					void setReleaseTimer(dtypes::int32 _time){ timer.start(_time); }
					Tchannel(){
						on(timer){ 
							init(); 
						};
					}
			};

			private:
				Tchannel Fchannels[N_CHANNELS];
				dtypes::uint32 FcurrCh = 0;
				dtypes::uint32 FusedChannels = 0;
				bool Foverflow = false;
		
				bool allocateChannel(){
					for (int i=0; i<N_CHANNELS; i++){
						if (Fchannels[i].idle()){
							Fchannels[i].init();
							FcurrCh = i;
							FusedChannels |= (1<<i);
							return true;
						}
					}
					Foverflow = true;
					return false;
				}

				void write(const char* _str) override{
					auto strLen = strlen(_str);
					while (strLen-- > 0) write(*_str++);
				}

				void write(const char _char) override{
					if (Fchannels[FcurrCh].append(_char)) return;
					if (!allocateChannel()) return;
					Fchannels[FcurrCh].append(_char);
				}

			public:
				bool init(){
					FusedChannels = 0;
					if (!allocateChannel()) return false;
					Foverflow = false;
					return true;
				}

				bool overflow() { return Foverflow; }
				dtypes::uint32 usedChannels(){ return FusedChannels; }
				
				void setReleaseTimer(dtypes::int32 _time){
					for (int i=0; i<N_CHANNELS; i++){
						if (FusedChannels & (1<<i))
							Fchannels[i].setReleaseTimer(_time);
					}
				}

				void releaseChannels(){
					for (int i=0; i<N_CHANNELS; i++){
						if (FusedChannels & (1<<i))
							Fchannels[i].release();
					}
				}

				void registerChannels(){
					for (int i=0; i<N_CHANNELS; i++){
						char name[8];
						snprintf(name, sizeof(name), "r%d", i);
						Particle.variable(name, &Fchannels[i].Fbuffer[0]);
					}
				}
#if MARKI_DEBUG_PLATFORM
				dtypes::string to_string(dtypes::uint32 _chs){
					dtypes::string res = "";
					for (int i=0; i<N_CHANNELS; i++){
						if (_chs && (1<<i)){
							if (!Fchannels[i].idle())
								res += Fchannels[i].Fbuffer;
						}
					}
					return res;
				}
#endif
		} Fss;

		class TnamedMenuHandle : public TmenuHandle{
			dtypes::string Fname;
			Tmeta meta() override { return Tmeta{TYPE_ID,0,Fname.c_str()}; }
			public:
				TnamedMenuHandle(const dtypes::string _name){
					Fname = _name;
				}
		};

		class TparticleVarWrapper : public TnamedMenuHandle{
			private:
				Ttimer Ftimer;
				TcallbackWrapper Fcbw{this};
			protected:
				dtypes::string FcloudName; 
				const char* cloudName() { return FcloudName.c_str(); }
				Tdescr* Fvoi = nullptr;

				virtual void doOnValueChange(){}
				virtual void doOnUpdateTimer(){}
				int particleFunction(dtypes::string _cmd){
					Fvoi->setValue(_cmd.c_str());
					return 0;
				}
			public:
				sdds_var(Tuint32,UpdateTime,sdds::opt::saveval,0)

				TparticleVarWrapper(const dtypes::string _varPath, Tdescr* _voi) 
					: TnamedMenuHandle(_voi->name())
				{
					FcloudName = _varPath;
					Fvoi = _voi;
					
					Fcbw = [this](void* _ctx){ 
						doOnValueChange();
						if (UpdateTime == 1) doOnUpdateTimer();
					};

					/**
					 * @brief UpdateTime:
					 * 0 		-> no publishes
					 * 1 		-> publish on change
					 * 0-999 	-> not allowed
					 * 1000+ 	-> publish every 1000+ ms
					 * 
					 */
					on(UpdateTime){
						Fvoi->callbacks()->remove(&Fcbw);
						Ftimer.stop();
						
						if (UpdateTime == 0)
							return;
						
						Fvoi->callbacks()->addCbw(Fcbw);
						if (UpdateTime == 1)
							return;

						if (UpdateTime < 1000) 
							UpdateTime = 1000;
						Ftimer.start(UpdateTime);
					};

					on(Ftimer){
						if (UpdateTime > 0)
							Ftimer.start(UpdateTime*1000);
						doOnUpdateTimer();
					};
				}
		};

		template<class sdds_dtype>
		class TparticleNumericVarWrapper : public TparticleVarWrapper{
			private:
				dtypes::float32 FvalAccu;
				dtypes::uint32 FsumCnt = 0;
			protected:
				sdds_dtype* typedVoi(){ return static_cast<sdds_dtype*>(this->Fvoi); } 
			public:

				sdds_var(Tuint8,decimals,sdds::opt::saveval,2)

				TparticleNumericVarWrapper(const dtypes::string _varPath, Tdescr* _voi) 
					: TparticleVarWrapper(_varPath,_voi)
				{
					FvalAccu = 0;
					Particle.variable(cloudName(),&typedVoi()->Fvalue);
				}

				void doOnValueChange() override{
					FvalAccu+=typedVoi()->Fvalue;
					FsumCnt++;
				}

				dtypes::float32 getAvgValue(){
					return FsumCnt>0?(FvalAccu/FsumCnt):this->typedVoi()->Fvalue;
				}

				void doOnUpdateTimer() override{
					char buf[32];
					snprintf(buf, sizeof(buf)-1, "%.*f", static_cast<int>(decimals), getAvgValue());
					Particle.publish(this->cloudName(),&buf[0]);
					FsumCnt = 0;
					FvalAccu = 0;
				}
		};

		class TparticleEnumVarWrapper : public TparticleVarWrapper{
			private:
				TenumBase* typedVoi(){ return static_cast<TenumBase*>(this->Fvoi); } 
			public:
				TparticleEnumVarWrapper(const dtypes::string _varPath, Tdescr* _voi) 
					: TparticleVarWrapper(_varPath,_voi)
				{
				}
		};

		void createTree(TmenuHandle* _src, TmenuHandle* _dst, dtypes::string _path = ""){
			for (auto it=_src->iterator(); it.hasCurrent(); it.jumpToNext()){
				auto d = it.current();
				if (!d) continue;

				dtypes::string varPath = _path==""?d->name():_path + "/" + d->name();

				auto dt = d->type();
				if (dt == sdds::Ttype::UINT8)
					_dst->addDescr(new TparticleNumericVarWrapper<Tuint8>(varPath,d));
				else if (dt == sdds::Ttype::UINT16)
					_dst->addDescr(new TparticleNumericVarWrapper<Tuint16>(varPath,d));
				else if (dt == sdds::Ttype::UINT32)
					_dst->addDescr(new TparticleNumericVarWrapper<Tuint32>(varPath,d));
				else if (dt == sdds::Ttype::INT8)
					_dst->addDescr(new TparticleNumericVarWrapper<Tint8>(varPath,d));
				else if (dt == sdds::Ttype::INT16)
					_dst->addDescr(new TparticleNumericVarWrapper<Tint16>(varPath,d));
				else if (dt == sdds::Ttype::INT32)
					_dst->addDescr(new TparticleNumericVarWrapper<Tint32>(varPath,d));
				else if (dt == sdds::Ttype::FLOAT32)
					_dst->addDescr(new TparticleNumericVarWrapper<Tfloat32>(varPath,d));
				else if (dt == sdds::Ttype::ENUM)
					_dst->addDescr(new TparticleNumericVarWrapper<Tfloat32>(varPath,d));
				else if (dt == sdds::Ttype::STRUCT){
					TmenuHandle* mh = static_cast<Tstruct*>(d)->value();
					if (mh){
						TmenuHandle* nextLevel = new TnamedMenuHandle(d->name());
						_dst->addDescr(nextLevel);
						createTree(mh,nextLevel,varPath);
					} 
				}
			}
		}

	private:
		TmenuHandle* Froot = nullptr;
		Ttimer Ftimer;

		int setVariable(dtypes::string _cmd){
			TstringRef sr(_cmd.c_str());
			Tlocator l(Froot);
			if (l.locate(sr)){
			}
			return 0;
		}

		constexpr static int ERR_INV_TIME = -200;
		constexpr static int ERR_INV_UNIT = -201;
		constexpr static int ERR_NO_CH = -202;

		dtypes::int32 parseTime(TstringRef& _msg){
			dtypes::int16 reqTime = 0;
			if (!_msg.parseValue(reqTime)) return ERR_INV_TIME;

			char tUnit = _msg.next();
			if (tUnit != ' '){
				if (_msg.next() != ' ') return ERR_INV_TIME;
				if (tUnit = 's') return static_cast<dtypes::int32>(reqTime) * 1000;
				else if (tUnit == 'm') return static_cast<dtypes::int32>(reqTime) * 60*1000;
				else return ERR_INV_UNIT;
			}
			return reqTime;
		}

		dtypes::int32 invokePlainCommHandler(TstringRef& _cmd){
			Fss.init();
			Fpch.setStream(&Fss);
			Fpch.resetLastError();
			Fpch.handleMessage(_cmd);
			Fpch.setStream(nullptr);
			if (Fss.overflow()){
				Fss.releaseChannels();
				return ERR_NO_CH;
			} 
			if (Fpch.lastError() != TplainCommHandler::Terror::e::___){
				Fss.releaseChannels();
				return -TplainCommHandler::Terror::toInt(Fpch.lastError());
			}
			return 0;
		}
		
		dtypes::int32 handlerT(dtypes::string _cmd){
			TstringRef cmd(_cmd.c_str());
			auto reqTime = parseTime(cmd);
			if (reqTime < 0) return reqTime;

			dtypes::int32 res = invokePlainCommHandler(cmd);
			if (res < 0) return res;

			Fss.setReleaseTimer(reqTime);
			return Fss.usedChannels();
		}

		dtypes::int32 setVarHandler(dtypes::string _cmd){
			Fpch.resetLastError();
			Fpch.handleMessage(_cmd);
			return -TplainCommHandler::Terror::toInt(Fpch.lastError());
		}

	public:
		TparticleSpike(TmenuHandle& _root)
		: Fpch(_root,nullptr)
		{
			Froot = _root;
			on(sddsParticleInst.func){
				sddsParticleInst.funcRes = Particle.callFunction(sddsParticleInst.func,sddsParticleInst.param);
			};

			on(sddsParticleInst.param){
				sddsParticleInst.funcRes = Particle.callFunction(sddsParticleInst.func,sddsParticleInst.param);
			};

			on(sddsParticleInst.logChs){
				dtypes::uint32 chs = sddsParticleInst.logChs;
				auto res = Fss.to_string(chs);
				std::cout << res;
			};

		}

		void setup(){
			createTree(Froot,&sddsParticleInst.variables);
			Froot->addDescr(&sddsParticleInst);
			
			Particle.function("T",&handlerT,this);
			Particle.function("sdds_var",&setVarHandler,this);
			Fss.registerChannels();
		}
};

#endif //UPARTICLESPIKE_H