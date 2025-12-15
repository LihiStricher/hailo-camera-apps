

// general includes
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <tl/expected.hpp>
#include <signal.h>
#include <cxxopts/cxxopts.hpp>

// medialibrary includes
#include "media_library/media_library.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/signal_utils.hpp"

// infra includes
#include "pipeline.hpp"
#include "ai_stage.hpp"
#include "dsp_stages.hpp"
#include "postprocess_stage.hpp"
#include "overlay_stage.hpp"
#include "udp_stage.hpp"
#include "encoder_stage.hpp"
#include "frontend_stage.hpp"
#include "tracker_stage.hpp"
#include "persist_stage.hpp"
#include "aggregator_stage.hpp"
#include "reference_camera_logger.hpp"
#include "output_metadata_stage.hpp"
#include "dsp_convert_stage.hpp"
// #include "fire_detection_ai_stage.hpp"
// #include "drop_frames_stage.hpp"
#include "pipeline_builder.hpp"
#include "muxer_stage.hpp"

// Frontend Params
#define FRONTEND_STAGE "frontend_stage"

#define OVERLAY_STAGE "overlay"
#define TRACKER_STAGE "tracker"
#define OUTPUT_METADATA_STAGE "output_metadata"
#define UDP_0_STAGE "udp_0"
#define HOST_IP "10.0.0.2"

#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/s1_pipeline_medialib_config.json"

// AI Pipeline Params
#define AI_VISION_SINK "sink0" // The streamid from frontend to 4K stream that shows vision results
#define SECONDARY_VISION_SINK "sink1" // The small streamid from frontend that would be used for overlay
#define AI_SINK "sink2"        // The streamid from frontend to AI
// Detection AI Params
#define YOLO_HEF_FILE "/home/root/apps/s1_demo/resources/yolov5m_wo_spp_60p_nv12_fhd.hef"
#define DETECTION_AI_STAGE "yolo_detection"
// Detection Postprocess Params
#define POST_STAGE "yolo_post"
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"
#define YOLO_FUNC_NAME "yolov5"

// #define CLIP_HEF_FILE "/home/root/apps/s1_demo/resources/clip_convnext_visual_256_quantized.hef"
// #define CLIP_AI_STAGE "fire_detection"

// Aggregator Params

#define AFTER_RESIZE_AGGREGATOR_STAGE "after_resize_aggregator"
#define CROPPED_PERSONS_AGGREGATOR "cropped_persons_aggregator"
#define CROPPED_FACES_AGGREGATOR "cropped_faces_aggregator"
#define FIRE_DET_TILLING_AGGREGATOR_STAGE "fire_det_tiling_aggregator"

#define RESULTS_AGGREGATOR_STAGE "results_aggregator"
#define AGGREGATOR_STAGE "aggregator"

// Tee Params
#define TEE_STAGE "vision_tee"
#define DEMO_TEE_STAGE "demo_tee"
#define MUXER_STAGE "muxer"
#define CALLBACK_STAGE "callback_stage"

#define CONVERT_STAGE "convert"

// Resize Params
#define PERSON_DET_RESIZE_STAGE "person_detection_resize_stage"
#define PERSON_DET_RESIZE_INPUT_WIDTH 1920
#define PERSON_DET_RESIZE_INPUT_HEIGHT 1080
#define PERSON_DET_RESIZE_OUTPUT_WIDTH 1920
#define PERSON_DET_RESIZE_OUTPUT_HEIGHT 1080
std::vector<HailoBBox> PERSON_DET_TILE = {{0.0, 0.0, 1.0, 1.0}};

// #define FIRE_DET_TILLING_STAGE "fire_detection_tilling_stage"
// #define FIRE_DET_TILLING_INPUT_WIDTH 1920
// #define FIRE_DET_TILLING_INPUT_HEIGHT 1080
// #define FIRE_DET_TILLING_OUTPUT_WIDTH 256
// #define FIRE_DET_TILLING_OUTPUT_HEIGHT 256
// std::vector<HailoBBox> FIRE_DET_TILES = {
//     {0.0, 0.0, 0.333, 0.5},   // top-left
//     {0.333, 0.0, 0.333, 0.5}, // top-middle
//     {0.666, 0.0, 0.333, 0.5}, // top-right
//     {0.0, 0.5, 0.333, 0.5},   // bottom-left
//     {0.333, 0.5, 0.333, 0.5}, // bottom-middle
//     {0.666, 0.5, 0.333, 0.5}  // bottom-right
// };

// face Bbox crop Parms
#define FACE_BBOX_CROP_STAGE "face_bbox_crops"
#define FACE_BBOX_CROP_LABEL "face"
#define FACE_BBOX_CROP_INPUT_WIDTH 3840
#define FACE_BBOX_CROP_INPUT_HEIGHT 2160
#define FACE_BBOX_CROP_OUTPUT_WIDTH 120
#define FACE_BBOX_CROP_OUTPUT_HEIGHT 120

// person Bbox crop Parms
#define PERSON_BBOX_CROP_STAGE "person_bbox_crops"
#define PERSON_BBOX_CROP_LABEL "person"
#define PERSON_BBOX_CROP_INPUT_WIDTH 3840
#define PERSON_BBOX_CROP_INPUT_HEIGHT 2160
#define PERSON_BBOX_CROP_OUTPUT_WIDTH 320
#define PERSON_BBOX_CROP_OUTPUT_HEIGHT 240

