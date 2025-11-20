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
#include "kms_stage.hpp"
#include "frontend_stage.hpp"
#include "reference_camera_logger.hpp"
#include "dsp_convert_stage.hpp"

// Stage Params
#define FRONTEND_STAGE "frontend_stage"
#define KMS_DRIVER_NAME "hailo-drm"
#define NO_PROFILE_SELECTED ""
#define MEDIALIB_CONFIG_PATH "/etc/imaging/cfg/medialib_configs/case_studies/single_stream_medialib_config.json"

enum class ArgumentType
{
    Help,
    PrintFPS,
    PrintLatency,
    Timeout,
    Config,
    Profile,
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
        cxxopts::value<std::string>()->default_value(NO_PROFILE_SELECTED));
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
    std::shared_ptr<DspConvertStage> dsp_convert;
    std::shared_ptr<KmsStage> kms_output;
    PipelinePtr pipeline;
    bool print_fps;
    bool print_latency;
    std::string medialib_config_path;
    std::string profile_name;

    void clear()
    {
        frontend = nullptr;
        dsp_convert = nullptr;
        kms_output = nullptr;
        pipeline = nullptr;
        print_fps = false;
        print_latency = false;
        medialib_config_path = "";
        media_library = nullptr;
        profile_name = NO_PROFILE_SELECTED;
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
 * @brief Subscribe elements within the application pipeline.
 *
 * This function subscribes the output streams from the frontend to appropriate
 * pipeline stages, ensuring that the data flows correctly through the pipeline.
 * The data flows from frontend -> DSP convert -> KMS output for display.
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
        throw std::runtime_error("Failed to get stream ids");
    }

    // Subscribe to frontend
    for (auto s : streams.value())
    {
        std::cout << "subscribing to frontend for '" << s.id << "'" << std::endl;
        // Subscribe DSP convert stage to frontend
        if (app_resources->dsp_convert)
        {
            app_resources->frontend->subscribe_to_stream(s.id, app_resources->dsp_convert);
            // Subscribe KMS output to DSP convert stage output
            app_resources->dsp_convert->add_subscriber(app_resources->kms_output);
        }
        else
        {
            // If DSP convert is not enabled, subscribe KMS directly to frontend
            app_resources->frontend->subscribe_to_stream(s.id, app_resources->kms_output);
        }
    }
}

/**
 * @brief Create and configure the KMS output stage for display.
 *
 * This function sets up the KMS (Kernel Mode Setting) stage for displaying
 * the video on the screen using the specified display driver.
 *
 * @param app_resources Shared pointer to the application's resources.
 * @param width Width of the frames.
 * @param height Height of the frames.
 */
void create_kms_output(std::shared_ptr<AppResources> app_resources, int width, int height)
{
    // Create and configure KMS stage
    std::string kms_name = "kms_output";
    std::cout << "Creating KMS output stage " << kms_name << std::endl;
    std::shared_ptr<KmsStage> kms_stage = std::make_shared<KmsStage>(kms_name);
    app_resources->kms_output = kms_stage;
    
    AppStatus kms_config_status = kms_stage->configure(KMS_DRIVER_NAME, false, true, EncodingType::H264, width, height);
    if (kms_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure KMS stage " << kms_name << std::endl;
        throw std::runtime_error("Failed to configure KMS stage");
    }

    // Add KMS stage to pipeline as a sink
    app_resources->pipeline->add_stage(app_resources->kms_output, StageType::SINK);
}

/**
 * @brief Configure the DSP convert stage for format conversion.
 *
 * This function initializes and configures the DSP convert stage to convert
 * input frames to the desired output format (e.g., RGB/BGR).
 *
 * @param app_resources Shared pointer to the application's resources.
 * @param width Width of the frames.
 * @param height Height of the frames.
 * @param output_format Output format for conversion.
 */
void configure_dsp_convert_stage(std::shared_ptr<AppResources> app_resources, 
                                 int width, int height, 
                                 HailoFormat output_format = HAILO_FORMAT_RGB)
{
    std::cout << "Creating DSP convert stage" << std::endl;
    app_resources->dsp_convert = std::make_shared<DspConvertStage>("dsp_convert_stage");
    app_resources->pipeline->add_stage(app_resources->dsp_convert, StageType::GENERAL);
    
    AppStatus dsp_config_status = app_resources->dsp_convert->configure(width, height, output_format);
    if (dsp_config_status != AppStatus::SUCCESS)
    {
        std::cerr << "Failed to configure DSP convert stage" << std::endl;
        throw std::runtime_error("Failed to configure DSP convert stage");
    }
}

/**
 * @brief Configure the frontend and output stages for the application.
 *
 * This function initializes the frontend and sets up the DSP convert and KMS output stages.
 * It reads configuration files to properly configure the components.
 *
 * @param app_resources Shared pointer to the application's resources.
 */
void configure_frontend_and_output(std::shared_ptr<AppResources> app_resources)
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
    app_resources->pipeline->add_stage(app_resources->frontend, StageType::SOURCE);
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

    // Configure DSP convert stage with dimensions from frontend
    if (!streams.value().empty())
    {
        int width = streams.value()[0].width;
        int height = streams.value()[0].height;
        configure_dsp_convert_stage(app_resources, width, height, HAILO_FORMAT_RGB);
        
        // Create KMS output stage with dimensions
        create_kms_output(app_resources, width, height);
    }
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
    // App resources
    std::shared_ptr<AppResources> app_resources = std::make_shared<AppResources>();
    app_resources->medialib_config_path = MEDIALIB_CONFIG_PATH;

    // register signal SIGINT and signal handler
    signal_utils::SignalHandler signal_handler(false);
    signal_handler.register_signal_handler([app_resources](int signal) {
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
        case ArgumentType::Error:
            return 1;
        }
    }

    // Create pipeline
    app_resources->pipeline = std::make_shared<Pipeline>();

    // Configure frontend and output stages
    configure_frontend_and_output(app_resources);

    // Subscribe stages to frontend
    subscribe_to_frontend(app_resources);

    // Start pipeline
    std::cout << "Starting." << std::endl;
    REFERENCE_CAMERA_LOG_INFO("Starting.");
    app_resources->media_library->start_pipeline();
    app_resources->pipeline->start_pipeline();

    REFERENCE_CAMERA_LOG_INFO("Started playing for {} seconds.", timeout);

    // Wait for either timeout or signal
    std::unique_lock<std::mutex> lk(g_stop_mutex);
    g_stop_cv.wait_for(lk, std::chrono::seconds(timeout));

    // Stop pipeline
    std::cout << "Stopping." << std::endl;
    REFERENCE_CAMERA_LOG_INFO("Stopping.");
    app_resources->pipeline->stop_pipeline();
    app_resources->media_library->stop_pipeline();
    app_resources->clear();
    return 0;
}
