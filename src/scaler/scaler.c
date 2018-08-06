/*****************************************************************************
* This file is part of Kvazaar HEVC encoder.
*
* Copyright (C) 2013-2015 Tampere University of Technology and others (see
* COPYING file).
*
* Kvazaar is free software: you can redistribute it and/or modify it under
* the terms of the GNU Lesser General Public License as published by the
* Free Software Foundation; either version 2.1 of the License, or (at your
* option) any later version.
*
* Kvazaar is distributed in the hope that it will be useful, but WITHOUT ANY
* WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
* FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with Kvazaar.  If not, see <http://www.gnu.org/licenses/>.
****************************************************************************/

#include "scaler.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#define SCALER_MIN(x,y) (((x) < (y)) ? (x) : (y))
#define SCALER_MAX(x,y) (((x) > (y)) ? (x) : (y))
#define SCALER_CLIP(val, mn, mx) (SCALER_MIN(mx,SCALER_MAX(mn,val)))

#define SCALER_SHIFT(val,shift) (((shift) < 0) ? ((val)>>(-(shift))) : ((val)<<(shift)))
#define SCALER_ROUND_SHIFT(val,shift) (((shift) < 0) ? SCALER_SHIFT((val)+(1<<(-(shift)))-1,shift) : SCALER_SHIFT(val, shift))

#define SCALER_SHIFT_CONST 16 //TODO: Make dependant on stuff?
#define SCALER_UNITY_SCALE_CONST (1<<SCALER_SHIFT_CONST)

//Define filters for scaling operations
//Values from SHM
static const int downFilter[8][16][12] = {
  // ratio <= 20/19
  {
    {0, 0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 2, -6, 127, 7, -2, 0, 0, 0, 0},
    {0, 0, 0, 3, -12, 125, 16, -5, 1, 0, 0, 0},
    {0, 0, 0, 4, -16, 120, 26, -7, 1, 0, 0, 0},
    {0, 0, 0, 5, -18, 114, 36, -10, 1, 0, 0, 0},
    {0, 0, 0, 5, -20, 107, 46, -12, 2, 0, 0, 0},
    {0, 0, 0, 5, -21, 99, 57, -15, 3, 0, 0, 0},
    {0, 0, 0, 5, -20, 89, 68, -18, 4, 0, 0, 0},
    {0, 0, 0, 4, -19, 79, 79, -19, 4, 0, 0, 0},
    {0, 0, 0, 4, -18, 68, 89, -20, 5, 0, 0, 0},
    {0, 0, 0, 3, -15, 57, 99, -21, 5, 0, 0, 0},
    {0, 0, 0, 2, -12, 46, 107, -20, 5, 0, 0, 0},
    {0, 0, 0, 1, -10, 36, 114, -18, 5, 0, 0, 0},
    {0, 0, 0, 1, -7, 26, 120, -16, 4, 0, 0, 0},
    {0, 0, 0, 1, -5, 16, 125, -12, 3, 0, 0, 0},
    {0, 0, 0, 0, -2, 7, 127, -6, 2, 0, 0, 0}
  },
  // 20/19 < ratio <= 5/4
  {
    {0, 2, 0, -14, 33, 86, 33, -14, 0, 2, 0, 0},
    {0, 1, 1, -14, 29, 85, 38, -13, -1, 2, 0, 0},
    {0, 1, 2, -14, 24, 84, 43, -12, -2, 2, 0, 0},
    {0, 1, 2, -13, 19, 83, 48, -11, -3, 2, 0, 0},
    {0, 0, 3, -13, 15, 81, 53, -10, -4, 3, 0, 0},
    {0, 0, 3, -12, 11, 79, 57, -8, -5, 3, 0, 0},
    {0, 0, 3, -11, 7, 76, 62, -5, -7, 3, 0, 0},
    {0, 0, 3, -10, 3, 73, 65, -2, -7, 3, 0, 0},
    {0, 0, 3, -9, 0, 70, 70, 0, -9, 3, 0, 0},
    {0, 0, 3, -7, -2, 65, 73, 3, -10, 3, 0, 0},
    {0, 0, 3, -7, -5, 62, 76, 7, -11, 3, 0, 0},
    {0, 0, 3, -5, -8, 57, 79, 11, -12, 3, 0, 0},
    {0, 0, 3, -4, -10, 53, 81, 15, -13, 3, 0, 0},
    {0, 0, 2, -3, -11, 48, 83, 19, -13, 2, 1, 0},
    {0, 0, 2, -2, -12, 43, 84, 24, -14, 2, 1, 0},
    {0, 0, 2, -1, -13, 38, 85, 29, -14, 1, 1, 0}
  },
  // 5/4 < ratio <= 5/3
  {
    {0, 5, -6, -10, 37, 76, 37, -10, -6, 5, 0, 0},
    {0, 5, -4, -11, 33, 76, 40, -9, -7, 5, 0, 0},
    {-1, 5, -3, -12, 29, 75, 45, -7, -8, 5, 0, 0},
    {-1, 4, -2, -13, 25, 75, 48, -5, -9, 5, 1, 0},
    {-1, 4, -1, -13, 22, 73, 52, -3, -10, 4, 1, 0},
    {-1, 4, 0, -13, 18, 72, 55, -1, -11, 4, 2, -1},
    {-1, 4, 1, -13, 14, 70, 59, 2, -12, 3, 2, -1},
    {-1, 3, 1, -13, 11, 68, 62, 5, -12, 3, 2, -1},
    {-1, 3, 2, -13, 8, 65, 65, 8, -13, 2, 3, -1},
    {-1, 2, 3, -12, 5, 62, 68, 11, -13, 1, 3, -1},
    {-1, 2, 3, -12, 2, 59, 70, 14, -13, 1, 4, -1},
    {-1, 2, 4, -11, -1, 55, 72, 18, -13, 0, 4, -1},
    {0, 1, 4, -10, -3, 52, 73, 22, -13, -1, 4, -1},
    {0, 1, 5, -9, -5, 48, 75, 25, -13, -2, 4, -1},
    {0, 0, 5, -8, -7, 45, 75, 29, -12, -3, 5, -1},
    {0, 0, 5, -7, -9, 40, 76, 33, -11, -4, 5, 0},
  },
  // 5/3 < ratio <= 2
  {
    {2, -3, -9, 6, 39, 58, 39, 6, -9, -3, 2, 0},
    {2, -3, -9, 4, 38, 58, 43, 7, -9, -4, 1, 0},
    {2, -2, -9, 2, 35, 58, 44, 9, -8, -4, 1, 0},
    {1, -2, -9, 1, 34, 58, 46, 11, -8, -5, 1, 0},
    {1, -1, -8, -1, 31, 57, 47, 13, -7, -5, 1, 0},
    {1, -1, -8, -2, 29, 56, 49, 15, -7, -6, 1, 1},
    {1, 0, -8, -3, 26, 55, 51, 17, -7, -6, 1, 1},
    {1, 0, -7, -4, 24, 54, 52, 19, -6, -7, 1, 1},
    {1, 0, -7, -5, 22, 53, 53, 22, -5, -7, 0, 1},
    {1, 1, -7, -6, 19, 52, 54, 24, -4, -7, 0, 1},
    {1, 1, -6, -7, 17, 51, 55, 26, -3, -8, 0, 1},
    {1, 1, -6, -7, 15, 49, 56, 29, -2, -8, -1, 1},
    {0, 1, -5, -7, 13, 47, 57, 31, -1, -8, -1, 1},
    {0, 1, -5, -8, 11, 46, 58, 34, 1, -9, -2, 1},
    {0, 1, -4, -8, 9, 44, 58, 35, 2, -9, -2, 2},
    {0, 1, -4, -9, 7, 43, 58, 38, 4, -9, -3, 2},
  },
  // 2 < ratio <= 5/2
  {
    {-2, -7, 0, 17, 35, 43, 35, 17, 0, -7, -5, 2},
    {-2, -7, -1, 16, 34, 43, 36, 18, 1, -7, -5, 2},
    {-1, -7, -1, 14, 33, 43, 36, 19, 1, -6, -5, 2},
    {-1, -7, -2, 13, 32, 42, 37, 20, 3, -6, -5, 2},
    {0, -7, -3, 12, 31, 42, 38, 21, 3, -6, -5, 2},
    {0, -7, -3, 11, 30, 42, 39, 23, 4, -6, -6, 1},
    {0, -7, -4, 10, 29, 42, 40, 24, 5, -6, -6, 1},
    {1, -7, -4, 9, 27, 41, 40, 25, 6, -5, -6, 1},
    {1, -6, -5, 7, 26, 41, 41, 26, 7, -5, -6, 1},
    {1, -6, -5, 6, 25, 40, 41, 27, 9, -4, -7, 1},
    {1, -6, -6, 5, 24, 40, 42, 29, 10, -4, -7, 0},
    {1, -6, -6, 4, 23, 39, 42, 30, 11, -3, -7, 0},
    {2, -5, -6, 3, 21, 38, 42, 31, 12, -3, -7, 0},
    {2, -5, -6, 3, 20, 37, 42, 32, 13, -2, -7, -1},
    {2, -5, -6, 1, 19, 36, 43, 33, 14, -1, -7, -1},
    {2, -5, -7, 1, 18, 36, 43, 34, 16, -1, -7, -2}
  },
  // 5/2 < ratio <= 20/7
  {
    {-6, -3, 5, 19, 31, 36, 31, 19, 5, -3, -6, 0},
    {-6, -4, 4, 18, 31, 37, 32, 20, 6, -3, -6, -1},
    {-6, -4, 4, 17, 30, 36, 33, 21, 7, -3, -6, -1},
    {-5, -5, 3, 16, 30, 36, 33, 22, 8, -2, -6, -2},
    {-5, -5, 2, 15, 29, 36, 34, 23, 9, -2, -6, -2},
    {-5, -5, 2, 15, 28, 36, 34, 24, 10, -2, -6, -3},
    {-4, -5, 1, 14, 27, 36, 35, 24, 10, -1, -6, -3},
    {-4, -5, 0, 13, 26, 35, 35, 25, 11, 0, -5, -3},
    {-4, -6, 0, 12, 26, 36, 36, 26, 12, 0, -6, -4},
    {-3, -5, 0, 11, 25, 35, 35, 26, 13, 0, -5, -4},
    {-3, -6, -1, 10, 24, 35, 36, 27, 14, 1, -5, -4},
    {-3, -6, -2, 10, 24, 34, 36, 28, 15, 2, -5, -5},
    {-2, -6, -2, 9, 23, 34, 36, 29, 15, 2, -5, -5},
    {-2, -6, -2, 8, 22, 33, 36, 30, 16, 3, -5, -5},
    {-1, -6, -3, 7, 21, 33, 36, 30, 17, 4, -4, -6},
    {-1, -6, -3, 6, 20, 32, 37, 31, 18, 4, -4, -6}
  },
  // 20/7 < ratio <= 15/4
  {
    {-9, 0, 9, 20, 28, 32, 28, 20, 9, 0, -9, 0},
    {-9, 0, 8, 19, 28, 32, 29, 20, 10, 0, -4, -5},
    {-9, -1, 8, 18, 28, 32, 29, 21, 10, 1, -4, -5},
    {-9, -1, 7, 18, 27, 32, 30, 22, 11, 1, -4, -6},
    {-8, -2, 6, 17, 27, 32, 30, 22, 12, 2, -4, -6},
    {-8, -2, 6, 16, 26, 32, 31, 23, 12, 2, -4, -6},
    {-8, -2, 5, 16, 26, 31, 31, 23, 13, 3, -3, -7},
    {-8, -3, 5, 15, 25, 31, 31, 24, 14, 4, -3, -7},
    {-7, -3, 4, 14, 25, 31, 31, 25, 14, 4, -3, -7},
    {-7, -3, 4, 14, 24, 31, 31, 25, 15, 5, -3, -8},
    {-7, -3, 3, 13, 23, 31, 31, 26, 16, 5, -2, -8},
    {-6, -4, 2, 12, 23, 31, 32, 26, 16, 6, -2, -8},
    {-6, -4, 2, 12, 22, 30, 32, 27, 17, 6, -2, -8},
    {-6, -4, 1, 11, 22, 30, 32, 27, 18, 7, -1, -9},
    {-5, -4, 1, 10, 21, 29, 32, 28, 18, 8, -1, -9},
    {-5, -4, 0, 10, 20, 29, 32, 28, 19, 8, 0, -9}
  },
  // ratio > 15/4
  {
    {-8, 7, 13, 18, 22, 24, 22, 18, 13, 7, 2, -10},
    {-8, 7, 13, 18, 22, 23, 22, 19, 13, 7, 2, -10},
    {-8, 6, 12, 18, 22, 23, 22, 19, 14, 8, 2, -10},
    {-9, 6, 12, 17, 22, 23, 23, 19, 14, 8, 3, -10},
    {-9, 6, 12, 17, 21, 23, 23, 19, 14, 9, 3, -10},
    {-9, 5, 11, 17, 21, 23, 23, 20, 15, 9, 3, -10},
    {-9, 5, 11, 16, 21, 23, 23, 20, 15, 9, 4, -10},
    {-9, 5, 10, 16, 21, 23, 23, 20, 15, 10, 4, -10},
    {-10, 5, 10, 16, 20, 23, 23, 20, 16, 10, 5, -10},
    {-10, 4, 10, 15, 20, 23, 23, 21, 16, 10, 5, -9},
    {-10, 4, 9, 15, 20, 23, 23, 21, 16, 11, 5, -9},
    {-10, 3, 9, 15, 20, 23, 23, 21, 17, 11, 5, -9},
    {-10, 3, 9, 14, 19, 23, 23, 21, 17, 12, 6, -9},
    {-10, 3, 8, 14, 19, 23, 23, 22, 17, 12, 6, -9},
    {-10, 2, 8, 14, 19, 22, 23, 22, 18, 12, 6, -8},
    {-10, 2, 7, 13, 19, 22, 23, 22, 18, 13, 7, -8}
  }
};