// Landmarks AI Params
#define LANDMARKS_HEF_FILE "/home/root/apps/s1_demo/resources/tddfa_mobilenet_v1_nv12_quantized.hef"
#define LANDMARKS_AI_STAGE "face_landmarks"
// Landmarks Postprocess Params
#define LANDMARKS_POST_STAGE "landmarks_post"
#define LANDMARKS_POST_SO "/usr/lib/hailo-post-processes/libfacial_landmarks_post.so"
#define LANDMARKS_FUNC_NAME "facial_landmarks_nv12"
// Whitelist landmarks range
#define LANDMARKS_RANGE_MIN 36
#define LANDMARKS_RANGE_MAX 47

// Face detection AI Params
#define FACE_DETECTION_HEF_FILE "/home/root/apps/s1_demo/resources/lightface_slim_nv12_quantized.hef"
#define FACE_DETECTION_AI_STAGE "face_detection"
// Landmarks Postprocess Params
#define FACE_DETECTION_POST_STAGE "face_detection_post"
#define FACE_DETECTION_POST_SO "/usr/lib/hailo-post-processes/libface_detection_post.so"
#define FACE_DETECTION_FUNC_NAME "lightface"

// Macro that turns coverts stream ids to port #s
#define PORT_FROM_ID(id) std::to_string(5000 + std::stoi(id.substr(4)) * 2)

// int fire_detection_fps = 3; // Default fire detection FPS
int fire_detection_fps = 3; // Commented out with fire detection

enum class ArgumentType
{
    Help,
    PrintFPS,
    PrintLatency,
    Timeout,
    Config,
    SkipDrawing,
    FullLandmarks,
    FireDetectionFPS,
    Error
};

void print_help(const cxxopts::Options &options)
{
    std::cout << options.help() << std::endl;
}

cxxopts::Options build_arg_parser()
{
    cxxopts::Options options("AI pipeline app");
    options.add_options()("h, help", "Show this help")("t, timeout", "Time to run", cxxopts::value<int>()->default_value("3000"))("p, print-fps", "Print FPS", cxxopts::value<bool>()->default_value("false"))("l, print-latency", "Print Latency", cxxopts::value<bool>()->default_value("false"))("c, config-file-path", "media library Configuration Path", cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH))("s, skip-drawing", "Skip drawing", cxxopts::value<bool>()->default_value("false"))("f, full-landmarks", "Draw all landmarks (default draws only eyes for face landmarks)", cxxopts::value<bool>()->default_value("false"))("d, fd_fps", "Fire detection FPS", cxxopts::value<int>()->default_value("3"));
    return options;
}

std::vector<ArgumentType> handle_arguments(const cxxopts::ParseResult &result, const cxxopts::Options &options)
{
    std::vector<ArgumentType> arguments;

    if (result.count("help"))
    {
        print_help(options);
        arguments.push_back(ArgumentType::Help);
    }

    if (result.count("print-fps"))
    {
        arguments.push_back(ArgumentType::PrintFPS);
    }

    if (result.count("timeout"))
    {
        arguments.push_back(ArgumentType::Timeout);
    }

    if (result.count("print-latency"))
    {
        arguments.push_back(ArgumentType::PrintLatency);
    }

    if (result.count("config-file-path"))
    {
        arguments.push_back(ArgumentType::Config);
    }

    if (result.count("skip-drawing"))
    {
        arguments.push_back(ArgumentType::SkipDrawing);
    }

    if (result.count("full-landmarks"))
    {
        arguments.push_back(ArgumentType::FullLandmarks);
    }

    if (result.count("fd_fps"))
    {
        arguments.push_back(ArgumentType::FireDetectionFPS);
    }

    // Handle unrecognized options
    for (const auto &unrecognized : result.unmatched())
    {
        std::cerr << "Error: Unrecognized option or argument: " << unrecognized << std::endl;
        return {ArgumentType::Error};
    }

    return arguments;
}
/**
 * @brief Holds the resources required for the application.
 *
 * This structure contains pointers to various components and modules
 * used by the application, including the frontend, encoders, UDP outputs,
 * and the pipeline. It also includes a flag to control whether FPS (frames per second)
 * information should be printed.
 */
struct AppResources
{
    std::shared_ptr<MediaLibrary> media_library;
    std::shared_ptr<FrontendStage> frontend;
    std::map<output_stream_id_t, std::shared_ptr<EncoderStage>> encoders;
    std::map<output_stream_id_t, std::shared_ptr<UdpStage>> udp_outputs;
    PipelinePtr pipeline;
    bool print_fps;
    bool print_latency;
    bool skip_drawing;
    bool full_landmarks;
    std::string medialib_config_path;

    void clear()
    {
        frontend = nullptr;
        pipeline = nullptr;
        encoders.clear();
        udp_outputs.clear();
        print_fps = false;
        print_latency = false;
        skip_drawing = false;
        full_landmarks = false;
        medialib_config_path = "";
        media_library = nullptr;
    }

    ~AppResources()
    {
        clear();
    }
};

std::string read_string_from_file(const char *file_path)
{
    std::ifstream file_to_read;
    file_to_read.open(file_path);
    if (!file_to_read.is_open())
        throw std::runtime_error(std::string("config path (") + file_path + ") is not valid");
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)),
                            std::istreambuf_iterator<char>());
    file_to_read.close();
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
}

