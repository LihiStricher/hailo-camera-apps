#pragma once

// General includes
#include <algorithm>

// Media-Library includes
#include "media_library/encoder.hpp"

// Tappas includes
#include "hailo_common.hpp"

// Infra includes
#include "stage.hpp"
#include "buffer.hpp"
#include "rtsp_module.hpp"

#define RTSP_QUEUE_SIZE_DEFAULT (1)

class RtspStage : public ConnectedStage
{
  private:
    std::string m_location;
    std::string m_protocols;
    uint32_t m_latency;
    EncodingType m_type;
    RtspModulePtr m_rtsp;

  public:
    inline RtspStage(std::string name, size_t queue_size = RTSP_QUEUE_SIZE_DEFAULT, bool leaky = false, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps)
    {
    }

    inline AppStatus create(std::string location, std::string protocols, uint32_t latency, EncodingType type)
    {
        if (m_rtsp == nullptr)
        {
            tl::expected<RtspModulePtr, AppStatus> rtsp_expected =
                RtspModule::create(m_stage_name, location, protocols, latency, type, m_print_fps);
            if (!rtsp_expected.has_value())
            {
                std::cout << "Failed to create rtsp" << std::endl;
                REFERENCE_CAMERA_LOG_ERROR("Failed to create rtsp");
                return AppStatus::CONFIGURATION_ERROR;
            }
            m_rtsp = rtsp_expected.value();
            m_location = location;
            m_protocols = protocols;
            m_latency = latency;
            m_type = type;
        }
        return AppStatus::SUCCESS;
    }

    inline AppStatus init() override
    {
        if (m_rtsp == nullptr)
        {
            std::cerr << "Rtsp " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Rtsp {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }
        m_rtsp->start();
        return AppStatus::SUCCESS;
    }

    inline AppStatus deinit() override
    {
        m_rtsp->stop();
        return AppStatus::SUCCESS;
    }

    inline AppStatus configure(std::string location, std::string protocols, uint32_t latency, EncodingType type)
    {
        if (m_rtsp == nullptr)
        {
            return create(location, protocols, latency, type);
        }
        m_rtsp->stop();
        m_rtsp = nullptr;
        return create(location, protocols, latency, type);
    }

    inline AppStatus process(BufferPtr data)
    {
        if (m_rtsp == nullptr)
        {
            std::cerr << "Rtsp " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Rtsp {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }

        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::SIZE);
        if (metadata.size() <= 0)
        {
            std::cerr << "Rtsp " << m_stage_name << " got buffer of unknown size, add SizeMeta" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Rtsp {} got buffer of unknown size, add SizeMeta", m_stage_name);
            return AppStatus::PIPELINE_ERROR;
        }
        SizeMetadataPtr size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata[0]);
        size_t size = size_metadata->get_size();
        m_rtsp->add_buffer(data->get_buffer(), size);

        return AppStatus::SUCCESS;
    }
};


class RtspStageBuild : public RtspStage
{
public:
    class Builder {
    
    private:
        std::optional<std::string>  m_stage_name;
        size_t                      m_queue_size=RTSP_QUEUE_SIZE_DEFAULT;
        bool                        m_leaky=false;
        bool                        m_print_fps=false;        
    
    public:
        Builder& set_stage_name(std::string name) { m_stage_name=name; return *this;}
        Builder& set_queue_size_opt(size_t size) { m_queue_size=size; return *this;}
        Builder& set_leaky_opt(bool activate) { m_leaky=activate; return *this;}
        Builder& set_printfps_opt(bool activate) { m_print_fps=activate; return *this;}

        std::shared_ptr<RtspStage> buildptr() const { 
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

            return std::make_shared<RtspStage>(m_stage_name.value(), m_queue_size, m_leaky, m_print_fps);}
    };

    static Builder create() { return Builder(); }

};
