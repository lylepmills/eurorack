// Copyright 2026 Matt Policastro.
// SPDX-License-Identifier: MIT
//
// The Rack side: Plaits' controls and jacks around LabVoice, which renders
// 12-sample blocks at 48 kHz that are resampled to Rack's rate, as VCV's own
// Plaits port does.

#include <rack.hpp>

#include <vector>

#include "lab_voice.h"

// Engine-specific names belong in tooltips; panel labels stay familiar.
#ifndef PLAITS_LAB_LABEL_HARMONICS
#define PLAITS_LAB_LABEL_HARMONICS "Harmonics"
#endif
#ifndef PLAITS_LAB_LABEL_TIMBRE
#define PLAITS_LAB_LABEL_TIMBRE "Timbre"
#endif
#ifndef PLAITS_LAB_LABEL_MORPH
#define PLAITS_LAB_LABEL_MORPH "Morph"
#endif
#ifndef PLAITS_LAB_LABEL_MACRO
#define PLAITS_LAB_LABEL_MACRO "Twist"
#endif
#ifndef PLAITS_LAB_DESCRIPTION_HARMONICS
#define PLAITS_LAB_DESCRIPTION_HARMONICS ""
#endif
#ifndef PLAITS_LAB_DESCRIPTION_TIMBRE
#define PLAITS_LAB_DESCRIPTION_TIMBRE ""
#endif
#ifndef PLAITS_LAB_DESCRIPTION_MORPH
#define PLAITS_LAB_DESCRIPTION_MORPH ""
#endif
#ifndef PLAITS_LAB_DESCRIPTION_MACRO
#define PLAITS_LAB_DESCRIPTION_MACRO ""
#endif

using namespace rack;
namespace lab = plaits_lab_vcv;

Plugin* pluginInstance;

static std::string ControlName(const std::string& panel, const std::string& engine) {
  return panel == engine ? panel : panel + ": " + engine;
}

struct PlaitsLabEngine : Module {
  enum ParamId {
    FREQ_PARAM,
    HARMONICS_PARAM,
    TIMBRE_PARAM,
    MORPH_PARAM,
    MACRO_PARAM,
    TIMBRE_CV_PARAM,
    FREQ_CV_PARAM,
    MORPH_CV_PARAM,
    LPG_COLOUR_PARAM,
    LPG_DECAY_PARAM,
    PLAITS_LPG_PARAM,
    PARAMS_LEN
  };
  enum InputId {
    TIMBRE_INPUT,
    FREQ_INPUT,
    MORPH_INPUT,
    HARMONICS_INPUT,
    TRIGGER_INPUT,
    LEVEL_INPUT,
    NOTE_INPUT,
    INPUTS_LEN
  };
  enum OutputId { OUT_OUTPUT, AUX_OUTPUT, OUTPUTS_LEN };

  lab::LabVoice voice;
  dsp::SampleRateConverter<2> converter;
  dsp::DoubleRingBuffer<dsp::Frame<2>, 256> buffer;
  std::string patchEngineId = PLAITS_LAB_ENGINE_ID;
  std::string patchEngineName = lab::LabVoice::name();

  bool engineChanged() const { return patchEngineId != PLAITS_LAB_ENGINE_ID; }

  void acceptCurrentEngine() {
    patchEngineId = PLAITS_LAB_ENGINE_ID;
    patchEngineName = lab::LabVoice::name();
  }

  json_t* dataToJson() override {
    json_t* root = json_object();
    // Preserve a mismatch across saves until the author acknowledges it.
    json_object_set_new(root, "engineId", json_string(patchEngineId.c_str()));
    json_object_set_new(root, "engineName", json_string(patchEngineName.c_str()));
    return root;
  }

  void dataFromJson(json_t* root) override {
    acceptCurrentEngine();
    const char* id = json_string_value(json_object_get(root, "engineId"));
    const char* name = json_string_value(json_object_get(root, "engineName"));
    // Older patches did not save an identity; keep them usable.
    if (id && *id) {
      patchEngineId = id;
      patchEngineName = name && *name ? name : id;
    }
  }

