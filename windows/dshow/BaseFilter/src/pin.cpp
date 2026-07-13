/* akvirtualcamera, virtual camera for Mac and Windows.
 * Copyright (C) 2020  Gonzalo Exequiel Pedone
 *
 * akvirtualcamera is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * akvirtualcamera is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with akvirtualcamera. If not, see <http://www.gnu.org/licenses/>.
 *
 * Web-Site: http://webcamoid.github.io/
 */

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <functional>
#include <limits>
#include <mutex>
#include <cstring> //FIXNO29 - change the random frame to black frames
#include <sstream>
#include <thread>
#include <dshow.h>
#include <amvideo.h>
#include <dvdmedia.h>
#include <uuids.h>

#include "pin.h"
#include "basefilter.h"
#include "enummediatypes.h"
#include "memallocator.h"
#include "PlatformUtils/src/preferences.h"
#include "PlatformUtils/src/utils.h"
#include "VCamUtils/src/fraction.h"
#include "VCamUtils/src/videoadjusts.h"
#include "VCamUtils/src/videoconverter.h"
#include "VCamUtils/src/videoframe.h"
#include "VCamUtils/src/videoformatspec.h"
#include "VCamUtils/src/utils.h"
#include "VCamUtils/src/timer.h"

#define TIME_BASE 1.0e7

namespace AkVCam
{
    class PinPrivate
    {
        public:
            Pin *self;
            std::atomic<uint64_t> m_refCount {1};
            IpcBridgePtr m_bridge;
            BaseFilter *m_baseFilter {nullptr};
            std::string m_pinName;
            std::string m_deviceId;
            AM_MEDIA_TYPE *m_mediaType {nullptr};
            EnumMediaTypes *m_mediaTypes {nullptr};
            IPin *m_connectedPin {nullptr};
            IMemInputPin *m_memInputPin {nullptr};
            IMemAllocator *m_memAllocator {nullptr};
            REFERENCE_TIME m_pts {-1};
            REFERENCE_TIME m_start {0};
            REFERENCE_TIME m_stop {MAXLONGLONG};
            double m_rate {1.0};
            std::atomic<FILTER_STATE> m_currentState {State_Stopped}; //FIXNO29
            ULONG m_pushFlags {0};
            REFERENCE_TIME m_streamOffset {0};
            REFERENCE_TIME m_maxStreamOffset {0};
            Timer m_timer;
            std::mutex m_mutex;
            VideoFrame m_currentFrame;
            VideoFrame m_testFrame;
            VideoAdjusts m_videoAdjusts;
            VideoConverter m_videoConverter;
            bool m_horizontalFlip {false};   // Controlled by client
            bool m_verticalFlip {false};
            LONG m_brightness {0};
            LONG m_contrast {0};
            LONG m_saturation {0};
            LONG m_gamma {0};
            LONG m_hue {0};
            LONG m_colorEnable {1};
            bool m_isRgb {false};
            bool m_firstFrame {false};
            bool m_frameReady {false};
            bool m_directMode {false};

            static void sendFrame(void *userData);
            VideoFrame applyAdjusts(const VideoFrame &frame);
            static void propertyChanged(void *userData,
                                        LONG property,
                                        LONG value,
                                        LONG flags);
            VideoFrame fallbackFrame(); //FIXNO29 changed from randomframe
    };
}

AkVCam::Pin::Pin(BaseFilter *baseFilter,
                 const std::vector<VideoFormat> &formats,
                 const std::string &pinName)
{
    AkLogFunction();

    this->d = new PinPrivate;
    this->d->self = this;
    this->d->m_baseFilter = baseFilter;
    this->d->m_bridge = baseFilter->ipcBridge();
    this->d->m_pinName = pinName;
    this->d->m_mediaTypes = new AkVCam::EnumMediaTypes(formats);
    this->d->m_deviceId = baseFilter->deviceId();
    this->d->m_directMode = baseFilter->directMode();
    this->d->m_timer.connectTimeout(this->d, &PinPrivate::sendFrame);

    if (!formats.empty())
        this->d->m_mediaType = mediaTypeFromFormat(formats.front());

    auto cameraIndex = Preferences::cameraFromId(this->d->m_deviceId);

    auto horizontalMirror = Preferences::cameraControlValue(cameraIndex, "hflip") > 0;
    auto verticalMirror = Preferences::cameraControlValue(cameraIndex, "vflip") > 0;
    auto scaling = VideoConverter::ScalingMode(Preferences::cameraControlValue(cameraIndex, "scaling"));
    auto aspectRatio = VideoConverter::AspectRatioMode(Preferences::cameraControlValue(cameraIndex, "aspect_ratio"));
    auto swapRgb = Preferences::cameraControlValue(cameraIndex, "swap_rgb") > 0;

    this->d->m_videoAdjusts.setHue(this->d->m_hue);
    this->d->m_videoAdjusts.setSaturation(this->d->m_saturation);
    this->d->m_videoAdjusts.setLuminance(this->d->m_brightness);
    this->d->m_videoAdjusts.setGamma(this->d->m_gamma);
    this->d->m_videoAdjusts.setContrast(this->d->m_contrast);
    this->d->m_videoAdjusts.setGrayScaled(!this->d->m_colorEnable);
    this->d->m_videoAdjusts.setHorizontalMirror(horizontalMirror);
    this->d->m_videoAdjusts.setVerticalMirror(verticalMirror);
    this->d->m_videoAdjusts.setSwapRGB(swapRgb);
    this->d->m_videoConverter.setAspectRatioMode(VideoConverter::AspectRatioMode(aspectRatio));
    this->d->m_videoConverter.setScalingMode(VideoConverter::ScalingMode(scaling));

    auto picture = Preferences::picture();

    if (!picture.empty()) {
        this->d->m_mutex.lock();
        this->d->m_testFrame = loadPicture(picture);
        this->d->m_mutex.unlock();
    }

    LONG flags = 0;
    this->d->m_baseFilter->Get(VideoProcAmp_Brightness,
                               &this->d->m_brightness,
                               &flags);
    this->d->m_baseFilter->Get(VideoProcAmp_Contrast,
                               &this->d->m_contrast,
                               &flags);
    this->d->m_baseFilter->Get(VideoProcAmp_Saturation,
                               &this->d->m_saturation,
                               &flags);
    this->d->m_baseFilter->Get(VideoProcAmp_Gamma,
                               &this->d->m_gamma,
                               &flags);
    this->d->m_baseFilter->Get(VideoProcAmp_Hue,
                               &this->d->m_hue,
                               &flags);
    this->d->m_baseFilter->Get(VideoProcAmp_ColorEnable,
                               &this->d->m_colorEnable,
                               &flags);

    this->d->m_baseFilter->connectPropertyChanged(this->d,
                                                  &PinPrivate::propertyChanged);
}