static const int lumaUpFilter[16][8] = {
  {0, 0, 0, 64, 0, 0, 0, 0},
  {0, 1, -3, 63, 4, -2, 1, 0},
  {-1, 2, -5, 62, 8, -3, 1, 0},
  {-1, 3, -8, 60, 13, -4, 1, 0},
  {-1, 4, -10, 58, 17, -5, 1, 0},
  {-1, 4, -11, 52, 26, -8, 3, -1},
  {-1, 3, -9, 47, 31, -10, 4, -1},
  {-1, 4, -11, 45, 34, -10, 4, -1},
  {-1, 4, -11, 40, 40, -11, 4, -1},
  {-1, 4, -10, 34, 45, -11, 4, -1},
  {-1, 4, -10, 31, 47, -9, 3, -1},
  {-1, 3, -8, 26, 52, -11, 4, -1},
  {0, 1, -5, 17, 58, -10, 4, -1},
  {0, 1, -4, 13, 60, -8, 3, -1},
  {0, 1, -3, 8, 62, -5, 2, -1},
  {0, 1, -2, 4, 63, -3, 1, 0}
};

static const int chromaUpFilter[16][4] = {
  {0, 64, 0, 0},
  {-2, 62, 4, 0},
  {-2, 58, 10, -2},
  {-4, 56, 14, -2},
  {-4, 54, 16, -2},
  {-6, 52, 20, -2},
  {-6, 46, 28, -4},
  {-4, 42, 30, -4},
  {-4, 36, 36, -4},
  {-4, 30, 42, -4},
  {-4, 28, 46, -6},
  {-2, 20, 52, -6},
  {-2, 16, 54, -4},
  {-2, 14, 56, -4},
  {-2, 10, 58, -2},
  {0, 4, 62, -2}
};

static const int downFilter1D[8][16*12] = {
  // ratio <= 20/19
  {
     0, 0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 2, -6, 127, 7, -2, 0, 0, 0, 0,
     0, 0, 0, 3, -12, 125, 16, -5, 1, 0, 0, 0,
     0, 0, 0, 4, -16, 120, 26, -7, 1, 0, 0, 0,
     0, 0, 0, 5, -18, 114, 36, -10, 1, 0, 0, 0,
     0, 0, 0, 5, -20, 107, 46, -12, 2, 0, 0, 0,
     0, 0, 0, 5, -21, 99, 57, -15, 3, 0, 0, 0,
     0, 0, 0, 5, -20, 89, 68, -18, 4, 0, 0, 0,
     0, 0, 0, 4, -19, 79, 79, -19, 4, 0, 0, 0,
     0, 0, 0, 4, -18, 68, 89, -20, 5, 0, 0, 0,
     0, 0, 0, 3, -15, 57, 99, -21, 5, 0, 0, 0,
     0, 0, 0, 2, -12, 46, 107, -20, 5, 0, 0, 0,
     0, 0, 0, 1, -10, 36, 114, -18, 5, 0, 0, 0,
     0, 0, 0, 1, -7, 26, 120, -16, 4, 0, 0, 0,
     0, 0, 0, 1, -5, 16, 125, -12, 3, 0, 0, 0,
     0, 0, 0, 0, -2, 7, 127, -6, 2, 0, 0, 0,
  },
  // 20/19 < ratio <= 5/4
  {
     0, 2, 0, -14, 33, 86, 33, -14, 0, 2, 0, 0,
     0, 1, 1, -14, 29, 85, 38, -13, -1, 2, 0, 0,
     0, 1, 2, -14, 24, 84, 43, -12, -2, 2, 0, 0,
     0, 1, 2, -13, 19, 83, 48, -11, -3, 2, 0, 0,
     0, 0, 3, -13, 15, 81, 53, -10, -4, 3, 0, 0,
     0, 0, 3, -12, 11, 79, 57, -8, -5, 3, 0, 0,
     0, 0, 3, -11, 7, 76, 62, -5, -7, 3, 0, 0,
     0, 0, 3, -10, 3, 73, 65, -2, -7, 3, 0, 0,
     0, 0, 3, -9, 0, 70, 70, 0, -9, 3, 0, 0,
     0, 0, 3, -7, -2, 65, 73, 3, -10, 3, 0, 0,
     0, 0, 3, -7, -5, 62, 76, 7, -11, 3, 0, 0,
     0, 0, 3, -5, -8, 57, 79, 11, -12, 3, 0, 0,
     0, 0, 3, -4, -10, 53, 81, 15, -13, 3, 0, 0,
     0, 0, 2, -3, -11, 48, 83, 19, -13, 2, 1, 0,
     0, 0, 2, -2, -12, 43, 84, 24, -14, 2, 1, 0,
     0, 0, 2, -1, -13, 38, 85, 29, -14, 1, 1, 0,
  },
  // 5/4 < ratio <= 5/3
  {
     0, 5, -6, -10, 37, 76, 37, -10, -6, 5, 0, 0,
     0, 5, -4, -11, 33, 76, 40, -9, -7, 5, 0, 0,
     -1, 5, -3, -12, 29, 75, 45, -7, -8, 5, 0, 0,
     -1, 4, -2, -13, 25, 75, 48, -5, -9, 5, 1, 0,
     -1, 4, -1, -13, 22, 73, 52, -3, -10, 4, 1, 0,
     -1, 4, 0, -13, 18, 72, 55, -1, -11, 4, 2, -1,
     -1, 4, 1, -13, 14, 70, 59, 2, -12, 3, 2, -1,
     -1, 3, 1, -13, 11, 68, 62, 5, -12, 3, 2, -1,
     -1, 3, 2, -13, 8, 65, 65, 8, -13, 2, 3, -1,
     -1, 2, 3, -12, 5, 62, 68, 11, -13, 1, 3, -1,
     -1, 2, 3, -12, 2, 59, 70, 14, -13, 1, 4, -1,
     -1, 2, 4, -11, -1, 55, 72, 18, -13, 0, 4, -1,
     0, 1, 4, -10, -3, 52, 73, 22, -13, -1, 4, -1,
     0, 1, 5, -9, -5, 48, 75, 25, -13, -2, 4, -1,
     0, 0, 5, -8, -7, 45, 75, 29, -12, -3, 5, -1,
     0, 0, 5, -7, -9, 40, 76, 33, -11, -4, 5, 0,
  },
  // 5/3 < ratio <= 2
  {
     2, -3, -9, 6, 39, 58, 39, 6, -9, -3, 2, 0,
     2, -3, -9, 4, 38, 58, 43, 7, -9, -4, 1, 0,
     2, -2, -9, 2, 35, 58, 44, 9, -8, -4, 1, 0,
     1, -2, -9, 1, 34, 58, 46, 11, -8, -5, 1, 0,
     1, -1, -8, -1, 31, 57, 47, 13, -7, -5, 1, 0,
     1, -1, -8, -2, 29, 56, 49, 15, -7, -6, 1, 1,
     1, 0, -8, -3, 26, 55, 51, 17, -7, -6, 1, 1,
     1, 0, -7, -4, 24, 54, 52, 19, -6, -7, 1, 1,
     1, 0, -7, -5, 22, 53, 53, 22, -5, -7, 0, 1,
     1, 1, -7, -6, 19, 52, 54, 24, -4, -7, 0, 1,
     1, 1, -6, -7, 17, 51, 55, 26, -3, -8, 0, 1,
     1, 1, -6, -7, 15, 49, 56, 29, -2, -8, -1, 1,
     0, 1, -5, -7, 13, 47, 57, 31, -1, -8, -1, 1,
     0, 1, -5, -8, 11, 46, 58, 34, 1, -9, -2, 1,
     0, 1, -4, -8, 9, 44, 58, 35, 2, -9, -2, 2,
     0, 1, -4, -9, 7, 43, 58, 38, 4, -9, -3, 2,
  },
  // 2 < ratio <= 5/2
  {
     -2, -7, 0, 17, 35, 43, 35, 17, 0, -7, -5, 2,
     -2, -7, -1, 16, 34, 43, 36, 18, 1, -7, -5, 2,
     -1, -7, -1, 14, 33, 43, 36, 19, 1, -6, -5, 2,
     -1, -7, -2, 13, 32, 42, 37, 20, 3, -6, -5, 2,
     0, -7, -3, 12, 31, 42, 38, 21, 3, -6, -5, 2,
     0, -7, -3, 11, 30, 42, 39, 23, 4, -6, -6, 1,
     0, -7, -4, 10, 29, 42, 40, 24, 5, -6, -6, 1,
     1, -7, -4, 9, 27, 41, 40, 25, 6, -5, -6, 1,
     1, -6, -5, 7, 26, 41, 41, 26, 7, -5, -6, 1,
     1, -6, -5, 6, 25, 40, 41, 27, 9, -4, -7, 1,
     1, -6, -6, 5, 24, 40, 42, 29, 10, -4, -7, 0,
     1, -6, -6, 4, 23, 39, 42, 30, 11, -3, -7, 0,
     2, -5, -6, 3, 21, 38, 42, 31, 12, -3, -7, 0,
     2, -5, -6, 3, 20, 37, 42, 32, 13, -2, -7, -1,
     2, -5, -6, 1, 19, 36, 43, 33, 14, -1, -7, -1,
     2, -5, -7, 1, 18, 36, 43, 34, 16, -1, -7, -2,
  },
  // 5/2 < ratio <= 20/7
  {
     -6, -3, 5, 19, 31, 36, 31, 19, 5, -3, -6, 0,
     -6, -4, 4, 18, 31, 37, 32, 20, 6, -3, -6, -1,
     -6, -4, 4, 17, 30, 36, 33, 21, 7, -3, -6, -1,
     -5, -5, 3, 16, 30, 36, 33, 22, 8, -2, -6, -2,
     -5, -5, 2, 15, 29, 36, 34, 23, 9, -2, -6, -2,
     -5, -5, 2, 15, 28, 36, 34, 24, 10, -2, -6, -3,
     -4, -5, 1, 14, 27, 36, 35, 24, 10, -1, -6, -3,
     -4, -5, 0, 13, 26, 35, 35, 25, 11, 0, -5, -3,
     -4, -6, 0, 12, 26, 36, 36, 26, 12, 0, -6, -4,
     -3, -5, 0, 11, 25, 35, 35, 26, 13, 0, -5, -4,
     -3, -6, -1, 10, 24, 35, 36, 27, 14, 1, -5, -4,
     -3, -6, -2, 10, 24, 34, 36, 28, 15, 2, -5, -5,
     -2, -6, -2, 9, 23, 34, 36, 29, 15, 2, -5, -5,
     -2, -6, -2, 8, 22, 33, 36, 30, 16, 3, -5, -5,
     -1, -6, -3, 7, 21, 33, 36, 30, 17, 4, -4, -6,
     -1, -6, -3, 6, 20, 32, 37, 31, 18, 4, -4, -6,
  },
  // 20/7 < ratio <= 15/4
  {
     -9, 0, 9, 20, 28, 32, 28, 20, 9, 0, -9, 0,
     -9, 0, 8, 19, 28, 32, 29, 20, 10, 0, -4, -5,
     -9, -1, 8, 18, 28, 32, 29, 21, 10, 1, -4, -5,
     -9, -1, 7, 18, 27, 32, 30, 22, 11, 1, -4, -6,
     -8, -2, 6, 17, 27, 32, 30, 22, 12, 2, -4, -6,
     -8, -2, 6, 16, 26, 32, 31, 23, 12, 2, -4, -6,
     -8, -2, 5, 16, 26, 31, 31, 23, 13, 3, -3, -7,
     -8, -3, 5, 15, 25, 31, 31, 24, 14, 4, -3, -7,
     -7, -3, 4, 14, 25, 31, 31, 25, 14, 4, -3, -7,
     -7, -3, 4, 14, 24, 31, 31, 25, 15, 5, -3, -8,
     -7, -3, 3, 13, 23, 31, 31, 26, 16, 5, -2, -8,
     -6, -4, 2, 12, 23, 31, 32, 26, 16, 6, -2, -8,
     -6, -4, 2, 12, 22, 30, 32, 27, 17, 6, -2, -8,
     -6, -4, 1, 11, 22, 30, 32, 27, 18, 7, -1, -9,
     -5, -4, 1, 10, 21, 29, 32, 28, 18, 8, -1, -9,
     -5, -4, 0, 10, 20, 29, 32, 28, 19, 8, 0, -9,
  },
  // ratio > 15/4
  {
     -8, 7, 13, 18, 22, 24, 22, 18, 13, 7, 2, -10,
     -8, 7, 13, 18, 22, 23, 22, 19, 13, 7, 2, -10,
     -8, 6, 12, 18, 22, 23, 22, 19, 14, 8, 2, -10,
     -9, 6, 12, 17, 22, 23, 23, 19, 14, 8, 3, -10,
     -9, 6, 12, 17, 21, 23, 23, 19, 14, 9, 3, -10,
     -9, 5, 11, 17, 21, 23, 23, 20, 15, 9, 3, -10,
     -9, 5, 11, 16, 21, 23, 23, 20, 15, 9, 4, -10,
     -9, 5, 10, 16, 21, 23, 23, 20, 15, 10, 4, -10,
     -10, 5, 10, 16, 20, 23, 23, 20, 16, 10, 5, -10,
     -10, 4, 10, 15, 20, 23, 23, 21, 16, 10, 5, -9,
     -10, 4, 9, 15, 20, 23, 23, 21, 16, 11, 5, -9,
     -10, 3, 9, 15, 20, 23, 23, 21, 17, 11, 5, -9,
     -10, 3, 9, 14, 19, 23, 23, 21, 17, 12, 6, -9,
     -10, 3, 8, 14, 19, 23, 23, 22, 17, 12, 6, -9,
     -10, 2, 8, 14, 19, 22, 23, 22, 18, 12, 6, -8,
     -10, 2, 7, 13, 19, 22, 23, 22, 18, 13, 7, -8,
  }
};

