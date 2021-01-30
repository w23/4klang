#include "Go4kVSTiCore.h"
#include "Export.h"

#include <math.h>

#include "Go4kVSTiGUI.h"
#include "..\..\win\resource.h"
#include <stdio.h>

#include <vector>
#include <map>
#include <list>
#include <string>

#define MessageBox(msg, title, flags) fprintf(stderr, "MessageBox: %s: %s %08x\n", title, msg, flags)

DWORD versiontag10 = 0x30316b34; // 4k10
DWORD versiontag11 = 0x31316b34; // 4k11
DWORD versiontag12 = 0x32316b34; // 4k12
DWORD versiontag13 = 0x33316b34; // 4k13
DWORD versiontag   = 0x34316b34; // 4k14

static SynthObject SynthObj;
static std::unique_ptr<Recorder> recorder;

extern "C" void __stdcall go4kENV_func();
extern "C" void __stdcall go4kVCO_func();
extern "C" void __stdcall go4kVCF_func();
extern "C" void __stdcall go4kDST_func();
extern "C" void __stdcall go4kDLL_func();
extern "C" void __stdcall go4kFOP_func();
extern "C" void __stdcall go4kFST_func();
extern "C" void __stdcall go4kPAN_func();
extern "C" void __stdcall go4kOUT_func();
extern "C" void __stdcall go4kACC_func();
extern "C" void __stdcall go4kFLD_func();
extern "C" void __stdcall go4kGLITCH_func();
extern "C" DWORD go4k_delay_buffer_ofs;
extern "C" float go4k_delay_buffer;
extern "C" WORD go4k_delay_times;
extern "C" float LFO_NORMALIZE;

typedef void (__stdcall *go4kFunc)(void); 

void __stdcall NULL_func()
{
};

static go4kFunc SynthFuncs[] =
{
	NULL_func,
	go4kENV_func,
	go4kVCO_func,
	go4kVCF_func,
	go4kDST_func,
	go4kDLL_func,
	go4kFOP_func,
	go4kFST_func,
	go4kPAN_func,
	go4kOUT_func,
	go4kACC_func,
	go4kFLD_func,
	go4kGLITCH_func
};

static float BeatsPerMinute = 120.0f;

// solo mode handling
static int SoloChannel = 0;
static int Solo = 0;

// init synth
void Go4kVSTi_Init()
{
	static bool initialized = false;
	// do one time initialisation here (e.g. wavtable generation ...)
	if (!initialized)
	{
		memset(&SynthObj, 0, sizeof(SynthObj));
		BeatsPerMinute = 120.0f;
		SoloChannel = 0;
		Solo = 0;
		Go4kVSTi_ResetPatch();
		initialized = true;
	}
}

// reset synth

void Go4kVSTi_ClearInstrumentSlot(char channel, int slot)
{
	memset(SynthObj.InstrumentValues[channel][slot], 0, MAX_UNIT_SLOTS);
	for (int i = 0; i < MAX_POLYPHONY; i++)
	{
		float* w = &(SynthObj.InstrumentWork[channel*MAX_POLYPHONY+i].workspace[slot*MAX_UNIT_SLOTS]);
		memset(w, 0, MAX_UNIT_SLOTS*4);
	}
}

void Go4kVSTi_ClearInstrumentWorkspace(char channel)
{
	// clear workspace
	InstrumentWorkspace* w = &(SynthObj.InstrumentWork[channel*MAX_POLYPHONY]);
	memset(w, 0, sizeof(InstrumentWorkspace)*MAX_POLYPHONY);
}

void Go4kVSTi_ResetInstrument(char channel)
{
	char name[128];
	sprintf(name, "Instrument %d", channel+1);
	memcpy(SynthObj.InstrumentNames[channel], name, strlen(name));

	// clear values
	BYTE* v = SynthObj.InstrumentValues[channel][0];
	memset(v, 0, MAX_UNITS*MAX_UNIT_SLOTS);

	// clear workspace
	InstrumentWorkspace* w = &(SynthObj.InstrumentWork[channel*MAX_POLYPHONY]);
	memset(w, 0, sizeof(InstrumentWorkspace)*MAX_POLYPHONY);

	// set default units
	Go4kVSTi_InitSlot(SynthObj.InstrumentValues[channel][0], channel, M_ENV);
	Go4kVSTi_InitSlot(SynthObj.InstrumentValues[channel][1], channel, M_VCO);
	Go4kVSTi_InitSlot(SynthObj.InstrumentValues[channel][2], channel, M_FOP); ((FOP_valP)(SynthObj.InstrumentValues[channel][2]))->flags = FOP_MULP;
	Go4kVSTi_InitSlot(SynthObj.InstrumentValues[channel][3], channel, M_DLL);
	Go4kVSTi_InitSlot(SynthObj.InstrumentValues[channel][4], channel, M_PAN);
	Go4kVSTi_InitSlot(SynthObj.InstrumentValues[channel][5], channel, M_OUT);

	SynthObj.HighestSlotIndex[channel] = 5;
	SynthObj.InstrumentSignalValid[channel] = 1;
	SynthObj.SignalTrace[channel] = 0.0f;
	SynthObj.ControlInstrument[channel] = 0;
	SynthObj.VoiceIndex[channel] = 0;
	

	Go4kVSTi_ClearDelayLines();
}

void Go4kVSTi_ClearGlobalSlot(int slot)
{
	memset(SynthObj.GlobalValues[slot], 0, MAX_UNIT_SLOTS);
	float* w = &(SynthObj.GlobalWork.workspace[slot*MAX_UNIT_SLOTS]);
	memset(w, 0, MAX_UNIT_SLOTS*4);
}

void Go4kVSTi_ClearGlobalWorkspace()
{
	// clear workspace
	memset(&(SynthObj.GlobalWork), 0, sizeof(InstrumentWorkspace));
}

