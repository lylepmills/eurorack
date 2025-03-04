// Copyright 2015 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Resources definitions.
//
// Automatically generated with:
// make resources


#ifndef RINGS_RESOURCES_H_
#define RINGS_RESOURCES_H_


#include "stmlib/stmlib.h"



namespace rings {

typedef uint8_t ResourceId;

extern const int16_t* lookup_table_int16_table[];

extern const uint32_t* lookup_table_uint32_table[];

extern const float* lookup_table_table[];

extern const int16_t* sample_table[];

extern const size_t* sample_boundary_table[];

extern const float lut_sine[];
extern const float lut_approx_svf_gain[];
extern const float lut_approx_svf_g[];
extern const float lut_approx_svf_r[];
extern const float lut_approx_svf_h[];
extern const float lut_4_decades[];
extern const float lut_svf_shift[];
extern const float lut_stiffness[];
extern const float lut_fm_frequency_quantizer[];
extern const int16_t smp_sample_data[];
extern const int16_t smp_noise_sample[];
extern const size_t smp_boundaries[];
#define LUT_SINE 0
#define LUT_SINE_SIZE 5121
#define LUT_APPROX_SVF_GAIN 1
#define LUT_APPROX_SVF_GAIN_SIZE 257
#define LUT_APPROX_SVF_G 2
#define LUT_APPROX_SVF_G_SIZE 257
#define LUT_APPROX_SVF_R 3
#define LUT_APPROX_SVF_R_SIZE 257
#define LUT_APPROX_SVF_H 4
#define LUT_APPROX_SVF_H_SIZE 257
#define LUT_4_DECADES 5
#define LUT_4_DECADES_SIZE 257
#define LUT_SVF_SHIFT 6
#define LUT_SVF_SHIFT_SIZE 257
#define LUT_STIFFNESS 7
#define LUT_STIFFNESS_SIZE 257
#define LUT_FM_FREQUENCY_QUANTIZER 8
#define LUT_FM_FREQUENCY_QUANTIZER_SIZE 129
#define SMP_SAMPLE_DATA 0
#define SMP_SAMPLE_DATA_SIZE 128013
#define SMP_NOISE_SAMPLE 1
#define SMP_NOISE_SAMPLE_SIZE 40963
#define SMP_BOUNDARIES 0
#define SMP_BOUNDARIES_SIZE 10

}  // namespace rings

#endif  // RINGS_RESOURCES_H_