static const int lumaUpFilter1D[16*8] = {
    0, 0, 0, 64, 0, 0, 0, 0,
    0, 1, -3, 63, 4, -2, 1, 0,
    -1, 2, -5, 62, 8, -3, 1, 0,
    -1, 3, -8, 60, 13, -4, 1, 0,
    -1, 4, -10, 58, 17, -5, 1, 0,
    -1, 4, -11, 52, 26, -8, 3, -1,
    -1, 3, -9, 47, 31, -10, 4, -1,
    -1, 4, -11, 45, 34, -10, 4, -1,
    -1, 4, -11, 40, 40, -11, 4, -1,
    -1, 4, -10, 34, 45, -11, 4, -1,
    -1, 4, -10, 31, 47, -9, 3, -1,
    -1, 3, -8, 26, 52, -11, 4, -1,
    0, 1, -5, 17, 58, -10, 4, -1,
    0, 1, -4, 13, 60, -8, 3, -1,
    0, 1, -3, 8, 62, -5, 2, -1,
    0, 1, -2, 4, 63, -3, 1, 0
};

static const int chromaUpFilter1D[16*4] = {
    0, 64, 0, 0,
    -2, 62, 4, 0,
    -2, 58, 10, -2,
    -4, 56, 14, -2,
    -4, 54, 16, -2,
    -6, 52, 20, -2,
    -6, 46, 28, -4,
    -4, 42, 30, -4,
    -4, 36, 36, -4,
    -4, 30, 42, -4,
    -4, 28, 46, -6,
    -2, 20, 52, -6,
    -2, 16, 54, -4,
    -2, 14, 56, -4,
    -2, 10, 58, -2,
    0, 4, 62, -2 
};

//Used for clipping values
static int clip(int val, int min, int max)
{
  if (val <= min)
    return min;
  if (val >= max)
    return max;

  return val;
}

pic_buffer_t* kvz_newPictureBuffer(int width, int height, int has_tmp_row)
{
  pic_buffer_t* buffer = (pic_buffer_t*)malloc(sizeof(pic_buffer_t));
  if (buffer == NULL) {
    return NULL; //TODO: Add error message?
  }

  //Allocate enough memory to fit a width-by-height picture
  buffer->data = (pic_data_t*)malloc(sizeof(pic_data_t) * width * height);

  buffer->width = width;
  buffer->height = height;

  //Initialize tmp_row or set as NULL
  if (has_tmp_row) {
    //Use max dim for size
    int max_dim = SCALER_MAX(width, height);
    buffer->tmp_row = (pic_data_t*)malloc(sizeof(pic_data_t) * max_dim);
  }
  else {
    buffer->tmp_row = NULL;
  }

  return buffer;
}

yuv_buffer_t* kvz_newYuvBuffer(int width, int height , chroma_format_t format, int has_tmp_row)
{
  yuv_buffer_t* yuv = (yuv_buffer_t*)malloc(sizeof(yuv_buffer_t));
  if (yuv == NULL) {
    return NULL; //TODO: Add error message?
  }
  yuv->format =format;
  yuv->y = kvz_newPictureBuffer(width, height, has_tmp_row);

  int w_factor = 0;
  int h_factor = 0;

  switch (format) {
    case CHROMA_400:
      {
        //No chroma
        width = height = 0;
        break;
      }
    case CHROMA_420:
      {
        w_factor = 1;
        h_factor = 1;
        break;
      }
    case CHROMA_422:
      {
        w_factor = 1;
        break;
      }
    case CHROMA_444:
      {
        break;
      }
    default:
      assert(0);//Unsupported format
  }

  width = width >> w_factor;
  height = height >> h_factor;

  yuv->u = kvz_newPictureBuffer( width, height, has_tmp_row);
  yuv->v = kvz_newPictureBuffer( width, height, has_tmp_row);

  return yuv;
}

// ======================= newPictureBuffer_ ==================================
//TODO: DO something about the lack of overloading?
/**
* \brief Create/Initialize a Picture buffer. Width/height should be the width/height of the data. The caller is responsible for deallocation.
*/
//static pic_buffer_t* newPictureBuffer_double(const double* const data, int width, int height, int has_tmp_row)
//{
//  pic_buffer_t* buffer = kvz_newPictureBuffer(width, height, has_tmp_row);
//
//  //If data is null skip initializing
//  if (data == NULL) return buffer;
//
//  //Initialize buffer
//  for (int i = width * height - 1; i >= 0; i--) {
//    buffer->data[i] = (int)data[i];
//  }
//
//  return buffer;
//}
//
///**
//* \brief Create/Initialize a Picture buffer. Width/height should be the width/height of the data. The caller is responsible for deallocation.
//*/
//static pic_buffer_t* newPictureBuffer_uint8(const uint8_t* const data, int width, int height, int has_tmp_row)
//{
//  pic_buffer_t* buffer = kvz_newPictureBuffer(width, height, has_tmp_row);
//
//  //If data is null skip initializing
//  if (data == NULL) return buffer;
//
//  //Initialize buffer
//  for (int i = width * height - 1; i >= 0; i--) {
//    buffer->data[i] = (int)data[i];
//  }
//
//  return buffer;
//}

/**
* \brief Create/Initialize a Picture buffer. Width/height should be the width/height of the final buffer. Stride should be the width of the input (padded image). The caller is responsible for deallocation
*/
static pic_buffer_t* newPictureBuffer_padded_uint8(const uint8_t* const data, int width, int height, int stride, int has_tmp_row)
{
  pic_buffer_t* buffer = kvz_newPictureBuffer(width, height, has_tmp_row);

  //If data is null skip initializing
  if (data == NULL) return buffer;

  //Initialize buffer
  for (int row = 0; row < height; row++) {
    for (int col = 0; col < width; col++) {
      buffer->data[col + row*width] =  (pic_data_t)data[col + row*stride];
    }
  }

  return buffer;
}

// ==============================================================================
/**
 * \brief Deallocate a picture buffer.
 */
static void deallocatePictureBuffer(pic_buffer_t* buffer)
{
  if (buffer != NULL) {
    free(buffer->data);
    free(buffer->tmp_row);
  }
  free(buffer);
}

/**
 * \brief Copies data from one buffer to the other.
 * \param src is the source buffer
 * \param dst is the destination buffer
 * \param fill signals if the inds in dst not overlapped by src should be filled
*    with values adjacent to the said index.
 */
static void copyPictureBuffer(const pic_buffer_t* const src, const pic_buffer_t* const dst, int fill)
{
  //TODO: add checks. Check if fill is necessary
  //max_dim_* is chosen so that no over indexing happenes (src/dst)
  //min_dim_* is chosen so that no over indexing happenes (src), but all inds in dst get a value
  int max_dim_x = fill ? dst->width : SCALER_MIN(src->width, dst->width);
  int max_dim_y = fill ? dst->height : SCALER_MIN(src->height, dst->height);
  int min_dim_x = fill ? src->width : max_dim_x;
  int min_dim_y = fill ? src->height : max_dim_y;

  int dst_row = 0;
  int src_row = 0;

  //Copy loop
  for (int i = 0; i < max_dim_y; i++) {
    if (i < min_dim_y) {
      for (int j = 0; j < max_dim_x; j++) {
        //If outside min o_ind, copy adjacent value.
        dst->data[dst_row + j] = (j < min_dim_x) ? src->data[src_row + j] : dst->data[dst_row + j - 1];
      }
    }
    //Handle extra rows if needed
    else {
      for (int j = 0; j < max_dim_x; j++) {
        dst->data[dst_row + j] = dst->data[dst_row + j - dst->width];
      }
    }
    dst_row += dst->width; //switch to the next row
    src_row += src->width; //switch to the next row
  }
}

/**
* \brief Copies data from one buffer to the other.
* \param src is the source buffer
* \param dst is the destination buffer
* \param src/dst_x is the x-coordinate for the sub-block (needs to be valid for both buffers).
* \param src/dst_y is the y-coordinate for the sub-block (needs to be valid for both buffers).
* \param block_width is the width for the sub-block (needs to be valid for both buffers).
* \param block_height is the height for the sub-block (needs to be valid for both buffers).
*/
static void copyPictureBufferBlock(const pic_buffer_t* const src, const pic_buffer_t* const dst, const int src_x, const int src_y, const int dst_x, const int dst_y, const int block_width, const int block_height )
{
  for( int sy = src_y, dy = dst_y; sy < block_height && dy < block_height; sy++, dy++ ){
    for(int sx = src_x, dx = dst_x; sx < block_height && dx < block_height; sx++, dx++){
      dst->data[dx + dy*dst->width] = src->data[sx + sy*src->width];
    }
  }
}

/**
* \brief Copies data from one buffer to the other.
* \param src is the source buffer
* \param dst is the destination buffer
* \param fill signals if the inds in dst not overlapped by src should be filled
*    with values adjacent to the said index.
*/
static void copyYuvBuffer(const yuv_buffer_t* const src, const yuv_buffer_t* const dst, int fill)
{
  copyPictureBuffer(src->y, dst->y, fill);
  copyPictureBuffer(src->u, dst->u, fill);
  copyPictureBuffer(src->v, dst->v, fill);
}

/**
* \brief Copies data from a sub-block from one buffer to the other.
* \param src is the source buffer
* \param dst is the destination buffer
* \param src/dst_x is the x-coordinate for the sub-block (needs to be valid for both buffers).
* \param src/dst_y is the y-coordinate for the sub-block (needs to be valid for both buffers).
* \param block_width is the width for the sub-block (needs to be valid for both buffers).
* \param block_height is the height for the sub-block (needs to be valid for both buffers).
* \param w_factor is how much chroma sizes are scaled (width).
* \param h_factor is how much chroma sizes are scaled (heigth).
*/
static void copyYuvBufferBlock(const yuv_buffer_t* const src, const yuv_buffer_t* const dst, const int src_x, const int src_y, const int dst_x, const int dst_y, const int block_width, const int block_height, const int w_factor, const int h_factor)
{
  copyPictureBufferBlock(src->y, dst->y, src_x, src_y, dst_x, dst_y, block_width, block_height);
  copyPictureBufferBlock(src->u, dst->u, SCALER_SHIFT(src_x, w_factor), SCALER_SHIFT(src_y, h_factor), SCALER_SHIFT(dst_x, w_factor), SCALER_SHIFT(dst_y, h_factor), SCALER_SHIFT(block_width, w_factor), SCALER_SHIFT(block_height, h_factor));
  copyPictureBufferBlock(src->v, dst->v, SCALER_SHIFT(src_x, w_factor), SCALER_SHIFT(src_y, h_factor), SCALER_SHIFT(dst_x, w_factor), SCALER_SHIFT(dst_y, h_factor), SCALER_SHIFT(block_width, w_factor), SCALER_SHIFT(block_height, h_factor));
}

//Copy memory blocks between different types
static void copyMemBlock( void * const dst, const void * const src, const int dst_sizeof, const int src_sizeof, const int dst_stride, const int src_stride, const int dst_x, const int dst_y, const int src_x, const int src_y, const int block_width, const int block_height)
{
  //Cast to char pointers to allow indexing
  char * dst_char = &((char *)dst)[(dst_x + dst_y * dst_stride) * dst_sizeof];
  const char * src_char = &((const char *)src)[(src_x + src_y * src_stride) * src_sizeof];

  assert(sizeof(char)==1); //May not work if char is not one byte

  //Loop over rows
  for(int y = 0; y < block_height; y++){
    //Init dst to zero
    memset(dst_char, 0, block_width * dst_sizeof);
    //Copy row
    for(int x = 0; x < block_width; x++){
      memcpy(&dst_char[x*dst_sizeof], &src_char[x*src_sizeof], SCALER_MIN(dst_sizeof,src_sizeof));
    }
    
    //Move to next row
    dst_char += dst_stride * dst_sizeof;
    src_char += src_stride * src_sizeof;
  }
}