AkVCam::Pin::~Pin()
{
    AkLogFunction();
    deleteMediaType(&this->d->m_mediaType);

    if (this->d->m_mediaTypes)
        this->d->m_mediaTypes->Release();

    if (this->d->m_connectedPin)
        this->d->m_connectedPin->Release();

    if (this->d->m_memInputPin)
        this->d->m_memInputPin->Release();

    if (this->d->m_memAllocator)
        this->d->m_memAllocator->Release();

    delete this->d;
}

AkVCam::BaseFilter *AkVCam::Pin::baseFilter() const
{
    AkLogFunction();

    return this->d->m_baseFilter;
}

HRESULT AkVCam::Pin::stop() //FIXNO29, changed the function
{
    AkLogFunction();

    if (this->d->m_currentState.load() == State_Stopped) //FIXNO29 add .load()
        return S_OK;

    this->d->m_currentState.store(State_Stopped); //FIXNO29 add .store()

    if (this->d->m_bridge)
        this->d->m_bridge->deviceStop(this->d->m_deviceId);

    // Wake a producer blocked in IMemAllocator::GetBuffer before joining it.
    if (this->d->m_memAllocator)
        this->d->m_memAllocator->Decommit();

    this->d->m_timer.stop();

    std::lock_guard<std::mutex> lock(this->d->m_mutex);
    this->d->m_currentFrame = {};
    this->d->m_frameReady = false;

    AkLogInfo("Stream stopped");

    return S_OK;
}

HRESULT AkVCam::Pin::pause() //FIXNO29
{
    AkLogFunction();

    if (this->d->m_currentState.load() == State_Paused)
        return S_OK;

    auto prevState = this->d->m_currentState.load();

    // A paused source must not keep producing a continuous stream.
    if (prevState == State_Running) {
    if (this->d->m_memAllocator) {
        auto hr = this->d->m_memAllocator->Decommit();

        if (FAILED(hr))
            return hr;
    }

    this->d->m_timer.stop();

    if (this->d->m_memAllocator) {
        auto hr = this->d->m_memAllocator->Commit();

        if (FAILED(hr))
            return hr;
    }
}

    if (prevState == State_Stopped) {
        if (!this->d->m_memAllocator)
            return VFW_E_NOT_CONNECTED;

        auto hr = this->d->m_memAllocator->Commit();

        if (FAILED(hr))
            return hr;

        std::lock_guard<std::mutex> lock(this->d->m_mutex);

        auto videoFormat = formatFromMediaType(this->d->m_mediaType);

        this->d->m_pts = 0;
        this->d->m_firstFrame = true;
        this->d->m_frameReady = false;
        this->d->m_currentFrame = {videoFormat};
        this->d->m_videoConverter.setOutputFormat(videoFormat);

        auto specs = VideoFormat::formatSpecs(videoFormat.format());
        this->d->m_isRgb = specs.type() == VideoFormatSpec::VFT_RGB;
    }

    this->d->m_currentState.store(State_Paused);

    if (this->d->m_bridge)
        this->d->m_bridge->deviceStop(this->d->m_deviceId);

    // A DirectShow source paused from stopped supplies one preroll sample.
    if (prevState == State_Stopped) {
        this->d->m_timer.setInterval(0);
        this->d->m_timer.singleShot();
    }

    return S_OK;
}

HRESULT AkVCam::Pin::run(REFERENCE_TIME tStart) //FIXNO29 Changed function entirely
{
    AkLogFunction();
    AkLogDebug("Start time: %zu", tStart);

    if (this->d->m_currentState.load() == State_Running)
        return S_OK;

    auto prevState = this->d->m_currentState.load();
    auto videoFormat = formatFromMediaType(this->d->m_mediaType);
    auto fps = videoFormat.fps();

    if (fps.num() <= 0 || fps.den() <= 0) {
        AkLogError("Invalid frame rate");

        return VFW_E_INVALIDMEDIATYPE;
    }

    if (prevState == State_Stopped) {
        if (!this->d->m_memAllocator)
            return VFW_E_NOT_CONNECTED;

        auto hr = this->d->m_memAllocator->Commit();

        if (FAILED(hr))
            return hr;

        std::lock_guard<std::mutex> lock(this->d->m_mutex);

        this->d->m_pts = 0;
        this->d->m_firstFrame = true;
        this->d->m_frameReady = false;
        this->d->m_currentFrame = {videoFormat};
        this->d->m_videoConverter.setOutputFormat(videoFormat);

        auto specs = VideoFormat::formatSpecs(videoFormat.format());
        this->d->m_isRgb = specs.type() == VideoFormatSpec::VFT_RGB;
    }

    // State changes only after the frame buffer is initialized.
    this->d->m_currentState.store(State_Running);

    if (this->d->m_bridge)
        this->d->m_bridge->deviceStart(IpcBridge::StreamType_Input,
                                       this->d->m_deviceId);

    this->d->m_timer.setInterval(
        (std::max)(1, static_cast<int>(1000 / fps.value())));
    this->d->m_timer.start();

    AkLogDebug("Stream running");

    return S_OK;
}

void AkVCam::Pin::frameReady(const VideoFrame &frame, bool isActive)
{
    AkLogFunction();
    AkLogDebug("Running: %d", this->d->m_currentState.load() == State_Running);

    if (this->d->m_currentState.load() != State_Running) //FIXNO29 add .load()
        return;

    AkLogDebug("Active: %d", isActive);

    this->d->m_mutex.lock();

    auto format = formatFromMediaType(this->d->m_mediaType);

    if (this->d->m_directMode) {
        if (isActive && frame && format.isSameFormat(frame.format())) {
            memcpy(this->d->m_currentFrame.data(),
                   frame.constData(),
                   frame.size());
            this->d->m_frameReady = true;
        } else if (!isActive && this->d->m_testFrame) {
            this->d->m_currentFrame =
                    this->d->applyAdjusts(this->d->m_testFrame);
            this->d->m_frameReady = true;
        } else {
            this->d->m_frameReady = false;
        }
    } else {
        auto frameAdjusted =
                this->d->applyAdjusts(isActive? frame: this->d->m_testFrame);

        if (frameAdjusted) {
            this->d->m_currentFrame = frameAdjusted;
            this->d->m_frameReady = true;
        } else {
            this->d->m_frameReady = false;
        }
    }

    this->d->m_mutex.unlock();
}

