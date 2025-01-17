/* Copyright 2023 The MediaPipe Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "mediapipe/tasks/cc/vision/face_landmarker/face_landmarker.h"

#include "mediapipe/framework/api2/builder.h"
#include "mediapipe/framework/formats/classification.pb.h"
#include "mediapipe/framework/formats/image.h"
#include "mediapipe/framework/formats/landmark.pb.h"
#include "mediapipe/framework/formats/matrix.h"
#include "mediapipe/framework/formats/matrix_data.pb.h"
#include "mediapipe/framework/formats/rect.pb.h"
#include "mediapipe/tasks/cc/components/containers/classification_result.h"
#include "mediapipe/tasks/cc/core/base_task_api.h"
#include "mediapipe/tasks/cc/core/task_runner.h"
#include "mediapipe/tasks/cc/core/utils.h"
#include "mediapipe/tasks/cc/vision/core/base_vision_task_api.h"
#include "mediapipe/tasks/cc/vision/core/image_processing_options.h"
#include "mediapipe/tasks/cc/vision/core/vision_task_api_factory.h"
#include "mediapipe/tasks/cc/vision/face_detector/proto/face_detector_graph_options.pb.h"
#include "mediapipe/tasks/cc/vision/face_geometry/proto/face_geometry.pb.h"
#include "mediapipe/tasks/cc/vision/face_landmarker/proto/face_landmarker_graph_options.pb.h"
#include "mediapipe/tasks/cc/vision/face_landmarker/proto/face_landmarks_detector_graph_options.pb.h"

namespace mediapipe {
namespace tasks {
namespace vision {
namespace face_landmarker {

namespace {

using FaceLandmarkerGraphOptionsProto = ::mediapipe::tasks::vision::
    face_landmarker::proto::FaceLandmarkerGraphOptions;

constexpr char kFaceLandmarkerGraphTypeName[] =
    "mediapipe.tasks.vision.face_landmarker.FaceLandmarkerGraph";

constexpr char kImageTag[] = "IMAGE";
constexpr char kImageInStreamName[] = "image_in";
constexpr char kImageOutStreamName[] = "image_out";
constexpr char kNormRectTag[] = "NORM_RECT";
constexpr char kNormRectStreamName[] = "norm_rect_in";
constexpr char kNormLandmarksTag[] = "NORM_LANDMARKS";
constexpr char kNormLandmarksStreamName[] = "norm_landmarks";
constexpr char kBlendshapesTag[] = "BLENDSHAPES";
constexpr char kBlendshapesStreamName[] = "blendshapes";
constexpr char kFaceGeometryTag[] = "FACE_GEOMETRY";
constexpr char kFaceGeometryStreamName[] = "face_geometry";
constexpr int kMicroSecondsPerMilliSecond = 1000;

// Creates a MediaPipe graph config that contains a subgraph node of
// "mediapipe.tasks.vision.face_ladnamrker.FaceLandmarkerGraph". If the task is
// running in the live stream mode, a "FlowLimiterCalculator" will be added to
// limit the number of frames in flight.
CalculatorGraphConfig CreateGraphConfig(
    std::unique_ptr<FaceLandmarkerGraphOptionsProto> options,
    bool output_face_blendshapes, bool output_facial_transformation_matrixes,
    bool enable_flow_limiting) {
  api2::builder::Graph graph;
  auto& subgraph = graph.AddNode(kFaceLandmarkerGraphTypeName);
  subgraph.GetOptions<FaceLandmarkerGraphOptionsProto>().Swap(options.get());
  graph.In(kImageTag).SetName(kImageInStreamName);
  graph.In(kNormRectTag).SetName(kNormRectStreamName);
  subgraph.Out(kNormLandmarksTag).SetName(kNormLandmarksStreamName) >>
      graph.Out(kNormLandmarksTag);
  subgraph.Out(kImageTag).SetName(kImageOutStreamName) >> graph.Out(kImageTag);
  if (output_face_blendshapes) {
    subgraph.Out(kBlendshapesTag).SetName(kBlendshapesStreamName) >>
        graph.Out(kBlendshapesTag);
  }
  if (output_facial_transformation_matrixes) {
    subgraph.Out(kFaceGeometryTag).SetName(kFaceGeometryStreamName) >>
        graph.Out(kFaceGeometryTag);
  }
  if (enable_flow_limiting) {
    return tasks::core::AddFlowLimiterCalculator(
        graph, subgraph, {kImageTag, kNormRectTag}, kNormLandmarksTag);
  }
  graph.In(kImageTag) >> subgraph.In(kImageTag);
  graph.In(kNormRectTag) >> subgraph.In(kNormRectTag);
  return graph.GetConfig();
}

// Converts the user-facing FaceLandmarkerOptions struct to the internal
// FaceLandmarkerGraphOptions proto.
std::unique_ptr<FaceLandmarkerGraphOptionsProto>
ConvertFaceLandmarkerGraphOptionsProto(FaceLandmarkerOptions* options) {
  auto options_proto = std::make_unique<FaceLandmarkerGraphOptionsProto>();
  auto base_options_proto = std::make_unique<tasks::core::proto::BaseOptions>(
      tasks::core::ConvertBaseOptionsToProto(&(options->base_options)));
  options_proto->mutable_base_options()->Swap(base_options_proto.get());
  options_proto->mutable_base_options()->set_use_stream_mode(
      options->running_mode != core::RunningMode::IMAGE);

  // Configure face detector options.
  auto* face_detector_graph_options =
      options_proto->mutable_face_detector_graph_options();
  face_detector_graph_options->set_num_faces(options->num_faces);
  face_detector_graph_options->set_min_detection_confidence(
      options->min_face_detection_confidence);

  // Configure face landmark detector options.
  options_proto->set_min_tracking_confidence(options->min_tracking_confidence);
  auto* face_landmarks_detector_graph_options =
      options_proto->mutable_face_landmarks_detector_graph_options();
  face_landmarks_detector_graph_options->set_min_detection_confidence(
      options->min_face_presence_confidence);

  return options_proto;
}

FaceLandmarkerResult GetFaceLandmarkerResultFromPacketMap(
    const tasks::core::PacketMap& packet_map) {
  const auto& face_landmarks = packet_map.at(kNormLandmarksStreamName)
                                   .Get<std::vector<NormalizedLandmarkList>>();
  std::optional<std::vector<ClassificationList>> face_blendshapes;
  if (packet_map.find(kBlendshapesStreamName) != packet_map.end()) {
    face_blendshapes = packet_map.at(kBlendshapesStreamName)
                           .Get<std::vector<ClassificationList>>();
  }
  std::optional<std::vector<MatrixData>> matrix_data_list;
  if (packet_map.find(kFaceGeometryStreamName) != packet_map.end()) {
    const auto& face_geometry_list =
        packet_map.at(kFaceGeometryStreamName)
            .Get<std::vector<face_geometry::proto::FaceGeometry>>();
    matrix_data_list = std::vector<MatrixData>(face_geometry_list.size());
    std::transform(face_geometry_list.begin(), face_geometry_list.end(),
                   matrix_data_list->begin(),
                   [](const face_geometry::proto::FaceGeometry& face_geometry) {
                     return face_geometry.pose_transform_matrix();
                   });
  }
  return ConvertToFaceLandmarkerResult(
      /* face_landmarks_proto = */ face_landmarks,
      /* face_blendshapes_proto= */ face_blendshapes,
      /* facial_transformation_matrixes_proto= */ matrix_data_list);
}

}  // namespace

