// Copyright 2012 Emilie Gillet.
// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// Braids' FOLD: a sine wavefolder and a triangle wavefolder, crossfaded.

#include "plaits/dsp/engine2/fold_engine.h"
#include "plaits/build_config.h"

#include <algorithm>
#include <cmath>

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"

#include "plaits/dsp/downsampler/4x_downsampler.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

using namespace std;
using namespace stmlib;

namespace {

// Braids' ws_sine_fold and ws_tri_fold, verbatim from braids/resources.cc.
//
// Both reproduce EXACTLY -- 0 LSB of deviation across all 257 entries -- from
// the closed forms in braids/resources/waveshapers.py:
//
//   tri_fold  = scale(sin(pi * (3x + (2x)^3)),                       center=True)
//   sine_fold = scale(sin(8*pi*x) * w + atan(3x) * (1 - w),          center=False)
//               with w = exp(-4x^2)^1.5, normalized by its own peak
//
// which is checked rather than asserted, following the practice bowed set. The
// data is embedded rather than evaluated because the closed forms cost a sin
// plus an exp plus an atan per lookup, and the folders are read eight times
// per output sample at 4x oversampling.
//
// Note the asymmetric ranges: sine_fold spans -32766..32682 and tri_fold
// -32766..32610, because `scale` normalizes on the larger side. Braids leaves
// that asymmetry in the signal, so the port does too.

const int16_t kSineFold[257] = {
  -32766, -32682, -32595, -32504, -32410, -32315, -32218, -32121,
  -32025, -31931, -31840, -31754, -31673, -31599, -31531, -31470,
  -31416, -31367, -31322, -31279, -31235, -31188, -31132, -31064,
  -30980, -30874, -30744, -30584, -30392, -30166, -29903, -29605,
  -29273, -28911, -28524, -28119, -27704, -27290, -26888, -26510,
  -26167, -25871, -25633, -25461, -25361, -25337, -25389, -25511,
  -25696, -25931, -26197, -26475, -26739, -26962, -27116, -27171,
  -27099, -26876, -26478, -25891, -25105, -24119, -22940, -21584,
  -20079, -18458, -16766, -15052, -13374, -11789, -10357,  -9135,
   -8176,  -7526,  -7217,  -7272,  -7698,  -8484,  -9603, -11013,
  -12652, -14446, -16308, -18142, -19847, -21320, -22465, -23190,
  -23420, -23094, -22176, -20652, -18533, -15860, -12697,  -9137,
   -5292,  -1294,   2713,   6576,  10143,  13266,  15812,  17665,
   18734,  18960,  18316,  16811,  14491,  11437,   7763,   3614,
    -844,  -5428,  -9944, -14200, -18011, -21206, -23640, -25199,
  -25802, -25410, -24026, -21695, -18501, -14568, -10051,  -5129,
       0,   5129,  10051,  14568,  18501,  21695,  24026,  25410,
   25802,  25199,  23640,  21206,  18011,  14200,   9944,   5428,
     844,  -3614,  -7763, -11437, -14491, -16811, -18316, -18960,
  -18734, -17665, -15812, -13266, -10143,  -6576,  -2713,   1294,
    5292,   9137,  12697,  15860,  18533,  20652,  22176,  23094,
   23420,  23190,  22465,  21320,  19847,  18142,  16308,  14446,
   12652,  11013,   9603,   8484,   7698,   7272,   7217,   7526,
    8176,   9135,  10357,  11789,  13374,  15052,  16766,  18458,
   20079,  21584,  22940,  24119,  25105,  25891,  26478,  26876,
   27099,  27171,  27116,  26962,  26739,  26475,  26197,  25931,
   25696,  25511,  25389,  25337,  25361,  25461,  25633,  25871,
   26167,  26510,  26888,  27290,  27704,  28119,  28524,  28911,
   29273,  29605,  29903,  30166,  30392,  30584,  30744,  30874,
   30980,  31064,  31132,  31188,  31235,  31279,  31322,  31367,
   31416,  31470,  31531,  31599,  31673,  31754,  31840,  31931,
   32025,  32121,  32218,  32315,  32410,  32504,  32595,  32682,
   32682
};

const int16_t kTriFold[257] = {
     -78, -20070, -31636, -30481, -17545,   1825,  20257,  31198,
   31144,  20555,   3335, -14748, -28051, -32765, -27869, -15160,
    1526,  17553,  28787,  32606,  28401,  17522,   2767, -12404,
  -24698, -31675, -32164, -26370, -15686,  -2305,  11261,  22670,
   30122,  32609,  29985,  22895,  12593,    688, -11123, -21293,
  -28618, -32346, -32225, -28466, -21675, -12730,  -2661,   7481,
   16727,  24281,  29569,  32266,  32296,  29804,  25121,  18711,
   11123,   2936,  -5286, -13027, -19851, -25419, -29496, -31955,
  -32766, -31988, -29754, -26252, -21712, -16387, -10537,  -4418,
    1726,   7680,  13252,  18287,  22661,  26287,  29107,  31097,
   32257,  32610,  32199,  31083,  29331,  27022,  24237,  21062,
   17579,  13872,  10018,   6089,   2152,  -1734,  -5515,  -9146,
  -12587, -15807, -18779, -21484, -23907, -26038, -27872, -29408,
  -30647, -31595, -32257, -32644, -32766, -32635, -32264, -31666,
  -30855, -29846, -28653, -27289, -25770, -24109, -22319, -20413,
  -18405, -16307, -14130, -11888,  -9591,  -7250,  -4877,  -2483,
     -78,   2327,   4722,   7095,   9435,  11732,  13975,  16151,
   18249,  20257,  22163,  23953,  25615,  27134,  28497,  29691,
   30700,  31510,  32108,  32479,  32610,  32488,  32101,  31439,
   30492,  29253,  27717,  25883,  23752,  21329,  18624,  15651,
   12431,   8990,   5359,   1578,  -2307,  -6245, -10174, -14028,
  -17735, -21217, -24393, -27177, -29487, -31238, -32354, -32765,
  -32412, -31253, -29263, -26442, -22817, -18442, -13407,  -7835,
   -1882,   4263,  10381,  16231,  21557,  26096,  29598,  31832,
   32610,  31800,  29341,  25263,  19695,  12871,   5130,  -3091,
  -11279, -18867, -25276, -29960, -32452, -32422, -29725, -24437,
  -16883,  -7636,   2505,  12574,  21519,  28311,  32069,  32191,
   28462,  21137,  10967,   -844, -12749, -23051, -30140, -32765,
  -30278, -22825, -11417,   2150,  15530,  26214,  32008,  31519,
   24543,  12248,  -2923, -17678, -28556, -32762, -28943, -17709,
   -1682,  15005,  27714,  32609,  27895,  14592,  -3491, -20710,
  -31300, -31354, -20413,  -1981,  17389,  30325,  31480,  19915,
   19915
};

// The same two tables pre-scaled by 1/32768 -- exact, since the scale is a
// power of two -- so ReadShaper skips two int-to-float conversions and a
// multiply per read (eight reads per output sample). Bit-identical output.
const float kSineFoldScaled[257] = {
  -0.99993896484375f, -0.99737548828125f, -0.994720458984375f, -0.991943359375f, -0.98907470703125f, -0.986175537109375f,
  -0.98321533203125f, -0.980255126953125f, -0.977325439453125f, -0.974456787109375f, -0.9716796875f, -0.96905517578125f,
  -0.966583251953125f, -0.964324951171875f, -0.962249755859375f, -0.96038818359375f, -0.958740234375f, -0.957244873046875f,
  -0.95587158203125f, -0.954559326171875f, -0.953216552734375f, -0.9517822265625f, -0.9500732421875f, -0.947998046875f,
  -0.9454345703125f, -0.94219970703125f, -0.938232421875f, -0.933349609375f, -0.927490234375f, -0.92059326171875f,
  -0.912567138671875f, -0.903472900390625f, -0.893341064453125f, -0.882293701171875f, -0.8704833984375f, -0.858123779296875f,
  -0.845458984375f, -0.83282470703125f, -0.820556640625f, -0.80902099609375f, -0.798553466796875f, -0.789520263671875f,
  -0.782257080078125f, -0.777008056640625f, -0.773956298828125f, -0.773223876953125f, -0.774810791015625f, -0.778533935546875f,
  -0.7841796875f, -0.791351318359375f, -0.799468994140625f, -0.807952880859375f, -0.816009521484375f, -0.82281494140625f,
  -0.8275146484375f, -0.829193115234375f, -0.826995849609375f, -0.8201904296875f, -0.80804443359375f, -0.790130615234375f,
  -0.766143798828125f, -0.736053466796875f, -0.7000732421875f, -0.65869140625f, -0.612762451171875f, -0.56329345703125f,
  -0.51165771484375f, -0.4593505859375f, -0.40814208984375f, -0.359771728515625f, -0.316070556640625f, -0.278778076171875f,
  -0.24951171875f, -0.22967529296875f, -0.220245361328125f, -0.221923828125f, -0.23492431640625f, -0.2589111328125f,
  -0.293060302734375f, -0.336090087890625f, -0.3861083984375f, -0.44085693359375f, -0.4976806640625f, -0.55364990234375f,
  -0.605682373046875f, -0.650634765625f, -0.685577392578125f, -0.70770263671875f, -0.7147216796875f, -0.70477294921875f,
  -0.6767578125f, -0.6302490234375f, -0.565582275390625f, -0.4840087890625f, -0.387481689453125f, -0.278839111328125f,
  -0.1614990234375f, -0.03948974609375f, 0.082794189453125f, 0.20068359375f, 0.309539794921875f, 0.40484619140625f,
  0.4825439453125f, 0.539093017578125f, 0.57171630859375f, 0.57861328125f, 0.5589599609375f, 0.513031005859375f,
  0.442230224609375f, 0.349029541015625f, 0.236907958984375f, 0.11029052734375f, -0.0257568359375f, -0.1656494140625f,
  -0.303466796875f, -0.433349609375f, -0.549652099609375f, -0.64715576171875f, -0.721435546875f, -0.769012451171875f,
  -0.78741455078125f, -0.77545166015625f, -0.73321533203125f, -0.662078857421875f, -0.564605712890625f, -0.444580078125f,
  -0.306732177734375f, -0.156524658203125f, 0.0f, 0.156524658203125f, 0.306732177734375f, 0.444580078125f,
  0.564605712890625f, 0.662078857421875f, 0.73321533203125f, 0.77545166015625f, 0.78741455078125f, 0.769012451171875f,
  0.721435546875f, 0.64715576171875f, 0.549652099609375f, 0.433349609375f, 0.303466796875f, 0.1656494140625f,
  0.0257568359375f, -0.11029052734375f, -0.236907958984375f, -0.349029541015625f, -0.442230224609375f, -0.513031005859375f,
  -0.5589599609375f, -0.57861328125f, -0.57171630859375f, -0.539093017578125f, -0.4825439453125f, -0.40484619140625f,
  -0.309539794921875f, -0.20068359375f, -0.082794189453125f, 0.03948974609375f, 0.1614990234375f, 0.278839111328125f,
  0.387481689453125f, 0.4840087890625f, 0.565582275390625f, 0.6302490234375f, 0.6767578125f, 0.70477294921875f,
  0.7147216796875f, 0.70770263671875f, 0.685577392578125f, 0.650634765625f, 0.605682373046875f, 0.55364990234375f,
  0.4976806640625f, 0.44085693359375f, 0.3861083984375f, 0.336090087890625f, 0.293060302734375f, 0.2589111328125f,
  0.23492431640625f, 0.221923828125f, 0.220245361328125f, 0.22967529296875f, 0.24951171875f, 0.278778076171875f,
  0.316070556640625f, 0.359771728515625f, 0.40814208984375f, 0.4593505859375f, 0.51165771484375f, 0.56329345703125f,
  0.612762451171875f, 0.65869140625f, 0.7000732421875f, 0.736053466796875f, 0.766143798828125f, 0.790130615234375f,
  0.80804443359375f, 0.8201904296875f, 0.826995849609375f, 0.829193115234375f, 0.8275146484375f, 0.82281494140625f,
  0.816009521484375f, 0.807952880859375f, 0.799468994140625f, 0.791351318359375f, 0.7841796875f, 0.778533935546875f,
  0.774810791015625f, 0.773223876953125f, 0.773956298828125f, 0.777008056640625f, 0.782257080078125f, 0.789520263671875f,
  0.798553466796875f, 0.80902099609375f, 0.820556640625f, 0.83282470703125f, 0.845458984375f, 0.858123779296875f,
  0.8704833984375f, 0.882293701171875f, 0.893341064453125f, 0.903472900390625f, 0.912567138671875f, 0.92059326171875f,
  0.927490234375f, 0.933349609375f, 0.938232421875f, 0.94219970703125f, 0.9454345703125f, 0.947998046875f,
  0.9500732421875f, 0.9517822265625f, 0.953216552734375f, 0.954559326171875f, 0.95587158203125f, 0.957244873046875f,
  0.958740234375f, 0.96038818359375f, 0.962249755859375f, 0.964324951171875f, 0.966583251953125f, 0.96905517578125f,
  0.9716796875f, 0.974456787109375f, 0.977325439453125f, 0.980255126953125f, 0.98321533203125f, 0.986175537109375f,
  0.98907470703125f, 0.991943359375f, 0.994720458984375f, 0.99737548828125f, 0.99737548828125f
};

const float kTriFoldScaled[257] = {
  -0.00238037109375f, -0.61248779296875f, -0.9654541015625f, -0.930206298828125f, -0.535430908203125f, 0.055694580078125f,
  0.618194580078125f, 0.95208740234375f, 0.950439453125f, 0.627288818359375f, 0.101776123046875f, -0.4500732421875f,
  -0.856048583984375f, -0.999908447265625f, -0.850494384765625f, -0.462646484375f, 0.04656982421875f, 0.535675048828125f,
  0.878509521484375f, 0.99505615234375f, 0.866729736328125f, 0.53472900390625f, 0.084442138671875f, -0.3785400390625f,
  -0.75372314453125f, -0.966644287109375f, -0.9815673828125f, -0.80474853515625f, -0.47869873046875f, -0.070343017578125f,
  0.343658447265625f, 0.69183349609375f, 0.91925048828125f, 0.995147705078125f, 0.915069580078125f, 0.698699951171875f,
  0.384307861328125f, 0.02099609375f, -0.339447021484375f, -0.649810791015625f, -0.87335205078125f, -0.98712158203125f,
  -0.983428955078125f, -0.86871337890625f, -0.661468505859375f, -0.38848876953125f, -0.081207275390625f, 0.228302001953125f,
  0.510467529296875f, 0.740997314453125f, 0.902374267578125f, 0.98468017578125f, 0.985595703125f, 0.9095458984375f,
  0.766632080078125f, 0.571014404296875f, 0.339447021484375f, 0.089599609375f, -0.16131591796875f, -0.397552490234375f,
  -0.605804443359375f, -0.775726318359375f, -0.900146484375f, -0.975189208984375f, -0.99993896484375f, -0.9761962890625f,
  -0.90802001953125f, -0.8011474609375f, -0.66259765625f, -0.500091552734375f, -0.321563720703125f, -0.13482666015625f,
  0.05267333984375f, 0.234375f, 0.4044189453125f, 0.558074951171875f, 0.691558837890625f, 0.802215576171875f,
  0.888275146484375f, 0.949005126953125f, 0.984405517578125f, 0.99517822265625f, 0.982635498046875f, 0.948577880859375f,
  0.895111083984375f, 0.82464599609375f, 0.739654541015625f, 0.64276123046875f, 0.536468505859375f, 0.42333984375f,
  0.30572509765625f, 0.185821533203125f, 0.065673828125f, -0.05291748046875f, -0.168304443359375f, -0.27911376953125f,
  -0.384124755859375f, -0.482391357421875f, -0.573089599609375f, -0.6556396484375f, -0.729583740234375f, -0.79461669921875f,
  -0.8505859375f, -0.8974609375f, -0.935272216796875f, -0.964202880859375f, -0.984405517578125f, -0.9962158203125f,
  -0.99993896484375f, -0.995941162109375f, -0.984619140625f, -0.96636962890625f, -0.941619873046875f, -0.91082763671875f,
  -0.874420166015625f, -0.832794189453125f, -0.78643798828125f, -0.735748291015625f, -0.681121826171875f, -0.622955322265625f,
  -0.561676025390625f, -0.497650146484375f, -0.43121337890625f, -0.36279296875f, -0.292694091796875f, -0.22125244140625f,
  -0.148834228515625f, -0.075775146484375f, -0.00238037109375f, 0.071014404296875f, 0.14410400390625f, 0.216522216796875f,
  0.287933349609375f, 0.3580322265625f, 0.426483154296875f, 0.492889404296875f, 0.556915283203125f, 0.618194580078125f,
  0.676361083984375f, 0.730987548828125f, 0.781707763671875f, 0.82806396484375f, 0.869659423828125f, 0.906097412109375f,
  0.9368896484375f, 0.96160888671875f, 0.9798583984375f, 0.991180419921875f, 0.99517822265625f, 0.991455078125f,
  0.979644775390625f, 0.959442138671875f, 0.9305419921875f, 0.892730712890625f, 0.845855712890625f, 0.789886474609375f,
  0.724853515625f, 0.650909423828125f, 0.568359375f, 0.477630615234375f, 0.379364013671875f, 0.27435302734375f,
  0.163543701171875f, 0.04815673828125f, -0.070404052734375f, -0.190582275390625f, -0.31048583984375f, -0.4281005859375f,
  -0.541229248046875f, -0.647491455078125f, -0.744415283203125f, -0.829376220703125f, -0.899871826171875f, -0.95330810546875f,
  -0.98736572265625f, -0.999908447265625f, -0.9891357421875f, -0.953765869140625f, -0.893035888671875f, -0.80694580078125f,
  -0.696319580078125f, -0.56280517578125f, -0.409149169921875f, -0.239105224609375f, -0.05743408203125f, 0.130096435546875f,
  0.316802978515625f, 0.495330810546875f, 0.657867431640625f, 0.79638671875f, 0.90325927734375f, 0.971435546875f,
  0.99517822265625f, 0.970458984375f, 0.895416259765625f, 0.770965576171875f, 0.601043701171875f, 0.392791748046875f,
  0.15655517578125f, -0.094329833984375f, -0.344207763671875f, -0.575775146484375f, -0.7713623046875f, -0.914306640625f,
  -0.9903564453125f, -0.98944091796875f, -0.907135009765625f, -0.745758056640625f, -0.515228271484375f, -0.2330322265625f,
  0.076446533203125f, 0.38372802734375f, 0.656707763671875f, 0.863983154296875f, 0.978668212890625f, 0.982391357421875f,
  0.86859130859375f, 0.645050048828125f, 0.334686279296875f, -0.0257568359375f, -0.389068603515625f, -0.703460693359375f,
  -0.9197998046875f, -0.999908447265625f, -0.92401123046875f, -0.696563720703125f, -0.348419189453125f, 0.06561279296875f,
  0.47393798828125f, 0.79998779296875f, 0.976806640625f, 0.961883544921875f, 0.748992919921875f, 0.373779296875f,
  -0.089202880859375f, -0.53948974609375f, -0.8714599609375f, -0.99981689453125f, -0.883270263671875f, -0.540435791015625f,
  -0.05133056640625f, 0.457916259765625f, 0.84576416015625f, 0.995147705078125f, 0.851287841796875f, 0.4453125f,
  -0.106536865234375f, -0.63201904296875f, -0.9552001953125f, -0.95684814453125f, -0.622955322265625f, -0.060455322265625f,
  0.530670166015625f, 0.925445556640625f, 0.960693359375f, 0.607757568359375f, 0.607757568359375f
};


// Braids reads these with Interpolate88(table, sample + 32768): the int16
// sample becomes a uint16 index, its top 8 bits select the entry and its low 8
// interpolate toward the next. `x` here is that same input, normalized.
inline float ReadShaper(const float* table, float x) {
  // Saturate the way Braids does, in the integer domain. The float clamp this
  // replaces -- CONSTRAIN on the table index -- measured 101.6 instructions and
  // 32 VCMPs per output sample, 19 points of the CPU budget, because it runs
  // EIGHT times per sample (two folders at 4x oversampling) and PERFORMANCE.md
  // is explicit that float compares in a loop stall the core. Clip16 is one
  // SSAT instruction, which is the same saturation Interpolate88 gets for free
  // from its int16 argument.
  //
  // This also moves the fraction from float precision to Braids' own 8 bits,
  // which is what Interpolate88 uses -- so it is a step TOWARD the module, not
  // away from it. Measured audio delta against the float version is in the
  // commit that introduced this.
  const int32_t u = Clip16(static_cast<int32_t>(x * 32768.0f)) + 32768;
  const int integral = u >> 8;
  const float fractional = static_cast<float>(u & 0xff) * (1.0f / 256.0f);
  const float a = table[integral];
  const float b = table[integral + 1];
  return a + (b - a) * fractional;
}

// Braids' triangle: a phase ramp folded about its midpoint, full scale.
// Verified against the integer original -- (phase_16 << 1) ^ (sign ? 0xffff :
// 0) then += 32768, read as int16 -- at every eighth of a cycle.
inline float Triangle(float phase) {
  const float t = phase < 0.5f ? phase : 1.0f - phase;
  return 4.0f * t - 1.0f;
}

// Braids' wav_sine is NOT Plaits' lut_sine, and not just in phase.
//
// Phase: wav_sine starts at -32512 and reaches zero a quarter of the way
// through, so it is -cos(2*pi*phase); Plaits' lut_sine starts at 0 and is
// sin(2*pi*phase). A quarter cycle apart.
//
// Amplitude and DC: fitted against braids/resources.cc (max residual 1 LSB
// across all 256 entries), wav_sine[i] = 32638 * -cos(2*pi*i/256) + 127. It
// is neither unit amplitude (-0.0343 dB quiet) nor zero mean (a +127/32768
// pedestal on every sample).
//
// For a linear oscillator all three are inaudible. Into a wavefolder they are
// not: the folder's lobes land at different points of the cycle and are
// driven from a different operating point, and the sine folder is then
// crossfaded against a triangle folder that carries none of this, so the
// blend produces a different waveform at every setting. The first A/B of this
// engine, phase only, measured 8.6 dB of spectral difference at shallow fold.
// Adding the amplitude/DC terms on top of the phase fix measured a further
// improvement -- spectrumDb dropped on every case that touches the sine
// folder (stock-mid 0.18->0.07 dB, shallow 0.10->0.05 dB, deep 0.19->0.10 dB,
// blend-sine -- the sine folder alone -- 0.33->0.07 dB, guard-band
// 1.06->0.99 dB) and was unchanged, as a control, on blend-triangle (the
// triangle folder alone, which never touches wav_sine): 0.14->0.14 dB. Kept.
const float kBraidsSinePhaseOffset = 0.75f;
const float kBraidsSineAmplitude = 32638.0f / 32768.0f;
const float kBraidsSineDcPedestal = 127.0f / 32768.0f;

inline float BraidsSine(float phase) {
  phase += kBraidsSinePhaseOffset;
  if (phase >= 1.0f) {
    phase -= 1.0f;
  }
  // phase is explicitly wrapped above, so avoid Sine()'s redundant wrap in
  // each of the four oversampled reads.
  return SineNoWrap(phase) * kBraidsSineAmplitude + kBraidsSineDcPedestal;
}

// The depth guard, expressed as a gain that falls from 1 to 0 over `span`
// semitones above `threshold`. Braids computes this from its integer pitch;
// the internal rate is the same 192 kHz on both sides, so the thresholds carry
// over unchanged (SPEC R5).
inline float DepthGuard(float note, float threshold, float span) {
  if (note <= threshold) {
    return 1.0f;
  }
  const float attenuation = 1.0f - (note - threshold) / span;
  return attenuation > 0.0f ? attenuation : 0.0f;
}

}  // namespace