void AkVCam::Pin::setPicture(const std::string &picture)
{
    AkLogFunction();
    AkLogDebug("Picture: %s", picture.c_str());
    this->d->m_mutex.lock();
    this->d->m_testFrame = loadPicture(picture);
    this->d->m_mutex.unlock();
}

void AkVCam::Pin::setControls(const std::map<std::string, int> &controls)
{
    AkLogFunction();
    std::lock_guard<std::mutex> lock(this->d->m_mutex); // FIXNO18.1

    if (this->d->m_directMode)
        return;

    for (auto &control: controls) {
        AkLogDebug("%s: %d", control.first.c_str(), control.second);

        if (control.first == "hflip")
            this->d->m_videoAdjusts.setHorizontalMirror(control.second > 0);
        else if (control.first == "vflip")
            this->d->m_videoAdjusts.setVerticalMirror(control.second > 0);
        else if (control.first == "swap_rgb")
            this->d->m_videoAdjusts.setSwapRGB(control.second > 0);
        else if (control.first == "aspect_ratio")
            this->d->m_videoConverter.setAspectRatioMode(VideoConverter::AspectRatioMode(control.second));
        else if (control.first == "scaling")
            this->d->m_videoConverter.setScalingMode(VideoConverter::ScalingMode(control.second));
    }
}

bool AkVCam::Pin::horizontalFlip() const
{
    return this->d->m_horizontalFlip;
}

void AkVCam::Pin::setHorizontalFlip(bool flip)
{
    this->d->m_horizontalFlip = flip;
    this->d->m_videoAdjusts.setHorizontalMirror(flip);
}

bool AkVCam::Pin::verticalFlip() const
{
    return this->d->m_verticalFlip;
}

void AkVCam::Pin::setVerticalFlip(bool flip)
{
    this->d->m_verticalFlip = flip;
    this->d->m_videoAdjusts.setVerticalMirror(flip);
}

HRESULT AkVCam::Pin::QueryInterface(const IID &riid, void **ppv)
{
    AkLogFunction();
    AkLogDebug("IID: %s", stringFromClsid(riid).c_str());

    if (!ppv)
        return E_POINTER;

    const struct
    {
        const IID *iid;
        void *ptr;
    } comInterfaceEntryMediaSample[] = {
        COM_INTERFACE(IPin)
        COM_INTERFACE(IAMStreamConfig)
        COM_INTERFACE(IAMLatency)
        COM_INTERFACE(IAMPushSource)
        COM_INTERFACE(IKsPropertySet)//FIXNO28 - added line.
        COM_INTERFACE(IQualityControl)//FIXNO28 - added line.
        COM_INTERFACE2(IUnknown, IPin)
        COM_INTERFACE_NULL
    };

    for (auto map = comInterfaceEntryMediaSample; map->ptr; ++map)
        if (*map->iid == riid) {
            *ppv = map->ptr;
            this->AddRef();

            return S_OK;
        }

    *ppv = nullptr;
    AkLogDebug("Interface not found");

    return E_NOINTERFACE;
}

ULONG AkVCam::Pin::AddRef()
{
    AkLogFunction();

    return ++this->d->m_refCount;
}

ULONG AkVCam::Pin::Release()
{
    AkLogFunction();
    ULONG ref = --this->d->m_refCount;

    if (ref == 0)
        delete this;

    return ref;
}

HRESULT AkVCam::Pin::GetLatency(REFERENCE_TIME *prtLatency)
{
    AkLogFunction();

    if (!prtLatency)
        return E_POINTER;

    std::lock_guard<std::mutex> lock(this->d->m_mutex);
    *prtLatency = 0;

    auto mediaType = this->d->m_mediaType;

    if (IsEqualGUID(mediaType->formattype, FORMAT_VideoInfo)) {
        auto format = reinterpret_cast<VIDEOINFOHEADER *>(mediaType->pbFormat);
        *prtLatency = format->AvgTimePerFrame;
    } else if (IsEqualGUID(mediaType->formattype, FORMAT_VideoInfo2)) {
        auto format = reinterpret_cast<VIDEOINFOHEADER2 *>(mediaType->pbFormat);
        *prtLatency = format->AvgTimePerFrame;
    }

    return S_OK;
}

HRESULT AkVCam::Pin::GetPushSourceFlags(ULONG *pFlags)
{
    AkLogFunction();

    if (!pFlags)
        return E_POINTER;

    *pFlags = this->d->m_pushFlags;

    return S_OK;
}

HRESULT AkVCam::Pin::SetPushSourceFlags(ULONG Flags)
{
    AkLogFunction();

    this->d->m_pushFlags = Flags;

    return S_OK;
}

HRESULT AkVCam::Pin::SetStreamOffset(REFERENCE_TIME rtOffset)
{
    AkLogFunction();

    this->d->m_streamOffset = rtOffset;

    return S_OK;
}

HRESULT AkVCam::Pin::GetStreamOffset(REFERENCE_TIME *prtOffset)
{
    AkLogFunction();

    if (!prtOffset)
        return E_POINTER;

    *prtOffset = this->d->m_streamOffset;

    return S_OK;
}

HRESULT AkVCam::Pin::GetMaxStreamOffset(REFERENCE_TIME *prtMaxOffset)
{
    AkLogFunction();

    if (!prtMaxOffset)
        return E_POINTER;

    *prtMaxOffset = this->d->m_maxStreamOffset;

    return S_OK;
}

HRESULT AkVCam::Pin::SetMaxStreamOffset(REFERENCE_TIME rtMaxOffset)
{
    AkLogFunction();

    this->d->m_maxStreamOffset = rtMaxOffset;

    return S_OK;
}

