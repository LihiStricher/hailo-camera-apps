/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include <set>
#include <vector>
#include <string>

#include "hailo_objects.hpp"
#include "hailo_common.hpp"
#include "xtensor/xarray.hpp"

#define MEDIAPIPE_LANDMARK_COUNT 468

// Enum to classify landmark group types
enum class LandmarkType
{
    EYE,
    NOSE,
    MOUTH
};

// Structure to control which landmark groups to draw at runtime
struct LandmarkDrawOptions
{
    bool show_all_landmarks = true;
    bool show_eye = true;
    bool show_nose = true;
    bool show_mouth = true;

    LandmarkDrawOptions() = default;
    LandmarkDrawOptions(bool eye, bool nose, bool mouth) : show_eye(eye), show_nose(nose), show_mouth(mouth)
    {
    }
};

float get_face_presence_score(HailoTensorPtr tensor);

// Main processing logic to extract and add landmarks to ROI
void mediapipe_landmark(HailoROIPtr roi, const LandmarkDrawOptions &options);

// Hailo plugin interface function
extern "C" void facial_landmarks_nv12(HailoROIPtr roi);

// Fallback filter function used by post-process plugin loader
void filter(HailoROIPtr roi);