void Go4kVSTi_ResetGlobal()
{
	// clear values
	memset(SynthObj.GlobalValues, 0, MAX_UNITS*MAX_UNIT_SLOTS);

	// clear workspace
	memset(&(SynthObj.GlobalWork), 0, sizeof(InstrumentWorkspace));

	// set default units
	Go4kVSTi_InitSlot(SynthObj.GlobalValues[0], 16, M_ACC); ((ACC_valP)(SynthObj.GlobalValues[0]))->flags = ACC_AUX;
	Go4kVSTi_InitSlot(SynthObj.GlobalValues[1], 16, M_DLL); 
		((DLL_valP)(SynthObj.GlobalValues[1]))->reverb = 1; 
		((DLL_valP)(SynthObj.GlobalValues[1]))->leftreverb = 1; 
		((DLL_valP)(SynthObj.GlobalValues[1]))->feedback = 125; 
		((DLL_valP)(SynthObj.GlobalValues[1]))->pregain = 40; 
	Go4kVSTi_InitSlot(SynthObj.GlobalValues[2], 16, M_FOP); ((FOP_valP)(SynthObj.GlobalValues[2]))->flags = FOP_XCH;
	Go4kVSTi_InitSlot(SynthObj.GlobalValues[3], 16, M_DLL);
		((DLL_valP)(SynthObj.GlobalValues[3]))->reverb = 1; 
		((DLL_valP)(SynthObj.GlobalValues[3]))->leftreverb = 0; 
		((DLL_valP)(SynthObj.GlobalValues[3]))->feedback = 125; 
		((DLL_valP)(SynthObj.GlobalValues[3]))->pregain = 40; 
	Go4kVSTi_InitSlot(SynthObj.GlobalValues[4], 16, M_FOP); ((FOP_valP)(SynthObj.GlobalValues[4]))->flags = FOP_XCH;
	Go4kVSTi_InitSlot(SynthObj.GlobalValues[5], 16, M_ACC); 
	Go4kVSTi_InitSlot(SynthObj.GlobalValues[6], 16, M_FOP); ((FOP_valP)(SynthObj.GlobalValues[6]))->flags = FOP_ADDP2;
	Go4kVSTi_InitSlot(SynthObj.GlobalValues[7], 16, M_OUT); 

	SynthObj.HighestSlotIndex[16] = 7;
	SynthObj.GlobalSignalValid = 1;	

	Go4kVSTi_ClearDelayLines();
}

// reset synth
void Go4kVSTi_ResetPatch()
{
	for (int i = 0; i < MAX_INSTRUMENTS; i ++)
	{
		Go4kVSTi_ResetInstrument(i);
	}
	// reset global settings
	Go4kVSTi_ResetGlobal();

	SynthObj.Polyphony = 1;
}

void Go4kVSTi_FlipInstrumentSlots(char channel, int a, int b)
{
	int s = a;
	if (b > a)
		s = b;
	if (s >= SynthObj.HighestSlotIndex[channel])
		SynthObj.HighestSlotIndex[channel] = s;

	DWORD temp[MAX_UNIT_SLOTS];
	BYTE* v1 = SynthObj.InstrumentValues[channel][a];
	BYTE* v2 = SynthObj.InstrumentValues[channel][b];
	memcpy(temp, v2, MAX_UNIT_SLOTS);
	memcpy(v2, v1, MAX_UNIT_SLOTS);
	memcpy(v1, temp, MAX_UNIT_SLOTS);
	for (int i = 0; i < MAX_POLYPHONY; i++)
	{
		float* w1 = &(SynthObj.InstrumentWork[channel*MAX_POLYPHONY+i].workspace[a*MAX_UNIT_SLOTS]);
		float* w2 = &(SynthObj.InstrumentWork[channel*MAX_POLYPHONY+i].workspace[b*MAX_UNIT_SLOTS]);
		memcpy(temp, w2, MAX_UNIT_SLOTS*4);
		memcpy(w2, w1, MAX_UNIT_SLOTS*4);
		memcpy(w1, temp, MAX_UNIT_SLOTS*4);
	}
	// reset dll workspaces, they are invalid now
	if ((v1[0] == M_DLL || v1[0] == M_GLITCH) && (v2[0] == M_DLL || v2[0] == M_GLITCH))
	{
		Go4kVSTi_ClearDelayLines();
		Go4kVSTi_UpdateDelayTimes();
	}	
}

void Go4kVSTi_FlipGlobalSlots(int a, int b)
{
	int s = a;
	if (b > a)
		s = b;
	if (s >= SynthObj.HighestSlotIndex[16])
		SynthObj.HighestSlotIndex[16] = s;

	DWORD temp[MAX_UNIT_SLOTS];
	BYTE* v1 = SynthObj.GlobalValues[a];
	BYTE* v2 = SynthObj.GlobalValues[b];
	memcpy(temp, v2, MAX_UNIT_SLOTS);
	memcpy(v2, v1, MAX_UNIT_SLOTS);
	memcpy(v1, temp, MAX_UNIT_SLOTS);
	for (int i = 0; i < MAX_POLYPHONY; i++)
	{
		float* w1 = &(SynthObj.GlobalWork.workspace[a*MAX_UNIT_SLOTS]);
		float* w2 = &(SynthObj.GlobalWork.workspace[b*MAX_UNIT_SLOTS]);
		memcpy(temp, w2, MAX_UNIT_SLOTS*4);
		memcpy(w2, w1, MAX_UNIT_SLOTS*4);
		memcpy(w1, temp, MAX_UNIT_SLOTS*4);
	}
	// reset dll workspaces, they are invalid now
	if ((v1[0] == M_DLL || v1[0] == M_GLITCH) && (v2[0] == M_DLL || v2[0] == M_GLITCH))
	{
		Go4kVSTi_ClearDelayLines();
		Go4kVSTi_UpdateDelayTimes();
	}	
}