//FIXNO28 - entire set of functions (set, get, query supported, notify, setsink) starts
HRESULT AkVCam::Pin::Set(REFGUID guidPropSet, //FIXNO29 changed the function to add some lines
                         DWORD dwPropID,
                         LPVOID pInstanceData,
                         DWORD cbInstanceData,
                         LPVOID pPropData,
                         DWORD cbPropData)
{
    UNUSED(pInstanceData);
    UNUSED(cbInstanceData);
    UNUSED(pPropData);
    UNUSED(cbPropData);
    AkLogFunction();

    if (!IsEqualGUID(guidPropSet, AMPROPSETID_Pin))
        return E_PROP_SET_UNSUPPORTED;

    if (dwPropID != AMPROPERTY_PIN_CATEGORY)
        return E_PROP_ID_UNSUPPORTED;

    // AMPROPERTY_PIN_CATEGORY is read-only.
    return E_PROP_SET_UNSUPPORTED;
}

HRESULT AkVCam::Pin::Get(REFGUID guidPropSet,
                         DWORD dwPropID,
                         LPVOID pInstanceData,
                         DWORD cbInstanceData,
                         LPVOID pPropData,
                         DWORD cbPropData,
                         DWORD *pcbReturned)
{
    UNUSED(pInstanceData);
    UNUSED(cbInstanceData);
    AkLogFunction();

    if (!IsEqualGUID(guidPropSet, AMPROPSETID_Pin))
        return E_PROP_SET_UNSUPPORTED;

    if (dwPropID != AMPROPERTY_PIN_CATEGORY)
        return E_PROP_ID_UNSUPPORTED;

    if (!pPropData && !pcbReturned)
        return E_POINTER;

    if (pcbReturned)
        *pcbReturned = sizeof(GUID);

    if (!pPropData)
        return S_OK;

    if (cbPropData < sizeof(GUID))
        return E_UNEXPECTED;

    *reinterpret_cast<GUID *>(pPropData) = PIN_CATEGORY_CAPTURE;

    return S_OK;
}

HRESULT AkVCam::Pin::QuerySupported(REFGUID guidPropSet, //FIXNO29, changed order and added some lines.
                                    DWORD dwPropID,
                                    DWORD *pTypeSupport)
{
    AkLogFunction();

    if (!pTypeSupport)
        return E_POINTER;

    if (!IsEqualGUID(guidPropSet, AMPROPSETID_Pin))
        return E_PROP_SET_UNSUPPORTED;

    if (dwPropID != AMPROPERTY_PIN_CATEGORY)
        return E_PROP_ID_UNSUPPORTED;

    *pTypeSupport = KSPROPERTY_SUPPORT_GET;

    return S_OK;
}

HRESULT AkVCam::Pin::Notify(IBaseFilter *pSelf, Quality q)
{
    UNUSED(pSelf);
    UNUSED(q);
    AkLogFunction();

    return S_OK;
}

HRESULT AkVCam::Pin::SetSink(IQualityControl *piqc)
{
    UNUSED(piqc);
    AkLogFunction();

    return E_NOTIMPL;
}
//FIXNO28 - entire function Ends

HRESULT AkVCam::Pin::SetFormat(AM_MEDIA_TYPE *pmt) //FIXNO29 changed the entire function
{
    AkLogFunction();

    if (!pmt)
        return E_POINTER;

    AkLogDebug("Media type: %s", stringFromMediaType(pmt).c_str());

    if (this->d->m_currentState.load() != State_Stopped) {
        AkLogError("The filter graph must be stopped");

        return VFW_E_NOT_STOPPED;
    }

    if (!this->d->m_mediaTypes->contains(pmt)) {
        AkLogError("Media type not supported");

        return VFW_E_INVALIDMEDIATYPE;
    }

    auto newMediaType = createMediaType(pmt);

    if (!newMediaType)
        return E_OUTOFMEMORY;

    AM_MEDIA_TYPE *oldMediaType = nullptr;
    IPin *connectedPin = nullptr;

    {
        std::lock_guard<std::mutex> lock(this->d->m_mutex);

        oldMediaType = this->d->m_mediaType;
        this->d->m_mediaType = newMediaType;

        if (this->d->m_connectedPin) {
            connectedPin = this->d->m_connectedPin;
            connectedPin->AddRef();
        }
    }

    if (!connectedPin) {
        deleteMediaType(&oldMediaType);

        return S_OK;
    }

    auto hr = connectedPin->Disconnect();

    if (FAILED(hr) && hr != VFW_E_NOT_CONNECTED)
        goto rollback_before_disconnect;

    hr = this->Disconnect();

    if (FAILED(hr) && hr != VFW_E_NOT_CONNECTED)
        goto rollback_before_disconnect;

    hr = this->Connect(connectedPin, pmt);

    if (SUCCEEDED(hr)) {
        connectedPin->Release();
        deleteMediaType(&oldMediaType);

        return S_OK;
    }

    {
        std::lock_guard<std::mutex> lock(this->d->m_mutex);

        deleteMediaType(&this->d->m_mediaType);
        this->d->m_mediaType = oldMediaType;
        oldMediaType = nullptr;
    }

    // Best-effort restoration of the prior connection.
    this->Connect(connectedPin, this->d->m_mediaType);
    connectedPin->Release();

    return hr;

rollback_before_disconnect:
    {
        std::lock_guard<std::mutex> lock(this->d->m_mutex);

        deleteMediaType(&this->d->m_mediaType);
        this->d->m_mediaType = oldMediaType;
        oldMediaType = nullptr;
    }

    connectedPin->Release();

    return hr;
}

HRESULT AkVCam::Pin::GetFormat(AM_MEDIA_TYPE **pmt)
{
    AkLogFunction();

    if (!pmt)
        return E_POINTER;

    *pmt = nullptr;
    std::lock_guard<std::mutex> lock(this->d->m_mutex);

    if (!this->d->m_mediaType) {
        AkLogError("Failed reading the media type");

        return E_FAIL;
    }

    *pmt = createMediaType(this->d->m_mediaType);
    AkLogDebug("MediaType: %s", stringFromMediaType(*pmt).c_str());

    return S_OK;
}

HRESULT AkVCam::Pin::GetNumberOfCapabilities(int *piCount, int *piSize)
{
    AkLogFunction();

    if (!piCount || !piSize)
        return E_POINTER;

    *piCount = static_cast<int>(this->d->m_mediaTypes->size());
    *piSize = static_cast<int>(sizeof(VIDEO_STREAM_CONFIG_CAPS));

    return S_OK;
}

