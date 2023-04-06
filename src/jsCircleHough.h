/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef _JS_CIRCLE_HOUGH_HPP
#define _JS_CIRCLE_HOUGH_HPP

#include <joescan_pinchot.h>

/**
 * @brief Opaque reference to an object used to perform Circle Hough Transform.
 */
typedef void* jsCircleHough;

typedef struct {
  /// decrease to increase result resolution, increase to decrease resolution
  uint32_t step_size;
  /// region of interest X minimum in 1/1000 inches
  int32_t x_lower;
  /// region of interest X maximum in 1/1000 inches
  int32_t x_upper;
  /// region of interest Y minimum in 1/1000 inches
  int32_t y_lower;
  /// region of interest Y maximum in 1/1000 inches
  int32_t y_upper;
} jsCircleHoughConstraints;

typedef struct {
  /// confidence circle was detected, higher values imply greater confidence
  double weight = 0.0;
  /// x coordinate of center of circle in 1/1000 inches
  int32_t x = 0;
  /// y coordinate of center of circle in 1/1000 inches
  int32_t y = 0;
} jsCircleHoughResults;

/**
 * @brief Creates jsCircleHough to be used for circle detection.
 *
 * @param radius The radius of the circle to find in 1/1000 inches.
 * @param c Constraints for circle finding detection.
 * @return Address to new object on success, `NULL` on error.
 */
jsCircleHough jsCircleHoughCreate(int32_t radius, int32_t num_circles, jsCircleHoughConstraints *c);

/**
 * @brief Runs Circle Hough Transform to attempt to find a circle.
 *
 * @param circle_hough Configured `jsCircleHough` object.
 * @param profile The data profile to search for circle.
 * @return The results of the calcualation indicating where a circle may be.
 */
jsCircleHoughResults* jsCircleHoughCalculate(jsCircleHough circle_hough,
                                            jsProfile *profile);

/**
 * @brief Frees resources allocated for `jsCircleHough` object.
 *
 * @param circle_hough Object to free.
 */
void jsCircleHoughFree(jsCircleHough circle_hough);

#endif