void kvz_copy_uint8_block_to_YuvBuffer(const yuv_buffer_t* dst, const uint8_t* const y, const uint8_t* const u, const uint8_t* const v, const int luma_stride, const int dst_x, const int dst_y, const int src_x, const int src_y, const int block_width, const int block_height, const int w_factor, const int h_factor){
  //TODO: Sanity checks

  copyMemBlock(dst->y->data, y, sizeof(pic_data_t), sizeof(uint8_t), dst->y->width, luma_stride, dst_x, dst_y, src_x, src_y, block_width, block_height);
  copyMemBlock(dst->u->data, u, sizeof(pic_data_t), sizeof(uint8_t), dst->u->width, SCALER_SHIFT(luma_stride, w_factor), SCALER_SHIFT(dst_x, w_factor), SCALER_SHIFT(dst_y, h_factor), SCALER_SHIFT(src_x, w_factor), SCALER_SHIFT(src_y, h_factor), SCALER_ROUND_SHIFT(block_width, w_factor), SCALER_ROUND_SHIFT(block_height, h_factor));
  copyMemBlock(dst->v->data, v, sizeof(pic_data_t), sizeof(uint8_t), dst->v->width, SCALER_SHIFT(luma_stride, w_factor), SCALER_SHIFT(dst_x, w_factor), SCALER_SHIFT(dst_y, h_factor), SCALER_SHIFT(src_x, w_factor), SCALER_SHIFT(src_y, h_factor), SCALER_ROUND_SHIFT(block_width, w_factor), SCALER_ROUND_SHIFT(block_height, h_factor));
}


void kvz_copy_YuvBuffer_block_to_uint8(uint8_t* const y, uint8_t* const u, uint8_t* const v, const int luma_stride, const yuv_buffer_t * const src, const int dst_x, const int dst_y, const int src_x, const int src_y, const int block_width, const int block_height, const int w_factor, const int h_factor){
  //TODO: Sanity checks
  copyMemBlock(y, src->y->data, sizeof(uint8_t), sizeof(pic_data_t), luma_stride, src->y->width, dst_x, dst_y, src_x, src_y, block_width, block_height);
  copyMemBlock(u, src->u->data, sizeof(uint8_t), sizeof(pic_data_t), SCALER_SHIFT(luma_stride, w_factor), src->u->width, SCALER_SHIFT(dst_x, w_factor), SCALER_SHIFT(dst_y, h_factor), SCALER_SHIFT(src_x, w_factor), SCALER_SHIFT(src_y, h_factor), SCALER_ROUND_SHIFT(block_width, w_factor), SCALER_ROUND_SHIFT(block_height, h_factor));
  copyMemBlock(v, src->v->data, sizeof(uint8_t), sizeof(pic_data_t), SCALER_SHIFT(luma_stride, w_factor), src->v->width, SCALER_SHIFT(dst_x, w_factor), SCALER_SHIFT(dst_y, h_factor), SCALER_SHIFT(src_x, w_factor), SCALER_SHIFT(src_y, h_factor), SCALER_ROUND_SHIFT(block_width, w_factor), SCALER_ROUND_SHIFT(block_height, h_factor));
}
// ======================= newYuvBuffer_ ==================================
//static yuv_buffer_t* newYuvBuffer_double(const double* const y_data, const double* const u_data, const double* const v_data, int width, int height, chroma_format_t format, int has_tmp_row)
//{
//  yuv_buffer_t* yuv = (yuv_buffer_t*)malloc(sizeof(yuv_buffer_t));
//  yuv->format = format;
//
//  //Allocate y pic_buffer
//  yuv->y = newPictureBuffer_double(y_data, width, height, has_tmp_row);
//
//  //Allocate u and v buffers
//  int w_factor = 0;
//  int h_factor = 0;
//
//  switch (format) {
//    case CHROMA_400:
//      {
//        //No chroma
//        width = height = 0;
//        break;
//      }
//    case CHROMA_420:
//      {
//        w_factor = 1;
//        h_factor = 1;
//        break;
//      }
//    case CHROMA_422:
//      {
//        w_factor = 1;
//        break;
//      }
//    case CHROMA_444:
//      {
//        break;
//      }
//    default:
//      assert(0);//Unsupported format
//  }
//
//  width = width >> w_factor;
//  height = height >> h_factor;
//  yuv->u = newPictureBuffer_double(u_data, width, height, has_tmp_row);
//  yuv->v = newPictureBuffer_double(v_data, width, height, has_tmp_row);
//
//  return yuv;
//}
//
//static yuv_buffer_t* newYuvBuffer_uint8(const uint8_t* const y_data, const uint8_t* const u_data, const uint8_t* const v_data, int width, int height, chroma_format_t format, int has_tmp_row)
//{
//  yuv_buffer_t* yuv = (yuv_buffer_t*)malloc(sizeof(yuv_buffer_t));
//
//  //Allocate y pic_buffer
//  yuv->y = newPictureBuffer_uint8(y_data, width, height, has_tmp_row);
//  yuv->format = format;
//
//  //Allocate u and v buffers
//  int w_factor = 0;
//  int h_factor = 0;
//
//  switch (format) {
//    case CHROMA_400: {
//      //No chroma
//      width = height = 0;
//      break;
//    }
//    case CHROMA_420: {
//      w_factor = 1;
//      h_factor = 1;
//      break;
//    }
//    case CHROMA_422: {
//      w_factor = 1;
//      break;
//    }
//    case CHROMA_444: {
//      break;
//    }
//    default:
//      assert(0);//Unsupported format
//  }
//
//  width = width >> w_factor;
//  height = height >> h_factor;
//  yuv->u = newPictureBuffer_uint8(u_data, width, height, has_tmp_row);
//  yuv->v = newPictureBuffer_uint8(v_data, width, height, has_tmp_row);
//
//  return yuv;
//}

yuv_buffer_t* kvz_newYuvBuffer_padded_uint8(const uint8_t* const y_data, const uint8_t* const u_data, const uint8_t* const v_data, int width, int height, int stride, chroma_format_t format, int has_tmp_row)
{
  yuv_buffer_t* yuv = (yuv_buffer_t*)malloc(sizeof(yuv_buffer_t));
  if (yuv == NULL) {
    return NULL; //TODO: Add error message?
  }
  //Allocate y pic_buffer
  yuv->y = newPictureBuffer_padded_uint8(y_data, width, height, stride, has_tmp_row);

  //Allocate u and v buffers
  int w_factor = 0;
  int h_factor = 0;

  switch (format) {
    case CHROMA_400: {
      //No chroma
      width = height = 0;
      break;
    }
    case CHROMA_420: {
      w_factor = 1;
      h_factor = 1;
      break;
    }
    case CHROMA_422: {
      w_factor = 1;
      break;
    }
    case CHROMA_444: {
      break;
    }
    default:
      assert(0);//Unsupported format
  }

  width = width >> w_factor;
  height = height >> h_factor;
  stride = stride >> w_factor;
  yuv->u = newPictureBuffer_padded_uint8(u_data, width, height, stride, has_tmp_row);
  yuv->v = newPictureBuffer_padded_uint8(v_data, width, height, stride, has_tmp_row);

  return yuv;
}

// ==============================================================================

/**
* \brief Clone the given pic buffer
*/
static pic_buffer_t* clonePictureBuffer(const pic_buffer_t* const pic)
{
  pic_buffer_t* ret = malloc(sizeof(pic_buffer_t));
  if (ret == NULL) {
    return NULL; //TODO: Add error message?
  }
  int size = pic->width * pic->height;

  *ret = *pic;
  ret->data = malloc(sizeof(pic_data_t) * size);
  if (ret->data == NULL) {
    free(ret);
    return NULL; //TODO: Add error message?
  }
  memcpy(ret->data, pic->data, size * sizeof(pic_data_t));

  if (pic->tmp_row) {
    int tmp_size = SCALER_MAX(pic->width, pic->height);
    ret->tmp_row = malloc(sizeof(pic_buffer_t) * tmp_size);
    if (ret->tmp_row == NULL) {
      deallocatePictureBuffer(ret);
      return NULL; //TODO: Add error message?
    }
    memcpy(ret->tmp_row, pic->tmp_row, tmp_size);
  }

  return ret;
}

yuv_buffer_t* kvz_cloneYuvBuffer(const yuv_buffer_t* const yuv)
{
  yuv_buffer_t* ret = malloc(sizeof(yuv_buffer_t));
  if (ret == NULL) {
    return NULL; //TODO: Add error message?
  }
  ret->y = clonePictureBuffer(yuv->y);
  ret->u = clonePictureBuffer(yuv->u);
  ret->v = clonePictureBuffer(yuv->v);

  return ret;
}

void kvz_deallocateYuvBuffer(yuv_buffer_t* yuv)
{
  if (yuv == NULL) return;

  deallocatePictureBuffer(yuv->y);
  deallocatePictureBuffer(yuv->u);
  deallocatePictureBuffer(yuv->v);

  free(yuv);
}


//Helper function for choosing the correct filter
//Returns the size of the filter and the filter param is set to the correct filter
static int getFilter(const int** const filter, int is_upsampling, int is_luma, int phase, int filter_ind)
{
  if (is_upsampling) {
    //Upsampling so use 8- or 4-tap filters
    if (is_luma) {
      *filter = lumaUpFilter[phase];
      return sizeof(lumaUpFilter[0]) / sizeof(lumaUpFilter[0][0]);
    }

    *filter = chromaUpFilter[phase];
    return sizeof(chromaUpFilter[0]) / sizeof(chromaUpFilter[0][0]);
  }

  //Downsampling so use 12-tap filter
  *filter = downFilter[filter_ind][phase];
  return (sizeof(downFilter[0][0]) / sizeof(downFilter[0][0][0]));
}

//Helper function for choosing the correct filter
//Returns the size of the filter and the filter param is set to the correct filter
static int prepareFilter(const int** const filter, int is_upsampling, int is_luma, int filter_ind)
{
  if (is_upsampling) {
    //Upsampling so use 8- or 4-tap filters
    if (is_luma) {
      *filter = lumaUpFilter1D;
      return sizeof(lumaUpFilter[0]) / sizeof(lumaUpFilter[0][0]);
    }

    *filter = chromaUpFilter1D;
    return sizeof(chromaUpFilter[0]) / sizeof(chromaUpFilter[0][0]);
  }

  //Downsampling so use 12-tap filter
  *filter = downFilter1D[filter_ind];
  return (sizeof(downFilter[0][0]) / sizeof(downFilter[0][0][0]));
}

#define getFilterCoeff(filter, stride, phase, ind) ((filter)[(ind)+(phase)*(stride)])

//Resampling is done here per buffer
static void _resample(const pic_buffer_t* const buffer, const scaling_parameter_t* const param, const int is_upscaling, const int is_luma)
{
  //TODO: Add cropping etc.

  //Choose best filter to use when downsampling
  //Need to use rounded values (to the closest multiple of 2,4,16 etc.)?
  int ver_filter = 0;
  int hor_filter = 0;

  int src_height = param->src_height;
  int src_width = param->src_width;
  int trgt_height = param->trgt_height;//param->rnd_trgt_height;
  int trgt_width = param->trgt_width;//param->rnd_trgt_width;

  if (!is_upscaling) {
    int crop_width = src_width - param->right_offset; //- param->left_offset;
    int crop_height = src_height - param->bottom_offset; //- param->top_offset;

    if (4 * crop_height > 15 * trgt_height)
      ver_filter = 7;
    else if (7 * crop_height > 20 * trgt_height)
      ver_filter = 6;
    else if (2 * crop_height > 5 * trgt_height)
      ver_filter = 5;
    else if (1 * crop_height > 2 * trgt_height)
      ver_filter = 4;
    else if (3 * crop_height > 5 * trgt_height)
      ver_filter = 3;
    else if (4 * crop_height > 5 * trgt_height)
      ver_filter = 2;
    else if (19 * crop_height > 20 * trgt_height)
      ver_filter = 1;

    if (4 * crop_width > 15 * trgt_width)
      hor_filter = 7;
    else if (7 * crop_width > 20 * trgt_width)
      hor_filter = 6;
    else if (2 * crop_width > 5 * trgt_width)
      hor_filter = 5;
    else if (1 * crop_width > 2 * trgt_width)
      hor_filter = 4;
    else if (3 * crop_width > 5 * trgt_width)
      hor_filter = 3;
    else if (4 * crop_width > 5 * trgt_width)
      hor_filter = 2;
    else if (19 * crop_width > 20 * trgt_width)
      hor_filter = 1;
  }

  int shift_x = param->shift_x - 4;
  int shift_y = param->shift_y - 4;

  pic_data_t* tmp_row = buffer->tmp_row;

  // Horizontal downsampling
  for (int i = 0; i < src_height; i++) {
    pic_data_t* src_row = &buffer->data[i * buffer->width];

    for (int j = 0; j < trgt_width; j++) {
      //Calculate reference position in src pic
      int ref_pos_16 = (int)((unsigned int)(j * param->scale_x + param->add_x) >> shift_x)  - param->delta_x;
      int phase = ref_pos_16 & 15;
      int ref_pos = ref_pos_16 >> 4;

      //Choose filter
      const int* filter;
      int size = getFilter(&filter, is_upscaling, is_luma, phase, hor_filter);

      //Apply filter
      tmp_row[j] = 0;
      for (int k = 0; k < size; k++) {
        int m = clip(ref_pos + k - (size >> 1) + 1, 0, src_width - 1);
        tmp_row[j] += filter[k] * src_row[m];
      }
    }
    //Copy tmp row to data
    memcpy(src_row, tmp_row, sizeof(pic_data_t) * trgt_width);
  }

  pic_data_t* tmp_col = tmp_row; //rename for clarity

  // Vertical downsampling
  for (int i = 0; i < trgt_width; i++) {
    pic_data_t* src_col = &buffer->data[i];
    for (int j = 0; j < trgt_height; j++) {
      //Calculate ref pos
      int ref_pos_16 = (int)((unsigned int)(j * param->scale_y + param->add_y) >> shift_y) - param->delta_y;
      int phase = ref_pos_16 & 15;
      int ref_pos = ref_pos_16 >> 4;

      //Choose filter
      const int* filter;
      int size = getFilter(&filter, is_upscaling, is_luma, phase, ver_filter);

      //Apply filter
      tmp_col[j] = 0;
      for (int k = 0; k < size; k++) {
        int m = clip(ref_pos + k - (size >> 1) + 1, 0, src_height - 1);
        tmp_col[j] += filter[k] * src_col[m * buffer->width];
      }
      //TODO: Why? Filter coefs summ up to 128 applied 2x 128*128= 2^14
      //TODO: Why?  Filter coefs summ up to 64 applied 2x 64*64= 2^12
      //Scale values back down
      tmp_col[j] = is_upscaling ? (tmp_col[j] + 2048) >> 12 : (tmp_col[j] + 8192) >> 14;
    }

    //Clip and move to buffer data
    for (int n = 0; n < trgt_height; n++) {
      src_col[n * buffer->width] = clip(tmp_col[n], 0, 255);
    }
  }
}