HRESULT AkVCam::Pin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE **pmt, BYTE *pSCC)
{
    AkLogFunction();

    if (!pmt || !pSCC)
        return E_POINTER;

    *pmt = nullptr;
    auto configCaps = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS *>(pSCC);
    memset(configCaps, 0, sizeof(VIDEO_STREAM_CONFIG_CAPS));

    if (iIndex < 0)
        return E_INVALIDARG;

    if (!this->d->m_mediaTypes->mediaType(iIndex, pmt)) {
        AkLogWarning("No media type found for index %d", iIndex);

        return S_FALSE;
    }

    if (IsEqualGUID((*pmt)->formattype, FORMAT_VideoInfo)) {
        auto format = reinterpret_cast<VIDEOINFOHEADER *>((*pmt)->pbFormat);
        configCaps->guid = (*pmt)->formattype;
        configCaps->VideoStandard = AnalogVideo_None;
        configCaps->InputSize.cx = format->bmiHeader.biWidth;
        configCaps->InputSize.cy = format->bmiHeader.biHeight;
        configCaps->MinCroppingSize.cx = format->bmiHeader.biWidth;
        configCaps->MinCroppingSize.cy = format->bmiHeader.biHeight;
        configCaps->MaxCroppingSize.cx = format->bmiHeader.biWidth;
        configCaps->MaxCroppingSize.cy = format->bmiHeader.biHeight;
        configCaps->CropGranularityX = 1;
        configCaps->CropGranularityY = 1;
        configCaps->CropAlignX = 0;
        configCaps->CropAlignY = 0;
        configCaps->MinOutputSize.cx = format->bmiHeader.biWidth;
        configCaps->MinOutputSize.cy = format->bmiHeader.biHeight;
        configCaps->MaxOutputSize.cx = format->bmiHeader.biWidth;
        configCaps->MaxOutputSize.cy = format->bmiHeader.biHeight;
        configCaps->OutputGranularityX = 1;
        configCaps->OutputGranularityY = 1;
        configCaps->StretchTapsX = 1;
        configCaps->StretchTapsY = 1;
        configCaps->ShrinkTapsX = 1;
        configCaps->ShrinkTapsY = 1;
        configCaps->MinFrameInterval = format->AvgTimePerFrame;
        configCaps->MaxFrameInterval = format->AvgTimePerFrame;
        configCaps->MinBitsPerSecond = LONG(format->dwBitRate);
        configCaps->MaxBitsPerSecond = LONG(format->dwBitRate);
    } else if (IsEqualGUID((*pmt)->formattype, FORMAT_VideoInfo2)) {
        auto format = reinterpret_cast<VIDEOINFOHEADER2 *>((*pmt)->pbFormat);
        configCaps->guid = (*pmt)->formattype;
        configCaps->VideoStandard = AnalogVideo_None;
        configCaps->InputSize.cx = format->bmiHeader.biWidth;
        configCaps->InputSize.cy = format->bmiHeader.biHeight;
        configCaps->MinCroppingSize.cx = format->bmiHeader.biWidth;
        configCaps->MinCroppingSize.cy = format->bmiHeader.biHeight;
        configCaps->MaxCroppingSize.cx = format->bmiHeader.biWidth;
        configCaps->MaxCroppingSize.cy = format->bmiHeader.biHeight;
        configCaps->CropGranularityX = 1;
        configCaps->CropGranularityY = 1;
        configCaps->CropAlignX = 0;
        configCaps->CropAlignY = 0;
        configCaps->MinOutputSize.cx = format->bmiHeader.biWidth;
        configCaps->MinOutputSize.cy = format->bmiHeader.biHeight;
        configCaps->MaxOutputSize.cx = format->bmiHeader.biWidth;
        configCaps->MaxOutputSize.cy = format->bmiHeader.biHeight;
        configCaps->OutputGranularityX = 1;
        configCaps->OutputGranularityY = 1;
        configCaps->StretchTapsX = 1;
        configCaps->StretchTapsY = 1;
        configCaps->ShrinkTapsX = 1;
        configCaps->ShrinkTapsY = 1;
        configCaps->MinFrameInterval = format->AvgTimePerFrame;
        configCaps->MaxFrameInterval = format->AvgTimePerFrame;
        configCaps->MinBitsPerSecond = LONG(format->dwBitRate);
        configCaps->MaxBitsPerSecond = LONG(format->dwBitRate);
    }

    AkLogInfo("Media Type: %s", stringFromMediaType(*pmt).c_str());

    return S_OK;
}

