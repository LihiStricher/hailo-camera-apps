

// general includes
#include <queue>
#include <fstream>
#include <iostream>
#include <fstream>
#include <thread>
#include <signal.h>
#include <vector>
#include <map>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <string>
#include <stdexcept>
#include <iterator>
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
#include "drop_frames_stage.hpp"
#include "file_source_stage.hpp"
#include "pipeline_builder.hpp"
#include "fire_detection_ai_stage.hpp"

#define PERSON_DET_RESIZE_STAGE "person_detection_resize_stage"
#define PERSON_DET_RESIZE_INPUT_WIDTH RESOLUTION_WIDTH
#define PERSON_DET_RESIZE_INPUT_HEIGHT RESOLUTION_HEIGHT
#define PERSON_DET_RESIZE_OUTPUT_WIDTH 640
#define PERSON_DET_RESIZE_OUTPUT_HEIGHT 640
std::vector<HailoBBox> PERSON_DET_TILE = {{0.0, 0.0, 1.0, 1.0}};

#define YOLO_HEF_FILE "/home/root/apps/s1_demo/resources/yolov7.hef"
#define DETECTION_AI_STAGE "yolo_detection"
// Detection Postprocess Params
#define POST_STAGE "yolo_post"
#define YOLO_POST_SO "/usr/lib/hailo-post-processes/libyolo_hailortpp_post.so"
#define YOLO_FUNC_NAME "yolov7"

#define AFTER_RESIZE_AGGREGATOR_STAGE "after_resize_aggregator"

// Face Detection Defines
#define FACE_DETECTION_HEF_FILE "/home/root/apps/s1_demo/resources/lightface_slim_nv12.hef"
#define FACE_DETECTION_AI_STAGE "face_detection"
#define FACE_DETECTION_POST_STAGE "face_detection_post"
#define FACE_DETECTION_POST_SO "/usr/lib/hailo-post-processes/libface_detection_post.so"
#define FACE_DETECTION_FUNC_NAME "lightface"

// Face Landmarks Defines
#define LANDMARKS_HEF_FILE "/home/root/apps/s1_demo/resources/tddfa_mobilenet_v1_nv12.hef"
#define LANDMARKS_AI_STAGE "face_landmarks"
#define LANDMARKS_POST_STAGE "landmarks_post"
#define LANDMARKS_POST_SO "/usr/lib/hailo-post-processes/libfacial_landmarks_post.so"
#define LANDMARKS_FUNC_NAME "facial_landmarks_nv12"

// Person and Face cropping
#define PERSON_BBOX_CROP_STAGE "person_bbox_crops"
#define PERSON_BBOX_CROP_LABEL "person"
#define PERSON_BBOX_CROP_INPUT_WIDTH RESOLUTION_WIDTH
#define PERSON_BBOX_CROP_INPUT_HEIGHT RESOLUTION_HEIGHT
#define PERSON_BBOX_CROP_OUTPUT_WIDTH 320
#define PERSON_BBOX_CROP_OUTPUT_HEIGHT 240

#define FACE_BBOX_CROP_STAGE "face_bbox_crops"
#define FACE_BBOX_CROP_LABEL "face"
#define FACE_BBOX_CROP_INPUT_WIDTH RESOLUTION_WIDTH
#define FACE_BBOX_CROP_INPUT_HEIGHT RESOLUTION_HEIGHT
#define FACE_BBOX_CROP_OUTPUT_WIDTH 120
#define FACE_BBOX_CROP_OUTPUT_HEIGHT 120

// Aggregator stages
#define CROPPED_PERSONS_AGGREGATOR "cropped_persons_aggregator"
#define CROPPED_FACES_AGGREGATOR "cropped_faces_aggregator"
#define RESULTS_AGGREGATOR_STAGE "results_aggregator"
#define AGGREGATOR_STAGE "aggregator"

// Tee stages
#define TEE_STAGE "vision_tee"
#define DEMO_TEE_STAGE "demo_tee"

// Tracker and output stages
#define TRACKER_STAGE "tracker"
#define OUTPUT_METADATA_STAGE "output_metadata"

#define LANDMARKS_RANGE_MIN 1
#define LANDMARKS_RANGE_MAX 47