// init a unit slot
void Go4kVSTi_InitSlot(BYTE* slot, int channel, int type)
{
		// set default values
	slot[0] = type;
	if (type == M_ENV)
	{
		ENV_valP v = (ENV_valP)slot;
		v->attac		=  64;
		v->decay		=  64;
		v->sustain		=  64;
		v->release		=  64;
		v->gain			= 128;
	}
	if (type == M_VCO)
	{
		VCO_valP v = (VCO_valP)slot;
		v->transpose	=  64;
		v->detune		=  64;
		v->phaseofs		=   0;
		v->gain			=0x55;
		v->color		=  64;
		v->shape		=  64;
		v->gain			= 128;
		v->flags		= VCO_SINE;
	}
	if (type == M_VCF)
	{
		VCF_valP v = (VCF_valP)slot;
		v->freq			=  64;
		v->res			=  64;
		v->type			= VCF_LOWPASS;
	}
	if (type == M_DST)
	{
		DST_valP v = (DST_valP)slot;
		v->drive		=  64;
		v->snhfreq		= 128;
		v->stereo		=   0;
	}
	if (type == M_DLL)
	{
		DLL_valP v = (DLL_valP)slot;
		v->reverb		=   0;
		v->delay		=   0;
		v->count		=   1;
		v->pregain		=  64;
		v->dry			=  128;
		v->feedback		=  64;
		v->damp			=  64;
		v->guidelay		=  40;
		v->synctype		=   1;
		v->leftreverb	=   0;
		v->depth		=	0;
		v->freq			=	0;
	}
	if (type == M_FOP)
	{
		FOP_valP v = (FOP_valP)slot;
		v->flags		= FOP_MULP;
	}
	if (type == M_FST)
	{
		FST_valP v = (FST_valP)slot;
		v->amount		=  64;
		v->type			= FST_SET;
		v->dest_stack	= -1;
		v->dest_unit	= -1;
		v->dest_slot	= -1;
		v->dest_id		= -1;
	}
	if (type == M_PAN)
	{
		PAN_valP v = (PAN_valP)slot;
		v->panning		=  64;
	}
	if (type == M_OUT)
	{
		OUT_valP v = (OUT_valP)slot;
		v->gain			=  64;
		v->auxsend		=   0;
	}
	if (type == M_ACC)
	{
		ACC_valP v = (ACC_valP)slot;
		v->flags		= ACC_OUT;
	}
	if (type == M_FLD)
	{
		FLD_valP v = (FLD_valP)slot;
		v->value		=  64;
	}
	if (type == M_GLITCH)
	{
		GLITCH_valP v = (GLITCH_valP)slot;
		v->active		=   0;
		v->dry			=   0;
		v->dsize		=  64;
		v->dpitch		=  64;
		v->guidelay		=  40;
	}
}

// init a instrument slot
void Go4kVSTi_InitInstrumentSlot(char channel, int s, int type)
{
	if (s >= SynthObj.HighestSlotIndex[channel])
		SynthObj.HighestSlotIndex[channel] = s;
	// clear values and workspace
	Go4kVSTi_ClearInstrumentSlot(channel, s);
	// init with default values
	Go4kVSTi_InitSlot(SynthObj.InstrumentValues[channel][s], channel, type);
	if (type == M_DLL || type == M_GLITCH)
	{
		Go4kVSTi_ClearDelayLines();
		Go4kVSTi_UpdateDelayTimes();
	}	
}

// init a global slot
void Go4kVSTi_InitGlobalSlot(int s, int type)
{
	if (s >= SynthObj.HighestSlotIndex[16])
		SynthObj.HighestSlotIndex[16] = s;
	// clear values and workspace
	Go4kVSTi_ClearGlobalSlot(s);
	// init with default values
	Go4kVSTi_InitSlot(SynthObj.GlobalValues[s], 16, type);
	if (type == M_DLL || type == M_GLITCH)
	{
		Go4kVSTi_ClearDelayLines();
		Go4kVSTi_UpdateDelayTimes();
	}	
}

// panic
void Go4kVSTi_Panic()
{
	for (int i = 0; i < MAX_INSTRUMENTS; i++)
	{
		// clear workspace
		InstrumentWorkspace* w = &(SynthObj.InstrumentWork[i*MAX_POLYPHONY]);
		memset(w, 0, sizeof(InstrumentWorkspace)*MAX_POLYPHONY);
		SynthObj.SignalTrace[i] = 0.0f;
	}
	// clear workspace
	memset(&(SynthObj.GlobalWork), 0, sizeof(InstrumentWorkspace));
	Go4kVSTi_ClearDelayLines();
}

static float delayTimeFraction[33] = 
{	
	4.0f * (1.0f/32.0f) * (2.0f/3.0f),
	4.0f * (1.0f/32.0f),
	4.0f * (1.0f/32.0f) * (3.0f/2.0f),
	4.0f * (1.0f/16.0f) * (2.0f/3.0f),
	4.0f * (1.0f/16.0f),
	4.0f * (1.0f/16.0f) * (3.0f/2.0f),
	4.0f * (1.0f/8.0f) * (2.0f/3.0f),
	4.0f * (1.0f/8.0f),
	4.0f * (1.0f/8.0f) * (3.0f/2.0f),
	4.0f * (1.0f/4.0f) * (2.0f/3.0f),
	4.0f * (1.0f/4.0f),
	4.0f * (1.0f/4.0f) * (3.0f/2.0f),
	4.0f * (1.0f/2.0f) * (2.0f/3.0f),
	4.0f * (1.0f/2.0f),
	4.0f * (1.0f/2.0f) * (3.0f/2.0f),
	4.0f * (1.0f) * (2.0f/3.0f),
	4.0f * (1.0f),
	4.0f * (1.0f) * (3.0f/2.0f),
	4.0f * (2.0f) * (2.0f/3.0f),
	4.0f * (2.0f),
	4.0f * (2.0f) * (3.0f/2.0f),
	4.0f * (3.0f/8.0f),
	4.0f * (5.0f/8.0f),
	4.0f * (7.0f/8.0f),
	4.0f * (9.0f/8.0f),
	4.0f * (11.0f/8.0f),
	4.0f * (13.0f/8.0f),
	4.0f * (15.0f/8.0f),
	4.0f * (3.0f/4.0f),
	4.0f * (5.0f/4.0f),
	4.0f * (7.0f/4.0f),
	4.0f * (3.0f/2.0f),
	4.0f * (3.0f/2.0f),
};

