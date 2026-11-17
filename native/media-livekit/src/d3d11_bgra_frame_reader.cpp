#include "screen_share/media/d3d11_bgra_frame_reader.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

namespace screen_share::media {
namespace {

using Microsoft::WRL::ComPtr;

[[nodiscard]] bool is_supported_bgra_format(const DXGI_FORMAT format) noexcept {
    return format == DXGI_FORMAT_B8G8R8A8_UNORM ||
           format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

}  // namespace

struct D3D11BgraFrameReader::Impl final {
    std::mutex mutex;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> staging_texture;
    std::uint32_t staging_width{0};
    std::uint32_t staging_height{0};
    DXGI_FORMAT staging_format{DXGI_FORMAT_UNKNOWN};

    void release_resources() noexcept {
        staging_texture.Reset();
        context.Reset();
        device.Reset();
        staging_width = 0;
        staging_height = 0;
        staging_format = DXGI_FORMAT_UNKNOWN;
    }

    [[nodiscard]] bool ensure_staging_texture(
        ID3D11Texture2D* const source_texture,
        const D3D11_TEXTURE2D_DESC& source_description,
        const std::uint32_t width,
        const std::uint32_t height) {
        ComPtr<ID3D11Device> source_device;
        source_texture->GetDevice(source_device.GetAddressOf());
        if (!source_device) {
            return false;
        }

        const bool resources_match = device.Get() == source_device.Get() && staging_texture &&
                                     staging_width == width && staging_height == height &&
                                     staging_format == source_description.Format;
        if (resources_match) {
            return true;
        }

        release_resources();
        device = std::move(source_device);
        device->GetImmediateContext(context.GetAddressOf());
        if (!context) {
            release_resources();
            return false;
        }

        ComPtr<ID3D11Multithread> multithread;
        if (SUCCEEDED(context.As(&multithread))) {
            multithread->SetMultithreadProtected(TRUE);
        }

        D3D11_TEXTURE2D_DESC staging_description{};
        staging_description.Width = width;
        staging_description.Height = height;
        staging_description.MipLevels = 1;
        staging_description.ArraySize = 1;
        staging_description.Format = source_description.Format;
        staging_description.SampleDesc.Count = 1;
        staging_description.Usage = D3D11_USAGE_STAGING;
        staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        if (FAILED(device->CreateTexture2D(
                &staging_description, nullptr, staging_texture.GetAddressOf()))) {
            release_resources();
            return false;
        }

        staging_width = width;
        staging_height = height;
        staging_format = source_description.Format;
        return true;
    }

    [[nodiscard]] D3D11ReadbackResult read(
        const screen_share::capture::D3D11CaptureFrameView& source,
        BgraVideoFrame& destination) {
        destination = {};
        if (source.texture == nullptr || source.width == 0U || source.height == 0U) {
            return D3D11ReadbackResult::InvalidFrame;
        }

        D3D11_TEXTURE2D_DESC source_description{};
        source.texture->GetDesc(&source_description);
        if (!is_supported_bgra_format(source_description.Format) ||
            source_description.SampleDesc.Count != 1U) {
            return D3D11ReadbackResult::UnsupportedFormat;
        }
        if (source.width > source_description.Width || source.height > source_description.Height) {
            return D3D11ReadbackResult::InvalidFrame;
        }

        constexpr auto bytes_per_pixel = std::size_t{4};
        const auto row_bytes = static_cast<std::size_t>(source.width) * bytes_per_pixel;
        if (row_bytes > std::numeric_limits<std::size_t>::max() /
                            static_cast<std::size_t>(source.height)) {
            return D3D11ReadbackResult::InvalidFrame;
        }

        destination.width = source.width;
        destination.height = source.height;
        destination.timestamp_us = source.system_relative_time_100ns / 10;
        destination.pixels.resize(row_bytes * static_cast<std::size_t>(source.height));

        if (!ensure_staging_texture(
                source.texture, source_description, source.width, source.height)) {
            destination = {};
            return D3D11ReadbackResult::DeviceUnavailable;
        }

        const D3D11_BOX source_box{
            .left = 0,
            .top = 0,
            .front = 0,
            .right = source.width,
            .bottom = source.height,
            .back = 1,
        };
        context->CopySubresourceRegion(
            staging_texture.Get(), 0, 0, 0, 0, source.texture, 0, &source_box);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto map_result =
            context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(map_result)) {
            destination = {};
            if (map_result == DXGI_ERROR_DEVICE_REMOVED ||
                map_result == DXGI_ERROR_DEVICE_RESET) {
                release_resources();
                return D3D11ReadbackResult::DeviceUnavailable;
            }
            return D3D11ReadbackResult::Failed;
        }

        if (static_cast<std::size_t>(mapped.RowPitch) < row_bytes) {
            context->Unmap(staging_texture.Get(), 0);
            destination = {};
            return D3D11ReadbackResult::Failed;
        }

        const auto* source_bytes = static_cast<const std::uint8_t*>(mapped.pData);
        for (std::uint32_t row = 0; row < source.height; ++row) {
            const auto source_offset = static_cast<std::size_t>(row) * mapped.RowPitch;
            const auto destination_offset = static_cast<std::size_t>(row) * row_bytes;
            std::memcpy(destination.pixels.data() + destination_offset,
                        source_bytes + source_offset,
                        row_bytes);
        }
        context->Unmap(staging_texture.Get(), 0);
        return D3D11ReadbackResult::Read;
    }
};

D3D11BgraFrameReader::D3D11BgraFrameReader() : implementation_(std::make_unique<Impl>()) {
}

D3D11BgraFrameReader::~D3D11BgraFrameReader() {
    reset();
}

D3D11ReadbackResult D3D11BgraFrameReader::read(
    const screen_share::capture::D3D11CaptureFrameView& source,
    BgraVideoFrame& destination) noexcept {
    try {
        const std::scoped_lock lock(implementation_->mutex);
        return implementation_->read(source, destination);
    } catch (...) {
        destination = {};
        return D3D11ReadbackResult::Failed;
    }
}

void D3D11BgraFrameReader::reset() noexcept {
    if (implementation_) {
        const std::scoped_lock lock(implementation_->mutex);
        implementation_->release_resources();
    }
}

}  // namespace screen_share::media
