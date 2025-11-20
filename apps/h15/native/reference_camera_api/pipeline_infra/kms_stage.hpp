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
#include "kms_module.hpp"

#define KMS_QUEUE_SIZE_DEFAULT (1)

class KmsStage : public ConnectedStage
{
  private:
    std::string m_driver_name;
    bool m_can_scale;
    bool m_force_modesetting;
    EncodingType m_type;
    KmsModulePtr m_kms;
    int m_width;
    int m_height;

  public:
    inline KmsStage(std::string name, size_t queue_size = KMS_QUEUE_SIZE_DEFAULT, bool leaky = false, bool print_fps = false)
        : ConnectedStage(name, queue_size, leaky, print_fps), m_width(0), m_height(0)
    {
    }

    inline AppStatus create(std::string driver_name, bool can_scale, bool force_modesetting, EncodingType type, int width = 0, int height = 0)
    {
        if (m_kms == nullptr)
        {
            tl::expected<KmsModulePtr, AppStatus> kms_expected =
                KmsModule::create(m_stage_name, driver_name, can_scale, force_modesetting, type, m_print_fps, width, height);
            if (!kms_expected.has_value())
            {
                std::cout << "Failed to create kms" << std::endl;
                REFERENCE_CAMERA_LOG_ERROR("Failed to create kms");
                return AppStatus::CONFIGURATION_ERROR;
            }
            m_kms = kms_expected.value();
            m_driver_name = driver_name;
            m_can_scale = can_scale;
            m_force_modesetting = force_modesetting;
            m_type = type;
            m_width = width;
            m_height = height;
        }
        return AppStatus::SUCCESS;
    }

    inline AppStatus init() override
    {
        if (m_kms == nullptr)
        {
            std::cerr << "Kms " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Kms  {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }
        m_kms->start();
        return AppStatus::SUCCESS;
    }

    inline AppStatus deinit() override
    {
        m_kms->stop();
        return AppStatus::SUCCESS;
    }

    inline AppStatus configure(std::string driver_name, bool can_scale, bool force_modesetting, EncodingType type, int width = 0, int height = 0)
    {
        if (m_kms == nullptr)
        {
            return create(driver_name, can_scale, force_modesetting, type, width, height);
        }
        m_kms->stop();
        m_kms = nullptr;
        return create(driver_name, can_scale, force_modesetting, type, width, height);
    }

    inline AppStatus process(BufferPtr data)
    {
        if (m_kms == nullptr)
        {
            std::cerr << "Kms " << m_stage_name << " not configured. Call configure()" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Kms {} not configured. Call configure()", m_stage_name);
            return AppStatus::UNINITIALIZED;
        }

        std::vector<MetadataPtr> metadata = data->get_metadata_of_type(MetadataType::SIZE);
        if (metadata.size() <= 0)
        {
            std::cerr << "Kms " << m_stage_name << " got buffer of unknown size, add SizeMeta" << std::endl;
            REFERENCE_CAMERA_LOG_ERROR("Kms {} got buffer of unknown size, add SizeMeta");
            return AppStatus::PIPELINE_ERROR;
        }
        SizeMetadataPtr size_metadata = std::dynamic_pointer_cast<SizeMetadata>(metadata[0]);
        size_t size = size_metadata->get_size();
        m_kms->add_buffer(data->get_buffer(), size);

        return AppStatus::SUCCESS;
    }
};


class KmsStageBuild : public KmsStage
{
public:
    class Builder {
    
    private:
        std::optional<std::string>  m_stage_name;
        size_t                      m_queue_size=KMS_QUEUE_SIZE_DEFAULT;
        bool                        m_leaky=false;
        bool                        m_print_fps=false;        
    
    public:
        Builder& set_stage_name(std::string name) { m_stage_name=name; return *this;}
        Builder& set_queue_size_opt(size_t size) { m_queue_size=size; return *this;}
        Builder& set_leaky_opt(bool activate) { m_leaky=activate; return *this;}
        Builder& set_printfps_opt(bool activate) { m_print_fps=activate; return *this;}

        std::shared_ptr<KmsStage> buildptr() const { 
            THROW_IF_MISSING(m_stage_name.has_value(), "set_stage_name");

            return std::make_shared<KmsStage>(m_stage_name.value(), m_queue_size, m_leaky, m_print_fps);}
    };

    static Builder create() { return Builder(); }

};
