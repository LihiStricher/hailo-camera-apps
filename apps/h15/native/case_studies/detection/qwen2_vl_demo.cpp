// general includes
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <tl/expected.hpp>
#include <cxxopts/cxxopts.hpp>

// medialibrary includes
#include "media_library/media_library.hpp"
#include "media_library/encoder.hpp"
#include "media_library/frontend.hpp"
#include "media_library/signal_utils.hpp"

// infra includes
#include "pipeline.hpp"
#include "ai_stage.hpp"
#include "rgb_ai_stage.hpp"
#include "dsp_stages.hpp"
#include "dsp_convert_stage.hpp"
#include "postprocess_stage.hpp"
#include "overlay_stage.hpp"
#include "udp_stage.hpp"
#include "encoder_stage.hpp"
#include "frontend_stage.hpp"
#include "file_source_stage.hpp"
#include "reference_camera_logger.hpp"
#include "pipeline_builder.hpp"
#include "output_metadata_stage.hpp"

// Stage Params
#define FRONTEND_STAGE "frontend_stage"
#define HOST_IP "10.0.0.2"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/case_studies/detection_medialib_config.json"

// AI Pipeline Params
#define VISION_SINK "sink0" // The streamid from frontend to 4K stream that shows vision results
#define AI_VISION_SINK "sink1" // The small streamid from frontend that would be used for overlay
#define VISION_STREAM "sink2" // The streamid for additional vision stream
// Input dimensions (no tiling/resizing)
#define INPUT_WIDTH 336
#define INPUT_HEIGHT 336
// DSP Convert Stage Params
#define DSP_CONVERT_STAGE "dsp_convert_nv12_to_rgb"
// Qwen VL AI Params
#define QWEN_VL_HEF_FILE "/home/root/apps/case_studies/detection/resources/qwen2_vl_7b_vision_336x336.hef"
#define QWEN_VL_AI_STAGE "qwen_vl_inference"
// Output Metadata Stage Params
#define OUTPUT_METADATA_STAGE "output_metadata"

// Macro that turns coverts stream ids to port #s
#define PORT_FROM_ID(id) std::to_string(5000 + std::stoi(id.substr(4)) * 2)

enum class ArgumentType
{
    Help,
    PrintFPS,
    PrintLatency,
    Timeout,
    Config,
    Profile,
    HostIP,
    InputFile,
    HefFile,
    InputWidth,
    InputHeight,
    InputLayerName,
    Error
};

void print_help(const cxxopts::Options &options)
{
    std::cout << options.help() << std::endl;
}