void Go4kVSTi_UpdateDelayTimes()
{
	int delayindex = 17;
	for (int i = 0; i <= MAX_INSTRUMENTS; i++)
	{
		for (int u = 0; u < MAX_UNITS; u++)
		{
			DLL_valP v;
			if (i < MAX_INSTRUMENTS)
				v = (DLL_valP)(SynthObj.InstrumentValues[i][u]);
			else
				v = (DLL_valP)(SynthObj.GlobalValues[u]);

			if (v->id == M_DLL)
			{
				//DLL_valP v = (DLL_valP)(SynthObj.InstrumentValues[i][u]);
				if (v->reverb)
				{
					if (v->leftreverb)
					{
						v->delay = 1;
						v->count = 8;
					}
					else
					{
						v->delay = 9;
						v->count = 8;
					}
				}
				else
				{
					int delay;
					if (v->synctype == 2)
					{
						(&go4k_delay_times)[delayindex] = 0; // added for debug. doesnt hurt though
						v->delay = 0;
						v->count = 1;
					}
					else
					{
						if (v->synctype == 1)
						{
							float ftime;
							float quarterlength = 60.0f/Go4kVSTi_GetBPM();
							ftime = quarterlength*delayTimeFraction[v->guidelay>>2];
							delay = 44100.0f*ftime;
							if (delay >= 65536)
								delay = 65535;
						}
						else
						{
							delay = v->guidelay*16;
						}
						(&go4k_delay_times)[delayindex] = delay;
						v->delay = delayindex;
						v->count = 1;
					}
					delayindex++;
				}
			}
			
			if (v->id == M_GLITCH)
			{
				GLITCH_valP v2 = (GLITCH_valP)(v);
				int delay;
				float ftime;
				float quarterlength = 60.0f/Go4kVSTi_GetBPM();
				ftime = quarterlength*delayTimeFraction[v2->guidelay>>2];
				delay = 44100.0f*ftime*0.25; // slice time is in fractions per beat (therefore / 4)
				if (delay >= 65536)
					delay = 65535;
				(&go4k_delay_times)[delayindex] = delay;
				v2->delay = delayindex;
				
				delayindex++;
			}
		}
	}
}

// clear delay lines
void Go4kVSTi_ClearDelayLines()
{
	memset((&go4k_delay_buffer), 0, 16*16*((65536+5)*4));
}

// set global bpm
void Go4kVSTi_SetBPM(float bpm)
{
	BeatsPerMinute = bpm;
	LFO_NORMALIZE = bpm/(44100.0*60.0);
	Go4kVSTi_UpdateDelayTimes();
}

// get bpm
float Go4kVSTi_GetBPM()
{
	return BeatsPerMinute;
}

// enable solo mode for a single channel only
void Go4kVSTi_Solo(int channel, int solo)
{
	if (solo)
	{
		SoloChannel = channel;
		Solo = true;
	}
	else
	{
		Solo = false;
	}
}

// sample times tick the whole synth pipeline. results are left and right output sample

