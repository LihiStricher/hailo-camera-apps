#pragma once

// General includes
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <tl/expected.hpp>
#include <functional>
#include <iostream>
#include <queue>
#include <thread>
#include <vector>

// Tappas includes
#include "hailo_objects.hpp"
#include "hailo_common.hpp"

// Media library includes
#include "gsthailobuffermeta.hpp"
#include "media_library/buffer_pool.hpp"
#include "media_library/media_library_types.hpp"
#include "output_module.hpp"

// Infra includes
#include "buffer.hpp"
#include "queue.hpp"
#include "stage.hpp"
#include "output_module.hpp"

// Defines
#define RTSP_SRC_QUEUE_NAME "appsrc_q"
#define RTSP_SOURCE "rtsp_src"

class RtspModule;
using RtspModulePtr = std::shared_ptr<RtspModule>;

class RtspModule : public OutputModule
{
  private:
    std::string m_location;
    std::string m_protocols;
    uint32_t m_latency;

  public:
    static tl::expected<RtspModulePtr, AppStatus> create(std::string name, std::string location, 
                                                         std::string protocols, uint32_t latency,
                                                         EncodingType type, bool print_fps);
    ~RtspModule() override = default;
    RtspModule(std::string name, std::string location, std::string protocols, uint32_t latency,
               EncodingType type, AppStatus &status, bool print_fps);

  private:
    std::string create_pipeline_string();
};

inline tl::expected<RtspModulePtr, AppStatus> RtspModule::create(std::string name, std::string location,
                                                                  std::string protocols, uint32_t latency,
                                                                  EncodingType type, bool print_fps)
{
    AppStatus status = AppStatus::UNINITIALIZED;
    RtspModulePtr rtsp_module = std::make_shared<RtspModule>(name, location, protocols, latency, type, status, print_fps);
    if (status != AppStatus::SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return rtsp_module;
}

inline RtspModule::RtspModule(std::string name, std::string location, std::string protocols, uint32_t latency,
                              EncodingType type, AppStatus &status, bool print_fps)
    : OutputModule(name, type, print_fps), m_location(location), m_protocols(protocols), m_latency(latency)
{
    // Initialize gstreamer
    gst_init(nullptr, nullptr);
    m_pipeline = gst_parse_launch(create_pipeline_string().c_str(), NULL);
    if (!m_pipeline)
    {
        std::cerr << "Failed create RTSP pipeline" << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed create RTSP pipeline");
        status = AppStatus::CONFIGURATION_ERROR;
        return;
    }
    gst_bus_add_watch(gst_element_get_bus(m_pipeline), (GstBusFunc)bus_call, this);
    this->OutputModule::set_gst_callbacks(RTSP_SOURCE);

    status = AppStatus::SUCCESS;
}

/**
 * Create the gstreamer pipeline as string
 *
 * @return A string containing the gstreamer pipeline.
 */
inline std::string RtspModule::create_pipeline_string()
{
    std::string pipeline = "";

    std::string caps_type;
    if (m_type == EncodingType::H264)
    {
        caps_type = "video/x-h264";
    }
    else
    {
        caps_type = "video/x-h265";
    }

    std::ostringstream caps2;
    caps2 << caps_type << ",stream-format=byte-stream,alignment=au";

    std::ostringstream rtsp_sink;
    rtsp_sink << "rtspclientsink location=" << m_location 
              << " protocols=" << m_protocols 
              << " latency=" << m_latency;

    pipeline = "appsrc do-timestamp=true format=time block=true is-live=true max-bytes=0 "
               "max-buffers=1 name=" +
               std::string(RTSP_SOURCE) +
               " ! "
               "queue name=" +
               std::string(RTSP_SRC_QUEUE_NAME) + " leaky=no max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! " +
               caps2.str() + " ! " +
               "h264parse config-interval=-1 ! " +
               caps2.str() + " ! " +
               "tee name=rtsp_tee "
               "rtsp_tee. ! "
               "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! " +
               rtsp_sink.str() +
               " name=rtsp_sink sync=true "
               "rtsp_tee. ! "
               "queue leaky=no max-size-buffers=2 max-size-bytes=0 max-size-time=0 ! "
               "fpsdisplaysink fps-update-interval=2000 signal-fps-measurements=true name=fpsdisplaysink "
               "text-overlay=false sync=true video-sink=fakesink ";

    REFERENCE_CAMERA_LOG_INFO("Pipeline: {}", pipeline);

    return pipeline;
}