//Resampling is done here per buffer
static void resample(const pic_buffer_t* const buffer, const scaling_parameter_t* const param, const int is_upscaling, const int is_luma)
{
  //TODO: Add cropping etc.

  //Choose best filter to use when downsampling
  //Need to use rounded values (to the closest multiple of 2,4,16 etc.)?
  int ver_filter = 0;
  int hor_filter = 0;

  int src_width = param->src_width + param->src_padding_x;
  int src_height = param->src_height + param->src_padding_y;
  int trgt_width = param->rnd_trgt_width;
  int trgt_height = param->rnd_trgt_height;

  if (!is_upscaling) {
    int crop_width = src_width - param->right_offset; //- param->left_offset;
    int crop_height = src_height - param->bottom_offset; //- param->top_offset;

    if (4 * crop_height > 15 * trgt_height)
      ver_filter = 7;
    else if (7 * crop_height > 20 * trgt_height)
      ver_filter = 6;
    else if (2 * crop_height > 5 * trgt_height)
      ver_filter = 5;
    else if (1 * crop_height > 2 * trgt_height)
      ver_filter = 4;
    else if (3 * crop_height > 5 * trgt_height)
      ver_filter = 3;
    else if (4 * crop_height > 5 * trgt_height)
      ver_filter = 2;
    else if (19 * crop_height > 20 * trgt_height)
      ver_filter = 1;

    if (4 * crop_width > 15 * trgt_width)
      hor_filter = 7;
    else if (7 * crop_width > 20 * trgt_width)
      hor_filter = 6;
    else if (2 * crop_width > 5 * trgt_width)
      hor_filter = 5;
    else if (1 * crop_width > 2 * trgt_width)
      hor_filter = 4;
    else if (3 * crop_width > 5 * trgt_width)
      hor_filter = 3;
    else if (4 * crop_width > 5 * trgt_width)
      hor_filter = 2;
    else if (19 * crop_width > 20 * trgt_width)
      hor_filter = 1;
  }

  const int *filter_hor;
  const int filter_size_hor = prepareFilter(&filter_hor, is_upscaling, is_luma, hor_filter);
  const int *filter_ver;
  const int filter_size_ver = prepareFilter(&filter_ver, is_upscaling, is_luma, ver_filter);

  int shift_x = param->shift_x - 4;
  int shift_y = param->shift_y - 4;

  pic_data_t* tmp_row = buffer->tmp_row;

  // Horizontal resampling
  for (int i = 0; i < src_height; i++) {
    pic_data_t* src_row = &buffer->data[i * buffer->width];

    for (int j = 0; j < trgt_width; j++) {
      //Calculate reference position in src pic
      int ref_pos_16 = (int)((unsigned int)(j * param->scale_x + param->add_x) >> shift_x) - param->delta_x;
      int phase = ref_pos_16 & 15;
      int ref_pos = ref_pos_16 >> 4;

      //Choose filter
      const int* filter;
      //int size = getFilter(&filter, is_upscaling, is_luma, phase, hor_filter);
      int size = filter_size_hor;
      filter = &getFilterCoeff(filter_hor, size, phase, 0);

      //Apply filter
      tmp_row[j] = 0;
      for (int k = 0; k < size; k++) {
        int m = SCALER_CLIP(ref_pos + k - (size >> 1) + 1, 0, src_width - 1);
        tmp_row[j] += filter[k] * src_row[m];
      }
    }
    //Copy tmp row to data
    memcpy(src_row, tmp_row, sizeof(pic_data_t) * trgt_width);
  }

  pic_data_t* tmp_col = tmp_row; //rename for clarity

  // Vertical resampling. TODO: Why this order? should loop over rows not cols?
  for (int i = 0; i < trgt_width; i++) {
    pic_data_t* src_col = &buffer->data[i];
    for (int j = 0; j < trgt_height; j++) {
      //Calculate ref pos
      int ref_pos_16 = (int)((unsigned int)(j * param->scale_y + param->add_y) >> shift_y) - param->delta_y;
      int phase = ref_pos_16 & 15;
      int ref_pos = ref_pos_16 >> 4;

      //Choose filter
      const int* filter;
      //int size = getFilter(&filter, is_upscaling, is_luma, phase, ver_filter);
      int size = filter_size_ver;
      filter = &getFilterCoeff(filter_ver, size, phase, 0);

      //Apply filter
      tmp_col[j] = 0;
      for (int k = 0; k < size; k++) {
        int m = SCALER_CLIP(ref_pos + k - (size >> 1) + 1, 0, src_height - 1);
        tmp_col[j] += filter[k] * src_col[m * buffer->width];
      }
      //TODO: Why? Filter coefs summ up to 128 applied 2x 128*128= 2^14
      //TODO: Why?  Filter coefs summ up to 64 applied 2x 64*64= 2^12
      //Scale values back down
      tmp_col[j] = is_upscaling ? (tmp_col[j] + 2048) >> 12 : (tmp_col[j] + 8192) >> 14;
    }

    //Clip and move to buffer data
    for (int n = 0; n < trgt_height; n++) {
      src_col[n * buffer->width] = SCALER_CLIP(tmp_col[n], 0, 255);
    }
  }
}

//Do only the vertical/horizontal resampling step in the given block
static void resampleBlockStep(const pic_buffer_t* const src_buffer, const pic_buffer_t *const trgt_buffer, const int src_offset, const int trgt_offset, const int block_x, const int block_y, const int block_width, const int block_height,  const scaling_parameter_t* const param, const int is_upscaling, const int is_luma, const int is_vertical){
  //TODO: Add cropping etc.

  //Choose best filter to use when downsampling
  //Need to use rounded values (to the closest multiple of 2,4,16 etc.)?
  int filter_phase = 0;

  const int src_size = is_vertical ? param->src_height + param->src_padding_y : param->src_width + param->src_padding_x;
  const int trgt_size = is_vertical ? param->rnd_trgt_height : param->rnd_trgt_width;

  if (!is_upscaling) {
    int crop_size = src_size - ( is_vertical ? param->bottom_offset : param->right_offset); //- param->left_offset/top_offset;
    if (4 * crop_size > 15 * trgt_size)
      filter_phase = 7;
    else if (7 * crop_size > 20 * trgt_size)
      filter_phase = 6;
    else if (2 * crop_size > 5 * trgt_size)
      filter_phase = 5;
    else if (1 * crop_size > 2 * trgt_size)
      filter_phase = 4;
    else if (3 * crop_size > 5 * trgt_size)
      filter_phase = 3;
    else if (4 * crop_size > 5 * trgt_size)
      filter_phase = 2;
    else if (19 * crop_size > 20 * trgt_size)
      filter_phase = 1;
  }

  const int shift = (is_vertical ? param->shift_y : param->shift_x) - 4;
  const int scale = is_vertical ? param->scale_y : param->scale_x;
  const int add = is_vertical ? param->add_y : param->add_x;
  const int delta = is_vertical ? param->delta_y : param->delta_x;

  //Set loop parameters based on the resampling dir
  const int *filter;
  const int filter_size = prepareFilter(&filter, is_upscaling, is_luma, filter_phase);
  const int outer_init = is_vertical ? 0 : block_x;
  const int outer_bound = is_vertical ? filter_size : block_x + block_width;
  const int inner_init = is_vertical ? block_x : 0;
  const int inner_bound = is_vertical ? block_x + block_width : filter_size;
  const int s_stride = is_vertical ? src_buffer->width : 1; //Multiplier to s_ind

  //Do resampling (vertical/horizontal) of the specified block into trgt_buffer using src_buffer
  for (int y = block_y; y < (block_y + block_height); y++) {

    pic_data_t* src = is_vertical ? src_buffer->data : &src_buffer->data[y * src_buffer->width];
    pic_data_t* trgt_row = &trgt_buffer->data[y * trgt_buffer->width];

    //Outer loop:
    //  if is_vertical -> loop over k (filter inds)
    //  if !is_vertical -> loop over x (block width)
    for (int o_ind = outer_init; o_ind < outer_bound; o_ind++) {
      
      const int t_ind = is_vertical ? y : o_ind; //trgt_buffer row/col index for cur resampling dir

      //Inner loop:
      //  if is_vertical -> loop over x (block width)
      //  if !is_vertical -> loop over k (filter inds)-
      for (int i_ind = inner_init; i_ind < inner_bound; i_ind++) {

        const int f_ind = is_vertical ? o_ind : i_ind; //Filter index
        const int t_col = is_vertical ? i_ind : o_ind; //trgt_buffer column

        //Calculate reference position in src pic
        int ref_pos_16 = (int)((unsigned int)(t_ind * scale + add) >> shift) - delta;
        int phase = ref_pos_16 & 15;
        int ref_pos = ref_pos_16 >> 4;

        //Choose filter
        //const int *filter;
        //const int f_size = getFilter(&filter, is_upscaling, is_luma, phase, filter_phase);

        //Set trgt buffer val to zero on first loop over filter
        if( f_ind == 0 ){
          trgt_row[t_col + trgt_offset] = 0;
        }

        const int s_ind = SCALER_CLIP(ref_pos + f_ind - (filter_size >> 1) + 1, 0, src_size - 1); //src_buffer row/col index for cur resampling dir

        //Move src pointer to correct position (correct column in vertical resampling)
        pic_data_t *src_col = src + (is_vertical ? i_ind : 0);
        trgt_row[t_col + trgt_offset] += getFilterCoeff(filter,filter_size,phase,f_ind) * src_col[s_ind * s_stride + src_offset];

        //Scale values in trgt buffer to the correct range. Only done in the final loop over o_ind (block width)
        if (is_vertical && o_ind == outer_bound - 1) {
          trgt_row[t_col + trgt_offset] = SCALER_CLIP(is_upscaling ? (trgt_row[t_col + trgt_offset] + 2048) >> 12 : (trgt_row[t_col + trgt_offset] + 8192) >> 14, 0, 255);
        }
      }
    }
  }

}