absl::StatusOr<std::unique_ptr<FaceLandmarker>> FaceLandmarker::Create(
    std::unique_ptr<FaceLandmarkerOptions> options) {
  auto options_proto = ConvertFaceLandmarkerGraphOptionsProto(options.get());
  tasks::core::PacketsCallback packets_callback = nullptr;
  if (options->result_callback) {
    auto result_callback = options->result_callback;
    packets_callback = [=](absl::StatusOr<tasks::core::PacketMap> packet_map) {
      if (!packet_map.ok()) {
        Image image;
        result_callback(packet_map.status(), image, Timestamp::Unset().Value());
        return;
      }
      if (packet_map->at(kImageOutStreamName).IsEmpty()) {
        return;
      }
      Packet image_packet = packet_map->at(kImageOutStreamName);
      if (packet_map->at(kNormLandmarksStreamName).IsEmpty()) {
        Packet empty_packet = packet_map->at(kNormLandmarksStreamName);
        result_callback(
            {FaceLandmarkerResult()}, image_packet.Get<Image>(),
            empty_packet.Timestamp().Value() / kMicroSecondsPerMilliSecond);
        return;
      }
      result_callback(
          GetFaceLandmarkerResultFromPacketMap(*packet_map),
          image_packet.Get<Image>(),
          packet_map->at(kNormLandmarksStreamName).Timestamp().Value() /
              kMicroSecondsPerMilliSecond);
    };
  }
  return core::VisionTaskApiFactory::Create<FaceLandmarker,
                                            FaceLandmarkerGraphOptionsProto>(
      CreateGraphConfig(
          std::move(options_proto), options->output_face_blendshapes,
          options->output_facial_transformation_matrixes,
          options->running_mode == core::RunningMode::LIVE_STREAM),
      std::move(options->base_options.op_resolver), options->running_mode,
      std::move(packets_callback));
}