HRESULT AkVCam::Pin::Connect(IPin *pReceivePin, const AM_MEDIA_TYPE *pmt)
{
    AkLogFunction();
    AkLogDebug("Receive pin: %p", pReceivePin);
    AkLogDebug("Media type: %s", stringFromMediaType(pmt).c_str());

    if (!pReceivePin) {
        AkLogError("Invalid pin pointer");

        return E_POINTER;
    }

    std::lock_guard<std::mutex> lock(this->d->m_mutex);

    if (this->d->m_connectedPin) {
        AkLogError("The pin is aready connected");

        return VFW_E_ALREADY_CONNECTED;
    }

    if (this->d->m_currentState.load() != State_Stopped) { //FIXNO29 add .load()
        AkLogError("The filter graph is not stopped");

        return VFW_E_NOT_STOPPED;
    }

    PIN_DIRECTION direction = PINDIR_OUTPUT;

    // Only connect to an input pin.
    if (FAILED(pReceivePin->QueryDirection(&direction))
        || direction != PINDIR_INPUT) {
        AkLogError("The pin is not an input pin");

        return VFW_E_NO_TRANSPORT;
    }

    /* When the Filter Graph Manager calls Connect, the output pin must request
     * a IMemInputPin and get a IMemAllocator interface to the input pin.
     */
    IMemInputPin *memInputPin = nullptr;

    if (FAILED(pReceivePin->QueryInterface(IID_IMemInputPin,
                                           reinterpret_cast<void **>(&memInputPin)))) {
        AkLogError("Can't get IMemInputPin interface");

        return VFW_E_NO_TRANSPORT;
    }

    AM_MEDIA_TYPE *mediaType = nullptr;

    if (pmt) {
        AkLogDebug("Testing requested media type: %s", stringFromMediaType(pmt).c_str());

        // Try setting requested media type.
        if (!this->d->m_mediaTypes->contains(pmt)) {
            AkLogError("Media type not supported: %s", stringFromMediaType(pmt).c_str());

            memInputPin->Release(); //FIXNO29 added this line
            return VFW_E_TYPE_NOT_ACCEPTED;
        }

        mediaType = createMediaType(pmt);
    } else {
        AkLogDebug("Testing media type: %s", stringFromMediaType(this->d->m_mediaType).c_str());

        // Test currently set media type.
        if (pReceivePin->QueryAccept(this->d->m_mediaType) == S_OK)
            mediaType = createMediaType(this->d->m_mediaType);

        if (!mediaType) {
            AkLogDebug("Currently set media type was not accepted. Trying with the mediatypes supported by receiver.");

            // Test media types supported by the input pin.
            AM_MEDIA_TYPE *mt = nullptr;
            IEnumMediaTypes *mediaTypes = nullptr;

            if (SUCCEEDED(pReceivePin->EnumMediaTypes(&mediaTypes))) {
                mediaTypes->Reset();

                while (mediaTypes->Next(1, &mt, nullptr) == S_OK) {
                    AkLogDebug("Testing media type: %s", stringFromMediaType(mt).c_str());

                    // If the mediatype match our suported mediatypes...
                    if (this->QueryAccept(mt) == S_OK) {
                        AkLogDebug("Receiver media type accepted: %s", stringFromMediaType(mt).c_str());
                        mediaType = mt;

                        break;
                    }

                    deleteMediaType(&mt);
                }

                mediaTypes->Release();
            }
        }

        if (!mediaType) {
            AkLogDebug("Receiver pin media types not supported by us. Ask if the receiver supports one of us.");

            /* If none of the input media types was suitable for us, ask to
             * input pin if it at least supports one of us.
             */
            for (size_t i = 0; i < this->d->m_mediaTypes->size(); ++i) {
                AM_MEDIA_TYPE *mt = nullptr;
                this->d->m_mediaTypes->mediaType(i, &mt);

                if (pReceivePin->QueryAccept(mt) == S_OK) {
                    AkLogDebug("Receiver accepted our media type: %s", stringFromMediaType(mt).c_str());
                    mediaType = mt;

                    break;
                }

                deleteMediaType(&mt);
            }
        }
    }

    if (!mediaType) {
        AkLogError("No acceptable media type was found");

        memInputPin->Release(); //FIXNO29 added this line
        return VFW_E_NO_ACCEPTABLE_TYPES;
    }

    AkLogInfo("Setting media type: %s", stringFromMediaType(mediaType).c_str());
    auto result = pReceivePin->ReceiveConnection(this, mediaType);

    if (FAILED(result)) {
        AkLogError("Failed setting the media type: 0x%x", result);
        deleteMediaType(&mediaType);
        memInputPin->Release(); //FIXNO29 added this line
        return result;
    }

    AkLogInfo("Connection accepted by input pin");

    // Define memory allocator requirements.
    ALLOCATOR_PROPERTIES allocatorRequirements;
    memset(&allocatorRequirements, 0, sizeof(ALLOCATOR_PROPERTIES));
    memInputPin->GetAllocatorRequirements(&allocatorRequirements);
    auto videoFormat = formatFromMediaType(mediaType);

    if (allocatorRequirements.cBuffers < 1)
        allocatorRequirements.cBuffers = 1;

    allocatorRequirements.cbBuffer = LONG(videoFormat.dataSize());

    if (allocatorRequirements.cbAlign < 1)
        allocatorRequirements.cbAlign = 1;

    // Get a memory allocator.
    IMemAllocator *memAllocator = nullptr;

    // if it fail use our own.
    if (FAILED(memInputPin->GetAllocator(&memAllocator)))
        memAllocator = new MemAllocator;

    ALLOCATOR_PROPERTIES actualRequirements;
    memset(&actualRequirements, 0, sizeof(ALLOCATOR_PROPERTIES));

    if (FAILED(memAllocator->SetProperties(&allocatorRequirements,
                                           &actualRequirements))) {
        AkLogError("Failed setting the allocator properties");
        memAllocator->Release();
        memInputPin->Release();
        deleteMediaType(&mediaType);

        return VFW_E_NO_TRANSPORT;
    }

    if (FAILED(memInputPin->NotifyAllocator(memAllocator, S_OK))) {
        AkLogError("Failed to notify the allocator");
        memAllocator->Release();
        memInputPin->Release();
        deleteMediaType(&mediaType);

        return VFW_E_NO_TRANSPORT;
    }

    if (this->d->m_memInputPin)
        this->d->m_memInputPin->Release();

    this->d->m_memInputPin = memInputPin;

    if (this->d->m_memAllocator)
        this->d->m_memAllocator->Release();

    this->d->m_memAllocator = memAllocator;
    deleteMediaType(&this->d->m_mediaType);
    this->d->m_mediaType = mediaType;

    if (this->d->m_connectedPin) {
        this->d->m_connectedPin->Release();
        this->d->m_connectedPin = nullptr;
    }

    if (pReceivePin) {
        this->d->m_connectedPin = pReceivePin;
        this->d->m_connectedPin->AddRef();
    }

    AkLogInfo("Connected to %p", pReceivePin);

    return S_OK;
}

HRESULT AkVCam::Pin::ReceiveConnection(IPin *pConnector,
                                       const AM_MEDIA_TYPE *pmt)
{
    UNUSED(pConnector);
    UNUSED(pmt);
    AkLogFunction();

    return VFW_E_TYPE_NOT_ACCEPTED;
}

HRESULT AkVCam::Pin::Disconnect()
{
    AkLogFunction();

    std::lock_guard<std::mutex> lock(this->d->m_mutex);

    if (this->d->m_currentState.load() != State_Stopped) //FIXNO29 add .load()
        return VFW_E_NOT_STOPPED;

    if (this->d->m_connectedPin) {
        this->d->m_connectedPin->Release();
        this->d->m_connectedPin = nullptr;
    }

    if (this->d->m_memInputPin) {
        this->d->m_memInputPin->Release();
        this->d->m_memInputPin = nullptr;
    }

    if (this->d->m_memAllocator) {
        this->d->m_memAllocator->Release();
        this->d->m_memAllocator = nullptr;
    }

    return S_OK;
}

HRESULT AkVCam::Pin::ConnectedTo(IPin **pPin)
{
    AkLogFunction();

    if (!pPin)
        return E_POINTER;

    std::lock_guard<std::mutex> lock(this->d->m_mutex);

    if (!this->d->m_connectedPin)
        return VFW_E_NOT_CONNECTED;

    *pPin = this->d->m_connectedPin;
    (*pPin)->AddRef();

    return S_OK;
}