void FoldEngine::Init(BufferAllocator* allocator) {
  (void) allocator;
  Reset();
}

void FoldEngine::Reset() {
  phase_ = 0.0f;
  frequency_ = 0.01f;
  depth_ = kFoldMinDepth;
  blend_ = 0.0f;
  symmetry_ = 0.0f;
  drive_ = 1.0f;
  downsampler_state_ = 0.0f;
  downsampler_state_aux_ = 0.0f;
  dc_input_ = 0.0f;
  dc_input_aux_ = 0.0f;
}

void FoldEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  *already_enveloped = false;

  if (parameters.trigger & TRIGGER_RISING_EDGE) {
    phase_ = 0.0f;
  }

  const bool stereo = PLAITS_STEREO_FOLD && parameters.stereo;

  const float frequency = NoteToFrequency(parameters.note);

  // Braids' two knobs, one each: TIMBRE is the fold depth for both folders,
  // COLOR is the crossfade between them.
  const float target_depth = kFoldMinDepth + kFoldDepthRange * parameters.timbre;
  const float target_blend = parameters.harmonics;

  const float target_symmetry = (parameters.morph - 0.5f) * 2.0f *
      kFoldMaxSymmetry;
  const float target_drive = ApplyMacro(1.0f, 0.5f, 2.5f, parameters.macro);

  // Braids attenuates each folder's depth as the note climbs, the triangle
  // first. Both guards are anti-aliasing measures against a 192 kHz internal
  // rate, which this port shares, so the thresholds transfer as written.
  const float tri_guard = DepthGuard(
      parameters.note, kFoldTriGuardNote, kFoldTriGuardSpan);
  const float sine_guard = DepthGuard(
      parameters.note, kFoldSineGuardNote, kFoldSineGuardSpan);

  ParameterInterpolator fm(&frequency_, frequency, size);
  ParameterInterpolator depth_modulation(&depth_, target_depth, size);
  ParameterInterpolator blend_modulation(&blend_, target_blend, size);
  ParameterInterpolator symmetry_modulation(&symmetry_, target_symmetry, size);
  ParameterInterpolator drive_modulation(&drive_, target_drive, size);

  Downsampler downsampler(&downsampler_state_);
  Downsampler downsampler_aux(&downsampler_state_aux_);