//Do the resampling in one pass using 2D-convolution.
static void resampleBlock( const pic_buffer_t* const src_buffer, const scaling_parameter_t* const param, const int is_upscaling, const int is_luma, const pic_buffer_t *const trgt_buffer, const int trgt_offset, const int block_x, const int block_y, const int block_width, const int block_height )
{
  //TODO: Add cropping etc.

  //Choose best filter to use when downsampling
  //Need to use rounded values (to the closest multiple of 2,4,16 etc.)?
  int ver_filter = 0;
  int hor_filter = 0;

  int src_width = param->src_width + param->src_padding_x;
  int src_height = param->src_height + param->src_padding_y;
  int trgt_width = param->rnd_trgt_width;
  int trgt_height = param->rnd_trgt_height;
  int trgt_stride = trgt_buffer->width;

  if (!is_upscaling) {
    int crop_width = src_width - param->right_offset; //- param->left_offset;
    int crop_height = src_height - param->bottom_offset; //- param->top_offset;

    if (4 * crop_height > 15 * trgt_height)
      ver_filter = 7;
    else if (7 * crop_height > 20 * trgt_height)
      ver_filter = 6;
    else if (2 * crop_height > 5 * trgt_height)
      ver_filter = 5;
    else if (1 * crop_height > 2 * trgt_height)
      ver_filter = 4;
    else if (3 * crop_height > 5 * trgt_height)
      ver_filter = 3;
    else if (4 * crop_height > 5 * trgt_height)
      ver_filter = 2;
    else if (19 * crop_height > 20 * trgt_height)
      ver_filter = 1;

    if (4 * crop_width > 15 * trgt_width)
      hor_filter = 7;
    else if (7 * crop_width > 20 * trgt_width)
      hor_filter = 6;
    else if (2 * crop_width > 5 * trgt_width)
      hor_filter = 5;
    else if (1 * crop_width > 2 * trgt_width)
      hor_filter = 4;
    else if (3 * crop_width > 5 * trgt_width)
      hor_filter = 3;
    else if (4 * crop_width > 5 * trgt_width)
      hor_filter = 2;
    else if (19 * crop_width > 20 * trgt_width)
      hor_filter = 1;
  }

  int shift_x = param->shift_x - 4;
  int shift_y = param->shift_y - 4;

  //Get the pointer to the target and source data.
  pic_data_t *src = src_buffer->data;
  pic_data_t *trgt = trgt_buffer->data; //&trgt_buffer->data[block_x + block_y*trgt_buffer->width];
  
  //Loop over every pixel in the target block and calculate the 2D-convolution to get the resampled value for the given pixel
  for( int y = block_y; y < (block_y + block_height); y++ ){
    for( int x = block_x; x < (block_x + block_width); x++ ){
      
      //Calculate reference position in src pic
      int ref_pos_x_16 = (int)((unsigned int)(x * param->scale_x + param->add_x) >> shift_x) - param->delta_x;
      int ref_pos_y_16 = (int)((unsigned int)(y * param->scale_y + param->add_y) >> shift_y) - param->delta_y;

      int phase_x = ref_pos_x_16 & 15;
      int phase_y = ref_pos_y_16 & 15;      

      int ref_pos_x = ref_pos_x_16 >> 4;
      int ref_pos_y = ref_pos_y_16 >> 4;

      //Choose filter
      const int *filter_x;
      const int *filter_y;
      const int size_x = getFilter(&filter_x, is_upscaling, is_luma, phase_x, hor_filter);
      const int size_y = getFilter(&filter_y, is_upscaling, is_luma, phase_y, ver_filter);

      pic_data_t new_val = 0; //Accumulate the new pixel value here

      //Convolution kernel, where (o_ind,y)<=>(0,0)
      //Size of kernel depends on the filter size
      for( int j = 0; j < size_y; j++ ){
        //Calculate src sample position for kernel element (i,j)
        int m_y = clip(ref_pos_y + j - (size_y >> 1) + 1, 0, src_height - 1);

        for (int i = 0; i < size_x; i++) {
          //Calculate src sample position for kernel element (i,j)
          int m_x = clip( ref_pos_x + i - (size_x >> 1) + 1, 0, src_width - 1);

          //Calculate filter value in the 2D-filter for pos (i,j) and apply to sample (m_x,m_y)
          new_val += filter_x[i]*filter_y[j] * src[m_x + m_y*src_buffer->width];
        }
      }

      //Scale and clip values and save to trgt buffer.
      //trgt offset is used to reposition the block in trgt in case buffer size is not target image size
      trgt[x + y*trgt_stride + trgt_offset] = clip(is_upscaling ? (new_val + 2048) >> 12 : (new_val + 8192) >> 14, 0, 255); //TODO: account for different bit dept / different filters etc
    }  
  }
}

//Calculate scaling parameters and update param. Factor determines if certain values are 
// divided eg. with chroma. 0 for no factor and -1 for halving stuff and 1 for doubling etc.
//Calculations from SHM
static void calculateParameters(scaling_parameter_t* const param, const int w_factor, const int h_factor, const int is_chroma)
{
  //First shift widths/height by an appropriate factor
  param->src_width = SCALER_SHIFT(param->src_width, w_factor);
  param->src_height = SCALER_SHIFT(param->src_height, h_factor);
  param->trgt_width = SCALER_SHIFT(param->trgt_width, w_factor);
  param->trgt_height = SCALER_SHIFT(param->trgt_height, h_factor);
  param->scaled_src_width = SCALER_SHIFT(param->scaled_src_width, w_factor);
  param->scaled_src_height = SCALER_SHIFT(param->scaled_src_height, h_factor);
  param->rnd_trgt_width = SCALER_SHIFT(param->rnd_trgt_width, w_factor);
  param->rnd_trgt_height = SCALER_SHIFT(param->rnd_trgt_height, h_factor);
  param->src_padding_x = SCALER_SHIFT(param->src_padding_x, w_factor);
  param->src_padding_y = SCALER_SHIFT(param->src_padding_y, h_factor);
  param->trgt_padding_x = SCALER_SHIFT(param->trgt_padding_x, w_factor);
  param->trgt_padding_y = SCALER_SHIFT(param->trgt_padding_y, h_factor);

  //Calculate sample positional parameters
  param->right_offset = param->src_width - param->scaled_src_width; //- left_offset
  param->bottom_offset = param->src_height - param->scaled_src_height; //- top_offset

  //TODO: Make dependant on width/height?
  param->shift_x = SCALER_SHIFT_CONST;
  param->shift_y = SCALER_SHIFT_CONST;

  param->scale_x = (((unsigned int)param->scaled_src_width << param->shift_x) + (param->rnd_trgt_width >> 1)) / param->rnd_trgt_width;
  param->scale_y = (((unsigned int)param->scaled_src_height << param->shift_y) + (param->rnd_trgt_height >> 1)) / param->rnd_trgt_height;

  //Phase calculations
  //param->phase_x = 0;
  //param->phase_y = 0;
  int phase_x = 0;
  int phase_y = 0;
  //Hardcode phases for chroma, values from SHM. TODO: Find out why these values?
  if( is_chroma != 0 && param->chroma!=CHROMA_444 ) {
    //param->phase_y = 1;
    phase_y = 1;
  }

  //TODO: Is delta_? strictly necessary?
  param->add_x = (((param->scaled_src_width * phase_x) << (param->shift_x - 2)) + (param->rnd_trgt_width >> 1)) / param->rnd_trgt_width + (1 << (param->shift_x - 5));
  param->add_y = (((param->scaled_src_height * phase_y) << (param->shift_y - 2)) + (param->rnd_trgt_height >> 1)) / param->rnd_trgt_height + (1 << (param->shift_y - 5));
  //param->add_x = -(((phase_x * param->scale_x + 8) >> 4 ) - (1 << (param->shift_x - 5)));
  //param->add_y = -(((phase_y * param->scale_y + 8) >> 4 ) - (1 << (param->shift_y - 5)));

  param->delta_x = 4 * phase_x; //- (left_offset << 4)
  param->delta_y = 4 * phase_y; //- (top_offset << 4)
}

scaling_parameter_t kvz_newScalingParameters(int src_width, int src_height, int trgt_width, int trgt_height, chroma_format_t chroma)
{
  scaling_parameter_t param = {
    .src_width = src_width,
    .src_height = src_height,
    .trgt_width = trgt_width,
    .trgt_height = trgt_height,
    .chroma = chroma,
    .src_padding_x = 0,
    .src_padding_y = 0,
    .trgt_padding_x = 0,
    .trgt_padding_y = 0
  };

  //Calculate Resampling parameters
  //Calculations from SHM
  int hor_div = param.trgt_width << 1;
  int ver_div = param.trgt_height << 1;

  param.rnd_trgt_width = ((param.trgt_width + (1 << 4) - 1) >> 4) << 4; //Round to multiple of 16
  param.rnd_trgt_height = ((param.trgt_height + (1 << 4) - 1) >> 4) << 4; //Round to multiple of 16

  //Round to multiple of 2
  //TODO: Why SCALER_MAX? Try using src
  int scaled_src_width = param.src_width;//SCALER_MAX(param.src_width, param.trgt_width); //Min/max of source/target values
  int scaled_src_height = param.src_height;//SCALER_MAX(param.src_height, param.trgt_size); //Min/max of source/target values
  param.scaled_src_width = ((scaled_src_width * param.rnd_trgt_width + (hor_div >> 1)) / hor_div) << 1;
  param.scaled_src_height = ((scaled_src_height * param.rnd_trgt_height + (ver_div >> 1)) / ver_div) << 1;

  //Pre-Calculate other parameters
  calculateParameters(&param, 0, 0, 0);

  return param;
}

scaling_parameter_t kvz_newScalingParameters_(int src_width, int src_height, int trgt_width, int trgt_height, chroma_format_t chroma)
{
  scaling_parameter_t param = {
    .src_width = src_width,
    .src_height = src_height,
    .trgt_width = trgt_width,
    .trgt_height = trgt_height,
    .chroma = chroma
  };

  //Calculate Resampling parameters
  //Calculations from SHM
  int hor_div = param.trgt_width << 1;
  int ver_div = param.trgt_height << 1;

  param.rnd_trgt_width = param.trgt_width;//((param.trgt_width + (1 << 4) - 1) >> 4) << 4; //Round to multiple of 16
  param.rnd_trgt_height = param.trgt_height;//((param.trgt_size + (1 << 4) - 1) >> 4) << 4; //Round to multiple of 16

  //Round to multiple of 2
  //TODO: Why SCALER_MAX? Try using src
  int scaled_src_width = param.src_width;//SCALER_MAX(param.src_width, param.trgt_width); //Min/max of source/target values
  int scaled_src_height = param.src_height;//SCALER_MAX(param.src_height, param.trgt_size); //Min/max of source/target values
  param.scaled_src_width = ((scaled_src_width * param.rnd_trgt_width + (hor_div >> 1)) / hor_div) << 1;
  param.scaled_src_height = ((scaled_src_height * param.rnd_trgt_height + (ver_div >> 1)) / ver_div) << 1;

  //Pre-Calculate other parameters
  calculateParameters(&param, 0, 0, 0);

  return param;
}


chroma_format_t kvz_getChromaFormat(int luma_width, int luma_height, int chroma_width, int chroma_height)
{
  if (chroma_width == 0 && chroma_height == 0) {
    return CHROMA_400;
  }
  if (chroma_width == luma_width && chroma_height == luma_height) {
    return CHROMA_444;
  }
  if (chroma_width == (luma_width >> 1) && chroma_height == (luma_height)) {
    return CHROMA_422;
  }
  //If not CHROMA_420, not a supported format
  assert(chroma_width == (luma_width >> 1) && chroma_height == (luma_height >> 1));

  return CHROMA_420;
}