/**
 * @brief Subscribe elements within the application pipeline.
 *
 * This function subscribes the output streams from the frontend to appropriate
 * pipeline stages and encoders, ensuring that the data flows correctly through
 * the pipeline. It sets up callbacks for handling the data and integrates encoders
 * with UDP outputs.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void subscribe_to_frontend(std::shared_ptr<AppResources> app_resources)
{
    // Get frontend output streams
    auto streams = app_resources->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        std::cout.flush();
        throw std::runtime_error("Failed to get stream ids");
    }

    // Subscribe to frontend
    for (auto s : streams.value())
    {
        if (s.id == AI_SINK)
        {
            std::cout << "subscribing ai pipeline to frontend for '" << s.id << "'" << std::endl;
            // Subscribe PERSON_DET_RESIZE_STAGE to AI stream (sink2)
            auto person_det_resize_ptr = app_resources->pipeline->get_stage_by_name(PERSON_DET_RESIZE_STAGE);
            if (person_det_resize_ptr)
            {
                auto connected_stage = std::static_pointer_cast<ConnectedStage>(person_det_resize_ptr);
                app_resources->frontend->subscribe_to_stream(s.id, connected_stage);
            }
        }
        else if (s.id == AI_VISION_SINK)
        {
            std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;
            // Subscribe TEE_STAGE to vision stream (sink0)
            auto tee_stage_ptr = app_resources->pipeline->get_stage_by_name(TEE_STAGE);
            if (tee_stage_ptr)
            {
                auto connected_stage = std::static_pointer_cast<ConnectedStage>(tee_stage_ptr);
                app_resources->frontend->subscribe_to_stream(s.id, connected_stage);
            }
        }
        else
        {
            std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;
            // Subscribe encoder to frontend for other streams
            if (app_resources->encoders.find(s.id) != app_resources->encoders.end())
            {
                app_resources->frontend->subscribe_to_stream(s.id, app_resources->encoders[s.id]);
            }
        }
    }
}

/**
 * @brief Create and configure an encoder and its corresponding UDP output file.
 *
 * This function sets up an encoder and a UDP output module for a given stream ID.
 * It reads configuration files and initializes the encoder and UDP module accordingly.
 *
 * @param id The ID of the output stream.
 * @param app_resources Shared pointer to the application's resources.
 */
void create_encoder_and_udp(const std::string &id, std::shared_ptr<AppResources> app_resources)
{
    // Create and configure encoder
    std::string enc_name = "enc_" + id;
    std::cout << "Creating encoder " << enc_name << std::endl;
    std::shared_ptr<EncoderStage> encoder_stage = EncoderStageBuild::create().set_stage_name(enc_name).buildptr();
    app_resources->encoders[id] = encoder_stage;
    AppStatus enc_config_status = encoder_stage->configure(app_resources->media_library->m_encoders[id]);
    if (enc_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure encoder " << enc_name << std::endl;
        throw std::runtime_error("Failed to configure encoder");
    }

    // Create and conifgure udp
    std::string udp_name = "udp_" + id;
    std::cout << "Creating udp " << udp_name << std::endl;
    std::shared_ptr<UdpStage> udp_stage = UdpStageBuild::create().set_stage_name(udp_name).set_leaky_opt(false).set_printfps_opt(true).buildptr();
    app_resources->udp_outputs[id] = udp_stage;
    AppStatus udp_config_status = udp_stage->configure(HOST_IP, PORT_FROM_ID(id), EncodingType::H264);
    if (udp_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure udp " << udp_name << std::endl;
        throw std::runtime_error("Failed to configure udp");
    }

    // Subscribe udp to encoder
    app_resources->encoders[id]->add_subscriber(app_resources->udp_outputs[id]);
}

/**
 * @brief Configure the frontend and encoders for the application.
 *
 * This function initializes the frontend and sets up encoders for each output stream
 * from the frontend. It reads configuration files to properly configure the components.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void configure_frontend_and_encoders(std::shared_ptr<AppResources> app_resources)
{
    std::string medialib_config_string = read_string_from_file(app_resources->medialib_config_path.c_str());
    
    auto media_lib_expected = MediaLibrary::create();
    if (!media_lib_expected.has_value())
    {
        std::cout << "Failed to create media library" << std::endl;
        std::cout.flush();
        throw std::runtime_error("Failed to create media library");
    }
    app_resources->media_library = media_lib_expected.value();
    
    if (app_resources->media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        std::cout.flush();
        return;
    }
    
    // Create and configure frontend
    app_resources->frontend = FrontendStageBuild::create().set_stage_name(FRONTEND_STAGE).buildptr();
    
    AppStatus frontend_config_status = app_resources->frontend->configure(app_resources->media_library->m_frontend);
    if (frontend_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure frontend " << FRONTEND_STAGE << std::endl;
        std::cout.flush();
        throw std::runtime_error("Failed to configure frontend");
    }

    // Get frontend output streams
    auto streams = app_resources->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        std::cout.flush();
        throw std::runtime_error("Failed to get stream ids");
    }

    // Create encoders and output files for each stream
    for (auto s : streams.value())
    {
        if (s.id == AI_SINK)
        {
            // AI pipeline does not get an encoder since it is merged into 4K
            continue;
        }
        create_encoder_and_udp(s.id, app_resources);
    }
}

/**
 * @brief Create and configure the application's processing pipeline.
 *
 * This function sets up the application's processing pipeline by creating various stages
 * and subscribing them to each other to form a complete pipeline. Each stage is initialized
 * with specific parameters and then added to the pipeline. The stages are also interconnected
 * by subscribing them to ensure data flows correctly between them.
 *
 * @param app_resources Shared pointer to the application's resources, which includes the pipeline object.
 */