#if PLAITS_BUILD_FREQUENCY_OFFSET_FM
  const float* frequency_offset = parameters.frequency_offset;
#else
  const float* frequency_offset = NULL;
#endif

  while (size--) {
    float f = fm.Next();
    if (frequency_offset) {
      f += *frequency_offset++;
      CONSTRAIN(f, -0.5f, 0.499999f);
    }
    const float depth = depth_modulation.Next();
    const float blend = blend_modulation.Next();
    const float symmetry = symmetry_modulation.Next();
    const float drive = drive_modulation.Next();

    const float sine_depth = depth * sine_guard * drive;
    const float tri_depth = depth * tri_guard * drive;

    float blend_main = blend;
    float blend_aux = 1.0f - blend;
    if (stereo) {
      blend_main = blend - kFoldStereoBlend;
      blend_aux = blend + kFoldStereoBlend;
      CONSTRAIN(blend_main, 0.0f, 1.0f);
      CONSTRAIN(blend_aux, 0.0f, 1.0f);
    }

    const float increment = f * 0.25f;
    for (int j = 0; j < 4; ++j) {
      phase_ += increment;
      if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
      } else if (phase_ < 0.0f) {
        phase_ += 1.0f;
      }

      // Braids drives the sine folder from its sine table and the triangle
      // folder from a raw ramp fold, both at full scale before the depth gain.
      const float sine_source = BraidsSine(phase_) * sine_depth + symmetry;
      const float tri_source = Triangle(phase_) * tri_depth + symmetry;

      const float folded_sine = ReadShaper(kSineFoldScaled, sine_source);
      const float folded_tri = ReadShaper(kTriFoldScaled, tri_source);

      downsampler.Accumulate(j, folded_sine +
          (folded_tri - folded_sine) * blend_main);
      downsampler_aux.Accumulate(j, folded_sine +
          (folded_tri - folded_sine) * blend_aux);
    }

    // Both shaper curves are asymmetric by construction (SPEC notes the
    // -32766..32682 span), and Symmetry deliberately adds an offset, so the
    // output carries DC that has to come out before the gain stage. R1 then
    // applies: an engine with a DC blocker cannot pin its post-blocker peak,
    // so this engine registers negative gains and takes the limiter path.
    const float raw = downsampler.Read();
    const float raw_aux = downsampler_aux.Read();

    ONE_POLE(dc_input_, raw, 0.001f);
    ONE_POLE(dc_input_aux_, raw_aux, 0.001f);
    const float dc_output = raw - dc_input_;
    const float dc_output_aux = raw_aux - dc_input_aux_;

    *out++ = dc_output;
    *aux++ = dc_output_aux;
  }
}

}  // namespace plaits
