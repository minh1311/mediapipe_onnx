// Copyright 2023 The MediaPipe Authors.
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

#import "mediapipe/tasks/ios/vision/core/sources/MPPVisionTaskRunner.h"

#import "mediapipe/tasks/ios/common/sources/MPPCommon.h"
#import "mediapipe/tasks/ios/common/utils/sources/MPPCommonUtils.h"

#include "absl/status/statusor.h"

#include <optional>

namespace {
using ::mediapipe::CalculatorGraphConfig;
using ::mediapipe::NormalizedRect;
using ::mediapipe::tasks::core::PacketMap;
using ::mediapipe::tasks::core::PacketsCallback;
}  // namespace

/** Rotation degress for a 90 degree rotation to the right. */
static const NSInteger kMPPOrientationDegreesRight = -90;

/** Rotation degress for a 180 degree rotation. */
static const NSInteger kMPPOrientationDegreesDown = -180;

/** Rotation degress for a 90 degree rotation to the left. */
static const NSInteger kMPPOrientationDegreesLeft = -270;

@interface MPPVisionTaskRunner () {
  MPPRunningMode _runningMode;
}
@end

@implementation MPPVisionTaskRunner

- (nullable instancetype)initWithCalculatorGraphConfig:(CalculatorGraphConfig)graphConfig
                                           runningMode:(MPPRunningMode)runningMode
                                       packetsCallback:(PacketsCallback)packetsCallback
                                                 error:(NSError **)error {
  switch (runningMode) {
    case MPPRunningModeImage:
    case MPPRunningModeVideo: {
      if (packetsCallback) {
        [MPPCommonUtils createCustomError:error
                                 withCode:MPPTasksErrorCodeInvalidArgumentError
                              description:@"The vision task is in image or video mode, a "
                                          @"user-defined result callback should not be provided."];
        return nil;
      }
      break;
    }
    case MPPRunningModeLiveStream: {
      if (!packetsCallback) {
        [MPPCommonUtils createCustomError:error
                                 withCode:MPPTasksErrorCodeInvalidArgumentError
                              description:@"The vision task is in live stream mode, a user-defined "
                                          @"result callback must be provided."];
        return nil;
      }
      break;
    }
    default: {
      [MPPCommonUtils createCustomError:error
                               withCode:MPPTasksErrorCodeInvalidArgumentError
                            description:@"Unrecognized running mode"];
      return nil;
    }
  }

  _runningMode = runningMode;
  self = [super initWithCalculatorGraphConfig:graphConfig
                              packetsCallback:packetsCallback
                                        error:error];
  return self;
}

- (std::optional<NormalizedRect>)normalizedRectFromRegionOfInterest:(CGRect)roi
                                                   imageOrientation:
                                                       (UIImageOrientation)imageOrientation
                                                         ROIAllowed:(BOOL)ROIAllowed
                                                              error:(NSError **)error {
  if (CGRectEqualToRect(roi, CGRectZero) && !ROIAllowed) {
    [MPPCommonUtils createCustomError:error
                             withCode:MPPTasksErrorCodeInvalidArgumentError
                          description:@"This task doesn't support region-of-interest."];
    return std::nullopt;
  }

  CGRect calculatedRoi = CGRectEqualToRect(roi, CGRectZero) ? roi : CGRectMake(0.0, 0.0, 1.0, 1.0);

  NormalizedRect normalizedRect;
  normalizedRect.set_x_center(CGRectGetMidX(calculatedRoi));
  normalizedRect.set_y_center(CGRectGetMidY(calculatedRoi));
  normalizedRect.set_width(CGRectGetWidth(calculatedRoi));
  normalizedRect.set_height(CGRectGetHeight(calculatedRoi));

  int rotationDegrees = 0;
  switch (imageOrientation) {
    case UIImageOrientationUp:
      break;
    case UIImageOrientationRight: {
      rotationDegrees = kMPPOrientationDegreesRight;
      break;
    }
    case UIImageOrientationDown: {
      rotationDegrees = kMPPOrientationDegreesDown;
      break;
    }
    case UIImageOrientationLeft: {
      rotationDegrees = kMPPOrientationDegreesLeft;
      break;
    }
    default:
      [MPPCommonUtils
          createCustomError:error
                   withCode:MPPTasksErrorCodeInvalidArgumentError
                description:
                    @"Unsupported UIImageOrientation. `imageOrientation` cannot be equal to "
                    @"any of the mirrored orientations "
                    @"(`UIImageOrientationUpMirrored`,`UIImageOrientationDownMirrored`,`"
                    @"UIImageOrientationLeftMirrored`,`UIImageOrientationRightMirrored`)"];
  }

  normalizedRect.set_rotation(rotationDegrees * M_PI / kMPPOrientationDegreesDown);

  return normalizedRect;
}

- (std::optional<PacketMap>)processImagePacketMap:(const PacketMap &)packetMap
                                            error:(NSError **)error {
  if (_runningMode != MPPRunningModeImage) {
    [MPPCommonUtils
        createCustomError:error
                 withCode:MPPTasksErrorCodeInvalidArgumentError
              description:[NSString stringWithFormat:@"The vision task is not initialized with "
                                                     @"image mode. Current Running Mode: %@",
                                                     MPPRunningModeDisplayName(_runningMode)]];
    return std::nullopt;
  }

  return [self processPacketMap:packetMap error:error];
}

- (std::optional<PacketMap>)processVideoFramePacketMap:(const PacketMap &)packetMap
                                                 error:(NSError **)error {
  if (_runningMode != MPPRunningModeVideo) {
    [MPPCommonUtils
        createCustomError:error
                 withCode:MPPTasksErrorCodeInvalidArgumentError
              description:[NSString stringWithFormat:@"The vision task is not initialized with "
                                                     @"video mode. Current Running Mode: %@",
                                                     MPPRunningModeDisplayName(_runningMode)]];
    return std::nullopt;
  }

  return [self processPacketMap:packetMap error:error];
}

- (BOOL)processLiveStreamPacketMap:(const PacketMap &)packetMap error:(NSError **)error {
  if (_runningMode != MPPRunningModeLiveStream) {
    [MPPCommonUtils
        createCustomError:error
                 withCode:MPPTasksErrorCodeInvalidArgumentError
              description:[NSString stringWithFormat:@"The vision task is not initialized with "
                                                     @"live stream mode. Current Running Mode: %@",
                                                     MPPRunningModeDisplayName(_runningMode)]];
    return NO;
  }

  return [self sendPacketMap:packetMap error:error];
}

@end