void Go4kVSTi_Tick(float *oleft, float *oright, int samples)
{
	if (recorder)
	{
		recorder->tick(samples);

		if (recorder->RecordingNoise)
		{
			// send a stayalive signal to the host
			for (int i = 0; i < samples; i++)
			{
				float signal = 0.03125*((float)(i & 255) / 128.0f - 1.0f);
				*oleft++ = signal;
				*oright++ = signal;
			}
			return;
		}
	}

	// do as many samples as requested
	int s = 0;
	while (s < samples)
	{
		float left=0.0f;
		float right=0.0f;
		
		go4k_delay_buffer_ofs = (DWORD)(&go4k_delay_buffer);
		// loop all instruments
		for (int i = 0; i < MAX_INSTRUMENTS; i++)
		{
			// solo mode and not the channel we want?
			if (Solo && i != SoloChannel)
			{
				// loop all voices and clear outputs
				for (int p = 0; p < SynthObj.Polyphony; p++)
				{
					InstrumentWorkspaceP iwork = &(SynthObj.InstrumentWork[i*MAX_POLYPHONY+p]);
					iwork->dlloutl = 0.0f;
					iwork->dlloutr = 0.0f;
					iwork->outl = 0.0f;
					iwork->outr = 0.0f;
				}
				// adjust delay index
				for (int s = 0; s < MAX_UNITS; s++)
				{		
					BYTE* val = SynthObj.InstrumentValues[i][s];
					if (val[0] == M_DLL || val[0] == M_GLITCH)
						go4k_delay_buffer_ofs += (5+65536)*4*SynthObj.Polyphony;
				}
				// go to next instrument
				continue;
			}
			// if the instrument signal stack is valid and we still got a signal from that instrument
			if (SynthObj.InstrumentSignalValid[i] && (fabs(SynthObj.SignalTrace[i]) > 0.00001f))
			{
				float sumSignals = 0.0f;
				// loop all voices
				for (int p = 0; p < SynthObj.Polyphony; p++)
				{
					InstrumentWorkspaceP iwork = &(SynthObj.InstrumentWork[i*MAX_POLYPHONY+p]);
					float *lwrk = iwork->workspace;
					DWORD inote = iwork->note;
					// loop each slot
					for (int s = 0; s <= SynthObj.HighestSlotIndex[i]; s++)
					{		
						BYTE* val = SynthObj.InstrumentValues[i][s];
						float *wrk = &(iwork->workspace[s*MAX_UNIT_SLOTS]);
						if (val[0] == M_FST)
						{
							FST_valP v = (FST_valP)val;
							// if a target slot is set
							if (v->dest_slot != -1)
							{
								InstrumentWorkspaceP mwork;
								int polyphonicStore = SynthObj.Polyphony;
								int stack = v->dest_stack;
								// local storage?
								if (stack == -1 || stack == i)
								{
									// only store the sample in the current workspace
									polyphonicStore = 1;
									mwork = iwork;
								}
								else if (stack == MAX_INSTRUMENTS)
									mwork = &(SynthObj.GlobalWork);
								else
									mwork = &(SynthObj.InstrumentWork[stack*MAX_POLYPHONY]);
								
								float* mdest = &(mwork->workspace[v->dest_unit*MAX_UNIT_SLOTS + v->dest_slot]);
								float amount = (2.0f*v->amount - 128.0f)*0.0078125f;
								int storetype = v->type;
								for (int stc = 0; stc < polyphonicStore; stc++)
								{	
									__asm
									{
										push	eax
										push	ebx
								
										mov		eax, mdest
										mov		ebx, storetype

										fld		amount
										fmul	st(0), st(1)
								
									//	test	ebx, FST_MUL
									//	jz		store_func_add
									//	fmul	dword ptr [eax] 
									//	jmp		store_func_set
									//store_func_add:
										test	ebx, FST_ADD
										jz		store_func_set
										fadd	dword ptr [eax] 
									store_func_set:
										fstp	dword ptr [eax]
									store_func_done:
										pop		ebx
										pop		eax
									}
									mdest += sizeof(InstrumentWorkspace)/4;
								}
								// remove signal on pop flag
								if (storetype & FST_POP)
								{
									_asm  fstp	st(0);
								}
							}
						}
						else
						{
							// only process if note active or dll unit
							if (val[0])
							{
								// set up and call synth core func
								__asm
								{
									pushad
									xor		eax, eax
									mov		esi, val
									lodsb
									mov		eax, dword ptr [SynthFuncs+eax*4]
									mov		ebx, inote
									mov		ecx, lwrk
									mov		ebp, wrk
									call	eax						
									popad
								}
							}
						}
					}
					// check for end of note
					DWORD envstate = *((BYTE*)(lwrk));
					if (envstate == ENV_STATE_OFF)
					{
						iwork->note = 0;
					}
					sumSignals += fabsf(iwork->outl) + fabsf(iwork->outr) + fabsf(iwork->dlloutl) + fabsf(iwork->dlloutr);
				}
				// update envelope follower only for non control instruments. (1s attack rate) for total instrument signal
				if (SynthObj.ControlInstrument[i])
					SynthObj.SignalTrace[i] = 1.0f;
				else
					SynthObj.SignalTrace[i] = sumSignals + 0.999977324f * ( SynthObj.SignalTrace[i] - sumSignals );	
			}
			// instrument stack invalid
			else
			{
				// adjust delay index
				for (int s = 0; s < MAX_UNITS; s++)
				{		
					BYTE* val = SynthObj.InstrumentValues[i][s];
					if (val[0] == M_DLL || val[0] == M_GLITCH)
						go4k_delay_buffer_ofs += (5+65536)*4*SynthObj.Polyphony;
				}
				// loop all voices
				for (int p = 0; p < SynthObj.Polyphony; p++)
				{
					InstrumentWorkspaceP iwork = &(SynthObj.InstrumentWork[i*MAX_POLYPHONY+p]);
					iwork->dlloutl = 0.0f;
					iwork->dlloutr = 0.0f;
					iwork->outl = 0.0f;
					iwork->outr = 0.0f;
				}
			}
		}
		// if the global stack is valid
		if (SynthObj.GlobalSignalValid)
		{
			InstrumentWorkspaceP gwork = &(SynthObj.GlobalWork);
			float *lwrk = gwork->workspace;
			DWORD gnote = 1;
			gwork->note = 1;
			// loop all global slots
			for (int s = 0; s <= SynthObj.HighestSlotIndex[16]; s++)
			{		
				BYTE* val = SynthObj.GlobalValues[s];
				float *wrk = &(lwrk[s*MAX_UNIT_SLOTS]);
				// manually accumulate signals
				float ACCL = 0.0f;
				float ACCR = 0.0f;
				if (val[0] == M_ACC)
				{
					ACC_valP av = (ACC_valP)val;
					if (av->flags == ACC_OUT)
					{
						for (int i = 0; i < MAX_INSTRUMENTS; i++)
						{
							for (int p = 0; p < SynthObj.Polyphony; p++)
							{
								ACCL += SynthObj.InstrumentWork[i*MAX_POLYPHONY+p].outl;
								ACCR += SynthObj.InstrumentWork[i*MAX_POLYPHONY+p].outr;
							}
						}
					}
					else
					{
						for (int i = 0; i < MAX_INSTRUMENTS; i++)
						{
							for (int p = 0; p < SynthObj.Polyphony; p++)
							{
								ACCL += SynthObj.InstrumentWork[i*MAX_POLYPHONY+p].dlloutl;
								ACCR += SynthObj.InstrumentWork[i*MAX_POLYPHONY+p].dlloutr;
							}
						}
					}
					// push the accumulated signals on the fp stack
					__asm
					{
						fld		ACCR
						fld		ACCL
					}
				}
				// no ACC unit, check store
				else if (val[0] == M_FST)
				{
					FST_valP v = (FST_valP)val;
					// if a target slot is set
					if (v->dest_slot != -1)
					{
						InstrumentWorkspaceP mwork;
						int polyphonicStore = SynthObj.Polyphony;
						int stack = v->dest_stack;
						// local storage?
						if (stack == -1 || stack == MAX_INSTRUMENTS)
						{
							// only store the sample in the current workspace
							polyphonicStore = 1;
							mwork = &(SynthObj.GlobalWork);
						}
						else
							mwork = &(SynthObj.InstrumentWork[stack*MAX_POLYPHONY]);
						
						float* mdest = &(mwork->workspace[v->dest_unit*MAX_UNIT_SLOTS + v->dest_slot]);
						float amount = (2.0f*v->amount - 128.0f)*0.0078125f;;
						int storetype = v->type;
						for (int stc = 0; stc < polyphonicStore; stc++)
						{
							__asm
							{
								push	eax
								push	ebx
								
								mov		eax, mdest
								mov		ebx, storetype

								fld		amount
								fmul	st(0), st(1)
								
							//	test	ebx, FST_MUL
							//	jz		gstore_func_add
							//	fmul	dword ptr [eax] 
							//	jmp		gstore_func_set
							//gstore_func_add:
								test	ebx, FST_ADD
								jz		gstore_func_set
								fadd	dword ptr [eax] 
							gstore_func_set:
								fstp	dword ptr [eax]
							gstore_func_done:
								pop		ebx
								pop		eax
							}
							mdest += sizeof(InstrumentWorkspace)/4;
						}
						// remove signal on pop flag
						if (storetype & FST_POP)
						{
							_asm  fstp	st(0);
						}
					}
				}
				// just call synth core func
				else
				{
					if (val[0])
					{
						__asm
						{
							pushad
							xor		eax, eax
							mov		esi, val
							lodsb
							mov		eax, dword ptr [SynthFuncs+eax*4]
							mov		ebx, gnote
							mov		ecx, lwrk
							mov		ebp, wrk
							call	eax						
							popad
						}
					}
				}
			}
			left = gwork->outl;
			right = gwork->outr;
		}

		// clip 
		if (left < -1.0f)
			left = -1.0f;
		if (left > 1.0f)
			left = 1.0f;
		if (right < -1.0f)
			right = -1.0f;
		if (right > 1.0f)
			right = 1.0f;

		*(oleft++) = left;
		*(oright++) = right;

		s++;
	} // end sample loop	
}

