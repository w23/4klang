#include "Export.h"
#include "Go4kVSTiCore.h"

#include <pugixml.hpp>
#include <unordered_map>
#include <stdio.h>

struct RnsInstrument {
	int midi_channel;
};

enum Note {
	NOTE_OFF = -1,
	NOTE_EMPTY = -2,
	NOTE_ERROR = -3,
};

struct RnsNote {
	int note = NOTE_EMPTY;
	int instrument;
};

struct RnsTrack {
	std::vector<RnsNote> notes;
};

struct RnsPattern {
	int lines;
	std::vector<RnsTrack> tracks;
};

static int rnsParseNote(const pugi::char_t* note) {
	if (strcmp(note, "OFF") == 0)
		return NOTE_OFF;
	const int len = strlen(note);
	if (len != 3)
		return NOTE_ERROR;
	int ret = 0;
	switch (note[0]) {
	case 'C': ret = 0; break;
	case 'D': ret = 2; break;
	case 'E': ret = 4; break;
	case 'F': ret = 5; break;
	case 'G': ret = 7; break;
	case 'A': ret = 9; break;
	case 'B': ret = 11; break;
	default: return NOTE_ERROR;
	}
	ret += note[1] == '#' ? 1 : 0;
	ret += (note[2] - '0') * 12;
	return ret;
}