yuv_buffer_t* kvz_yuvScaling(const yuv_buffer_t* const yuv, const scaling_parameter_t* const base_param,
                         yuv_buffer_t* dst)
{
  /*========== Basic Initialization ==============*/
  //Initialize basic parameters
  scaling_parameter_t param = *base_param;

  //How much to scale the luma sizes to get the chroma sizes
  int w_factor = 0;
  int h_factor = 0;
  switch (param.chroma) {
    case CHROMA_400: {
      //No chroma
      assert(yuv->u->height == 0 && yuv->u->width == 0 && yuv->v->height == 0 && yuv->v->width == 0);
      break;
    }
    case CHROMA_420: {
      assert(yuv->u->height == (yuv->y->height >> 1) && yuv->u->width == (yuv->y->width >> 1)
        && yuv->v->height == (yuv->y->height >> 1) && yuv->v->width == (yuv->y->width >> 1));
      w_factor = -1;
      h_factor = -1;
      break;
    }
    case CHROMA_422: {
      assert(yuv->u->height == (yuv->y->height) && yuv->u->width == (yuv->y->width >> 1)
        && yuv->v->height == (yuv->y->height) && yuv->v->width == (yuv->y->width >> 1));
      w_factor = -1;
      break;
    }
    case CHROMA_444: {
      assert(yuv->u->height == (yuv->y->height) && yuv->u->width == (yuv->y->width)
        && yuv->v->height == (yuv->y->height) && yuv->v->width == (yuv->y->width));
      break;
    }
    default:
      assert(0); //Unsupported chroma type
  }

  //Check if base param and yuv buffer are the same size, if yes we can asume parameters are initialized
  if (yuv->y->width != param.src_width + param.src_padding_x || yuv->y->height != param.src_height + param.src_padding_y) {
    param.src_width = yuv->y->width - param.src_padding_x;
    param.src_height = yuv->y->height - param.src_padding_y;
    calculateParameters(&param, 0, 0, 0);
  }

  //Check if we need to allocate a yuv buffer for the new image or re-use dst.
  //Make sure the sizes match
  if (dst == NULL || dst->y->width != param.trgt_width || dst->y->height != param.trgt_height
    || dst->u->width != SCALER_SHIFT(param.trgt_width, w_factor) || dst->u->height != SCALER_SHIFT(param.trgt_height, h_factor)
    || dst->v->width != SCALER_SHIFT(param.trgt_width, w_factor) || dst->v->height != SCALER_SHIFT(param.trgt_height, h_factor)) {

    kvz_deallocateYuvBuffer(dst); //Free old buffer if it exists

    dst = kvz_newYuvBuffer(param.trgt_width, param.trgt_height, param.chroma, 0);
  }

  //Calculate if we are upscaling or downscaling
  int is_downscaled_width = base_param->src_width > base_param->trgt_width;
  int is_downscaled_height = base_param->src_height > base_param->trgt_height;
  int is_equal_width = base_param->src_width == base_param->trgt_width;
  int is_equal_height = base_param->src_height == base_param->trgt_height;

  int is_upscaling = 1;

  //both dimensions need to be either up- or downscaled
  if ((is_downscaled_width && !is_downscaled_height && !is_equal_height) ||
      (is_downscaled_height && !is_downscaled_width && !is_equal_width)) {
    return NULL;
  }
  if (is_equal_height && is_equal_width) {
    //If equal just return source
    copyYuvBuffer(yuv, dst, 0);
    return dst;
  }
  if (is_downscaled_width || is_downscaled_height) {
    //Atleast one dimension is downscaled
    is_upscaling = 0;
  }
  /*=================================*/

  //Allocate a pic_buffer to hold the component data while the downscaling is done
  //Size calculation from SHM. TODO: Figure out why. Use yuv as buffer instead?
  int max_width = SCALER_MAX(param.src_width+param.src_padding_x, param.trgt_width);
  int max_height = SCALER_MAX(param.src_height+param.src_padding_y, param.trgt_height);
  int min_width = SCALER_MIN(param.src_width, param.trgt_width);
  int min_height = SCALER_MIN(param.src_height, param.trgt_height);
  int min_width_rnd16 = ((min_width + 15) >> 4) << 4;
  int min_height_rnd32 = ((min_height + 31) >> 5) << 5;
  int buffer_width = ((max_width * min_width_rnd16 + (min_width << 4) - 1) / (min_width << 4)) << 4;
  int buffer_height = ((max_height * min_height_rnd32 + (min_height << 4) - 1) / (min_height << 4)) << 4;;
  pic_buffer_t* buffer = kvz_newPictureBuffer(buffer_width, buffer_height, 1);


  /*==========Start Resampling=============*/
  //Resample y
  copyPictureBuffer(yuv->y, buffer, 1);
  resample(buffer, &param, is_upscaling, 1);
  copyPictureBuffer(buffer, dst->y, 0);

  //Skip chroma if CHROMA_400
  if (param.chroma != CHROMA_400) {
    //If chroma size differs from luma size, we need to recalculate the parameters
    if (h_factor != 0 || w_factor != 0) {
      calculateParameters(&param, w_factor, h_factor, 1);
    }

    //Resample u
    copyPictureBuffer(yuv->u, buffer, 1);
    resample(buffer, &param, is_upscaling, 0);
    copyPictureBuffer(buffer, dst->u, 0);

    //Resample v
    copyPictureBuffer(yuv->v, buffer, 1);
    resample(buffer, &param, is_upscaling, 0);
    copyPictureBuffer(buffer, dst->v, 0);
  }

  //Deallocate buffer
  deallocatePictureBuffer(buffer);

  return dst;
}

//Use yuv and dst as the buffer instead of allocating a new buffer. Also use unrounded sizes
//yuv is not quaranteet to contain the original data.
yuv_buffer_t* kvz_yuvScaling_(yuv_buffer_t* const yuv, const scaling_parameter_t* const base_param,
                          yuv_buffer_t* dst)
{
  /*========== Basic Initialization ==============*/
  //Initialize basic parameters
  scaling_parameter_t param = *base_param;

  //How much to scale the luma sizes to get the chroma sizes
  int w_factor = 0;
  int h_factor = 0;
  switch (param.chroma) {
    case CHROMA_400: {
      //No chroma
      assert(yuv->u->height == 0 && yuv->u->width == 0 && yuv->v->height == 0 && yuv->v->width == 0);
      break;
    }
    case CHROMA_420: {
      assert(yuv->u->height == (yuv->y->height >> 1) && yuv->u->width == (yuv->y->width >> 1)
        && yuv->v->height == (yuv->y->height >> 1) && yuv->v->width == (yuv->y->width >> 1));
      w_factor = -1;
      h_factor = -1;
      break;
    }
    case CHROMA_422: {
      assert(yuv->u->height == (yuv->y->height) && yuv->u->width == (yuv->y->width >> 1)
        && yuv->v->height == (yuv->y->height) && yuv->v->width == (yuv->y->width >> 1));
      w_factor = -1;
      break;
    }
    case CHROMA_444: {
      assert(yuv->u->height == (yuv->y->height) && yuv->u->width == (yuv->y->width)
        && yuv->v->height == (yuv->y->height) && yuv->v->width == (yuv->y->width));
      break;
    }
    default:
      assert(0); //Unsupported chroma type
  }

  //Check if base param and yuv buffer are the same size, if yes we can asume parameters are initialized
  if (yuv->y->width != param.src_width || yuv->y->height != param.src_height) {
    param.src_width = yuv->y->width;
    param.src_height = yuv->y->height;
    calculateParameters(&param, w_factor, h_factor, 0);
  }

  //Check if we need to allocate a yuv buffer for the new image or re-use dst.
  //Make sure the sizes match
  if (dst == NULL || dst->y->width != param.trgt_width || dst->y->height != param.trgt_height
    || dst->u->width != SCALER_SHIFT(param.trgt_width, w_factor) || dst->u->height != SCALER_SHIFT(param.trgt_height, h_factor)
    || dst->v->width != SCALER_SHIFT(param.trgt_width, w_factor) || dst->v->height != SCALER_SHIFT(param.trgt_height, h_factor)) {

    kvz_deallocateYuvBuffer(dst); //Free old buffer if it exists

    dst = kvz_newYuvBuffer(param.trgt_width, param.trgt_height, param.chroma, 0);
  }

  //Calculate if we are upscaling or downscaling
  int is_downscaled_width = base_param->src_width > base_param->trgt_width;
  int is_downscaled_height = base_param->src_height > base_param->trgt_height;
  int is_equal_width = base_param->src_width == base_param->trgt_width;
  int is_equal_height = base_param->src_height == base_param->trgt_height;

  int is_upscaling = 1;

  //both dimensions need to be either up- or downscaled
  if ((is_downscaled_width && !is_downscaled_height && !is_equal_height) ||
      (is_downscaled_height && !is_downscaled_width && !is_equal_width)) {
    return NULL;
  }
  if (is_equal_height && is_equal_width) {
    //If equal just return source
    copyYuvBuffer(yuv, dst, 0);
    return dst;
  }
  if (is_downscaled_width || is_downscaled_height) {
    //Atleast one dimension is downscaled
    is_upscaling = 0;
  }
  /*=================================*/

  //Allocate a pic_buffer to hold the component data while the downscaling is done
  //Size calculation from SHM. TODO: Figure out why. Use yuv as buffer instead?
  /*int max_width = SCALER_MAX(param.src_width, param.trgt_width);
  int max_height = SCALER_MAX(param.src_height, param.trgt_size);
  int min_width = SCALER_MIN(param.src_width, param.trgt_width);
  int min_height = SCALER_MIN(param.src_height, param.trgt_size);
  int min_width_rnd16 = ((min_width + 15) >> 4) << 4;
  int min_height_rnd32 = ((min_height + 31) >> 5) << 5;
  int buffer_width = ((max_width * min_width_rnd16 + (min_width << 4) - 1) / (min_width << 4)) << 4;
  int buffer_height = ((max_height * min_height_rnd32 + (min_height << 4) - 1) / (min_height << 4)) << 4;;
  pic_buffer_t* buffer = kvz_newPictureBuffer(buffer_width, buffer_height, 1);*/
  //TODO: Clean up this part and implement properly
  //param.rnd_trgt_height = param.trgt_size;
  //param.rnd_trgt_width = param.trgt_width;
  yuv_buffer_t* buffer = is_upscaling ? dst : yuv;//malloc(sizeof(pic_buffer_t)); //Choose bigger buffer
  if (buffer->y->tmp_row == NULL) {
    buffer->y->tmp_row = malloc(sizeof(pic_data_t) * (SCALER_MAX(buffer->y->width, buffer->y->height)));
  }
  if (buffer->u->tmp_row == NULL) {
    buffer->u->tmp_row = malloc(sizeof(pic_data_t) * (SCALER_MAX(buffer->u->width, buffer->u->height)));
  }
  if (buffer->v->tmp_row == NULL) {
    buffer->v->tmp_row = malloc(sizeof(pic_data_t) * (SCALER_MAX(buffer->v->width, buffer->v->height)));
  }

  /*==========Start Resampling=============*/
  //Resample y
  if (is_upscaling) copyPictureBuffer(yuv->y, buffer->y, 1);
  _resample(buffer->y, &param, is_upscaling, 1);
  if (!is_upscaling) copyPictureBuffer(buffer->y, dst->y, 0);

  //Skip chroma if CHROMA_400
  if (param.chroma != CHROMA_400) {
    //If chroma size differs from luma size, we need to recalculate the parameters
    if (h_factor != 0 || w_factor != 0) {
      calculateParameters(&param, w_factor, h_factor, 1);
    }

    //Resample u
    if (is_upscaling) copyPictureBuffer(yuv->u, buffer->u, 1);
    _resample(buffer->u, &param, is_upscaling, 0);
    if (!is_upscaling) copyPictureBuffer(buffer->u, dst->u, 0);

    //Resample v
    if (is_upscaling) copyPictureBuffer(yuv->v, buffer->v, 1);
    _resample(buffer->v, &param, is_upscaling, 0);
    if (!is_upscaling) copyPictureBuffer(buffer->v, dst->v, 0);
  }

  //Deallocate buffer
  //deallocatePictureBuffer(buffer);

  return dst;
}

// yuv buffer should not be modified
int kvz_yuvBlockScaling( const yuv_buffer_t* const yuv, const scaling_parameter_t* const base_param, yuv_buffer_t* dst, const int block_x, const int block_y, const int block_width, const int block_height )
{
  /*========== Basic Initialization ==============*/

  //Check that block parameters are valid
  if( block_x < 0 || block_y < 0 || block_x + block_width > base_param->trgt_width || block_y + block_height > base_param->trgt_height ){
    fprintf(stderr, "Specified block outside given target picture size.");
    return 0;
  }

  //Initialize basic parameters
  scaling_parameter_t param = *base_param;

  //How much to scale the luma sizes to get the chroma sizes
  int w_factor = 0;
  int h_factor = 0;
  switch (param.chroma) {
  case CHROMA_400: {
    //No chroma
    assert(yuv->u->height == 0 && yuv->u->width == 0 && yuv->v->height == 0 && yuv->v->width == 0);
    break;
  }
  case CHROMA_420: {
    assert(yuv->u->height == (yuv->y->height >> 1) && yuv->u->width == (yuv->y->width >> 1)
      && yuv->v->height == (yuv->y->height >> 1) && yuv->v->width == (yuv->y->width >> 1));
    w_factor = -1;
    h_factor = -1;
    break;
  }
  case CHROMA_422: {
    assert(yuv->u->height == (yuv->y->height) && yuv->u->width == (yuv->y->width >> 1)
      && yuv->v->height == (yuv->y->height) && yuv->v->width == (yuv->y->width >> 1));
    w_factor = -1;
    break;
  }
  case CHROMA_444: {
    assert(yuv->u->height == (yuv->y->height) && yuv->u->width == (yuv->y->width)
      && yuv->v->height == (yuv->y->height) && yuv->v->width == (yuv->y->width));
    break;
  }
  default:
    assert(0); //Unsupported chroma type
  }

  //Check if the buffers are large enough for the given parameters and destination is set.
  if (yuv == NULL || yuv->y->width < param.src_width + param.src_padding_x || yuv->y->height < param.src_height + param.src_padding_y || yuv->u->width < SCALER_SHIFT(param.src_width + param.src_padding_x, w_factor) || yuv->u->height < SCALER_SHIFT(param.src_height + param.src_padding_y, w_factor) || yuv->v->width < SCALER_SHIFT(param.src_width + param.src_padding_x, w_factor) || yuv->v->height < SCALER_SHIFT(param.src_height + param.src_padding_y, w_factor)) {
    fprintf(stderr, "Source buffer smaller than specified in the scaling parameters.\n");
    return 0;
  }
 
  //Calculate a dst offset depending on wheather dst is the whole image or just the block
  // if dst is smaller than the specified trgt size, the scaled block is written starting from (0,0)
  // if dst is the size of the specified trgt, the scaled block is written starting from (block_x,block_y)
  int dst_offset_luma = 0;
  int dst_offset_chroma = 0;
  if (dst == NULL || dst->y->width < param.trgt_width || dst->y->height < param.trgt_height
    || dst->u->width < SCALER_SHIFT(param.trgt_width, w_factor) || dst->u->height < SCALER_SHIFT(param.trgt_height, h_factor)
    || dst->v->width < SCALER_SHIFT(param.trgt_width, w_factor) || dst->v->height < SCALER_SHIFT(param.trgt_height, h_factor)) {

    //Check that dst is large enough to hold the block
    if (dst == NULL || dst->y->width < block_width || dst->y->height < block_height
      || dst->u->width < SCALER_SHIFT(block_width, w_factor) || dst->u->height < SCALER_SHIFT(block_height, h_factor)
      || dst->v->width < SCALER_SHIFT(block_width, w_factor) || dst->v->height < SCALER_SHIFT(block_height, h_factor)) {
      fprintf(stderr, "Destination buffer not large enough to hold block\n");
      return 0;
    }
    //Set dst offset so that the block is written to the correct pos
    dst_offset_luma = block_x + block_y * dst->y->width;
    dst_offset_chroma = SCALER_SHIFT(block_x, w_factor) + SCALER_SHIFT(block_y, h_factor) * dst->u->width;
  }

  //Calculate if we are upscaling or downscaling
  int is_downscaled_width = param.src_width > param.trgt_width;
  int is_downscaled_height = param.src_height > param.trgt_height;
  int is_equal_width = param.src_width == param.trgt_width;
  int is_equal_height = param.src_height == param.trgt_height;

  int is_upscaling = 1;

  //both dimensions need to be either up- or downscaled
  if ((is_downscaled_width && !is_downscaled_height && !is_equal_height) ||
    (is_downscaled_height && !is_downscaled_width && !is_equal_width)) {
    fprintf(stderr, "Both dimensions need to be either upscaled or downscaled");
    return 0;
  }
  if (is_equal_height && is_equal_width) {
    //If equal just copy block from src
    copyYuvBufferBlock(yuv, dst, block_x, block_y, dst_offset_luma != 0 ? 0 : block_x, dst_offset_luma != 0 ? 0 : block_y, block_width, block_height, w_factor, h_factor);
    return 1;
  }
  if (is_downscaled_width || is_downscaled_height) {
    //Atleast one dimension is downscaled
    is_upscaling = 0;
  }
  /*=================================*/

  /*==========Start Resampling=============*/

  //Resample y
  resampleBlock(yuv->y, &param, is_upscaling, 1, dst->y, -dst_offset_luma, block_x, block_y, block_width, block_height);

  //Skip chroma if CHROMA_400
  if (param.chroma != CHROMA_400) {
    //If chroma size differs from luma size, we need to recalculate the parameters
    if (h_factor != 0 || w_factor != 0) {
      calculateParameters(&param, w_factor, h_factor, 1);
    }

    //In order to scale blocks not divisible by 2 correctly, need to do some tricks
    //Resample u
    resampleBlock(yuv->u, &param, is_upscaling, 0, dst->u, -dst_offset_chroma, SCALER_SHIFT(block_x, w_factor), SCALER_SHIFT(block_y, h_factor), SCALER_ROUND_SHIFT(block_width, w_factor), SCALER_ROUND_SHIFT(block_height, h_factor));

    //Resample v
    resampleBlock(yuv->v, &param, is_upscaling, 0, dst->v, -dst_offset_chroma, SCALER_SHIFT(block_x, w_factor), SCALER_SHIFT(block_y, h_factor), SCALER_ROUND_SHIFT(block_width, w_factor), SCALER_ROUND_SHIFT(block_height, h_factor));
  }

  return 1;
}