// Fire Detection Defines
#define CLIP_HEF_FILE "/home/root/apps/s1_demo/resources/rgb_clip_convnext_visual_256.hef"
#define CLIP_AI_STAGE "fire_detection"
#define FIRE_DET_TILLING_STAGE "fire_detection_tilling_stage"
#define FIRE_DET_TILLING_INPUT_WIDTH 1920
#define FIRE_DET_TILLING_INPUT_HEIGHT 1080
#define FIRE_DET_TILLING_OUTPUT_WIDTH 256
#define FIRE_DET_TILLING_OUTPUT_HEIGHT 256
std::vector<HailoBBox> FIRE_DET_TILES = {
    {0.0, 0.0, 0.333, 0.5},   // top-left
    {0.333, 0.0, 0.333, 0.5}, // top-middle
    {0.666, 0.0, 0.333, 0.5}, // top-right
    {0.0, 0.5, 0.333, 0.5},   // bottom-left
    {0.333, 0.5, 0.333, 0.5}, // bottom-middle
    {0.666, 0.5, 0.333, 0.5}  // bottom-right
};
#define FIRE_DET_TILLING_AGGREGATOR_STAGE "fire_det_tiling_aggregator"
#define CONVERT_STAGE "convert"

// Fire Detection FPS (can be made configurable later)
int fire_detection_fps = 2; // Default fire detection FPS

#define RESOLUTION_WIDTH 1920
#define RESOLUTION_HEIGHT 1080

std::mutex g_stop_mutex;
std::condition_variable g_stop_cv;

struct PipelineConfig
{
    std::string file;
    int width;
    int height;
    int fps;
    std::string host;
    std::string port;
    int timeout;
    std::string encoder_config_path;
    bool print_fps;
};

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
        full_landmarks = true;
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
    std::string file_string((std::istreambuf_iterator<char>(file_to_read)), std::istreambuf_iterator<char>());
    file_to_read.close();
    return file_string;
}

PipelineConfig parse_arguments(int argc, char *argv[])
{
    cxxopts::Options options("File Source to UDP Case Study", "Pipeline: FileSourceStage -> EncoderStage -> UdpStage");
    options.add_options()("h,help", "Show help")("f,file", "Video file path", cxxopts::value<std::string>())(
        "w,width", "Video width", cxxopts::value<int>())("g,height", "Video height", cxxopts::value<int>())(
        "r,fps", "Frame rate", cxxopts::value<int>()->default_value("30"))(
        "o,host", "Target IP", cxxopts::value<std::string>()->default_value("10.0.0.2"))(
        "p,port", "UDP port", cxxopts::value<std::string>()->default_value("5000"))(
        "t,timeout", "Timeout (seconds)", cxxopts::value<int>()->default_value("60"))(
        "e,encoder-config", "Encoder config file path",
        cxxopts::value<std::string>())("s,print-fps", "Print FPS", cxxopts::value<bool>()->default_value("false"));

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        std::cout << "\nRequired parameters:\n";
        std::cout << "  -f, --file        Video file path (NV12 format)\n";
        std::cout << "  -w, --width       Video width in pixels\n";
        std::cout << "  -g, --height      Video height in pixels\n";
        std::cout << "  -e, --encoder-config  Encoder configuration file path\n";
        std::cout << "\nExamples:\n";
        std::cout << "  " << argv[0] << " -f /path/to/video.nv12 -w 1920 -g 1080 -e /path/to/encoder.json\n";
        std::cout << "  " << argv[0] << " -f video.nv12 -w 1280 -g 720 -o 192.168.1.100 -p 6000\n";
        std::cout << "\nView stream: vlc udp://<host>:<port>\n";
        exit(0);
    }

    // Check for required parameters
    if (!result.count("file"))
    {
        std::cerr << "Error: Video file path is required (-f/--file)" << std::endl;
        exit(1);
    }
    if (!result.count("width"))
    {
        std::cerr << "Error: Video width is required (-w/--width)" << std::endl;
        exit(1);
    }
    if (!result.count("height"))
    {
        std::cerr << "Error: Video height is required (-g/--height)" << std::endl;
        exit(1);
    }
    if (!result.count("encoder-config"))
    {
        std::cerr << "Error: Encoder config file path is required (-e/--encoder-config)" << std::endl;
        exit(1);
    }

    PipelineConfig config;
    config.file = result["file"].as<std::string>();
    config.width = result["width"].as<int>();
    config.height = result["height"].as<int>();
    config.fps = result["fps"].as<int>();
    config.host = result["host"].as<std::string>();
    config.port = result["port"].as<std::string>();
    config.timeout = result["timeout"].as<int>();
    config.encoder_config_path = result["encoder-config"].as<std::string>();
    config.print_fps = result["print-fps"].as<bool>();

    // Validate file exists
    std::ifstream check_file(config.file);
    if (!check_file.good())
    {
        std::cerr << "Error: File not found: " << config.file << std::endl;
        exit(1);
    }

    return config;
}