void create_ai_pipeline(std::shared_ptr<AppResources> app_resources)
{
    // AI Pipeline Stages
    /*
        +-------+    +------------+    +---------+    +---------+
        |  tee  | -> | aggregator | -> | tracker | -> | overlay |
        +-------+    +------------+    +---------+    +---------+
        +-------+________/
        |  ai   |
        +-------+
    */
    std::shared_ptr<MuxerStage> muxer_stage = MuxerStageBuild::create()
                                                      .set_stage_name(MUXER_STAGE)
                                                      .set_main_inlet_name(AI_VISION_SINK)
                                                      .set_sub_inlet_name(SECONDARY_VISION_SINK)
                                                      .set_main_queue_size(10)
                                                      .set_sub_queue_size(10)
                                                      .set_main_leaky(false)
                                                      .set_sub_leaky(false)
                                                      .set_printfps_opt(app_resources->print_fps)
                                                      .buildptr();

    std::shared_ptr<CallbackStage> callback_stage = CallbackStageBuild::create()
                                                            .set_stage_name(CALLBACK_STAGE)
                                                            .set_queue_size_opt(1)
                                                            .set_leaky_opt(false)
                                                            .set_printfps_opt(app_resources->print_fps)
                                                            .buildptr();
    callback_stage->set_callback([](BufferPtr data) {});

    std::shared_ptr<TeeStage> tee_stage = TeeStageBuild::create()
                                                  .set_stage_name(TEE_STAGE)
                                                  .set_queue_size(1)
                                                  .set_leaky_opt(false)
                                                  .set_printfps_opt(app_resources->print_fps)
                                                  .buildptr();
    std::shared_ptr<AggregatorStage> results_agg_stage = AggregatorStageBuild::create()
                                                                                .set_stage_name(RESULTS_AGGREGATOR_STAGE)
                                                                                .set_blocking(true)
                                                                                .set_main_inlet_name(CALLBACK_STAGE)
                                                                                .set_main_queue_size(4)
                                                                                .set_main_leaky(true)
                                                                                .set_sub_inlet_name(AFTER_RESIZE_AGGREGATOR_STAGE)
                                                                                .set_sub_queue_size(3)
                                                                                .set_sub_leaky(false)
                                                                                .set_multiscale_opt(false)
                                                                                .set_sync_opt(true)
                                                                                .set_iou_threshold_opt(0.3)
                                                                                .set_border_threshold_opt(0.1)
                                                                                .set_printfps_opt(app_resources->print_fps)
                                                                                .set_timeout_opt(std::chrono::milliseconds(66))
                                                                                .set_timeout_adjustment_period(std::chrono::milliseconds(500))
                                                                                .set_drop_rate_block(true)
                                                                                .buildptr();
    std::shared_ptr<PersistStage> tracker_stage = std::make_shared<PersistStage>(TRACKER_STAGE, 3, 1, false, app_resources->print_fps);
    std::shared_ptr<OutputMetadataStage> output_metadata_stage = std::make_shared<OutputMetadataStage>(OUTPUT_METADATA_STAGE, 5, app_resources->print_fps);
    std::shared_ptr<OverlayStage> overlay_stage = OverlayStageBuild::create()
                                                                            .set_stage_name(OVERLAY_STAGE)
                                                                            .set_skip_opt(app_resources->skip_drawing)
                                                                            .set_partial_landmarks(!app_resources->full_landmarks)
                                                                            .set_queue_size(1)
                                                                            .set_leaky_opt(false)
                                                                            .set_printfps_opt(app_resources->print_fps)
                                                                            .buildptr();

    /*
             _____________________________________
            /                                     \
        +--------+    +------+    +------+    +------------+
        | tiling | -> | yolo | -> | post | -> | aggregator |
        +--------+    +------+    +------+    +------------+
    */
    std::shared_ptr<TillingCropStage> person_det_resize_stage = TillingCropStageBuild::create()
                                                                              .set_stage_name(PERSON_DET_RESIZE_STAGE)
                                                                              .set_output_pool_size(51)
                                                                              .set_input_width(PERSON_DET_RESIZE_INPUT_WIDTH)
                                                                              .set_input_height(PERSON_DET_RESIZE_INPUT_HEIGHT)
                                                                              .set_output_width(PERSON_DET_RESIZE_OUTPUT_WIDTH)
                                                                              .set_output_height(PERSON_DET_RESIZE_OUTPUT_HEIGHT)
                                                                              .set_main_sub_name(AFTER_RESIZE_AGGREGATOR_STAGE)
                                                                              .set_sub_sub_name(DETECTION_AI_STAGE)
                                                                              .set_bbox_tiles(PERSON_DET_TILE)
                                                                              .set_queue_size(5)
                                                                              .set_leaky_opt(true)
                                                                              .set_printfps_opt(app_resources->print_fps)
                                                                              .set_pool_mode_opt(StagePoolMode::BLOCKING)
                                                                              .set_crop_every_x_frames(1)
                                                                              .buildptr();
    std::shared_ptr<HailortAsyncStage> yolo_detection_stage =
        HailortAsyncStageBuild::create()
            .set_stage_name(DETECTION_AI_STAGE)
            .set_hef_path(YOLO_HEF_FILE)
            .set_queue_size(5)
            .set_output_pool_size(52)
            .set_group_id("device0")
            .set_batch_size(5)
            .set_job_limit(10)
            .set_scheduler_threshold_opt(5)
            .set_dynamic_threshold_opt(false)
            .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
            .set_printfps_opt(app_resources->print_fps)
            .set_pool_mode_opt(StagePoolMode::BLOCKING)
            .buildptr();
    std::shared_ptr<PostprocessStage> yolo_post_stage = PostprocessStageBuild::create()
                                                                     .set_stage_name(POST_STAGE)
                                                                     .set_so_path(YOLO_POST_SO)
                                                                     .set_function_name_opt(YOLO_FUNC_NAME)
                                                                     .set_config_path_opt("")
                                                                     .set_queue_size_opt(5)
                                                                     .set_leaky_opt(false)
                                                                     .set_printfps_opt(app_resources->print_fps)
                                                                     .buildptr();
    std::shared_ptr<AggregatorStage> after_resize_agg_stage = AggregatorStageBuild::create()
                                                                                          .set_stage_name(AFTER_RESIZE_AGGREGATOR_STAGE)
                                                                                          .set_blocking(true)
                                                                                          .set_main_inlet_name(PERSON_DET_RESIZE_STAGE)
                                                                                          .set_main_queue_size(2)
                                                                                          .set_main_leaky(false)
                                                                                          .set_sub_inlet_name(POST_STAGE)
                                                                                          .set_sub_queue_size(5)
                                                                                          .set_sub_leaky(false)
                                                                                          .set_multiscale_opt(true)
                                                                                          .set_sync_opt(false)
                                                                                          .set_iou_threshold_opt(0.3)
                                                                                          .set_border_threshold_opt(0.1)
                                                                                          .set_printfps_opt(app_resources->print_fps)
                                                                                          .buildptr();
    std::shared_ptr<AggregatorStage> agg_stage = AggregatorStageBuild::create()
                                                                                   .set_stage_name(AGGREGATOR_STAGE)
                                                                                   .set_blocking(true)
                                                                                   .set_static_subframes_opt(1)
                                                                                   .set_main_inlet_name(TEE_STAGE)
                                                                                   .set_main_queue_size(5)
                                                                                   .set_main_leaky(false)
                                                                                   .set_sub_inlet_name(CROPPED_FACES_AGGREGATOR)
                                                                                   .set_sub_queue_size(3)
                                                                                   .set_sub_leaky(false)
                                                                                   .set_multiscale_opt(false)
                                                                                   .set_sync_opt(true)
                                                                                   .set_iou_threshold_opt(0.3)
                                                                                   .set_border_threshold_opt(0.1)
                                                                                   .set_skip_migration_opt(true)
                                                                                   .set_printfps_opt(app_resources->print_fps)
                                                                                   .set_timeout_opt(std::chrono::milliseconds(33))
                                                                                   .set_min_timeout_opt(std::chrono::milliseconds(33))
                                                                                   .set_max_timeout_opt(std::chrono::milliseconds(66))
                                                                                   .set_timeout_adjustment_period(std::chrono::milliseconds(500))
                                                                                   .set_drop_rate_threshold(0.1)
                                                                                   .buildptr();

    /*
             __________________________________________
            /                                          \
        +--------+    +-----------+    +------+    +------------+
        |  crop  | -> | lightface | -> | post | -> | aggregator |
        +--------+    +-----------+    +------+    +------------+
    */
    std::shared_ptr<BBoxCropStage> person_bbox_crop_stage = BBoxCropStageBuild::create()
                                                                                            .set_stage_name(PERSON_BBOX_CROP_STAGE)
                                                                                            .set_output_pool_size(150)
                                                                                            .set_input_width(PERSON_BBOX_CROP_INPUT_WIDTH)
                                                                                            .set_input_height(PERSON_BBOX_CROP_INPUT_HEIGHT)
                                                                                            .set_output_width(PERSON_BBOX_CROP_OUTPUT_WIDTH)
                                                                                            .set_output_height(PERSON_BBOX_CROP_OUTPUT_HEIGHT)
                                                                                            .set_main_sub_name(CROPPED_PERSONS_AGGREGATOR)
                                                                                            .set_sub_sub_name(FACE_DETECTION_AI_STAGE)
                                                                                            .set_label(PERSON_BBOX_CROP_LABEL)
                                                                                            .set_queue_size(10)
                                                                                            .set_leaky_opt(false)
                                                                                            .set_printfps_opt(app_resources->print_fps)
                                                                                            .set_pool_mode_opt(StagePoolMode::BLOCKING)
                                                                                            .buildptr();
    std::shared_ptr<HailortAsyncStage> face_detection_stage =
        HailortAsyncStageBuild::create()
            .set_stage_name(FACE_DETECTION_AI_STAGE)
            .set_hef_path(FACE_DETECTION_HEF_FILE)
            .set_queue_size(100)
            .set_output_pool_size(50)
            .set_group_id("device0")
            .set_batch_size(50)
            .set_job_limit(60)
            .set_scheduler_threshold_opt(50)
            .set_dynamic_threshold_opt(true)
            .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
            .set_printfps_opt(app_resources->print_fps)
            .set_pool_mode_opt(StagePoolMode::BLOCKING)
            .buildptr();
    std::shared_ptr<PostprocessStage> face_detection_post_stage = PostprocessStageBuild::create()
                                                                     .set_stage_name(FACE_DETECTION_POST_STAGE)
                                                                     .set_so_path(FACE_DETECTION_POST_SO)
                                                                     .set_function_name_opt(FACE_DETECTION_FUNC_NAME)
                                                                     .set_config_path_opt("")
                                                                     .set_queue_size_opt(100)
                                                                     .set_leaky_opt(false)
                                                                     .set_printfps_opt(app_resources->print_fps)
                                                                     .buildptr();
    std::shared_ptr<AggregatorStage> cropped_persons_agg = AggregatorStageBuild::create()
                                                                                     .set_stage_name(CROPPED_PERSONS_AGGREGATOR)
                                                                                     .set_blocking(true)
                                                                                     .set_main_inlet_name(PERSON_BBOX_CROP_STAGE)
                                                                                     .set_main_queue_size(3)
                                                                                     .set_main_leaky(false)
                                                                                     .set_sub_inlet_name(FACE_DETECTION_POST_STAGE)
                                                                                     .set_sub_queue_size(100)
                                                                                     .set_sub_leaky(false)
                                                                                     .set_multiscale_opt(false)
                                                                                     .set_sync_opt(false)
                                                                                     .set_iou_threshold_opt(0.3)
                                                                                     .set_border_threshold_opt(0.1)
                                                                                     .set_printfps_opt(app_resources->print_fps)
                                                                                     .buildptr();

    /*
         __________________________________________
        /                                          \
    +--------+    +-----------+    +------+    +------------+
    |  crop  | -> | mobilenet | -> | post | -> | aggregator |
    +--------+    +-----------+    +------+    +------------+
*/

    std::shared_ptr<BBoxCropStage> face_bbox_crop_stage = BBoxCropStageBuild::create()
                                                                                          .set_stage_name(FACE_BBOX_CROP_STAGE)
                                                                                          .set_output_pool_size(150)
                                                                                          .set_input_width(FACE_BBOX_CROP_INPUT_WIDTH)
                                                                                          .set_input_height(FACE_BBOX_CROP_INPUT_HEIGHT)
                                                                                          .set_output_width(FACE_BBOX_CROP_OUTPUT_WIDTH)
                                                                                          .set_output_height(FACE_BBOX_CROP_OUTPUT_HEIGHT)
                                                                                          .set_main_sub_name(CROPPED_FACES_AGGREGATOR)
                                                                                          .set_sub_sub_name(LANDMARKS_AI_STAGE)
                                                                                          .set_label(FACE_BBOX_CROP_LABEL)
                                                                                          .set_queue_size(10)
                                                                                          .set_leaky_opt(false)
                                                                                          .set_printfps_opt(app_resources->print_fps)
                                                                                          .set_pool_mode_opt(StagePoolMode::BLOCKING)
                                                                                          .buildptr();
    std::shared_ptr<HailortAsyncStage> landmarks_stage =
        HailortAsyncStageBuild::create()
            .set_stage_name(LANDMARKS_AI_STAGE)
            .set_hef_path(LANDMARKS_HEF_FILE)
            .set_queue_size(100)
            .set_output_pool_size(201)
            .set_group_id("device0")
            .set_batch_size(50)
            .set_job_limit(60)
            .set_scheduler_threshold_opt(50)
            .set_dynamic_threshold_opt(true)
            .set_scheduler_timeout_opt(std::chrono::milliseconds(100))
            .set_printfps_opt(app_resources->print_fps)
            .set_pool_mode_opt(StagePoolMode::BLOCKING)
            .buildptr();
    std::shared_ptr<PostprocessStage> landmarks_post_stage = PostprocessStageBuild::create()
                                                                     .set_stage_name(LANDMARKS_POST_STAGE)
                                                                     .set_so_path(LANDMARKS_POST_SO)
                                                                     .set_function_name_opt(LANDMARKS_FUNC_NAME)
                                                                     .set_config_path_opt("")
                                                                     .set_queue_size_opt(100)
                                                                     .set_leaky_opt(false)
                                                                     .set_printfps_opt(app_resources->print_fps)
                                                                     .buildptr();
    std::shared_ptr<AggregatorStage> cropped_faces_agg = AggregatorStageBuild::create()
                                                                                     .set_stage_name(CROPPED_FACES_AGGREGATOR)
                                                                                     .set_blocking(true)
                                                                                     .set_main_inlet_name(FACE_BBOX_CROP_STAGE)
                                                                                     .set_main_queue_size(3)
                                                                                     .set_main_leaky(false)
                                                                                     .set_sub_inlet_name(LANDMARKS_POST_STAGE)
                                                                                     .set_sub_queue_size(100)
                                                                                     .set_sub_leaky(false)
                                                                                     .set_multiscale_opt(false)
                                                                                     .set_sync_opt(false)
                                                                                     .set_iou_threshold_opt(0.3)
                                                                                     .set_border_threshold_opt(0.1)
                                                                                     .set_printfps_opt(app_resources->print_fps)
                                                                                     .buildptr();



    // std::shared_ptr<TeeStage> demo_tee_stage = TeeStageBuild::create()
                                                  // .set_stage_name(DEMO_TEE_STAGE)
                                                  // .set_queue_size(5)
                                                  // .set_leaky_opt(true)
                                                  // .set_printfps_opt(app_resources->print_fps)
                                                  // .buildptr();
    // std::shared_ptr<DropFrameStage> drop_frames_stage = std::make_shared<DropFrameStage>("drop_frames", 5, true, app_resources->print_fps, fire_detection_fps);
    // std::shared_ptr<TillingCropStage> fire_detection_tilling_stage = TillingCropStageBuild::create()
                                                                                         // .set_stage_name(FIRE_DET_TILLING_STAGE)
                                                                                         // .set_output_pool_size(55)
                                                                                         // .set_input_width(FIRE_DET_TILLING_INPUT_WIDTH)
                                                                                         // .set_input_height(FIRE_DET_TILLING_INPUT_HEIGHT)
                                                                                         // .set_output_width(FIRE_DET_TILLING_OUTPUT_WIDTH)
                                                                                         // .set_output_height(FIRE_DET_TILLING_OUTPUT_HEIGHT)
                                                                                         // .set_main_sub_name(FIRE_DET_TILLING_AGGREGATOR_STAGE)
                                                                                         // .set_sub_sub_name(CONVERT_STAGE)
                                                                                         // .set_bbox_tiles(FIRE_DET_TILES)
                                                                                         // .set_queue_size(5)
                                                                                         // .set_leaky_opt(true)
                                                                                         // .set_printfps_opt(app_resources->print_fps)
                                                                                         // .set_pool_mode_opt(StagePoolMode::BLOCKING)
                                                                                         // .set_crop_every_x_frames(1)
                                                                                         // .buildptr();
    // std::shared_ptr<DspConvertStage> convert_stage = std::make_shared<DspConvertStage>(CONVERT_STAGE, 30);
    // std::shared_ptr<FireDetectionHailortAsyncStage> clip_stage = std::make_shared<FireDetectionHailortAsyncStage>(CLIP_AI_STAGE, CLIP_HEF_FILE, 6, 54, "device0", 6, 10, 6, false,
                                                                                                                       // std::chrono::milliseconds(100), app_resources->print_fps, StagePoolMode::BLOCKING);
    // std::shared_ptr<AggregatorStage> fire_detection_tiling_agg_stage = AggregatorStageBuild::create()
                                                                                          // .set_stage_name(FIRE_DET_TILLING_AGGREGATOR_STAGE)
                                                                                          // .set_blocking(false)
                                                                                          // .set_static_subframes_opt(6)
                                                                                          // .set_main_inlet_name(FIRE_DET_TILLING_STAGE)
                                                                                          // .set_main_queue_size(6)
                                                                                          // .set_main_leaky(true)
                                                                                          // .set_sub_inlet_name(CLIP_AI_STAGE)
                                                                                          // .set_sub_queue_size(20)
                                                                                          // .set_sub_leaky(false)
                                                                                          // .set_multiscale_opt(true)
                                                                                          // .set_sync_opt(false)
                                                                                          // .set_iou_threshold_opt(0.3)
                                                                                          // .set_border_threshold_opt(0.1)
                                                                                          // .set_printfps_opt(app_resources->print_fps)
                                                                                          // .buildptr();

    // Subscribe stages to each other
    // AI Pipeline stages
    // demo_tee_stage->add_subscriber(person_det_resize_stage);
    // demo_tee_stage->add_subscriber(drop_frames_stage);
    // drop_frames_stage->add_subscriber(fire_detection_tilling_stage);
    // fire_detection_tilling_stage->add_subscriber(fire_detection_tiling_agg_stage);
    // fire_detection_tilling_stage->add_subscriber(convert_stage);
    // convert_stage->add_subscriber(clip_stage);
    // clip_stage->add_subscriber(fire_detection_tiling_agg_stage);

    muxer_stage->add_subscriber(callback_stage);
    callback_stage->add_subscriber(results_agg_stage);
    results_agg_stage->add_subscriber(tee_stage);
    tee_stage->add_subscriber(agg_stage);
    
    person_det_resize_stage->add_subscriber(after_resize_agg_stage);
    person_det_resize_stage->add_subscriber(yolo_detection_stage);                                                                                         
    yolo_detection_stage->add_subscriber(yolo_post_stage);
    yolo_post_stage->add_subscriber(after_resize_agg_stage);
    after_resize_agg_stage->add_subscriber(results_agg_stage);
    
    tee_stage->add_subscriber(person_bbox_crop_stage);
    person_bbox_crop_stage->add_subscriber(cropped_persons_agg);
    person_bbox_crop_stage->add_subscriber(face_detection_stage);
    face_detection_stage->add_subscriber(face_detection_post_stage);
    face_detection_post_stage->add_subscriber(cropped_persons_agg);
    cropped_persons_agg->add_subscriber(face_bbox_crop_stage);
    face_bbox_crop_stage->add_subscriber(cropped_faces_agg);
    face_bbox_crop_stage->add_subscriber(landmarks_stage);
    landmarks_stage->add_subscriber(landmarks_post_stage);
    landmarks_post_stage->add_subscriber(cropped_faces_agg);
    cropped_faces_agg->add_subscriber(agg_stage);

    // Vision Pipeline stages
    agg_stage->add_subscriber(tracker_stage);
    tracker_stage->add_subscriber(output_metadata_stage);
    output_metadata_stage->add_subscriber(overlay_stage);
    overlay_stage->add_subscriber(app_resources->encoders[AI_VISION_SINK]);
    
    // Add all stages to pipeline
    app_resources->pipeline->add_stage(app_resources->frontend, StageType::SOURCE);
    app_resources->pipeline->add_stage(muxer_stage);
    app_resources->pipeline->add_stage(callback_stage);
    app_resources->pipeline->add_stage(tee_stage);
    app_resources->pipeline->add_stage(results_agg_stage);
    app_resources->pipeline->add_stage(tracker_stage);
    app_resources->pipeline->add_stage(output_metadata_stage);
    app_resources->pipeline->add_stage(overlay_stage);
    app_resources->pipeline->add_stage(person_det_resize_stage);
    app_resources->pipeline->add_stage(yolo_detection_stage);
    app_resources->pipeline->add_stage(yolo_post_stage);
    app_resources->pipeline->add_stage(after_resize_agg_stage);
    app_resources->pipeline->add_stage(agg_stage);
    app_resources->pipeline->add_stage(person_bbox_crop_stage);
    app_resources->pipeline->add_stage(face_detection_stage);
    app_resources->pipeline->add_stage(face_detection_post_stage);
    app_resources->pipeline->add_stage(cropped_persons_agg);
    app_resources->pipeline->add_stage(face_bbox_crop_stage);
    app_resources->pipeline->add_stage(landmarks_stage);
    app_resources->pipeline->add_stage(landmarks_post_stage);
    app_resources->pipeline->add_stage(cropped_faces_agg);
    // app_resources->pipeline->add_stage(demo_tee_stage);
    // app_resources->pipeline->add_stage(drop_frames_stage);
    // app_resources->pipeline->add_stage(fire_detection_tilling_stage);
    // app_resources->pipeline->add_stage(convert_stage);
    // app_resources->pipeline->add_stage(clip_stage);
    // app_resources->pipeline->add_stage(fire_detection_tiling_agg_stage);
    
    // Add encoders and UDP outputs as sinks
    for (auto &encoder_pair : app_resources->encoders)
    {
        app_resources->pipeline->add_stage(encoder_pair.second, StageType::SINK);
        app_resources->pipeline->add_stage(app_resources->udp_outputs[encoder_pair.first], StageType::SINK);
    }
}
// Global variables for signal handling
std::mutex g_stop_mutex;
std::condition_variable g_stop_cv;
bool g_stop_requested = false;