static void blockScalingSrcRange( int range[2], const int scale, const int add, const int shift, const int delta, const int block_low, const int block_high, const int src_size )
{
  //Check if equal size
  if(scale == SCALER_UNITY_SCALE_CONST){
    range[0] = block_low;
    range[1] = block_high;
    return;
  }

  //Get filter size
  int size = scale < SCALER_UNITY_SCALE_CONST ? sizeof(lumaUpFilter[0]) / sizeof(lumaUpFilter[0][0]) : sizeof(downFilter[0][0]) / sizeof(downFilter[0][0][0]);

  //Calculate lower bound
  range[0] = ((int)((unsigned int)((block_low * scale + add) >> (shift - 4)) - delta) >> 4) - (size >> 1) + 1;

  //Calculate upper bound
  range[1] = ((int)((unsigned int)((block_high * scale + add) >> (shift - 4)) - delta) >> 4) - (size >> 1) + size;

  //clip the ranges so that they are within the pic
  range[0] = SCALER_CLIP(range[0], 0, src_size - 1);
  range[1] = SCALER_CLIP(range[1], 0, src_size - 1);

}

void kvz_blockScalingSrcWidthRange(int range[2], const scaling_parameter_t * const base_param, const int block_x, const int block_width)
{
  //Calculate parameters
  calculateParameters((scaling_parameter_t*)base_param, 0, 0, 0);

  blockScalingSrcRange(range, base_param->scale_x, base_param->add_x, base_param->shift_x, base_param->delta_x, block_x, block_x + block_width - 1, base_param->src_width + base_param->src_padding_x);
}

void kvz_blockScalingSrcHeightRange(int range[2], const scaling_parameter_t * const base_param, const int block_y, const int block_height)
{
  //Calculate parameters
  calculateParameters((scaling_parameter_t*)base_param, 0, 0, 0);

  blockScalingSrcRange(range, base_param->scale_y, base_param->add_y, base_param->shift_y, base_param->delta_y, block_y, block_y + block_height - 1, base_param->src_height + base_param->src_padding_y);
}

// Do block scaling in one direction. yuv buffer should not be modified.
int kvz_yuvBlockStepScaling(yuv_buffer_t* const dst, const yuv_buffer_t* const src, const scaling_parameter_t* const base_param, const int block_x, const int block_y, const int block_width, const int block_height, const int is_vertical)
{
  /*========== Basic Initialization ==============*/

  //Check that block parameters are valid
  int width_bound = base_param->trgt_width;
  int height_bound = is_vertical ? base_param->trgt_height : base_param->src_height + base_param->src_padding_y;
  if (block_x < 0 || block_y < 0 || block_x + block_width > width_bound || block_y + block_height > height_bound) {
    fprintf(stderr, "Specified block outside given target picture size.");
    return 0;
  }

  //Initialize basic parameters
  scaling_parameter_t param = *base_param;

  //How much to scale the luma sizes to get the chroma sizes
  int w_factor = 0;
  int h_factor = 0;
  switch (param.chroma) {
  case CHROMA_400: {
    //No chroma
    assert(src->u->height == 0 && src->u->width == 0 && src->v->height == 0 && src->v->width == 0);
    break;
  }
  case CHROMA_420: {
    assert(src->u->height == (src->y->height >> 1) && src->u->width == (src->y->width >> 1)
      && src->v->height == (src->y->height >> 1) && src->v->width == (src->y->width >> 1));
    w_factor = -1;
    h_factor = -1;
    break;
  }
  case CHROMA_422: {
    assert(src->u->height == (src->y->height) && src->u->width == (src->y->width >> 1)
      && src->v->height == (src->y->height) && src->v->width == (src->y->width >> 1));
    w_factor = -1;
    break;
  }
  case CHROMA_444: {
    assert(src->u->height == (src->y->height) && src->u->width == (src->y->width)
      && src->v->height == (src->y->height) && src->v->width == (src->y->width));
    break;
  }
  default:
    assert(0); //Unsupported chroma type
  }

  //Calculate a src offset depending on wheather src is the whole image or just the block
  // if src is smaller than the specified src size, the src buffer is intepreted to contain the area specified by kvz_blockScaling*Range.
  // if src is the size of the specified src, the src buffer is accessed in the area specified by kvz_blockScaling*Range.
  int src_offset_luma = 0;
  int src_offset_chroma = 0;
  width_bound = is_vertical ? param.trgt_width : param.src_width + param.src_padding_x;
  height_bound = param.src_height + param.src_padding_y;
  if (src == NULL || src->y->width < width_bound || src->y->height < height_bound || src->u->width < SCALER_SHIFT(width_bound, w_factor) || src->u->height < SCALER_SHIFT(height_bound, w_factor) || src->v->width < SCALER_SHIFT(width_bound, w_factor) || src->v->height < SCALER_SHIFT(height_bound, w_factor)) {
    
    //Get src range needed for scaling
    int range[4];
    kvz_blockScalingSrcWidthRange(range, base_param, block_x, block_width);
    kvz_blockScalingSrcHeightRange(range+2, base_param, block_y, block_height);
    width_bound = is_vertical ? block_width : range[1] - range[0];
    height_bound = is_vertical ? range[3] - range[2] : block_height;

    //Check that src is large enough to hold the block
    if (src == NULL || src->y->width < width_bound || src->y->height < height_bound
      || src->u->width < SCALER_SHIFT(width_bound, w_factor) || src->u->height < SCALER_SHIFT(height_bound, h_factor)
      || src->v->width < SCALER_SHIFT(width_bound, w_factor) || src->v->height < SCALER_SHIFT(height_bound, h_factor)) {
      fprintf(stderr, "Source buffer smaller than specified in the scaling parameters.\n");
      return 0;
    }
    //Set src offset so that the block is written to the correct pos
    src_offset_luma = is_vertical ? block_x + range[2] * src->y->width : range[0] + block_y * src->y->width;
    src_offset_chroma = is_vertical ? SCALER_SHIFT(block_x, w_factor) + SCALER_SHIFT(range[2], h_factor) * src->u->width : SCALER_SHIFT(range[0], w_factor) + SCALER_SHIFT(block_y, h_factor) * src->u->width;
  }

  //Calculate a dst offset depending on wheather dst is the whole image or just the block
  // if dst is smaller than the specified trgt size, the scaled block is written starting from (0,0)
  // if dst is the size of the specified trgt, the scaled block is written starting from (block_x,block_y)
  int dst_offset_luma = 0;
  int dst_offset_chroma = 0;
  width_bound = param.trgt_width;
  height_bound = is_vertical ? param.trgt_height : param.src_height + param.src_padding_y;
  if (dst == NULL || dst->y->width < width_bound || dst->y->height < height_bound
    || dst->u->width < SCALER_SHIFT(width_bound, w_factor) || dst->u->height < SCALER_SHIFT(height_bound, h_factor)
    || dst->v->width < SCALER_SHIFT(width_bound, w_factor) || dst->v->height < SCALER_SHIFT(height_bound, h_factor)) {

    //Check that dst is large enough to hold the block
    if (dst == NULL || dst->y->width < block_width || dst->y->height < block_height
      || dst->u->width < SCALER_SHIFT(block_width, w_factor) || dst->u->height < SCALER_SHIFT(block_height, h_factor)
      || dst->v->width < SCALER_SHIFT(block_width, w_factor) || dst->v->height < SCALER_SHIFT(block_height, h_factor)) {
      fprintf(stderr, "Destination buffer not large enough to hold block\n");
      return 0;
    }
    //Set dst offset so that the block is written to the correct pos
    dst_offset_luma = block_x + block_y * dst->y->width;
    dst_offset_chroma = SCALER_SHIFT(block_x, w_factor) + SCALER_SHIFT(block_y, h_factor) * dst->u->width;
  }

  //Calculate if we are upscaling or downscaling
  int is_downscaled_width = param.src_width > param.trgt_width;
  int is_downscaled_height = param.src_height > param.trgt_height;
  int is_equal_width = param.src_width == param.trgt_width;
  int is_equal_height = param.src_height == param.trgt_height;

  int is_upscaling = 1;

  //both dimensions need to be either up- or downscaled
  if ((is_downscaled_width && !is_downscaled_height && !is_equal_height) ||
    (is_downscaled_height && !is_downscaled_width && !is_equal_width)) {
    fprintf(stderr, "Both dimensions need to be either upscaled or downscaled");
    return 0;
  }
  if (is_equal_height && is_equal_width) {
    //If equal just copy block from src
    copyYuvBufferBlock(src, dst, src_offset_luma != 0 ? 0 : block_x, src_offset_luma != 0 ? 0 : block_y, dst_offset_luma != 0 ? 0 : block_x, dst_offset_luma != 0 ? 0 : block_y, block_width, block_height, w_factor, h_factor);
    return 1;
  }
  if (is_downscaled_width || is_downscaled_height) {
    //Atleast one dimension is downscaled
    is_upscaling = 0;
  }
  /*=================================*/

  /*==========Start Resampling=============*/

  //Resample y
  resampleBlockStep(src->y, dst->y, -src_offset_luma, -dst_offset_luma, block_x, block_y, block_width, block_height, &param, is_upscaling, 1, is_vertical);

  //Skip chroma if CHROMA_400
  if (param.chroma != CHROMA_400) {
    //If chroma size differs from luma size, we need to recalculate the parameters
    if (h_factor != 0 || w_factor != 0) {
      calculateParameters(&param, w_factor, h_factor, 1);
    }

    //In order to scale blocks not divisible by 2 correctly, need to do some tricks
    //Resample u
    resampleBlockStep(src->u, dst->u, -src_offset_chroma, -dst_offset_chroma, SCALER_SHIFT(block_x, w_factor), SCALER_SHIFT(block_y, h_factor), SCALER_ROUND_SHIFT(block_width, w_factor), SCALER_ROUND_SHIFT(block_height, h_factor), &param, is_upscaling, 0, is_vertical);

    //Resample v
    resampleBlockStep(src->v, dst->v, -src_offset_chroma, -dst_offset_chroma, SCALER_SHIFT(block_x, w_factor), SCALER_SHIFT(block_y, h_factor), SCALER_ROUND_SHIFT(block_width, w_factor), SCALER_ROUND_SHIFT(block_height, h_factor), &param, is_upscaling, 0, is_vertical);
  }

  return 1;
}