// Copyright 2019 The MediaPipe Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MEDIAPIPE_EXAMPLES_DESKTOP_AUTOFLIP_QUALITY_VISUAL_SCORER_H_
#define MEDIAPIPE_EXAMPLES_DESKTOP_AUTOFLIP_QUALITY_VISUAL_SCORER_H_

#include "mediapipe/examples/desktop/autoflip/autoflip_messages.pb.h"
#include "mediapipe/examples/desktop/autoflip/quality/visual_scorer.pb.h"
#include "mediapipe/framework/port/opencv_core_inc.h"
#include "mediapipe/framework/port/status.h"

namespace mediapipe {
namespace autoflip {

// This class scores a SalientRegion within an image based on weighted averages
// of various signals computed on the patch.
class VisualScorer {
public:
    explicit VisualScorer(const VisualScorerOptions& options);

    // Computes a score on a salientregion and returns a value [0...1].
    absl::Status CalculateScore(const cv::Mat& image, const SalientRegion& region,
                                float* score) const;

private:
    absl::Status CalculateColorfulness(const cv::Mat& image,
                                       float* colorfulness) const;

    VisualScorerOptions options_;
};

}  // namespace autoflip
}  // namespace mediapipe

#endif  // MEDIAPIPE_EXAMPLES_DESKTOP_AUTOFLIP_QUALITY_VISUAL_SCORER_H_