absl::StatusOr<FaceLandmarkerResult> FaceLandmarker::Detect(
    mediapipe::Image image,
    std::optional<core::ImageProcessingOptions> image_processing_options) {
  ASSIGN_OR_RETURN(NormalizedRect norm_rect,
                   ConvertToNormalizedRect(image_processing_options,
                                           /*roi_allowed=*/false));
  ASSIGN_OR_RETURN(
      auto output_packets,
      ProcessImageData(
          {{kImageInStreamName, MakePacket<Image>(std::move(image))},
           {kNormRectStreamName,
            MakePacket<NormalizedRect>(std::move(norm_rect))}}));
  if (output_packets[kNormLandmarksStreamName].IsEmpty()) {
    return {FaceLandmarkerResult()};
  }
  return GetFaceLandmarkerResultFromPacketMap(output_packets);
}

absl::StatusOr<FaceLandmarkerResult> FaceLandmarker::DetectForVideo(
    mediapipe::Image image, int64_t timestamp_ms,
    std::optional<core::ImageProcessingOptions> image_processing_options) {
  ASSIGN_OR_RETURN(NormalizedRect norm_rect,
                   ConvertToNormalizedRect(image_processing_options,
                                           /*roi_allowed=*/false));
  ASSIGN_OR_RETURN(
      auto output_packets,
      ProcessVideoData(
          {{kImageInStreamName,
            MakePacket<Image>(std::move(image))
                .At(Timestamp(timestamp_ms * kMicroSecondsPerMilliSecond))},
           {kNormRectStreamName,
            MakePacket<NormalizedRect>(std::move(norm_rect))
                .At(Timestamp(timestamp_ms * kMicroSecondsPerMilliSecond))}}));
  if (output_packets[kNormLandmarksStreamName].IsEmpty()) {
    return {FaceLandmarkerResult()};
  }
  return GetFaceLandmarkerResultFromPacketMap(output_packets);
}

absl::Status FaceLandmarker::DetectAsync(
    mediapipe::Image image, int64_t timestamp_ms,
    std::optional<core::ImageProcessingOptions> image_processing_options) {
  ASSIGN_OR_RETURN(NormalizedRect norm_rect,
                   ConvertToNormalizedRect(image_processing_options,
                                           /*roi_allowed=*/false));
  return SendLiveStreamData(
      {{kImageInStreamName,
        MakePacket<Image>(std::move(image))
            .At(Timestamp(timestamp_ms * kMicroSecondsPerMilliSecond))},
       {kNormRectStreamName,
        MakePacket<NormalizedRect>(std::move(norm_rect))
            .At(Timestamp(timestamp_ms * kMicroSecondsPerMilliSecond))}});
}

}  // namespace face_landmarker
}  // namespace vision
}  // namespace tasks
}  // namespace mediapipe