/**
 * @brief Main function to initialize and run the application.
 *
 * This function sets up the application resources, registers a signal handler for SIGINT,
 * parses user arguments, configures the frontend and encoders, creates the pipeline,
 * subscribes elements, starts the pipeline, waits for a specified timeout, and then stops the pipeline.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return int Exit status of the application.
 */
int main(int argc, char *argv[])
{
    std::cout << "PERSON DETECTION PIPELINE" << std::endl;
    std::cout.flush();
    
    // App resources
    std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();
    app_resources->medialib_config_path = MEDIALIB_CONFIG_PATH;
    
    // Register signal SIGINT and signal handler
    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([](int signal) {
        {
            std::unique_lock<std::mutex> lk(g_stop_mutex);
            g_stop_requested = true;
        }
        g_stop_cv.notify_all();
    });
    
    // Parse user arguments
    cxxopts::Options options = build_arg_parser();
    auto result = options.parse(argc, argv);
    std::vector<ArgumentType> argument_handling_results = handle_arguments(result, options);
    int timeout = result["timeout"].as<int>();
    
    for (ArgumentType argument : argument_handling_results)
    {
        switch (argument)
        {
        case ArgumentType::Help:
            return 0;
        case ArgumentType::Timeout:
            break;
        case ArgumentType::PrintFPS:
            app_resources->print_fps = true;
            break;
        case ArgumentType::PrintLatency:
            app_resources->print_latency = true;
            break;
        case ArgumentType::Config:
            app_resources->medialib_config_path = result["config-file-path"].as<std::string>();
            break;
        case ArgumentType::SkipDrawing:
            app_resources->skip_drawing = true;
            break;
        case ArgumentType::FullLandmarks:
            app_resources->full_landmarks = true;
            break;
        case ArgumentType::FireDetectionFPS:
            fire_detection_fps = result["fd_fps"].as<int>();
            if(fire_detection_fps > 15)
            {
                std::cerr << "Fire detection FPS cannot be greater than 15. Setting to 15." << std::endl;
                fire_detection_fps = 15;
            }
            std::cout << "Fire detection FPS set to: " << fire_detection_fps << std::endl;
            break;
        case ArgumentType::Error:
            return 1;
        }
    }

    // Configure frontend and encoders
    configure_frontend_and_encoders(app_resources);

    // Create pipeline and stages
    app_resources->pipeline = std::make_shared<Pipeline>();
    
    create_ai_pipeline(app_resources);

    // Subscribe stages to frontend
    subscribe_to_frontend(app_resources);

    // Start pipeline
    std::cout << "Starting." << std::endl;
    REFERENCE_CAMERA_LOG_INFO("Starting.");
    app_resources->pipeline->start_pipeline();

    REFERENCE_CAMERA_LOG_INFO("Started playing for {} seconds.", timeout);

    // Wait for either timeout or signal
    {
        std::unique_lock<std::mutex> lk(g_stop_mutex);
        if (!g_stop_requested)
        {
            g_stop_cv.wait_for(lk, std::chrono::seconds(timeout));
        }
    }
    
    // Stop pipeline
    std::cout << "Stopping." << std::endl;
    REFERENCE_CAMERA_LOG_INFO("Stopping.");
    app_resources->pipeline->stop_pipeline();
    app_resources->clear();
    
    return 0;
}