// add a voice with given parameters to synth
void Go4kVSTi_AddVoice(int channel, int note)
{
	if (recorder)
	{
		recorder->voiceAdd(channel, note);

		// no signals to synth when using recording noise
		if (recorder->RecordingNoise)
			return;
	}

	InstrumentWorkspaceP work,work2;
	work = &(SynthObj.InstrumentWork[channel*MAX_POLYPHONY+0]);
	work->release = 1;
	work2 = &(SynthObj.InstrumentWork[channel*MAX_POLYPHONY+1]);
	work2->release = 1;
	// filp worspace
	if (SynthObj.Polyphony > 1)
	{
		work = &(SynthObj.InstrumentWork[channel*MAX_POLYPHONY+SynthObj.VoiceIndex[channel]]);
		SynthObj.VoiceIndex[channel] = SynthObj.VoiceIndex[channel] ^ 0x1;
	}
	// add new note
	memset(work, 0, (2+MAX_UNITS*MAX_UNIT_SLOTS)*4);
	work->note = note;
	SynthObj.SignalTrace[channel] = 1.0f;
	// check if its a controll instrument which is played
	SynthObj.ControlInstrument[channel] = 1;
	for (int i = 0; i < MAX_UNITS; i++)
	{
		if (SynthObj.InstrumentValues[channel][i][0] == M_OUT)
		{
			SynthObj.ControlInstrument[channel] = 0;
			break;
		}
	}	
}

// stop a voice with given parameters in synth
void Go4kVSTi_StopVoice(int channel, int note)
{	
	// record song
	if (recorder)
	{
		recorder->voiceStop(channel, note);

		// no signals to synth when only using recording noise
		if (recorder->RecordingNoise)
			return;
	}

	InstrumentWorkspaceP work,work2;
	// release notes
	work = &(SynthObj.InstrumentWork[channel*MAX_POLYPHONY+0]);
	work->release = 1;
	work2 = &(SynthObj.InstrumentWork[channel*MAX_POLYPHONY+1]);
	work2->release = 1;
}

//----------------------------------------------------------------------------------------------------
// convenience functions for loading and storing patches and instruments
//----------------------------------------------------------------------------------------------------

void FlipSlotModulations(int stack, int unit1, int unit2)
{
	// look in all instruments if a store unit had its target on one of the changed units
	for (int i = 0; i <= MAX_INSTRUMENTS; i++)
	{
		BYTE* values;
		if (i < MAX_INSTRUMENTS)
			values = SynthObj.InstrumentValues[i][0];
		else
			values = SynthObj.GlobalValues[0];
		for (int u = 0; u < MAX_UNITS; u++)
		{
			if (values[u*MAX_UNIT_SLOTS+0] == M_FST)
			{
				FST_valP v = (FST_valP)(&values[u*MAX_UNIT_SLOTS+0]);
								
				int target_inst;
				if (v->dest_stack == -1)
					target_inst = i;
				else
					target_inst = v->dest_stack;

				// the store points to another stack, so continue
				if (target_inst != stack)
					continue;

				// a up/down process 
				if (unit2 != -1)
				{
					// if the target unit was unit1 or unit2 realign target unit
					if (v->dest_unit == unit1)
					{
						v->dest_unit = unit2;
					}
					else if (v->dest_unit == unit2)
					{
						v->dest_unit = unit1;
					}
				}
			}
		}
	}
}

// autoconvert 1.0 instrument stacks
bool Autoconvert10(int stack)
{
	// get desired stack
	BYTE* values;
	if (stack < MAX_INSTRUMENTS)
		values = SynthObj.InstrumentValues[stack][0];
	else
		values = SynthObj.GlobalValues[0];

	// replace the delay with the new one
	for (int u = 0; u < MAX_UNITS; u++)
	{
		if (values[u*MAX_UNIT_SLOTS+0] == M_DLL)
		{
			DLL10_val ov;
			memcpy(&ov, &values[u*MAX_UNIT_SLOTS+0], sizeof(DLL10_val));
			DLL_valP nv = (DLL_valP)(&values[u*MAX_UNIT_SLOTS+0]);
			nv->id = ov.id;
			nv->pregain = ov.pregain;
			nv->dry = ov.dry;
			nv->feedback = ov.feedback;
			nv->damp = ov.damp;
			nv->freq = 0;
			nv->depth = 0;
			nv->guidelay = ov.guidelay;
			nv->synctype = ov.synctype;
			nv->leftreverb = ov.leftreverb;
			nv->reverb = ov.reverb;
		}
	}
	return true;
}

// autoconvert 1.1 instrument stacks
bool Autoconvert11(int stack)
{
	// get desired stack
	BYTE* values;
	if (stack < MAX_INSTRUMENTS)
		values = SynthObj.InstrumentValues[stack][0];
	else
		values = SynthObj.GlobalValues[0];

	// replace the osc with the new one
	for (int u = 0; u < MAX_UNITS; u++)
	{
		if (values[u*MAX_UNIT_SLOTS+0] == M_VCO)
		{
			VCO11_val ov;
			memcpy(&ov, &values[u*MAX_UNIT_SLOTS+0], sizeof(VCO11_val));
			VCO_valP nv = (VCO_valP)(&values[u*MAX_UNIT_SLOTS+0]);
			nv->id = ov.id;
			nv->transpose = ov.transpose;
			nv->detune = ov.detune;
			nv->phaseofs = ov.phaseofs;
			nv->gate = 0x55;
			nv->color = ov.color;
			nv->shape = ov.shape;
			nv->gain = ov.gain;
			nv->flags = ov.flags;
		}
	}
	return true;
}

// autoconvert 1.3 instrument stacks
bool Autoconvert13(int stack)
{
	// get desired stack
	BYTE* values;
	if (stack < MAX_INSTRUMENTS)
		values = SynthObj.InstrumentValues[stack][0];
	else
		values = SynthObj.GlobalValues[0];

	// replace the osc with the new one
	for (int u = 0; u < MAX_UNITS; u++)
	{
		if (values[u*MAX_UNIT_SLOTS+0] == M_VCO)
		{
			VCO_valP nv = (VCO_valP)(&values[u*MAX_UNIT_SLOTS+0]);
			// correct sine color as it has a meaning now in 1.4 format
			if (nv->flags & VCO_SINE)
				nv->color = 128;
		}
	}
	return true;
}


