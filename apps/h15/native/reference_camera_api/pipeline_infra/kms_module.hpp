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
#define SRC_QUEUE_NAME "appsrc_q"
#define KMS_SOURCE "kms_src"

class KmsModule;
using KmsModulePtr = std::shared_ptr<KmsModule>;

class KmsModule : public OutputModule
{
  private:
    std::string m_driver_name;
    bool m_can_scale;
    bool m_force_modesetting;

  public:
    static tl::expected<KmsModulePtr, AppStatus> create(std::string name, std::string driver_name, bool can_scale,
                                                        bool force_modesetting, EncodingType type, bool print_fps);
    ~KmsModule() override = default;
    KmsModule(std::string name, std::string driver_name, bool can_scale, bool force_modesetting, EncodingType type,
              AppStatus &status, bool print_fps);

  private:
    std::string create_pipeline_string();
};

inline tl::expected<KmsModulePtr, AppStatus> KmsModule::create(std::string name, std::string driver_name,
                                                               bool can_scale, bool force_modesetting,
                                                               EncodingType type, bool print_fps)
{
    AppStatus status = AppStatus::UNINITIALIZED;
    KmsModulePtr kms_module = std::make_shared<KmsModule>(name, driver_name, can_scale, force_modesetting, type,
                                                          status, print_fps);
    if (status != AppStatus::SUCCESS)
    {
        return tl::make_unexpected(status);
    }
    return kms_module;
}

inline KmsModule::KmsModule(std::string name, std::string driver_name, bool can_scale, bool force_modesetting,
                            EncodingType type, AppStatus &status, bool print_fps)
    : OutputModule(name, type, print_fps), m_driver_name(driver_name), m_can_scale(can_scale),
      m_force_modesetting(force_modesetting)
{
    // Initialize gstreamer
    gst_init(nullptr, nullptr);
    m_pipeline = gst_parse_launch(create_pipeline_string().c_str(), NULL);
    if (!m_pipeline)
    {
        std::cerr << "Failed create KMS pipeline" << std::endl;
        REFERENCE_CAMERA_LOG_ERROR("Failed create KMS pipeline");
        status = AppStatus::CONFIGURATION_ERROR;
        return;
    }
    gst_bus_add_watch(gst_element_get_bus(m_pipeline), (GstBusFunc)bus_call, this);
    this->OutputModule::set_gst_callbacks(KMS_SOURCE);

    status = AppStatus::SUCCESS;
}

/**
 * Create the gstreamer pipeline as string
 *
 * @return A string containing the gstreamer pipeline.
 */
inline std::string KmsModule::create_pipeline_string()
{
    std::string pipeline = "";

    std::ostringstream kms_sink;
    kms_sink << "kmssink driver-name=\"" << m_driver_name << "\" "
             << "can-scale=" << (m_can_scale ? "true" : "false") << " "
             << "force-modesetting=" << (m_force_modesetting ? "true" : "false");

    // Pipeline for raw video input (BGR format from DSP convert)
    // appsrc -> queue -> video/x-raw,format=BGR -> queue -> kmssink
    pipeline = "appsrc do-timestamp=true format=time block=true is-live=true max-bytes=0 "
               "max-buffers=1 name=" +
               std::string(KMS_SOURCE) +
               " ! "
               "queue name=" +
               std::string(SRC_QUEUE_NAME) + " leaky=no max-size-buffers=5 max-size-bytes=0 max-size-time=0 ! " +
               "video/x-raw,format=BGR,framerate=30/1 ! "
               "queue leaky=no max-size-buffers=1 max-size-bytes=0 max-size-time=0 ! " +
               kms_sink.str();

    REFERENCE_CAMERA_LOG_INFO("Pipeline: {}", pipeline);

    return pipeline;
}