HRESULT AkVCam::Pin::ConnectionMediaType(AM_MEDIA_TYPE *pmt)
{
    AkLogFunction();

    if (!pmt)
        return E_POINTER;

    memset(pmt, 0, sizeof(AM_MEDIA_TYPE));
    std::lock_guard<std::mutex> lock(this->d->m_mutex);

    if (!this->d->m_connectedPin)
        return VFW_E_NOT_CONNECTED;

    copyMediaType(pmt, this->d->m_mediaType);
    AkLogInfo("Media Type: %s", stringFromMediaType(this->d->m_mediaType).c_str());

    return S_OK;
}

HRESULT AkVCam::Pin::QueryPinInfo(PIN_INFO *pInfo)
{
    AkLogFunction();

    if (!pInfo)
        return E_POINTER;

    pInfo->pFilter = this->d->m_baseFilter;

    if (pInfo->pFilter)
        pInfo->pFilter->AddRef();

    pInfo->dir = PINDIR_OUTPUT;
    memset(pInfo->achName, 0, MAX_PIN_NAME * sizeof(WCHAR));

    if (!this->d->m_pinName.empty()) {
        auto pinName = wstrFromString(this->d->m_pinName);
        size_t len = wcsnlen(pinName, MAX_PIN_NAME - 1);//FIXNO23.2 Starts (add this line)
        memcpy(pInfo->achName,
               pinName,
               len * sizeof(WCHAR)); //FIXNO23.2 Change this line
        pInfo->achName[len] = L'\0'; //FIXNO23.2 Ends ------ (add this line)
        CoTaskMemFree(pinName);
    }

    return S_OK;
}

HRESULT AkVCam::Pin::QueryDirection(PIN_DIRECTION *pPinDir)
{
    AkLogFunction();

    if (!pPinDir)
        return E_POINTER;

    *pPinDir = PINDIR_OUTPUT;

    return S_OK;
}

HRESULT AkVCam::Pin::QueryId(LPWSTR *Id)
{
    AkLogFunction();

    if (!Id)
        return E_POINTER;

    *Id = wstrFromString(this->d->m_pinName);

    if (!*Id)
        return E_OUTOFMEMORY;

    return S_OK;
}

HRESULT AkVCam::Pin::QueryAccept(const AM_MEDIA_TYPE *pmt)
{
    AkLogFunction();

    if (!pmt)
        return E_POINTER;

    AkLogDebug("Accept? %s", stringFromMediaType(pmt).c_str());

    if (!this->d->m_mediaTypes->contains(pmt)) {
        AkLogInfo("NO");

        return S_FALSE;
    }

    AkLogInfo("YES");

    return S_OK;
}

HRESULT AkVCam::Pin::EnumMediaTypes(IEnumMediaTypes **ppEnum)
{
    AkLogFunction();

    if (!ppEnum)
        return E_POINTER;

    *ppEnum = new AkVCam::EnumMediaTypes(*this->d->m_mediaTypes);

    return S_OK;
}

HRESULT AkVCam::Pin::QueryInternalConnections(IPin **apPin, ULONG *nPin)
{
    AkLogFunction();

    if (!apPin || !nPin)
        return E_POINTER;

    *apPin = nullptr;
    *nPin = 0;
    std::lock_guard<std::mutex> lock(this->d->m_mutex);

    if (!this->d->m_connectedPin)
        return S_OK;

    apPin[0] = this->d->m_connectedPin;
    this->d->m_connectedPin->AddRef();
    *nPin = 1;

    return S_OK;
}

HRESULT AkVCam::Pin::EndOfStream()
{
    AkLogFunction();

    return S_OK;
}

HRESULT AkVCam::Pin::BeginFlush()
{
    AkLogFunction();

    return S_OK;
}

HRESULT AkVCam::Pin::EndFlush()
{
    AkLogFunction();

    return S_OK;
}

HRESULT AkVCam::Pin::NewSegment(REFERENCE_TIME tStart,
                                REFERENCE_TIME tStop,
                                double dRate)
{
    AkLogFunction();
    this->d->m_start = tStart;
    this->d->m_stop = tStop;
    this->d->m_rate = dRate;

    return S_OK;
}