  PlaitsLabEngine() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN);
    configParam(FREQ_PARAM, -4.f, 4.f, 0.f, "Frequency", " semitones", 0.f, 12.f);
    configParam(HARMONICS_PARAM, 0.f, 1.f, 0.5f, ControlName("Harmonics", PLAITS_LAB_LABEL_HARMONICS), "%", 0.f, 100.f)
        ->description = PLAITS_LAB_DESCRIPTION_HARMONICS;
    configParam(TIMBRE_PARAM, 0.f, 1.f, 0.5f, ControlName("Timbre", PLAITS_LAB_LABEL_TIMBRE), "%", 0.f, 100.f)
        ->description = PLAITS_LAB_DESCRIPTION_TIMBRE;
    configParam(MORPH_PARAM, 0.f, 1.f, 0.5f, ControlName("Morph", PLAITS_LAB_LABEL_MORPH), "%", 0.f, 100.f)
        ->description = PLAITS_LAB_DESCRIPTION_MORPH;
    configParam(MACRO_PARAM, 0.f, 1.f, 0.5f, ControlName("Twist", PLAITS_LAB_LABEL_MACRO), "%", 0.f, 100.f)
        ->description = PLAITS_LAB_DESCRIPTION_MACRO;
    configParam(TIMBRE_CV_PARAM, -1.f, 1.f, 0.f, "Timbre CV amount", "%", 0.f, 100.f);
    configParam(FREQ_CV_PARAM, -1.f, 1.f, 0.f, "FM amount", "%", 0.f, 100.f);
    configParam(MORPH_CV_PARAM, -1.f, 1.f, 0.f, "Morph CV amount", "%", 0.f, 100.f);
    // Hidden settings, as on the hardware: see the widget's context menu.
    configParam(LPG_COLOUR_PARAM, 0.f, 1.f, 0.5f, "Lowpass gate response", "%", 0.f, 100.f)
        ->description = "From VCA (CCW) to low-pass filter (CW).";
    configParam(LPG_DECAY_PARAM, 0.f, 1.f, 0.5f, "Lowpass gate decay", "%", 0.f, 100.f);
    configSwitch(PLAITS_LPG_PARAM, 0.f, 1.f, 0.f, "With TRIG patched", {"Engine's own envelope", "Plaits LPG"});
    configInput(TIMBRE_INPUT, ControlName("Timbre", PLAITS_LAB_LABEL_TIMBRE));
    configInput(FREQ_INPUT, "FM");
    configInput(MORPH_INPUT, ControlName("Morph", PLAITS_LAB_LABEL_MORPH));
    configInput(HARMONICS_INPUT, ControlName("Harmonics", PLAITS_LAB_LABEL_HARMONICS));
    configInput(TRIGGER_INPUT, "Trigger");
    configInput(LEVEL_INPUT, "Level");
    configInput(NOTE_INPUT, "Pitch (1V/oct)");
    configOutput(OUT_OUTPUT, "Main");
    configOutput(AUX_OUTPUT, "Auxiliary");
    converter.setChannels(2);
  }

  void process(const ProcessArgs& args) override {
    if (buffer.empty()) {
      lab::Controls c;
      c.note = 60.f + params[FREQ_PARAM].getValue() * 12.f;
      c.harmonics = params[HARMONICS_PARAM].getValue();
      c.timbre = params[TIMBRE_PARAM].getValue();
      c.morph = params[MORPH_PARAM].getValue();
      c.macro = params[MACRO_PARAM].getValue();
      c.frequency_modulation_amount = params[FREQ_CV_PARAM].getValue();
      c.timbre_modulation_amount = params[TIMBRE_CV_PARAM].getValue();
      c.morph_modulation_amount = params[MORPH_CV_PARAM].getValue();
      c.lpg_colour = params[LPG_COLOUR_PARAM].getValue();
      c.decay = params[LPG_DECAY_PARAM].getValue();
      c.plaits_lpg = params[PLAITS_LPG_PARAM].getValue() > 0.5f;

      lab::Inputs in;
      in.v_oct = inputs[NOTE_INPUT].getVoltage();
      in.fm = inputs[FREQ_INPUT].getVoltage();
      in.harmonics = inputs[HARMONICS_INPUT].getVoltage();
      in.timbre = inputs[TIMBRE_INPUT].getVoltage();
      in.morph = inputs[MORPH_INPUT].getVoltage();
      in.trigger = inputs[TRIGGER_INPUT].getVoltage();
      in.level = inputs[LEVEL_INPUT].getVoltage();
      in.fm_patched = inputs[FREQ_INPUT].isConnected();
      in.timbre_patched = inputs[TIMBRE_INPUT].isConnected();
      in.morph_patched = inputs[MORPH_INPUT].isConnected();
      in.trigger_patched = inputs[TRIGGER_INPUT].isConnected();
      in.level_patched = inputs[LEVEL_INPUT].isConnected();

      float out[lab::kBlockSize], aux[lab::kBlockSize];
      voice.Render(c, in, out, aux);
      dsp::Frame<2> frames[lab::kBlockSize];
      for (size_t i = 0; i < lab::kBlockSize; i++) {
        frames[i].samples[0] = out[i];
        frames[i].samples[1] = aux[i];
      }
      converter.setRates(48000, (int) args.sampleRate);
      int in_length = lab::kBlockSize;
      int out_length = buffer.capacity();
      converter.process(frames, &in_length, buffer.endData(), &out_length);
      buffer.endIncr(out_length);
    }
    if (!buffer.empty()) {
      dsp::Frame<2> frame = buffer.shift();
      outputs[OUT_OUTPUT].setVoltage(frame.samples[0]);
      outputs[AUX_OUTPUT].setVoltage(frame.samples[1]);
    }
  }
};

