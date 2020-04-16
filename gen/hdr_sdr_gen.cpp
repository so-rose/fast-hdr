/* 
 * This file is part of the fast-hdr project (https://git.sofusrose.com/so-rose/fast-hdr).
 * Copyright (c) 2020 Sofus Rose.
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


// Usage: <RGB PRODUCER> | ./hdr_sdr [WIDTH] [HEIGHT] | <RGB ENCODER>

// TODO:
//    - Desaturate Highlights
//    - Dithering

// Libraries
#include <iostream>
#include <fstream>
#include <string>

#include <math.h>
#include <algorithm>

#include <thread>

#include <unistd.h> 
#include <sys/stat.h>



// User Defines
#define IMG_BITS 8



// User Types
typedef uint8_t img_uint; // Must Hold Image Data Point of >= IMG_BITS Size

#define IMG_INT_MAX ((1 << IMG_BITS) - 1)
#define IMG_INT_MAX_D ( (double) IMG_INT_MAX )



// Resolution and Size of LUTD (Dimensioned LUT
#define LUTD_BITS IMG_BITS
#define LUTD_CHNLS 3

#define LUTD_RES (1 << LUTD_BITS) // 2**LUTD_BITS

#define LUTD_SIZE (LUTD_RES * LUTD_RES * LUTD_RES * LUTD_CHNLS)

// Each 8-Bit YUV Triplet => Corresponding YUV Triplet.
//    4D LUT, Three "Cubes": Y Cube, U Cube, V Cube.
//    0. To Advance One Y, U, V, C(hannel) vue, Advance by a Stride
//    1. Use Old YUV to Find X,Y,Z Index On Cube(s).
//    2. Compute New YUV by Indexing Each Cube Identically

#define LUTD_Y_STRIDE(y) (y << (0 * LUTD_BITS)) // Y: Shift by (2**LUTD_BITS)**0
#define LUTD_U_STRIDE(u) (u << (1 * LUTD_BITS)) // U: Shift by (2**LUTD_BITS)**1
#define LUTD_V_STRIDE(v) (v << (2 * LUTD_BITS)) // V: Shift by (2**LUTD_BITS)**2
#define LUTD_C_STRIDE(c) (c << (3 * LUTD_BITS)) // C: Shift by (2**LUTD_BITS)**3



// Namespacing
using namespace std;



//###########
// - LUT Methods
//###########

void write_lutd(img_uint *lutd, string path_lutd) {
	// The array must be sized as LUTD_SIZE.
	
	ofstream file_lutd(path_lutd, ofstream::binary);
		
	if (file_lutd.is_open()) {
		file_lutd.write(reinterpret_cast<char*>(lutd), LUTD_SIZE);
	}
}



//###########
// - Color Model Conversions
//###########

void yuv_rgb(
	img_uint *y,
	img_uint *u,
	img_uint *v,
	double  *buf_rgb
) {
	int c = (double) *y;
	int d = (double) *u - 128.0;
	int e = (double) *v - 128.0;
	
	buf_rgb[0] = clamp( c                + 1.370705 * e, 0.0, IMG_INT_MAX_D ) / IMG_INT_MAX_D;
	buf_rgb[1] = clamp( c - 0.698001 * d - 0.337633 * e, 0.0, IMG_INT_MAX_D ) / IMG_INT_MAX_D;
	buf_rgb[2] = clamp( c + 1.732446 * d               , 0.0, IMG_INT_MAX_D ) / IMG_INT_MAX_D;
}

void rgb_yuv(
	double  *buf_rgb,
	img_uint *y,
	img_uint *u,
	img_uint *v
) {
	double r = buf_rgb[0] * 255.0;
	double g = buf_rgb[1] * 255.0;
	double b = buf_rgb[2] * 255.0;
	
	*y = (img_uint) clamp( 0.257 * r + 0.504 * g + 0.098 * b + 16.0 , 0.0, IMG_INT_MAX_D);
	*u = (img_uint) clamp(-0.148 * r - 0.291 * g + 0.439 * b + 128.0, 0.0, IMG_INT_MAX_D);
	*v = (img_uint) clamp( 0.439 * r - 0.368 * g - 0.071 * b + 128.0, 0.0, IMG_INT_MAX_D);
}

void rgb_hsl(
	double *buf_rgb,
	double *buf_hsl
) {
	// RGB to HSL with Luma Lightness
	
	
	// Calculate Chroma
	double chnl_max = max(buf_rgb[0], max(buf_rgb[1], buf_rgb[2]));
	double chnl_min = min(buf_rgb[0], min(buf_rgb[1], buf_rgb[2]));
	
	double chroma = chnl_max - chnl_min;
	
	// Calculate Lightness
	buf_hsl[2] = (chnl_max + chnl_min) / 2;
	
	// Calculate Saturation
	if (buf_hsl[2] != 0.0 && buf_hsl[2] != 1.0) {
		buf_hsl[1] = chroma / ( 1 - abs(2 * buf_hsl[2] - 1) );
	} else {
		buf_hsl[1] = 0.0;
	}
	
	// Calculate Hue (Degrees)
	if (chroma == 0) {
		buf_hsl[0] = 0.0;
	} else if (chnl_max == buf_rgb[0]) {
		buf_hsl[0] = (buf_rgb[1] - buf_rgb[2]) / chroma;
	} else if (chnl_max == buf_rgb[1]) {
		buf_hsl[0] = 2.0 + (buf_rgb[2] - buf_rgb[0]) / chroma;
	} else if (chnl_max == buf_rgb[2]) {
		buf_hsl[0] = 4.0 + (buf_rgb[0] - buf_rgb[1]) / chroma;
	}
	
	buf_hsl[0] *= 60.0;
	
}

void hsl_rgb(
	double *buf_hsl,
	double *buf_rgb
) {
	// HSL to RGB with Luma Lightness
	double buf_rgb_1[3] {0.0, 0.0, 0.0};
	
	double chroma = ( 1 - abs(2 * buf_hsl[2] - 1) ) * buf_hsl[1];
	
	double H_reduc = buf_hsl[0] / 60.0;
	double X = chroma * ( 1 - abs(fmod(H_reduc, 2.0) - 1) );
	
	if (ceil(H_reduc) == 1) {
		buf_rgb_1[0] = chroma;
		buf_rgb_1[1] = X;
		buf_rgb_1[2] = 0.0;
	} else if (ceil(H_reduc) == 2) {
		buf_rgb_1[0] = X;
		buf_rgb_1[1] = chroma;
		buf_rgb_1[2] = 0.0;
	} else if (ceil(H_reduc) == 3) {
		buf_rgb_1[0] = 0.0;
		buf_rgb_1[1] = chroma;
		buf_rgb_1[2] = X;
	} else if (ceil(H_reduc) == 4) {
		buf_rgb_1[0] = 0.0;
		buf_rgb_1[1] = X;
		buf_rgb_1[2] = chroma;
	} else if (ceil(H_reduc) == 5) {
		buf_rgb_1[0] = X;
		buf_rgb_1[1] = 0.0;
		buf_rgb_1[2] = chroma;
	} else if (ceil(H_reduc) == 6) {
		buf_rgb_1[0] = chroma;
		buf_rgb_1[1] = 0.0;
		buf_rgb_1[2] = X;
	} else {
		buf_rgb_1[0] = 0.0;
		buf_rgb_1[1] = 0.0;
		buf_rgb_1[2] = 0.0;
	}
	
	double m = buf_hsl[2] - (chroma / 2);
	
	buf_rgb[0] = buf_rgb_1[0] + m;
	buf_rgb[1] = buf_rgb_1[1] + m;
	buf_rgb[2] = buf_rgb_1[2] + m;
}

void s_sat(double *buf_rgb, double fac_sat) {
	double buf_hsl[3] {0.0, 0.0, 0.0};
	rgb_hsl(buf_rgb, buf_hsl);
	
	buf_hsl[1] *= fac_sat;
	
	hsl_rgb(buf_hsl, buf_rgb);
	
	//~ buf_rgb[0] = clamp(buf_rgb[0], 0.0, 1.0);
	//~ buf_rgb[1] = clamp(buf_rgb[1], 0.0, 1.0);
	//~ buf_rgb[2] = clamp(buf_rgb[2], 0.0, 1.0);
}



//###########
// - HDR Transfer Curves
//###########

#define c1 0.8359375
#define c2 18.8515625
#define c3 18.6875

#define m1 0.1593017578125
#define m2 78.84375

double gam_pq_lin(double v) {
	return pow( (pow(v, 1/m2) - c1) / (c2 - c3 * pow(v, 1/m2)), 1/m1 );
}

double gam_lin_pq(double v) {
	return pow( (c1 + c2 * pow(v, m1)) / (1 + c3 * pow(v, m1)), m2 );
}



//###########
// - Colorspace Conversions
//###########

void bt2020_bt709(double *buf_rgb) {
	// bt2020_bt709
	double cmat[9] {
		 1.6605, -0.5876, -0.0728,
		-0.1246,  1.1329, -0.0083,
		-0.0182, -0.1006,  1.1187
	};
	
	double r = buf_rgb[0];
	double g = buf_rgb[1];
	double b = buf_rgb[2];
	
	buf_rgb[0] = r * cmat[0] + g * cmat[1] + b * cmat[2]; //Red
	buf_rgb[1] = r * cmat[3] + g * cmat[4] + b * cmat[5]; //Green
	buf_rgb[2] = r * cmat[6] + g * cmat[7] + b * cmat[8]; //Blue
	
	//~ buf_rgb[0] = r * cmat[0] + g * cmat[1] + b * cmat[2]; //Red
	//~ buf_rgb[1] = r * cmat[3] + g * cmat[4] + b * cmat[5]; //Green
	//~ buf_rgb[2] = r * cmat[6] + g * cmat[7] + b * cmat[8]; //Blue
}

float normal_pdf(double x, double m, double s)
{
    static const double inv_sqrt_2pi = 0.3989422804014327;
    double a = (x - m) / s;

    return inv_sqrt_2pi / s * std::exp(-0.5f * a * a);
}

void render_gamut(double *buf_rgb) {
	// Calculate Lightness
	double chnl_max = max(buf_rgb[0], max(buf_rgb[1], buf_rgb[2]));
	double chnl_min = min(buf_rgb[0], min(buf_rgb[1], buf_rgb[2]));
	
	// Zebras
	if (chnl_min <= 0.0) {
		buf_rgb[0] = 0.0;
		buf_rgb[1] = 0.0;
		buf_rgb[2] = 1.0;
	}
	
	if (chnl_max >= 1.0) {
		double chroma = (chnl_max - chnl_min);
		
		buf_rgb[0] = 1.0;
		buf_rgb[1] = 0.0;
		buf_rgb[2] = 0.0;
	}
	
	//~ double luma = 0.2126 * buf_rgb[0] + 0.7152 * buf_rgb[1] + 0.0722 * buf_rgb[2];
	//~ double L = (chnl_max + chnl_min) / 2;
	
	//~ // Scale Towards Gray Based on Luminance
	//~ double scl = normal_pdf(clamp(chroma, 0.0, 1.0), 1.0, 0.01) / 3.98942;
	
	//~ buf_rgb[0] = (1 - scl) * buf_rgb[0] + scl * luma;
	//~ buf_rgb[1] = (1 - scl) * buf_rgb[1] + scl * luma;
	//~ buf_rgb[2] = (1 - scl) * buf_rgb[2] + scl * luma;
	//~ buf_rgb[0] = (1 - chroma) * buf_rgb[0] + chroma * luma;
	//~ buf_rgb[1] = (1 - chroma) * buf_rgb[1] + chroma * luma;
	//~ buf_rgb[2] = (1 - chroma) * buf_rgb[2] + chroma * luma;
	//~ buf_rgb[0] = chroma;
	//~ buf_rgb[1] = chroma;
	//~ buf_rgb[2] = chroma;
}



//###########
// - Tonemapping
//###########

double tm_cool(double v) {
	v *= 150.0;
	
	double A = 0.15;
	double B = 0.50;
	double C = 0.10;
	double D = 0.20;
	double E = 0.02;
	double F = 0.30;
	double W = 11.2;
	
	return ((v*(A*v+C*B)+D*E)/(v*(A*v+B)+D*F))-E/F;
	
	//~ return v / (v + 1);
}



//###########
// - SDR Transfer Curves
//###########
double gam_lin_srgb(double v) {
	return v > 0.0031308 ? (( 1.055 * pow(v, (1.0f / 2.4)) ) - 0.055) : v * 12.92;
}

double gam_srgb_lin(double v) {
	return v > 0.04045 ? pow(( (v + 0.055) / 1.055 ),  2.4) : v / 12.92;
}


#define alpha 1.099
#define beta 0.018
#define zeta 0.081

double gam_lin_709(double v) {
	if (v <= 0) {
		return 0.0;
	} else if (v < beta) {
		return 4.5 * v;
	} else if (v <= 1) {
		return alpha * pow(v, 0.45) - (alpha - 1);
	} else {
		return 1.0;
	}
}

double gam_709_lin(double v) {
	if (v <= 0) {
		return 0.0;
	} else if (v < zeta) {
		return v * (1 / 4.5);
	} else if (v <= 1) {
		return pow( (v + (alpha - 1)) / (alpha), (1 / 0.45) );
	} else {
		return 1.0;
	}
}



//###########
// - Processing Methods
//###########

void proc(img_uint *y, img_uint *u, img_uint *v) {
	
	// Create RGB Buffer                 
	double buf_rgb[3] {0.0, 0.0, 0.0};
	
	// YUV to RGB
	yuv_rgb(y, u, v, buf_rgb);
	
	// PQ --> Lin
	for (int chnl = 0; chnl < 3; chnl++) {
		buf_rgb[chnl] = gam_pq_lin( buf_rgb[chnl] );
	}
	
	// Global Tonemapping
	for (int chnl = 0; chnl < 3; chnl++) {
		buf_rgb[chnl] = tm_cool( buf_rgb[chnl] );
	}
		
	// Lin --> sRGB
	for (int chnl = 0; chnl < 3; chnl++) {
		buf_rgb[chnl] = gam_lin_srgb( buf_rgb[chnl] );
	}
	
	// RGB to YUV
	rgb_yuv(buf_rgb, y, u, v);
}

void gen_lutd(img_uint *lutd) {
	// Process the Payload Using Precomputed YUV Destinations
		#pragma omp parallel for
		for (size_t v = 0; v <= IMG_INT_MAX; v++) {
			for (size_t u = 0; u <= IMG_INT_MAX; u++) {
				for (size_t y = 0; y <= IMG_INT_MAX; y++) {
					size_t ind_lutd = (
						LUTD_Y_STRIDE(y) +
						LUTD_U_STRIDE(u) +
						LUTD_V_STRIDE(v)
					);
					
					// Get LUTD Pointers
					uint8_t *y_lutd = &lutd[ind_lutd + LUTD_C_STRIDE(0)];
					uint8_t *u_lutd = &lutd[ind_lutd + LUTD_C_STRIDE(1)];
					uint8_t *v_lutd = &lutd[ind_lutd + LUTD_C_STRIDE(2)];
					
					// Set LUTD Values to Default
					*y_lutd = y;
					*u_lutd = u;
					*v_lutd = v;
					
					// Process Default LUTD Values
					proc(y_lutd, u_lutd, v_lutd);
				}
			}
		}
}



//###########
// - Application
//###########

int main(int argc, char **argv) {
	
	// PARSE: (Width, Height) => Image Size, LUT Path from Command Line
	string path_lutd;
	if (argc == 2) {
		path_lutd = string(argv[1]);
	} else {
		cout << "Usage: ./hdr_sdr_gen [PATH_LUTD]" << endl;
		
		return 1;
	}
	
	// LUTD: Allocate LUTD
	img_uint *lutd = (img_uint*) malloc( sizeof(img_uint) * LUTD_SIZE );
	
	// LUTD: Generate LUTD
	gen_lutd(lutd);
	
	// LUTD: Wrote LUTD
	write_lutd(lutd, path_lutd);
}