std::shared_ptr<MediaLibraryEncoder> create_media_library_encoder(const std::string &encoder_config_path)
{
    std::string encoder_config_string = read_string_from_file(encoder_config_path.c_str());

    auto encoder_expected = MediaLibraryEncoder::create("encoder");
    if (!encoder_expected.has_value())
    {
        std::cerr << "Failed to create MediaLibraryEncoder" << std::endl;
        exit(1);
    }

    auto media_encoder = encoder_expected.value();
    if (media_encoder->set_config(encoder_config_string) != MEDIA_LIBRARY_SUCCESS)
    {
        std::cerr << "Failed to configure MediaLibraryEncoder" << std::endl;
        exit(1);
    }

    return media_encoder;
}


void create_pipeline(const PipelineConfig &config, std::shared_ptr<AppResources> app_resources, 
                     std::shared_ptr<MediaLibraryEncoder> media_encoder)
{
    //build the file source stage
    auto file_source_stage = FileSourceStageBuild::create()
                           .set_stage_name("file_source")
                           .set_file_location(config.file)
                           .set_width(config.width)
                           .set_height(config.height)
                           .set_fps(config.fps)
                           .set_printfps_opt(config.print_fps)
                           .set_buffer_pool_size_opt(100)
                           .buildptr();
    
    std::shared_ptr<EncoderStage> encoder_stage = std::make_shared<EncoderStage>("encoder_stage");  
    std::shared_ptr<UdpStage> udp_stage = std::make_shared<UdpStage>("udp_stage");  

    if (encoder_stage->configure(media_encoder) != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure encoder stage" << std::endl;
        exit(1);
    }
    udp_stage->configure(config.host, config.port, EncodingType::H264);

    // Create Tee stages for branching the pipeline
    std::shared_ptr<TeeStage> tee_stage = std::make_shared<TeeStage>(TEE_STAGE, 5, true, app_resources->print_fps);
    std::shared_ptr<TeeStage> demo_tee_stage = std::make_shared<TeeStage>(DEMO_TEE_STAGE, 5, true, app_resources->print_fps);
    std::shared_ptr<TeeStage> ai_tee = std::make_shared<TeeStage>("ai_tee", 5, true, app_resources->print_fps);
    std::shared_ptr<DropFrameStage> ai_pipeline_drop_frames_stage = std::make_shared<DropFrameStage>(
        "ai_pipeline_drop_frames_stage", 5, true, app_resources->print_fps, 15); // 15 FPS for the person detection 

    // Person Detection Pipeline
    std::shared_ptr<TillingCropStage> person_det_resize_stage = std::make_shared<TillingCropStage>(
        PERSON_DET_RESIZE_STAGE, 51, PERSON_DET_RESIZE_INPUT_WIDTH, PERSON_DET_RESIZE_INPUT_HEIGHT,
        PERSON_DET_RESIZE_OUTPUT_WIDTH, PERSON_DET_RESIZE_OUTPUT_HEIGHT,
        AFTER_RESIZE_AGGREGATOR_STAGE, DETECTION_AI_STAGE, PERSON_DET_TILE,
        5, true, app_resources->print_fps, StagePoolMode::BLOCKING);
        
    std::shared_ptr<HailortAsyncStage> yolo_detection_stage = std::make_shared<HailortAsyncStage>(
        DETECTION_AI_STAGE, YOLO_HEF_FILE, 5, 52, "device0", 1, 10, 1, false,
        std::chrono::milliseconds(100), app_resources->print_fps, StagePoolMode::BLOCKING);
        
    std::shared_ptr<PostprocessStage> yolo_post_stage = std::make_shared<PostprocessStage>(
        POST_STAGE, YOLO_POST_SO, YOLO_FUNC_NAME, "", 5, false, app_resources->print_fps);
        
    std::shared_ptr<AggregatorStage> after_resize_agg_stage = std::make_shared<AggregatorStage>(
        AFTER_RESIZE_AGGREGATOR_STAGE, true, 1,
        PERSON_DET_RESIZE_STAGE, 2, false,
        POST_STAGE, 5, false,
        true, false, 0.3, 0.1,
        app_resources->print_fps);

    // Main aggregator connecting tee and person detection results
    std::shared_ptr<AggregatorStage> agg_stage = std::make_shared<AggregatorStage>(
        AGGREGATOR_STAGE, true, 1,
        TEE_STAGE, 10, false,
        AFTER_RESIZE_AGGREGATOR_STAGE, 3, false,
        false, true, 0.3, 0.1,
        app_resources->print_fps);

    // Person bbox cropping for face detection
    std::shared_ptr<BBoxCropStage> person_bbox_crop_stage = std::make_shared<BBoxCropStage>(
        PERSON_BBOX_CROP_STAGE, 150, PERSON_BBOX_CROP_INPUT_WIDTH, PERSON_BBOX_CROP_INPUT_HEIGHT,
        PERSON_BBOX_CROP_OUTPUT_WIDTH, PERSON_BBOX_CROP_OUTPUT_HEIGHT,
        CROPPED_PERSONS_AGGREGATOR, FACE_DETECTION_AI_STAGE, PERSON_BBOX_CROP_LABEL, 10, false,
        app_resources->print_fps, StagePoolMode::BLOCKING);

    // Face Detection Pipeline
    std::shared_ptr<HailortAsyncStage> face_detection_stage = std::make_shared<HailortAsyncStage>(
        FACE_DETECTION_AI_STAGE, FACE_DETECTION_HEF_FILE, 100, 50, "device0", 10, 20, 10, true,
        std::chrono::milliseconds(100), app_resources->print_fps, StagePoolMode::BLOCKING);
        
    std::shared_ptr<PostprocessStage> face_detection_post_stage = std::make_shared<PostprocessStage>(
        FACE_DETECTION_POST_STAGE, FACE_DETECTION_POST_SO, FACE_DETECTION_FUNC_NAME, "", 100, false, app_resources->print_fps);
        
    std::shared_ptr<AggregatorStage> cropped_persons_agg = std::make_shared<AggregatorStage>(
        CROPPED_PERSONS_AGGREGATOR, true,
        PERSON_BBOX_CROP_STAGE, 3, false,
        FACE_DETECTION_POST_STAGE, 100, false,
        false, false, 0.3, 0.1,
        app_resources->print_fps, std::chrono::milliseconds(66));

    // Face bbox cropping for landmarks
    std::shared_ptr<BBoxCropStage> face_bbox_crop_stage = std::make_shared<BBoxCropStage>(
        FACE_BBOX_CROP_STAGE, 150, FACE_BBOX_CROP_INPUT_WIDTH, FACE_BBOX_CROP_INPUT_HEIGHT,
        FACE_BBOX_CROP_OUTPUT_WIDTH, FACE_BBOX_CROP_OUTPUT_HEIGHT,
        CROPPED_FACES_AGGREGATOR, LANDMARKS_AI_STAGE, FACE_BBOX_CROP_LABEL, 10, false,
        app_resources->print_fps, StagePoolMode::BLOCKING);

    // Facial Landmarks Pipeline
    std::shared_ptr<HailortAsyncStage> landmarks_stage = std::make_shared<HailortAsyncStage>(
        LANDMARKS_AI_STAGE, LANDMARKS_HEF_FILE, 100, 201, "device0", 10, 20, 10, true,
        std::chrono::milliseconds(100), app_resources->print_fps, StagePoolMode::BLOCKING);
        
    std::shared_ptr<PostprocessStage> landmarks_post_stage = std::make_shared<PostprocessStage>(
        LANDMARKS_POST_STAGE, LANDMARKS_POST_SO, LANDMARKS_FUNC_NAME, "", 100, false, app_resources->print_fps);
        
    std::shared_ptr<AggregatorStage> cropped_faces_agg = std::make_shared<AggregatorStage>(
        CROPPED_FACES_AGGREGATOR, true,
        FACE_BBOX_CROP_STAGE, 3, false,
        LANDMARKS_POST_STAGE, 100, false,
        false, false, 0.3, 0.1,
        app_resources->print_fps, std::chrono::milliseconds(66));

    // Final aggregator and visualization stages
    std::shared_ptr<AggregatorStage> results_agg_stage = std::make_shared<AggregatorStage>(
        RESULTS_AGGREGATOR_STAGE, true, 1,
        TEE_STAGE, 10, false,
        CROPPED_FACES_AGGREGATOR, 5, false,
        false, true, 0.3, 0.1,
        app_resources->print_fps, std::chrono::milliseconds(300));
        
    std::shared_ptr<PersistStage> tracker_stage = std::make_shared<PersistStage>(
        TRACKER_STAGE, 3, 1, false, app_resources->print_fps);
        
    std::shared_ptr<OutputMetadataStage> output_metadata_stage = std::make_shared<OutputMetadataStage>(
        OUTPUT_METADATA_STAGE, 5, app_resources->print_fps);
        
    std::shared_ptr<OverlayStage> overlay_stage = std::make_shared<OverlayStage>(
        "overlay_stage", app_resources->skip_drawing, !app_resources->full_landmarks, 
        LANDMARKS_RANGE_MIN, LANDMARKS_RANGE_MAX, 1, false, app_resources->print_fps);

    // Fire Detection Pipeline
    std::shared_ptr<DropFrameStage> fire_drop_frames_stage = std::make_shared<DropFrameStage>(
        "fire_drop_frames", 5, true, app_resources->print_fps, fire_detection_fps, 15);
    
    std::shared_ptr<TillingCropStage> fire_detection_tilling_stage = std::make_shared<TillingCropStage>(
        FIRE_DET_TILLING_STAGE, 55, FIRE_DET_TILLING_INPUT_WIDTH, FIRE_DET_TILLING_INPUT_HEIGHT,
        FIRE_DET_TILLING_OUTPUT_WIDTH, FIRE_DET_TILLING_OUTPUT_HEIGHT,
        FIRE_DET_TILLING_AGGREGATOR_STAGE, CONVERT_STAGE, FIRE_DET_TILES,
        5, true, app_resources->print_fps, StagePoolMode::BLOCKING);
    
    std::shared_ptr<DspConvertStage> convert_stage = std::make_shared<DspConvertStage>(CONVERT_STAGE, 30);
    
    std::shared_ptr<FireDetectionHailortAsyncStage> fire_detection_stage = std::make_shared<FireDetectionHailortAsyncStage>(
        CLIP_AI_STAGE, CLIP_HEF_FILE, 6, 54, "device0", 6, 10, 6, false,
        std::chrono::milliseconds(100), app_resources->print_fps, StagePoolMode::BLOCKING);
    
    std::shared_ptr<AggregatorStage> fire_detection_tiling_agg_stage = std::make_shared<AggregatorStage>(
        FIRE_DET_TILLING_AGGREGATOR_STAGE, false,
        FIRE_DET_TILLING_STAGE, 6, true,
        CLIP_AI_STAGE, 20, false,
        true, false, 0.3, 0.1,
        app_resources->print_fps, std::chrono::milliseconds(66));

    // Add all stages to pipeline
    app_resources->pipeline->add_stage(file_source_stage);
    app_resources->pipeline->add_stage(encoder_stage);
    app_resources->pipeline->add_stage(udp_stage);
    
    // Add tee stages
    app_resources->pipeline->add_stage(tee_stage);
    app_resources->pipeline->add_stage(demo_tee_stage);
    app_resources->pipeline->add_stage(ai_tee);
    app_resources->pipeline->add_stage(ai_pipeline_drop_frames_stage);

    // Add person detection stages
    app_resources->pipeline->add_stage(person_det_resize_stage);
    app_resources->pipeline->add_stage(yolo_detection_stage);
    app_resources->pipeline->add_stage(yolo_post_stage);
    app_resources->pipeline->add_stage(after_resize_agg_stage);
    app_resources->pipeline->add_stage(agg_stage);
    
    // Add face detection stages
    app_resources->pipeline->add_stage(person_bbox_crop_stage);
    app_resources->pipeline->add_stage(face_detection_stage);
    app_resources->pipeline->add_stage(face_detection_post_stage);
    app_resources->pipeline->add_stage(cropped_persons_agg);
    
    // Add facial landmarks stages
    app_resources->pipeline->add_stage(face_bbox_crop_stage);
    app_resources->pipeline->add_stage(landmarks_stage);
    app_resources->pipeline->add_stage(landmarks_post_stage);
    app_resources->pipeline->add_stage(cropped_faces_agg);
    
    // Add final stages
    app_resources->pipeline->add_stage(results_agg_stage);
    app_resources->pipeline->add_stage(tracker_stage);
    app_resources->pipeline->add_stage(output_metadata_stage);
    app_resources->pipeline->add_stage(overlay_stage);

    // Add fire detection stages
    app_resources->pipeline->add_stage(fire_drop_frames_stage);
    app_resources->pipeline->add_stage(fire_detection_tilling_stage);
    app_resources->pipeline->add_stage(convert_stage);
    app_resources->pipeline->add_stage(fire_detection_stage);
    app_resources->pipeline->add_stage(fire_detection_tiling_agg_stage);

    // Connect file source to tee vision
    file_source_stage->add_subscriber(ai_tee);
    ai_tee->add_subscriber(tee_stage);
    ai_tee->add_subscriber(ai_pipeline_drop_frames_stage); 

    ai_pipeline_drop_frames_stage->add_subscriber(demo_tee_stage); 

    demo_tee_stage->add_subscriber(fire_drop_frames_stage); 
    demo_tee_stage->add_subscriber(person_det_resize_stage);

    // Fire detection pipeline connections
    fire_drop_frames_stage->add_subscriber(fire_detection_tilling_stage);
    fire_detection_tilling_stage->add_subscriber(fire_detection_tiling_agg_stage);
    fire_detection_tilling_stage->add_subscriber(convert_stage);
    convert_stage->add_subscriber(fire_detection_stage);
    fire_detection_stage->add_subscriber(fire_detection_tiling_agg_stage);

    // Person detection connections
    person_det_resize_stage->add_subscriber(after_resize_agg_stage);
    person_det_resize_stage->add_subscriber(yolo_detection_stage);
    yolo_detection_stage->add_subscriber(yolo_post_stage);
    yolo_post_stage->add_subscriber(after_resize_agg_stage);
    
    // Main aggregator connections
    tee_stage->add_subscriber(agg_stage);
    after_resize_agg_stage->add_subscriber(agg_stage);
    
    // Face detection pipeline connections
    agg_stage->add_subscriber(person_bbox_crop_stage);
    person_bbox_crop_stage->add_subscriber(cropped_persons_agg);
    person_bbox_crop_stage->add_subscriber(face_detection_stage);
    face_detection_stage->add_subscriber(face_detection_post_stage);
    face_detection_post_stage->add_subscriber(cropped_persons_agg);
    
    // Facial landmarks pipeline connections
    cropped_persons_agg->add_subscriber(face_bbox_crop_stage);
    face_bbox_crop_stage->add_subscriber(cropped_faces_agg);
    face_bbox_crop_stage->add_subscriber(landmarks_stage);
    landmarks_stage->add_subscriber(landmarks_post_stage);
    landmarks_post_stage->add_subscriber(cropped_faces_agg);

    // Final pipeline connections
    tee_stage->add_subscriber(results_agg_stage);
    cropped_faces_agg->add_subscriber(results_agg_stage);
    results_agg_stage->add_subscriber(tracker_stage);
    tracker_stage->add_subscriber(output_metadata_stage);
    output_metadata_stage->add_subscriber(overlay_stage);
    overlay_stage->add_subscriber(encoder_stage);
    encoder_stage->add_subscriber(udp_stage);
}