// load patch data
void Go4kVSTi_LoadPatch(const char *filename)
{
	Go4kVSTi_ResetPatch();
	FILE *file = fopen(filename, "rb");
	if (file)
	{
		DWORD version;
		bool version10 = false;
		bool version11 = false;
		bool version12 = false;
		bool version13 = false;
		fread(&version, 1, 4, file);
		if (versiontag != version)
		{
			// version 1.3 file
			if (version == versiontag13)
			{
				// only mulp2 unit added and layout for instruments changed, no need for message
				//MessageBox(0,"Autoconvert. Please save file again", "1.3 File Format", MB_OK | MB_SETFOREGROUND);
				version13 = true;
			}
			// version 1.2 file
			else if (version == versiontag12)
			{
				// only fld unit added, no need for message
				//MessageBox(0,"Autoconvert. Please save file again", "1.2 File Format", MB_OK | MB_SETFOREGROUND);
				version12 = true;
				version13 = true;
			}
			// version 1.1 file
			else if (version == versiontag11)
			{
				MessageBox(0,"Autoconvert. Please save file again", "1.1 File Format", MB_OK | MB_SETFOREGROUND);
				version11 = true;
				version12 = true;
				version13 = true;
			}
			// version 1.0 file
			else if (version == versiontag10)
			{
				MessageBox(0,"Autoconvert. Please save file again", "1.0 File Format", MB_OK | MB_SETFOREGROUND);
				version10 = true;
				version11 = true;
				version12 = true;
				version13 = true;
			}
			// newer format than supported
			else
			{
				MessageBox(0,"The file was created with a newer version of 4klang.", "File Format Error", MB_ICONERROR | MB_SETFOREGROUND);
				fclose(file);
				return;
			}
		}

		// read data
		fread(&(SynthObj.Polyphony), 1, 4, file);
		fread(SynthObj.InstrumentNames, 1, MAX_INSTRUMENTS*64, file);
		for (int i=0; i<MAX_INSTRUMENTS; i++)
		{
			if (version13)
			{
				BYTE dummyBuf[16];
				for (int j = 0; j < 32; j++) // 1.3 format had 32 units
				{
					fread(SynthObj.InstrumentValues[i][j], 1, 16, file);  // 1.3 format had 32 unit slots, but not fully used
					fread(dummyBuf, 1, 16, file); // 1.3 read remaining block to dummy
				}
			}
			else
				fread(SynthObj.InstrumentValues[i], 1, MAX_UNITS*MAX_UNIT_SLOTS, file);
		}
		if (version13)
		{
			BYTE dummyBuf[16];
			for (int j = 0; j < 32; j++) // 1.3 format had 32 units
			{
				fread(SynthObj.GlobalValues[j], 1, 16, file);  // 1.3 format had 32 unit slots, but not fully used
				fread(dummyBuf, 1, 16, file); // 1.3 read remaining block to dummy
			}
		}
		else
			fread(SynthObj.GlobalValues, 1, MAX_UNITS*MAX_UNIT_SLOTS, file);
		fclose(file);

		// convert 1.0 file format
		if (version10)
		{
			// convert all instruments
			for (int i = 0; i < MAX_INSTRUMENTS; i++)
			{
				if (!Autoconvert10(i))
				{
					char errmsg[64];
					sprintf(errmsg, "Instrument %d could not be converted", i+1);
					MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
				}
			}
			// convert global
			if (!Autoconvert10(MAX_INSTRUMENTS))
			{
				char errmsg[64];
				sprintf(errmsg, "Global could not be converted");
				MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
			}
		}
		// convert 1.1 file format
		if (version11)
		{
			// convert all instruments
			for (int i = 0; i < MAX_INSTRUMENTS; i++)
			{
				if (!Autoconvert11(i))
				{
					char errmsg[64];
					sprintf(errmsg, "Instrument %d could not be converted", i+1);
					MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
				}
			}
			// convert global
			if (!Autoconvert11(MAX_INSTRUMENTS))
			{
				char errmsg[64];
				sprintf(errmsg, "Global could not be converted");
				MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
			}
		}
		// convert 1.2 file format
		if (version12)
		{
			// nothing to do, only fld unit added at the end
		}
		// version 1.3 file format
		if (version13)
		{
			// convert all instruments
			for (int i = 0; i < MAX_INSTRUMENTS; i++)
			{
				if (!Autoconvert13(i))
				{
					char errmsg[64];
					sprintf(errmsg, "Instrument %d could not be converted", i+1);
					MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
				}
			}
			// convert global
			if (!Autoconvert13(MAX_INSTRUMENTS))
			{
				char errmsg[64];
				sprintf(errmsg, "Global could not be converted");
				MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
			}
		}
	}
	Go4kVSTi_UpdateDelayTimes();
}

// save patch data
void Go4kVSTi_SavePatch(char *filename)
{
	FILE *file = fopen(filename, "wb");
	if (file)
	{
		fwrite(&versiontag, 1, 4, file);
		fwrite(&(SynthObj.Polyphony), 1, 4, file);
		fwrite(SynthObj.InstrumentNames, 1, MAX_INSTRUMENTS*64, file);
		fwrite(SynthObj.InstrumentValues, 1, MAX_INSTRUMENTS*MAX_UNITS*MAX_UNIT_SLOTS, file);
		fwrite(SynthObj.GlobalValues, 1, MAX_UNITS*MAX_UNIT_SLOTS, file);
		fclose(file);
	}
}

