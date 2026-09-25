// Copyright 2026 Rubato Audio.
//
// Timestamps at fixed points of one audio block, for the private scene-check
// firmware (plaits/scene_check.h), which reports the time between successive
// marks. Compiled out of every other build.

#ifndef PLAITS_SECTION_MARKS_H_
#define PLAITS_SECTION_MARKS_H_

namespace plaits {

// In block order. Section k runs from mark k (0 = callback entry) to mark
// k + 1 (the last one ends when Voice::Render returns).
enum SectionMark {
  SECTION_MARK_UI_INPUTS = 1,    // pots and CVs read
  SECTION_MARK_UI_TASK,          // the round-robin UI task done
  SECTION_MARK_UI_END,           // ADC restart, pitch range: Ui::Poll returns
  SECTION_MARK_VOICE_ENTRY,      // Voice::Render entered
  SECTION_MARK_ENGINE_SELECTED,  // trigger, engine choice, engine reload
  SECTION_MARK_ENVELOPES,        // decay and articulation envelopes
  SECTION_MARK_ENGINE_START,     // modulation and randomizer: engine starts
  SECTION_MARK_ENGINE_END,       // engine rendered
  SECTION_MARK_SUBOSC,           // sub-oscillator and AUX crossfade
  SECTION_MARK_LPG_PARAMS,       // outer LPG envelope
  SECTION_MARK_OUT_WRITTEN,      // OUT post-processed and written
  SECTION_MARK_COUNT             // = the number of sections
};

}  // namespace plaits

#if PLAITS_SCENE_CHECK
extern "C" void plaits_section_mark(int mark);
// SECTION_MARK_UI_TASK, also naming which of Ui::Poll's four round-robin
// tasks ran, so the report can time each one.
extern "C" void plaits_ui_task_mark(int task);
// Probe points inside one function (0 = its entry): the report gives the time
// from SECTION_MARK_UI_INPUTS to probe 0, and between successive probes.
extern "C" void plaits_probe_mark(int probe);
#define PLAITS_SECTION_MARK(mark) plaits_section_mark(plaits::mark)
#define PLAITS_UI_TASK_MARK(task) plaits_ui_task_mark(task)
#define PLAITS_PROBE_MARK(probe) plaits_probe_mark(probe)
#else
#define PLAITS_SECTION_MARK(mark) do { } while (0)
#define PLAITS_UI_TASK_MARK(task) do { } while (0)
#define PLAITS_PROBE_MARK(probe) do { } while (0)
#endif

#endif  // PLAITS_SECTION_MARKS_H_
