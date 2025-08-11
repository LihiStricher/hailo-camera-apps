

#include <iostream>
#include <fstream>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <string>
#include <stdexcept>
#include <iterator>
#include <cxxopts/cxxopts.hpp>

#include "media_library/signal_utils.hpp"
#include "media_library/media_library.hpp"
#include "media_library/encoder.hpp"
#include "pipeline.hpp"
#include "encoder_stage.hpp"
#include "udp_stage.hpp"
#include "file_source_stage.hpp"
#include "pipeline_builder.hpp"
#include "overlay_stage.hpp"
#include "ai_stage.hpp"
#include "dsp_stages.hpp"
#include "postprocess_stage.hpp"
#include "aggregator_stage.hpp"

#define PERSON_DET_RESIZE_STAGE "person_detection_resize_stage"
#define PERSON_DET_RESIZE_INPUT_WIDTH 1920
#define PERSON_DET_RESIZE_INPUT_HEIGHT 1080
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

#define LANDMARKS_RANGE_MIN 36
#define LANDMARKS_RANGE_MAX 47


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
                           .buildptr();
    
    std::shared_ptr<EncoderStage> encoder_stage = std::make_shared<EncoderStage>("encoder_stage");  
    std::shared_ptr<UdpStage> udp_stage = std::make_shared<UdpStage>("udp_stage");  

    if (encoder_stage->configure(media_encoder) != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure encoder stage" << std::endl;
        exit(1);
    }
    udp_stage->configure(config.host, config.port, EncodingType::H264);


    std::shared_ptr<TillingCropStage> person_det_resize_stage = std::make_shared<TillingCropStage>(PERSON_DET_RESIZE_STAGE, 51, PERSON_DET_RESIZE_INPUT_WIDTH, PERSON_DET_RESIZE_INPUT_HEIGHT,
                                                                                         PERSON_DET_RESIZE_OUTPUT_WIDTH, PERSON_DET_RESIZE_OUTPUT_HEIGHT,
                                                                                         AFTER_RESIZE_AGGREGATOR_STAGE, DETECTION_AI_STAGE, PERSON_DET_TILE,
                                                                                         5, true, app_resources->print_fps, StagePoolMode::BLOCKING);
    std::shared_ptr<HailortAsyncStage> yolo_detection_stage = std::make_shared<HailortAsyncStage>(DETECTION_AI_STAGE, YOLO_HEF_FILE, 5, 52, "device0", 5, 10, 5, false,
                                                                                             std::chrono::milliseconds(100), app_resources->print_fps, StagePoolMode::BLOCKING);
    std::shared_ptr<PostprocessStage> yolo_post_stage = std::make_shared<PostprocessStage>(POST_STAGE, YOLO_POST_SO, YOLO_FUNC_NAME, "", 5, false, app_resources->print_fps);
    std::shared_ptr<AggregatorStage> after_resize_agg_stage = std::make_shared<AggregatorStage>(AFTER_RESIZE_AGGREGATOR_STAGE, true, 1,
                                                                                          PERSON_DET_RESIZE_STAGE, 2, false,
                                                                                          POST_STAGE, 5, false,
                                                                                          true, false, 0.3, 0.1,
                                                                                          app_resources->print_fps);
    std::shared_ptr<OverlayStage> overlay_stage = std::make_shared<OverlayStage>("overlay_stage", app_resources->skip_drawing, !app_resources->full_landmarks, LANDMARKS_RANGE_MIN, LANDMARKS_RANGE_MAX, 1, false, app_resources->print_fps);


    app_resources->pipeline->add_stage(file_source_stage);
    app_resources->pipeline->add_stage(encoder_stage);
    app_resources->pipeline->add_stage(udp_stage);

    app_resources->pipeline->add_stage(person_det_resize_stage);
    app_resources->pipeline->add_stage(yolo_detection_stage);
    app_resources->pipeline->add_stage(yolo_post_stage);
    app_resources->pipeline->add_stage(after_resize_agg_stage);
    app_resources->pipeline->add_stage(overlay_stage);

    file_source_stage->add_subscriber(person_det_resize_stage);
    person_det_resize_stage->add_subscriber(yolo_detection_stage);
    person_det_resize_stage->add_subscriber(after_resize_agg_stage);
    yolo_detection_stage->add_subscriber(yolo_post_stage);
    yolo_post_stage->add_subscriber(after_resize_agg_stage);
    after_resize_agg_stage->add_subscriber(overlay_stage);
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