cxxopts::Options build_arg_parser()
{
    // clang-format off
    cxxopts::Options options("AI pipeline app");
    options.add_options()
    ("h,help", "Show this help")
    ("t,timeout", "Time to run", 
        cxxopts::value<int>()->default_value("60"))
    ("p,print-fps", "Print FPS", 
        cxxopts::value<bool>()->default_value("false"))
    ("l,print-latency", "Print Latency", 
        cxxopts::value<bool>()->default_value("false"))
    ("c,config-file-path", "Media library configuration path", 
        cxxopts::value<std::string>()->default_value(MEDIALIB_CONFIG_PATH))
    ("a,profile", "Profile name", 
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED))
    ("o,host-ip", "Host IP address for UDP output", 
        cxxopts::value<std::string>()->default_value(HOST_IP))
    ("i,input-file", "Input video file path (NV12 format, if not specified uses frontend)",
        cxxopts::value<std::string>())
    ("hef-file", "HEF model file path", 
        cxxopts::value<std::string>()->default_value(QWEN_VL_HEF_FILE))
    ("input-width", "Input width for the model", 
        cxxopts::value<int>()->default_value("336"))
    ("input-height", "Input height for the model", 
        cxxopts::value<int>()->default_value("336"))
    ("input-layer-name", "Input layer name in HEF model", 
        cxxopts::value<std::string>()->default_value("qwen2_vl_7b_vision_336x336/input_layer1"));
    // clang-format on

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

    if (result.count("profile"))
    {
        arguments.push_back(ArgumentType::Profile);
    }

    if (result.count("host-ip"))
    {
        arguments.push_back(ArgumentType::HostIP);
    }

    if (result.count("input-file"))
    {
        arguments.push_back(ArgumentType::InputFile);
    }

    if (result.count("hef-file"))
    {
        arguments.push_back(ArgumentType::HefFile);
    }

    if (result.count("input-width"))
    {
        arguments.push_back(ArgumentType::InputWidth);
    }

    if (result.count("input-height"))
    {
        arguments.push_back(ArgumentType::InputHeight);
    }

    if (result.count("input-layer-name"))
    {
        arguments.push_back(ArgumentType::InputLayerName);
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
    std::string medialib_config_path;
    std::string profile_name;
    std::string host_ip = HOST_IP;
    std::string input_file = "";  /**< Optional input file path for file-based input */
    std::string hef_file = QWEN_VL_HEF_FILE;  /**< HEF model file path */
    int input_width = INPUT_WIDTH;  /**< Input width for the model */
    int input_height = INPUT_HEIGHT;  /**< Input height for the model */
    std::string input_layer_name = "qwen2_vl_7b_vision_336x336/input_layer1";  /**< Input layer name in HEF model */

    void clear()
    {
        frontend = nullptr;
        pipeline = nullptr;
        encoders.clear();
        udp_outputs.clear();
        print_fps = false;
        print_latency = false;
        medialib_config_path = "";
        media_library = nullptr;
        profile_name = NO_PROFILE_SELECTED;
        host_ip = HOST_IP;
        input_file = "";
        hef_file = QWEN_VL_HEF_FILE;
        input_width = INPUT_WIDTH;
        input_height = INPUT_HEIGHT;
        input_layer_name = "qwen2_vl_7b_vision_336x336/input_layer1";
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
    std::cout << "Read config from file: " << file_path << std::endl;
    return file_string;
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
    std::shared_ptr<EncoderStage> encoder_stage = std::make_shared<EncoderStage>(enc_name);
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
    std::shared_ptr<UdpStage> udp_stage = std::make_shared<UdpStage>(udp_name);
    app_resources->udp_outputs[id] = udp_stage;
    AppStatus udp_config_status = udp_stage->configure(app_resources->host_ip, PORT_FROM_ID(id), EncodingType::H264);
    if (udp_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure udp " << udp_name << std::endl;
        throw std::runtime_error("Failed to configure udp");
    }
    
    // Note: Pipeline stages and subscriptions will be configured in create_ai_pipeline()
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
        throw std::runtime_error("Failed to create media library");
    }
    app_resources->media_library = media_lib_expected.value();
    if (app_resources->media_library->initialize(medialib_config_string) != media_library_return::MEDIA_LIBRARY_SUCCESS)
    {
        std::cout << "Failed to initialize media library" << std::endl;
        throw std::runtime_error("Failed to initialize media library");
    }
    if (app_resources->profile_name != NO_PROFILE_SELECTED)
    {
        app_resources->media_library->set_profile(app_resources->profile_name);
    }
    // Create and configure frontend
    app_resources->frontend = std::make_shared<FrontendStage>(FRONTEND_STAGE);
    AppStatus frontend_config_status = app_resources->frontend->configure(app_resources->media_library->m_frontend);
    if (frontend_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure frontend " << FRONTEND_STAGE << std::endl;
        throw std::runtime_error("Failed to configure frontend");
    }

    // Get frontend output streams
    auto streams = app_resources->frontend->get_outputs_streams();
    if (!streams.has_value())
    {
        std::cout << "Failed to get stream ids" << std::endl;
        throw std::runtime_error("Failed to get stream ids");
    }

    // Create encoders and output files for each stream
    for (auto s : streams.value())
    {
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
    std::cout << "Creating AI pipeline..." << std::endl;
    // AI Pipeline Stages

    // Determine if using file input or frontend
    bool use_file_input = !app_resources->input_file.empty();
    
    // Source stage (either FileSourceStage or FrontendStage)
    ConnectedStagePtr source_stage;
    
    if (use_file_input)
    {
        std::cout << "Using file input: " << app_resources->input_file << std::endl;
        auto file_source = FileSourceStageBuild::create()
                               .set_stage_name("file_source")
                               .set_file_location(app_resources->input_file)
                               .set_width(app_resources->input_width)
                               .set_height(app_resources->input_height)
                               .set_fps(1)
                               .set_printfps_opt(app_resources->print_fps)
                               .set_buffer_pool_size(10)
                               .buildptr();
        source_stage = file_source;
    }
    else
    {
        std::cout << "Using frontend input" << std::endl;
        source_stage = app_resources->frontend;
    }

    // DSP Convert stage for NV12 to RGB conversion
    auto dsp_convert_stage = std::make_shared<DspConvertStage>(
        DSP_CONVERT_STAGE, app_resources->input_width, app_resources->input_height, 5, true, app_resources->print_fps, false);
    dsp_convert_stage->m_input_width = app_resources->input_width;
    dsp_convert_stage->m_input_height = app_resources->input_height;
    dsp_convert_stage->m_output_width = app_resources->input_width;
    dsp_convert_stage->m_output_height = app_resources->input_height;

    // Qwen VL inference stage with RGB input
    std::shared_ptr<RGBHailortAsyncStage> qwen_vl_stage = std::make_shared<RGBHailortAsyncStage>(
        QWEN_VL_AI_STAGE,
        app_resources->hef_file,
        app_resources->input_layer_name,
        5,
        50,
        "device0",
        5,
        10,
        5,
        false,
        std::chrono::milliseconds(100),
        app_resources->print_fps,
        StagePoolMode::BLOCKING);

    // Output Metadata stage to publish metadata
    std::shared_ptr<OutputMetadataStage> output_metadata_stage = std::make_shared<OutputMetadataStage>(
        OUTPUT_METADATA_STAGE, 5, false, app_resources->print_fps);

    // Add stages to pipeline using pipeline builder
    PipelineBuilder pip_builder;

    // Add source stage (either frontend or file source)
    if (use_file_input)
    {
        pip_builder.add_stage(source_stage, StageType::SOURCE);
    }
    else
    {
        pip_builder.add_stage(app_resources->frontend, StageType::SOURCE);
        pip_builder.add_stage(app_resources->encoders[VISION_SINK], StageType::SINK);
        pip_builder.add_stage(app_resources->encoders[AI_VISION_SINK], StageType::SINK);
        pip_builder.add_stage(app_resources->encoders[VISION_STREAM], StageType::SINK);
        pip_builder.add_stage(app_resources->udp_outputs[VISION_SINK], StageType::SINK);
        pip_builder.add_stage(app_resources->udp_outputs[AI_VISION_SINK], StageType::SINK);
        pip_builder.add_stage(app_resources->udp_outputs[VISION_STREAM], StageType::SINK);
    }
    
    pip_builder.add_stage(dsp_convert_stage);
    pip_builder.add_stage(qwen_vl_stage);
    pip_builder.add_stage(output_metadata_stage);

    // Connect source to DSP convert (no tilling)
    if (use_file_input)
    {
        // For file input, directly connect file source to DSP convert
        pip_builder.connect("file_source", DSP_CONVERT_STAGE);
    }
    else
    {
        // For frontend input, use connect_frontend
        auto streams = app_resources->frontend->get_outputs_streams();
        if (streams.has_value())
        {
            for (auto s : streams.value())
            {
                // VISION_SINK: Frontend → Encoder → UDP (raw video)
                if (s.id == VISION_SINK)
                {
                    pip_builder.connect_frontend(FRONTEND_STAGE, s.id, app_resources->encoders[VISION_SINK]->get_name());
                }
                // AI_VISION_SINK: Frontend → DSP Convert (no tilling)
                else if (s.id == AI_VISION_SINK)
                {
                    pip_builder.connect_frontend(FRONTEND_STAGE, s.id, DSP_CONVERT_STAGE);
                }
                // VISION_STREAM: Frontend → Encoder → UDP (additional vision stream)
                else if (s.id == VISION_STREAM)
                {
                    pip_builder.connect_frontend(FRONTEND_STAGE, s.id, app_resources->encoders[VISION_STREAM]->get_name());
                }
            }
        }
        
        // Stream 1 - VISION_SINK: Frontend → Encoder → UDP (raw video)
        pip_builder.connect(app_resources->encoders[VISION_SINK]->get_name(), app_resources->udp_outputs[VISION_SINK]->get_name());
        
        // Stream 3 - VISION_STREAM: Frontend → Encoder → UDP (additional vision stream)
        pip_builder.connect(app_resources->encoders[VISION_STREAM]->get_name(), app_resources->udp_outputs[VISION_STREAM]->get_name());
    }
    
    // Stream 2 - AI Pipeline: DSP → AI → Output Metadata → Encoder
    pip_builder.connect(DSP_CONVERT_STAGE, QWEN_VL_AI_STAGE)
        .connect(QWEN_VL_AI_STAGE, OUTPUT_METADATA_STAGE);  // Inference → Metadata (publishes tensors)
    
    if (!use_file_input)
    {
        pip_builder.connect(OUTPUT_METADATA_STAGE, app_resources->encoders[AI_VISION_SINK]->get_name());
        // Connect AI encoder to UDP
        pip_builder.connect(app_resources->encoders[AI_VISION_SINK]->get_name(), app_resources->udp_outputs[AI_VISION_SINK]->get_name());
    }

    // Build and assign pipeline
    app_resources->pipeline = pip_builder.build();
}

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
std::mutex g_stop_mutex;
std::condition_variable g_stop_cv;
int main(int argc, char *argv[])
{
    std::cout << "Starting Qwen2-VL demo application." << std::endl;
    // App resources
    std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();
    app_resources->medialib_config_path = MEDIALIB_CONFIG_PATH;

    // register signal SIGINT and signal handler
    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([](int signal) {
        std::cout << "Stopping Pipeline..." << std::endl;
        REFERENCE_CAMERA_LOG_INFO("Stopping Pipeline...");
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
        case ArgumentType::Profile:
            app_resources->profile_name = result["profile"].as<std::string>();
            break;
        case ArgumentType::HostIP:
            app_resources->host_ip = result["host-ip"].as<std::string>();
            break;
        case ArgumentType::InputFile:
            app_resources->input_file = result["input-file"].as<std::string>();
            break;
        case ArgumentType::HefFile:
            app_resources->hef_file = result["hef-file"].as<std::string>();
            break;
        case ArgumentType::InputWidth:
            app_resources->input_width = result["input-width"].as<int>();
            break;
        case ArgumentType::InputHeight:
            app_resources->input_height = result["input-height"].as<int>();
            break;
        case ArgumentType::InputLayerName:
            app_resources->input_layer_name = result["input-layer-name"].as<std::string>();
            break;
        case ArgumentType::Error:
            return 1;
        }
    }

    // Configure frontend and encoders
    configure_frontend_and_encoders(app_resources);

    // Create pipeline and stages (this will set app_resources->pipeline via PipelineBuilder)
    // Note: PipelineBuilder handles all frontend connections, so no separate subscribe_to_frontend() call needed
    create_ai_pipeline(app_resources);

    // Start pipeline
    std::cout << "Starting." << std::endl;
    REFERENCE_CAMERA_LOG_INFO("Starting.");
    if (app_resources->media_library)
    {
        app_resources->media_library->start_pipeline();
    }
    app_resources->pipeline->start_pipeline();

    REFERENCE_CAMERA_LOG_INFO("Started playing for {} seconds.", timeout);

    // Wait for either timeout or signal
    std::unique_lock<std::mutex> lk(g_stop_mutex);
    g_stop_cv.wait_for(lk, std::chrono::seconds(timeout));

    // Stop pipeline
    std::cout << "Stopping." << std::endl;
    REFERENCE_CAMERA_LOG_INFO("Stopping.");
    app_resources->pipeline->stop_pipeline();
    if (app_resources->media_library)
    {
        app_resources->media_library->stop_pipeline();
    }
    return 0;
}