// Rack's SVG renderer draws no text, so the labels are drawn here.
struct PanelLabels : TransparentWidget {
  struct Label {
    Label(std::string text, float x, float y, float size, bool light,
          std::string lpg_text = "", float max_width_mm = 0.f, bool engine_status = false)
        : text(text), x(x), y(y), size(size), light(light), lpg_text(lpg_text), max_width_mm(max_width_mm), engine_status(engine_status) {}
    std::string text;
    float x, y, size;
    bool light;
    std::string lpg_text;  // shown instead while editing the LPG, if set
    float max_width_mm;
    bool engine_status;
  };
  std::vector<Label> labels;
  const bool* lpg_mode = NULL;
  const PlaitsLabEngine* module = NULL;

  void draw(const DrawArgs& args) override {
    std::shared_ptr<window::Font> font = APP->window->uiFont;
    if (!font || font->handle < 0) return;
    nvgFontFaceId(args.vg, font->handle);
    nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    const bool editing = lpg_mode && *lpg_mode;
    for (const Label& label : labels) {
      const bool swapped = editing && !label.lpg_text.empty();
      const bool mismatch = label.engine_status && module && module->engineChanged();
      nvgFontSize(args.vg, label.size);
      const std::string text = mismatch ? "ENGINE CHANGED" : swapped ? label.lpg_text : label.text;
      if (label.max_width_mm > 0.f) {
        const float width = nvgTextBounds(args.vg, 0, 0, text.c_str(), NULL, NULL);
        const float available = mm2px(label.max_width_mm);
        if (width > available) nvgFontSize(args.vg, label.size * available / width);
      }
      nvgFillColor(args.vg, mismatch ? nvgRGB(0xa5, 0x52, 0x14) : swapped ? nvgRGB(0x2a, 0x5d, 0xb0)
          : label.light ? nvgRGB(0xf2, 0xf0, 0xea) : nvgRGB(0x2b, 0x2b, 0x2b));
      Vec p = mm2px(Vec(label.x, label.y));
      nvgText(args.vg, p.x, p.y, text.c_str(), NULL);
    }
  }
};

struct PlaitsLabEngineWidget : ModuleWidget {
  // Plaits' button-hold settings: TIMBRE and MORPH become the LPG's
  // response and decay while editing.
  bool lpg_mode = false;