// load instrumen data to specified channel
void Go4kVSTi_LoadInstrument(char* filename, char channel)
{
	FILE *file = fopen(filename, "rb");
	if (file)
	{
		DWORD version;
		bool version10 = false;
		bool version11 = false;
		bool version12 = false;
		bool version13 = false;
		fread(&version, 1, 4, file);
		if (versiontag != version) // 4k10
		{
			// version 1.3 file
			if (version == versiontag13)
			{
				// only mulp2 unit added and layout for instruments changed, no need for message
				//MessageBox(0,"Autoconvert. Please save file again", "1.3 File Format", MB_OK | MB_SETFOREGROUND);
				version13 = true;
			}
			// version 1.2 file
			else if (version == versiontag12)
			{
				// only fld unit added, no need for message
				//MessageBox(0,"Autoconvert. Please save file again", "1.2 File Format", MB_OK | MB_SETFOREGROUND);
				version12 = true;
				version13 = true;
			}
			// version 1.1 file
			else if (version == versiontag11)
			{
				MessageBox(0,"Autoconvert. Please save file again", "1.1 File Format", MB_OK | MB_SETFOREGROUND);
				version11 = true;
				version12 = true;
				version13 = true;
			}
			// version 1.0 file
			else if (version == versiontag10)
			{
				MessageBox(0,"Autoconvert. Please save file again", "1.0 File Format", MB_OK | MB_SETFOREGROUND);
				version10 = true;
				version11 = true;
				version12 = true;
				version13 = true;
			}
			// newer format than supported
			else
			{
				MessageBox(0,"The file was created with a newer version of 4klang.", "File Format Error", MB_ICONERROR | MB_SETFOREGROUND);
				fclose(file);
				return;
			}
		}
		
		if (channel < 16)
		{
			Go4kVSTi_ResetInstrument(channel);
			fread(SynthObj.InstrumentNames[channel], 1, 64, file);
			if (version13)
			{
				BYTE dummyBuf[16];
				for (int j = 0; j < 32; j++) // 1.3 format had 32 units
				{
					fread(SynthObj.InstrumentValues[channel][j], 1, 16, file); // 1.3 format had 32 unit slots, but not fully used
					fread(dummyBuf, 1, 16, file); // 1.3 read remaining block to dummy
				}
			}
			else
				fread(SynthObj.InstrumentValues[channel], 1, MAX_UNITS*MAX_UNIT_SLOTS, file);
		}
		else
		{
			Go4kVSTi_ResetGlobal();
			// read the instrument name in a dummy buffer, as global section doesnt have an own name
			BYTE dummyNameBuf[64];
			fread(dummyNameBuf, 1, 64, file);
			if (version13)
			{
				BYTE dummyBuf[16];
				for (int j = 0; j < 32; j++) // 1.3 format had 32 units
				{
					fread(SynthObj.GlobalValues[j], 1, 16, file);  // 1.3 format had 32 unit slots, but not fully used
					fread(dummyBuf, 1, 16, file); // 1.3 read remaining block to dummy
				}
			}
			else
				fread(SynthObj.GlobalValues, 1, MAX_UNITS*MAX_UNIT_SLOTS, file);
		}
		fclose(file);
		// convert 1.0 file format
		if (version10)
		{
			// convert instruments
			if (channel < 16)
			{
				if (!Autoconvert10(channel))
				{
					char errmsg[64];
					sprintf(errmsg, "Instrument %d could not be converted", channel+1);
					MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
				}
			}
			// convert global
			else
			{
				if (!Autoconvert10(MAX_INSTRUMENTS))
				{
					char errmsg[64];
					sprintf(errmsg, "Global could not be converted");
					MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
				}
			}
		}
		// convert 1.1 file format
		if (version11)
		{
			// convert instruments
			if (channel < 16)
			{
				if (!Autoconvert11(channel))
				{
					char errmsg[64];
					sprintf(errmsg, "Instrument %d could not be converted", channel+1);
					MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
				}
			}
			// convert global
			else
			{
				if (!Autoconvert11(MAX_INSTRUMENTS))
				{
					char errmsg[64];
					sprintf(errmsg, "Global could not be converted");
					MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
				}
			}
		}
		// convert 1.2 file format
		if (version12)
		{
			// nothing to do, only fld unit added at the end
		}
		// version 1.3 file format
		if (version13)
		{
			// convert instruments
			if (channel < 16)
			{
				if (!Autoconvert13(channel))
				{
					char errmsg[64];
					sprintf(errmsg, "Instrument %d could not be converted", channel+1);
					MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
				}
			}
			// convert global
			else
			{
				if (!Autoconvert13(MAX_INSTRUMENTS))
				{
					char errmsg[64];
					sprintf(errmsg, "Global could not be converted");
					MessageBox(0, errmsg, "Error", MB_OK | MB_SETFOREGROUND);
				}
			}
		}
	}
	Go4kVSTi_UpdateDelayTimes();
}

// save instrument data from current channel
void Go4kVSTi_SaveInstrument(char* filename, char channel)
{
	FILE *file = fopen(filename, "wb");
	if (file)
	{
		fwrite(&versiontag, 1, 4, file);
		if (channel < 16)
		{
			fwrite(SynthObj.InstrumentNames[channel], 1, 64, file);
			fwrite(SynthObj.InstrumentValues[channel], 1, MAX_UNITS*MAX_UNIT_SLOTS, file);
		}
		else
		{
			// write a dummy name for global section as it doesnt have an own name
			fwrite("GlobalUnitsStoredAs.4ki                                         ", 1, 64, file);
			fwrite(SynthObj.GlobalValues, 1, MAX_UNITS*MAX_UNIT_SLOTS, file);
		}
		fclose(file);
	}
}

// load unit data into specified slot
void Go4kVSTi_LoadUnit(char* filename, BYTE* slot)
{
	FILE *file = fopen(filename, "rb");
	if (file)
	{
		fread(slot, 1, MAX_UNIT_SLOTS, file);
		fclose(file);
		if (slot[0] == M_DLL || slot[0] == M_GLITCH)
		{
			Go4kVSTi_ClearDelayLines();
			Go4kVSTi_UpdateDelayTimes();
		}
	}
}

// save unit date from specified slot
void Go4kVSTi_SaveUnit(char* filename, BYTE* slot)
{
	FILE *file = fopen(filename, "wb");
	if (file)
	{
		fwrite(slot, 1, MAX_UNIT_SLOTS, file);
		fclose(file);
	}
}

SynthObjectP Go4kVSTi_GetSynthObject()
{
	return &SynthObj;
}

////////////////////////////////////////////////////////////////////////////
// 
//	Synth input processing 
//
////////////////////////////////////////////////////////////////////////////

void Go4kVSTi_RecordBegin(bool recordingNoise, int patternsize, float patternquant)
{
	if (recorder)
		return;

	recorder.reset(new Recorder(SynthObj, recordingNoise, BeatsPerMinute, patternsize, patternquant));
}

void Go4kVSTi_RecordEndAndSave(const char* filename, int useenvlevels, int useenotevalues, int clipoutput, int undenormalize, int objformat, int output16)
{
	if (!recorder)
		return;

	Recorder::finishAndSaveByteStream(std::move(recorder), filename, useenvlevels, useenotevalues, clipoutput, undenormalize, objformat, output16);
}

void Go4kVSTi_RecordAbort()
{
	if (!recorder)
		return;

	Recorder::finishAndSaveByteStream(std::move(recorder), nullptr, 0, 0, 0, 0, 0, 0);
}