int main(int argc, const char* argv[])
{
	if (argc < 4)
	{
		fprintf(stderr, "Usage: %s Song.xml patch.4kp output.inc\n", argv[0]);
		return 1;
	}

	const char* renoise_file = argv[1];
	const char* patch_file = argv[2];
	const char* output_file = argv[3];
	fprintf(stderr, "Reading renoise song file %s\n", renoise_file);

	SynthObjectP SynthObjP = Go4kVSTi_GetSynthObject();
	for (int i = 0; i < MAX_INSTRUMENTS; i++)
	{
		SynthObjP->InstrumentSignalValid[i] = 0;
	}
	SynthObjP->GlobalSignalValid = 0;
	Go4kVSTi_LoadPatch(patch_file);
	//instrument check
	for (int i = 0; i < MAX_INSTRUMENTS; i++)
	{
		// try setting up instrument links
		if (i > 0)
		{
			for (int j = 0; j < i; j++)
			{
				int linkToInstrument = j;
				// compare instruments
				for (int u = 0; u < MAX_UNITS; u++)
				{
					// special case, compare manually
					if (SynthObjP->InstrumentValues[i][u][0] == M_DLL)
					{
						DLL_valP ds = (DLL_valP)SynthObjP->InstrumentValues[i][u];
						DLL_valP dl = (DLL_valP)SynthObjP->InstrumentValues[j][u];
						if (ds->pregain != dl->pregain ||
							ds->dry != dl->dry ||
							ds->feedback != dl->feedback ||
							ds->damp != dl->damp ||
							ds->freq != dl->freq ||
							ds->depth != dl->depth ||
							ds->guidelay != dl->guidelay ||
							ds->synctype != dl->synctype ||
							ds->leftreverb != dl->leftreverb ||
							ds->reverb != dl->reverb)
						{
							linkToInstrument = 16;
							break;
						}										
					}
					else if (SynthObjP->InstrumentValues[i][u][0] == M_GLITCH)
					{
						GLITCH_valP ds = (GLITCH_valP)SynthObjP->InstrumentValues[i][u];
						GLITCH_valP dl = (GLITCH_valP)SynthObjP->InstrumentValues[j][u];
						if (ds->active != dl->active ||
							ds->dry != dl->dry ||
							ds->dsize != dl->dsize ||
							ds->dpitch != dl->dpitch ||
							ds->guidelay != dl->guidelay
							)
						{
							linkToInstrument = 16;
							break;
						}
					}
					else
					{
						if (memcmp(SynthObjP->InstrumentValues[i][u], SynthObjP->InstrumentValues[j][u], MAX_UNIT_SLOTS))
						{
							linkToInstrument = 16;
							break;
						}
					}										
				}
				// set link
				if (linkToInstrument != 16)
				{
					break;
				}
			}
		}
	}

	pugi::xml_document song;
	pugi::xml_parse_result result = song.load_file(renoise_file);
	if (!result)
	{
		fprintf(stderr, "Error parsing song file\n");
		return 1;
	}

	const auto &song_data = song.child("RenoiseSong");
	const auto &global_song_data = song_data.child("GlobalSongData");

	const float bpm = std::stof(global_song_data.child_value("BeatsPerMin"));
	fprintf(stderr, "BPM: %f\n", bpm);
	
	const auto& xinstruments = song_data.child("Instruments").children();
	std::vector<RnsInstrument> instruments;
	for (const auto& xinstr : xinstruments) {
		const auto& name = xinstr.child_value("Name");
		const auto& xplugin_props = xinstr.child("PluginProperties");
		if (!xplugin_props) {
			instruments.push_back({ -1 });
			continue;
		}
		const int midi_channel = std::stoi(xplugin_props.child_value("Channel"));

		/* TODO: verify that it's 4klang
		const int alias_index = std::stoi(xplugin_props.child_value("AliasInstrumentIndex"));
		if (alias_index >= 0) {
		} else {
		}
		*/

		// TODO: read embedded 4klang chunk

		fprintf(stderr, "Instrument %d: %s, midi_channel=%d\n", (int)instruments.size(), name, midi_channel);
		instruments.push_back({ midi_channel });
	}

	std::vector<RnsPattern> patterns;
	for (const auto& xpattern : song_data.child("PatternPool").child("Patterns").children()) {
		const int lines = std::stoi(xpattern.child_value("NumberOfLines"));
		std::vector<RnsTrack> tracks;
		int instrument_index = -1;
		for (const auto& xtrack : xpattern.child("Tracks").children("PatternTrack")) {
			RnsTrack track;
			track.notes.resize(lines);
			for (const auto& xline : xtrack.child("Lines").children()) {
				const int line = xline.attribute("index").as_int();
				if (line >= lines) {
					fprintf(stderr, "Line %d is out of bounds %d\n", line, lines);
					continue;
				}
				int column = 0;
				for (const auto& xcolm : xline.child("NoteColumns").children("NoteColumn")) {
					if (column > 0) {
						fprintf(stderr, "Only a single note column is supported for now\n");
						continue;
					}
					const auto xnote = xcolm.child_value("Note");
					const int note = rnsParseNote(xnote);
					if (note < -1) {
						fprintf(stderr, "Note %s is not valid\n", xnote);
						return 3;
					}
					instrument_index = note >= 0 ? std::stoi(xcolm.child_value("Instrument")) : -1;
					track.notes[line] = { note, instrument_index };
					//fprintf(stderr, "%d: col=%d inst=%d note=%d(%s)\n", index, column, instrument_index, note, xnote);
					++column;
				}
			}
			tracks.emplace_back(std::move(track));
		}
		patterns.emplace_back(RnsPattern{ lines, std::move(tracks) });
	}

	std::unique_ptr<Recorder> recorder{ new Recorder(*Go4kVSTi_GetSynthObject(), false, bpm, 16, 2.0f) };

	const auto& xpattern_sequence = song_data.child("PatternSequence").child("PatternSequence").children("Pattern");
	//std::unordered_map<std::pair<int, int>, int> trk_col_instr;

	std::vector<RnsNote> track_instrument(patterns[0].tracks.size(), { NOTE_EMPTY, -1 });
	for (const auto& xpat_index : xpattern_sequence) {
		const int pattern_index = std::stoi(xpat_index.child_value());
		fprintf(stderr, "Pattern %d\n", pattern_index);

		const RnsPattern& pat = patterns[pattern_index];
		for (int line = 0; line < pat.lines; ++line) {
			for (int trk = 0; trk < pat.tracks.size(); ++trk) {
				const RnsTrack& track = pat.tracks[trk];
				const RnsNote& note = track.notes[line];
				if (note.note == NOTE_EMPTY)
					continue;

				if (track_instrument[trk].note != NOTE_EMPTY) {
					//fprintf(stderr, "voiceStop(%d, %d)\n", track_instrument[trk].instrument, track_instrument[trk].note);
					recorder->voiceStop(track_instrument[trk].instrument, track_instrument[trk].note);
					track_instrument[trk] = { NOTE_EMPTY, -1 };
				}

				if (note.note != NOTE_OFF) {
					const int midi_channel = instruments.at(note.instrument).midi_channel;
					track_instrument[trk] = { note.note, midi_channel };
					//fprintf(stderr, "voiceAdd(%d, %d)\n", midi_channel, note.note);
					recorder->voiceAdd(midi_channel, note.note);
				}
			}
			recorder->tick(44100.f * 60.f / (bpm * 2));
		}
	}

	const int useenvlevels = 0;
	const int useenotevalues = 0;
	const int clipoutput = 1;
	const int undenormalize = 0;
	const int objformat = 0;
	const int output16 = 0;
	Recorder::finishAndSaveByteStream(std::move(recorder), output_file, useenvlevels, useenotevalues, clipoutput, undenormalize, objformat, output16);

	return 0;
}