  PlaitsLabEngineWidget(PlaitsLabEngine* module) {
    typedef PlaitsLabEngine M;
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/PlaitsLabEngine.svg")));

    PanelLabels* labels = new PanelLabels;
    labels->box.size = box.size;
    labels->labels = {
      {"plaits lab", 30.48f, 5.4f, 12.f, false, "", 39.f, true},
      {lab::LabVoice::name(), 30.48f, 15.2f, 11.f, false, "", 53.f},
      {"FREQUENCY", 11.94f, 42.3f, 8.f, false},
      {"HARMONICS", 48.11f, 42.3f, 8.f, false},
      {"TIMBRE", 10.76f, 67.6f, 8.f, false, "LPG RESPONSE"},
      {"TWIST", 30.48f, 42.3f, 7.f, false},
      {"MORPH", 49.44f, 67.6f, 8.f, false, "LPG DECAY"},
      {"FM", 30.25f, 72.8f, 8.f, false},
      {"TIMBRE", 18.77f, 91.0f, 6.f, false},
      {"FM", 30.22f, 91.0f, 6.f, false},
      {"MORPH", 41.67f, 91.0f, 6.f, false},
      {"HARMO", 53.11f, 91.0f, 6.f, false},
      {"TRIG", 7.33f, 105.4f, 7.5f, false},
      {"LEVEL", 18.77f, 105.4f, 7.5f, false},
      {"V/OCT", 30.22f, 105.4f, 7.5f, false},
      {"OUT", 41.67f, 105.4f, 7.5f, true},
      {"AUX", 53.11f, 105.4f, 7.5f, true},
      {"TEST BENCH", 30.48f, 124.3f, 6.f, false},
    };
    labels->lpg_mode = &lpg_mode;
    labels->module = module;
    addChild(labels);

    addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    // Hardware control centres, rounded to 0.01 mm. The original panel artwork
    // is not bundled; the matching physical arrangement is drawn independently.
    addParam(createParamCentered<Rogan3PSWhite>(mm2px(Vec(11.94, 28.99)), module, M::FREQ_PARAM));
    addParam(createParamCentered<Rogan3PSWhite>(mm2px(Vec(48.11, 28.99)), module, M::HARMONICS_PARAM));
    addParam(createParamCentered<Rogan1PSWhite>(mm2px(Vec(10.76, 56.38)), module, M::TIMBRE_PARAM));
    addParam(createParamCentered<Trimpot>(mm2px(Vec(30.48, 49.5)), module, M::MACRO_PARAM));
    addParam(createParamCentered<Rogan1PSWhite>(mm2px(Vec(49.44, 56.38)), module, M::MORPH_PARAM));
    addParam(createParamCentered<Trimpot>(mm2px(Vec(10.91, 80.63)), module, M::TIMBRE_CV_PARAM));
    addParam(createParamCentered<Trimpot>(mm2px(Vec(30.25, 80.63)), module, M::FREQ_CV_PARAM));
    addParam(createParamCentered<Trimpot>(mm2px(Vec(49.59, 80.63)), module, M::MORPH_CV_PARAM));
    ParamWidget* response = createParamCentered<Rogan1PSBlue>(mm2px(Vec(10.76, 56.38)), module, M::LPG_COLOUR_PARAM);
    response->hide();
    addParam(response);
    ParamWidget* decay = createParamCentered<Rogan1PSBlue>(mm2px(Vec(49.44, 56.38)), module, M::LPG_DECAY_PARAM);
    decay->hide();
    addParam(decay);

    // Leave the MODEL CV position empty: this build contains a single engine.
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(18.77, 96.49)), module, M::TIMBRE_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.22, 96.49)), module, M::FREQ_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(41.67, 96.49)), module, M::MORPH_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(53.11, 96.49)), module, M::HARMONICS_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.33, 111.09)), module, M::TRIGGER_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(18.77, 111.09)), module, M::LEVEL_INPUT));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.22, 111.09)), module, M::NOTE_INPUT));

    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(41.67, 111.09)), module, M::OUT_OUTPUT));
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(53.11, 111.09)), module, M::AUX_OUTPUT));
  }

  void SetLpgMode(bool editing) {
    typedef PlaitsLabEngine M;
    if (!module) return;  // getParam() needs a module
    getParam(M::TIMBRE_PARAM)->setVisible(!editing);
    getParam(M::MORPH_PARAM)->setVisible(!editing);
    getParam(M::LPG_COLOUR_PARAM)->setVisible(editing);
    getParam(M::LPG_DECAY_PARAM)->setVisible(editing);
    lpg_mode = editing;
  }

  void appendContextMenu(Menu* menu) override {
    PlaitsLabEngine* module = dynamic_cast<PlaitsLabEngine*>(this->module);
    if (!module) return;
    if (module->engineChanged()) {
      menu->addChild(new MenuSeparator);
      menu->addChild(createMenuLabel("Saved with " + module->patchEngineName));
      menu->addChild(createMenuItem("Use " + std::string(lab::LabVoice::name()) + " for this patch", "",
          [=]() { module->acceptCurrentEngine(); }));
    }
    menu->addChild(new MenuSeparator);
    menu->addChild(createBoolMenuItem("Edit LPG response/decay", "",
        [=]() { return lpg_mode; },
        [=](bool editing) { SetLpgMode(editing); }));
#if PLAITS_LAB_ALREADY_ENVELOPED
    // TRIG unpatched is always on. Patched, this engine can keep its own
    // envelope or be struck by Plaits' LPG like any other model.
    typedef PlaitsLabEngine M;
    menu->addChild(new MenuSeparator);
    menu->addChild(createMenuLabel("With TRIG patched"));
    menu->addChild(createCheckMenuItem("Engine's own envelope", "",
        [=]() { return module->params[M::PLAITS_LPG_PARAM].getValue() < 0.5f; },
        [=]() { module->params[M::PLAITS_LPG_PARAM].setValue(0.f); }));
    menu->addChild(createCheckMenuItem("Plaits LPG", "",
        [=]() { return module->params[M::PLAITS_LPG_PARAM].getValue() >= 0.5f; },
        [=]() { module->params[M::PLAITS_LPG_PARAM].setValue(1.f); }));
#else
    (void) module;
#endif
  }
};

Model* modelPlaitsLabEngine = createModel<PlaitsLabEngine, PlaitsLabEngineWidget>("PlaitsLabEngine");

void init(Plugin* p) {
  pluginInstance = p;
  p->addModel(modelPlaitsLabEngine);
}
