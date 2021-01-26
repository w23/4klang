#pragma once

#include "Go4kVSTiCore.h"

#include <memory>
#include <vector>

// stream structures for recording sound
class Recorder {
public:
	Recorder(const SynthObject &synthObj, bool recordingNoise, float bpm, int patternsize, float patternquant);

	void tick(int samples);
	void voiceAdd(int channel, int note);
	void voiceStop(int channel, int note);

	static void finishAndSaveByteStream(std::unique_ptr<Recorder> && recorder, const char* filename, int useenvlevels, int useenotevalues, int clipoutput, int undenormalize, int objformat, int output16);

	const bool RecordingNoise;

private:
	void end();
	void write(const char* filename, int useenvlevels, int useenotevalues, int clipoutput, int undenormalize, int objformat, int output16);

private:
	const SynthObject &SynthObj;
	DWORD samplesProcessed = 0;
	const float BeatsPerMinute;
	// set first recording event variable to true to enable start time reset on the first occuring event
	bool FirstRecordingEvent = true;
	const DWORD PatternSize;
	const float SamplesPerTick;
	const float TickScaler;
	DWORD MaxTicks = 0;
	int InstrumentOn[MAX_INSTRUMENTS];
	std::vector<int> InstrumentRecord[MAX_INSTRUMENTS];
	std::vector<int> ReducedPatterns;
	std::vector<int> PatternIndices[MAX_INSTRUMENTS];
	int NumReducedPatterns = 0;
};