void run_pipeline(const PipelineConfig &config,std::shared_ptr<AppResources> app_resources)
{
    std::cout << "Streaming " << config.file << " (" << config.width << "x" << config.height << "@" << config.fps
              << "fps) to udp://" << config.host << ":" << config.port << std::endl;

    app_resources->pipeline->start_pipeline();

    std::unique_lock<std::mutex> lock(g_stop_mutex);
    g_stop_cv.wait_for(lock, std::chrono::seconds(config.timeout));

    app_resources->pipeline->stop_pipeline();
    app_resources->clear();
    std::cout << "Done." << std::endl;
}

int main(int argc, char *argv[])
{
    try
    {
        PipelineConfig config = parse_arguments(argc, argv);

        // App resources 
        std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();

        // register signal SIGINT and signal handler
        signal_utils::register_signal_handler([app_resources](int signal)
        { 
            std::cout << "Stopping Pipeline..." << std::endl;
            REFERENCE_CAMERA_LOG_INFO("Stopping Pipeline...");
            // Stop pipeline
            app_resources->pipeline->stop_pipeline();
            app_resources->clear();
            // terminate program  
            exit(0); 
        });

        // Create pipeline
        app_resources->pipeline = std::make_shared<Pipeline>();

        auto media_encoder = create_media_library_encoder(config.encoder_config_path);

        // Create pipeline and stages
        create_pipeline(config, app_resources, media_encoder);

        run_pipeline(config, app_resources);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