void AkVCam::PinPrivate::sendFrame(void *userData) //FIXNO20 changed the whole function
{
    AkLogFunction();

    auto self = reinterpret_cast<PinPrivate *>(userData);

    if (self->m_currentState.load() == State_Stopped)
        return;

    if (!self->m_memAllocator || !self->m_memInputPin)
        return;

    IMediaSample *sample = nullptr;

    if (FAILED(self->m_memAllocator->GetBuffer(&sample,
                                               nullptr,
                                               nullptr,
                                               0))
        || !sample) {
        AkLogError("Failed getting a sample");

        return;
    }

    BYTE *pData = nullptr;
    LONG size = sample->GetSize();

    if (size < 1 || FAILED(sample->GetPointer(&pData)) || !pData) {
        AkLogError("Failed getting the sample data pointer");
        sample->Release();

        return;
    }

    VideoFormat outputFormat;

    {
        std::lock_guard<std::mutex> lock(self->m_mutex);

        outputFormat = formatFromMediaType(self->m_mediaType);

        VideoFrame fallback;
        const VideoFrame *frameToRender = &self->m_currentFrame;

        if (!self->m_frameReady
            || !self->m_currentFrame
            || !self->m_currentFrame.format().isSameFormat(outputFormat)
            || self->m_currentFrame.size() != size) {
            fallback = self->fallbackFrame();
            frameToRender = &fallback;
        }

        if (!*frameToRender || frameToRender->size() != size) {
            AkLogError("Frame does not match the negotiated sample size");
            sample->Release();

            return;
        }

        // No sample byte is ever left from a previous frame.
        memset(pData, 0, size);

        if (self->m_isRgb) { //FIX29 
            auto dstLine = pData;
            auto bytesUsed = frameToRender->bytesUsed(0);
            auto lineSize = frameToRender->lineSize(0);
            auto height = frameToRender->format().height();

            // CRITICAL FIX 24 RESTORED: DirectShow RGB requires strict 4-byte (DWORD) alignment
            size_t dstStride = (bytesUsed + 3) & ~3;

            for (int y = 0; y < height; ++y) {
                // Buffer overflow protection
                if (dstLine + dstStride > pData + size) {
                    sample->Release();
                    return;
                }

                auto srcLine =
                        frameToRender->constPlane(0)
                        + size_t(height - y - 1) * lineSize;

                memcpy(dstLine, srcLine, bytesUsed);
                dstLine += dstStride; // Use the DirectShow stride, NOT the internal 32-byte SIMD stride
            }
        } else {
            auto dstLine = pData;

            for (size_t plane = 0; plane < frameToRender->planes(); ++plane) {
                auto bytesUsed = frameToRender->bytesUsed(int(plane));
                auto lineSize = frameToRender->lineSize(int(plane));
                auto planeHeight =
                        frameToRender->format().height()
                        >> frameToRender->heightDiv(int(plane));

                // CRITICAL FIX 24 RESTORED: 1-plane (YUY2) is DWORD aligned. 2-plane (NV12) is tightly packed.
                size_t dstStride = (frameToRender->planes() == 1) ? ((bytesUsed + 3) & ~3) : bytesUsed;

                for (int y = 0; y < planeHeight; ++y) {
                    // Buffer overflow protection
                    if (dstLine + dstStride > pData + size) {
                        sample->Release();
                        return;
                    }

                    auto srcLine =
                            frameToRender->constPlane(int(plane))
                            + size_t(y) * lineSize;

                    memcpy(dstLine, srcLine, bytesUsed);
                    dstLine += dstStride; // Use the DirectShow stride, NOT the internal 32-byte SIMD stride
                }
            }
        }
    }

    auto fps = outputFormat.fps();

    if (fps.num() <= 0 || fps.den() <= 0) {
        sample->Release();

        return;
    }

    auto duration =
            REFERENCE_TIME((TIME_BASE * fps.den()) / fps.num());

    auto timeStart = self->m_pts < 0? 0: self->m_pts;
    auto timeEnd = timeStart + duration;

    sample->SetMediaType(self->m_mediaType);
    sample->SetTime(&timeStart, &timeEnd);
    sample->SetMediaTime(&timeStart, &timeEnd);
    sample->SetActualDataLength(size);
    sample->SetDiscontinuity(self->m_firstFrame);
    sample->SetSyncPoint(true);
    sample->SetPreroll(false);

    self->m_firstFrame = false;
    self->m_pts = timeEnd;

    auto result = self->m_memInputPin->Receive(sample);

    if (result == S_FALSE)
        result = S_OK;

    sample->Release();
    AkLogDebug("Frame sent");
}

AkVCam::VideoFrame AkVCam::PinPrivate::applyAdjusts(const VideoFrame &frame)
{
    auto format = formatFromMediaType(this->m_mediaType);
    VideoFrame newFrame;

    this->m_videoConverter.begin();

    if (this->m_directMode) {
        newFrame = this->m_videoConverter.convert(frame);
    } else {
        int width = format.width();
        int height = format.height();

        if (width * height > frame.format().width() * frame.format().height()) {
            newFrame = this->m_videoAdjusts.adjust(frame);
            newFrame = this->m_videoConverter.convert(newFrame);
        } else {
            newFrame = this->m_videoConverter.convert(frame);
            newFrame = this->m_videoAdjusts.adjust(newFrame);
        }
    }

    this->m_videoConverter.end();

    return newFrame;
}

void AkVCam::PinPrivate::propertyChanged(void *userData,
                                         LONG property,
                                         LONG value,
                                         LONG flags)
{
    AkLogFunction();
    UNUSED(flags);
    auto self = reinterpret_cast<PinPrivate *>(userData);
    std::lock_guard<std::mutex> lock(self->m_mutex); // FIXNO18.2

    switch (property) {
    case VideoProcAmp_Brightness:
        self->m_brightness = value;
        self->m_videoAdjusts.setLuminance(self->m_brightness);

        break;

    case VideoProcAmp_Contrast:
        self->m_contrast = value;
        self->m_videoAdjusts.setContrast(self->m_contrast);

        break;

    case VideoProcAmp_Saturation:
        self->m_saturation = value;
        self->m_videoAdjusts.setSaturation(self->m_saturation);

        break;

    case VideoProcAmp_Gamma:
        self->m_gamma = value;
        self->m_videoAdjusts.setGamma(self->m_gamma);

        break;

    case VideoProcAmp_Hue:
        self->m_hue = value;
        self->m_videoAdjusts.setHue(self->m_hue);

        break;

    case VideoProcAmp_ColorEnable:
        self->m_colorEnable = value;
        self->m_videoAdjusts.setGrayScaled(!self->m_colorEnable);

        break;

    default:
        break;
    }
}

AkVCam::VideoFrame AkVCam::PinPrivate::fallbackFrame()
{
    auto format = formatFromMediaType(this->m_mediaType);
    VideoFrame frame(format);

    if (!frame || !frame.data() || frame.size() < 1)
        return {};

    // Initialize every byte, including internal stride padding.
    memset(frame.data(), 0, frame.size());

    switch (format.format()) {
    case PixelFormat_yuyv422:
        for (int y = 0; y < format.height(); ++y) {
            auto line = frame.plane(0) + size_t(y) * frame.lineSize(0);

            for (size_t x = 0; x + 3 < frame.bytesUsed(0); x += 4) {
                line[x]     = 16;
                line[x + 1] = 128;
                line[x + 2] = 16;
                line[x + 3] = 128;
            }
        }
        break;

    case PixelFormat_uyvy422:
        for (int y = 0; y < format.height(); ++y) {
            auto line = frame.plane(0) + size_t(y) * frame.lineSize(0);

            for (size_t x = 0; x + 3 < frame.bytesUsed(0); x += 4) {
                line[x]     = 128;
                line[x + 1] = 16;
                line[x + 2] = 128;
                line[x + 3] = 16;
            }
        }
        break;

    case PixelFormat_nv12:
    case PixelFormat_nv21:
        for (int y = 0; y < format.height(); ++y) {
            memset(frame.plane(0) + size_t(y) * frame.lineSize(0),
                   16,
                   frame.bytesUsed(0));
        }

        for (int y = 0;
             y < (format.height() >> frame.heightDiv(1));
             ++y) {
            memset(frame.plane(1) + size_t(y) * frame.lineSize(1),
                   128,
                   frame.bytesUsed(1));
        }
        break;
    default:
        // RGB/BGR zero is neutral black.
        break;
    }
    return frame;
}
