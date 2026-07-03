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

#include <cstring>

#include "message.h"
#include "servicemsg.h"
#include "videoformat.h"
#include "videoframe.h"

namespace AkVCam
{
    class MessagePrivate
    {
        public:
            int m_id {0};
            uint64_t m_queryId {0};
            std::vector<char> m_data;

            static uint64_t queryId()
            {
                static uint64_t akvcamMessagePrivateQueryId = 0;

                return akvcamMessagePrivateQueryId++;
            }
    };
}

AkVCam::Message::Message()
{
    this->d = new MessagePrivate;
    this->d->m_queryId = MessagePrivate::queryId();
}

AkVCam::Message::Message(int id)
{
    this->d = new MessagePrivate;
    this->d->m_id = id;
    this->d->m_queryId = MessagePrivate::queryId();
}

AkVCam::Message::Message(int id, uint64_t queryId)
{
    this->d = new MessagePrivate;
    this->d->m_id = id;
    this->d->m_queryId = queryId;
}

AkVCam::Message::Message(int id, uint64_t queryId, const std::vector<char> &data)
{
    this->d = new MessagePrivate;
    this->d->m_id = id;
    this->d->m_queryId = queryId;
    this->d->m_data = data;
}

AkVCam::Message::Message(int id, const std::vector<char> &data)
{
    this->d = new MessagePrivate;
    this->d->m_id = id;
    this->d->m_data = data;
}

AkVCam::Message::Message(const Message &other)
{
    this->d = new MessagePrivate;
    this->d->m_id = other.d->m_id;
    this->d->m_queryId = other.d->m_queryId;
    this->d->m_data = other.d->m_data;
}

AkVCam::Message::~Message()
{
    delete this->d;
}

AkVCam::Message &AkVCam::Message::operator =(const Message &other)
{
    if (this != &other) {
        this->d->m_id = other.d->m_id;
        this->d->m_queryId = other.d->m_queryId;
        this->d->m_data = other.d->m_data;
    }

    return *this;
}

bool AkVCam::Message::operator ==(const Message &other) const
{
    return this->d->m_id == other.d->m_id
            && this->d->m_queryId == other.d->m_queryId
            && this->d->m_data == other.d->m_data;
}

int AkVCam::Message::id() const
{
    return this->d->m_id;
}

uint64_t AkVCam::Message::queryId() const
{
    return this->d->m_queryId;
}

const std::vector<char> &AkVCam::Message::data() const
{
    return this->d->m_data;
}

namespace AkVCam
{
    class MsgCommonsPrivate
    {
        public:
            uint64_t m_queryId {0};
    };
}

AkVCam::MsgCommons::MsgCommons()
{
    this->d = new MsgCommonsPrivate;
    this->d->m_queryId = MessagePrivate::queryId();
}

AkVCam::MsgCommons::MsgCommons(uint64_t queryId)
{
    this->d = new MsgCommonsPrivate;
    this->d->m_queryId = queryId;
}

AkVCam::MsgCommons::~MsgCommons()
{
    delete d;
}

uint64_t AkVCam::MsgCommons::queryId() const
{
    return this->d->m_queryId;
}

void AkVCam::MsgCommons::setQueryId(uint64_t queryId)
{
    this->d->m_queryId = queryId;
}

namespace AkVCam
{
    class MsgStatusPrivate
    {
        public:
            int m_status {0};
    };
}

AkVCam::MsgStatus::MsgStatus():
    MsgCommons()
{
    this->d = new MsgStatusPrivate;
}

AkVCam::MsgStatus::MsgStatus(int status):
    MsgCommons()
{
    this->d = new MsgStatusPrivate;
    this->d->m_status = status;
}

AkVCam::MsgStatus::MsgStatus(int status, uint64_t queryId):
    MsgCommons(queryId)
{
    this->d = new MsgStatusPrivate;
    this->d->m_status = status;
}

AkVCam::MsgStatus::MsgStatus(const MsgStatus &other):
    MsgCommons(other.queryId())
{
    this->d = new MsgStatusPrivate;
    this->d->m_status = other.d->m_status;
}

AkVCam::MsgStatus::MsgStatus(const Message &message): //FIXNOpost2 - Changed this MsgStatus.
    MsgCommons(message.queryId())
{
    this->d = new MsgStatusPrivate;
    if (message.id() != AKVCAM_SERVICE_MSG_STATUS) return;

    if (message.data().size() != sizeof(this->d->m_status)) return;
    memcpy(&this->d->m_status, message.data().data(), sizeof(this->d->m_status));
}

AkVCam::MsgStatus::~MsgStatus()
{
    delete this->d;
}

AkVCam::MsgStatus &AkVCam::MsgStatus::operator =(const MsgStatus &other)
{
    if (this != &other) {
        this->d->m_status = other.d->m_status;
        this->setQueryId(other.queryId());
    }

    return *this;
}

bool AkVCam::MsgStatus::operator ==(const MsgStatus &other) const
{
    return this->d->m_status == other.d->m_status
           && this->queryId() == other.queryId();
}

AkVCam::Message AkVCam::MsgStatus::toMessage() const
{
    size_t totalSize = sizeof(this->d->m_status);
    std::vector<char> data(totalSize);
    memcpy(data.data(), &this->d->m_status, sizeof(this->d->m_status));

    return {AKVCAM_SERVICE_MSG_STATUS, this->queryId(), data};
}

int AkVCam::MsgStatus::status() const
{
    return this->d->m_status;
}

namespace AkVCam
{
    class MsgClientsPrivate
    {
        public:
            MsgClients::ClientType m_clientType {MsgClients::ClientType_Any};
            std::vector<uint64_t> m_clients;
    };
}

AkVCam::MsgClients::MsgClients():
    MsgCommons()
{
    this->d = new MsgClientsPrivate;
}

AkVCam::MsgClients::MsgClients(ClientType clientType):
    MsgCommons()
{
    this->d = new MsgClientsPrivate;
    this->d->m_clientType = clientType;
}
//FIXNOCodex1 - constructors used by the Service.
AkVCam::MsgClients::MsgClients(ClientType clientType,
                               const std::vector<uint64_t> &clients):
    MsgCommons()
{
    this->d = new MsgClientsPrivate;
    this->d->m_clientType = clientType;
    this->d->m_clients = clients;
}

AkVCam::MsgClients::MsgClients(ClientType clientType,
                               const std::vector<uint64_t> &clients,
                               uint64_t queryId):
    MsgCommons(queryId)
{
    this->d = new MsgClientsPrivate;
    this->d->m_clientType = clientType;
    this->d->m_clients = clients;
}
//FIXNOCodex1 Ends
AkVCam::MsgClients::MsgClients(const Message &message): //FIXNOpost2 Starts
    MsgCommons(message.queryId())
{
    this->d = new MsgClientsPrivate;
    if (message.id() != AKVCAM_SERVICE_MSG_CLIENTS) return;

    size_t offset = 0;
    size_t bufferSize = message.data().size();
    const char* buffer = message.data().data();

    if (bufferSize - offset < sizeof(this->d->m_clientType)) return;
    memcpy(&this->d->m_clientType, buffer + offset, sizeof(this->d->m_clientType));
    offset += sizeof(this->d->m_clientType);

    if (bufferSize - offset < sizeof(size_t)) return;
    size_t clientsSize = 0;
    memcpy(&clientsSize, buffer + offset, sizeof(size_t));
    offset += sizeof(size_t);

    size_t maxElements = (bufferSize - offset) / sizeof(uint64_t);
    if (clientsSize > maxElements) return; // Prevent integer overflow
    if (bufferSize - offset != clientsSize * sizeof(uint64_t)) return;

    if (clientsSize > 0) {
        this->d->m_clients.resize(clientsSize);
        memcpy(this->d->m_clients.data(), buffer + offset, clientsSize * sizeof(uint64_t));
    }
}//FIXNOpost2 ends

AkVCam::MsgClients::~MsgClients()
{
    delete this->d;
}

AkVCam::MsgClients &AkVCam::MsgClients::operator =(const MsgClients &other)
{
    if (this != &other) {
        this->d->m_clientType = other.d->m_clientType;
        this->d->m_clients = other.d->m_clients;
        this->setQueryId(other.queryId());
    }

    return *this;
}

bool AkVCam::MsgClients::operator ==(const MsgClients &other) const
{
    return this->d->m_clientType == other.d->m_clientType
           && this->d->m_clients == other.d->m_clients
           && this->queryId() == other.queryId();
}

AkVCam::Message AkVCam::MsgClients::toMessage() const
{
    size_t totalSize = sizeof(this->d->m_clientType)
                       + sizeof(size_t)
                       + sizeof(uint64_t) * this->d->m_clients.size();
    std::vector<char> data(totalSize);
    size_t offset = 0;
    memcpy(data.data() + offset, &this->d->m_clientType, sizeof(this->d->m_clientType));
    offset += sizeof(this->d->m_clientType);

    size_t clientsSize = this->d->m_clients.size();
    memcpy(data.data() + offset, &clientsSize, sizeof(clientsSize));
    offset += sizeof(clientsSize);

    if (clientsSize > 0)
        memcpy(data.data() + offset, this->d->m_clients.data(), sizeof(uint64_t) * clientsSize);

    return {AKVCAM_SERVICE_MSG_CLIENTS, this->queryId(), data};
}

AkVCam::MsgClients::ClientType AkVCam::MsgClients::clientType() const
{
    return this->d->m_clientType;
}

const std::vector<uint64_t> &AkVCam::MsgClients::clients() const
{
    return this->d->m_clients;
}

namespace AkVCam
{
    class MsgFrameReadyPrivate
    {
        public:
            std::string m_device;
            VideoFrame m_frame;
            bool m_isActive {false};
    };
}

AkVCam::MsgFrameReady::MsgFrameReady():
    MsgCommons()
{
    this->d = new MsgFrameReadyPrivate;
}

AkVCam::MsgFrameReady::MsgFrameReady(const std::string &device):
    MsgCommons()
{
    this->d = new MsgFrameReadyPrivate;
    this->d->m_device = device;
}

AkVCam::MsgFrameReady::MsgFrameReady(const std::string &device, bool isActive):
    MsgCommons()
{
    this->d = new MsgFrameReadyPrivate;
    this->d->m_device = device;
    this->d->m_isActive = isActive;
}

AkVCam::MsgFrameReady::MsgFrameReady(const std::string &device,
                                     bool isActive,
                                     uint64_t queryId):
    MsgCommons(queryId)
{
    this->d = new MsgFrameReadyPrivate;
    this->d->m_device = device;
    this->d->m_isActive = isActive;
}

AkVCam::MsgFrameReady::MsgFrameReady(const std::string &device,
                                     const VideoFrame &frame,
                                     bool isActive):
    MsgCommons()
{
    this->d = new MsgFrameReadyPrivate;
    this->d->m_device = device;
    this->d->m_frame = frame;
    this->d->m_isActive = isActive;
}

AkVCam::MsgFrameReady::MsgFrameReady(const std::string &device,
                                     const VideoFrame &frame,
                                     bool isActive,
                                     uint64_t queryId):
    MsgCommons(queryId)
{
    this->d = new MsgFrameReadyPrivate;
    this->d->m_device = device;
    this->d->m_frame = frame;
    this->d->m_isActive = isActive;
}

AkVCam::MsgFrameReady::MsgFrameReady(const MsgFrameReady &other):
    MsgCommons(other.queryId())
{
    this->d = new MsgFrameReadyPrivate;
    this->d->m_device = other.d->m_device;
    this->d->m_frame = other.d->m_frame;
    this->d->m_isActive = other.d->m_isActive;
}

AkVCam::MsgFrameReady::MsgFrameReady(const Message &message): //FIXNOpost2 Starts
    MsgCommons(message.queryId())
{
    this->d = new MsgFrameReadyPrivate;
    if (message.id() != AKVCAM_SERVICE_MSG_FRAME_READY) return;

    size_t offset = 0;
    size_t bufferSize = message.data().size();
    const char* buffer = message.data().data();

    if (bufferSize - offset < sizeof(size_t)) return;
    size_t deviceSize = 0;
    memcpy(&deviceSize, buffer + offset, sizeof(size_t));
    offset += sizeof(size_t);

    if (bufferSize - offset < deviceSize) return;
    if (deviceSize > 0) {
        this->d->m_device = std::string(buffer + offset, deviceSize);
        offset += deviceSize;
    }

    size_t fmtSize = sizeof(PixelFormat) + sizeof(int) + sizeof(int);
    if (bufferSize - offset < fmtSize) return;
    
    PixelFormat fourcc = PixelFormat_none;
    memcpy(&fourcc, buffer + offset, sizeof(fourcc));
    offset += sizeof(fourcc);

    int width = 0;
    memcpy(&width, buffer + offset, sizeof(width));
    offset += sizeof(width);

    int height = 0;
    memcpy(&height, buffer + offset, sizeof(height));
    offset += sizeof(height);

    if (bufferSize - offset < sizeof(size_t)) return;
    size_t dataSize = 0;
    memcpy(&dataSize, buffer + offset, sizeof(size_t));
    offset += sizeof(size_t);

    //FIXNOCodex1 starts
    if (bufferSize - offset < sizeof(bool)) return;
    
    size_t payloadSize = bufferSize - offset - sizeof(bool);
    if (payloadSize != dataSize) return;

    if (dataSize > 0) {
        VideoFrame frame(VideoFormat(fourcc, width, height));

        if (dataSize != frame.size()) return;

        this->d->m_frame = frame;
        memcpy(this->d->m_frame.data(), buffer + offset, dataSize);
        offset += dataSize;
    }
    //FIXNOCodex1 ends

    memcpy(&this->d->m_isActive, buffer + offset, sizeof(bool));
} //FIXNOpost2 Ends

AkVCam::MsgFrameReady::~MsgFrameReady()
{
    delete this->d;
}

AkVCam::MsgFrameReady &AkVCam::MsgFrameReady::operator =(const MsgFrameReady &other)
{
    if (this != &other) {
        this->d->m_device = other.d->m_device;
        this->d->m_frame = other.d->m_frame;
        this->d->m_isActive = other.d->m_isActive;
        this->setQueryId(other.queryId());
    }

    return *this;
}

bool AkVCam::MsgFrameReady::operator ==(const MsgFrameReady &other) const
{
    return this->d->m_device == other.d->m_device
           && this->d->m_frame == other.d->m_frame
           && this->d->m_isActive == other.d->m_isActive
           && this->queryId() == other.queryId();
}

AkVCam::Message AkVCam::MsgFrameReady::toMessage() const
{
    size_t totalSize = sizeof(size_t)
                       + this->d->m_device.size()
                       + sizeof(PixelFormat)
                       + sizeof(int)
                       + sizeof(int)
                       + sizeof(size_t)
                       + this->d->m_frame.size()
                       + sizeof(this->d->m_isActive);
    std::vector<char> data(totalSize);
    size_t offset = 0;

    size_t deviceSize = this->d->m_device.size();
    memcpy(data.data() + offset, &deviceSize, sizeof(deviceSize));
    offset += sizeof(size_t);

    if (deviceSize > 0) {
        memcpy(data.data() + offset, this->d->m_device.data(), deviceSize);
        offset += this->d->m_device.size();
    }

    auto fourcc = this->d->m_frame.format().format();
    memcpy(data.data() + offset, &fourcc, sizeof(fourcc));
    offset += sizeof(fourcc);

    auto width = this->d->m_frame.format().width();
    memcpy(data.data() + offset, &width, sizeof(width));
    offset += sizeof(width);

    auto height = this->d->m_frame.format().height();
    memcpy(data.data() + offset, &height, sizeof(height));
    offset += sizeof(height);

    size_t dataSize = this->d->m_frame.size();
    memcpy(data.data() + offset, &dataSize, sizeof(size_t));
    offset += sizeof(size_t);

    if (dataSize > 0) {
        memcpy(data.data() + offset, this->d->m_frame.constData(), dataSize);
        offset += dataSize;
    }

    memcpy(data.data() + offset, &this->d->m_isActive, sizeof(this->d->m_isActive));

    return {AKVCAM_SERVICE_MSG_FRAME_READY, this->queryId(), data};
}

const std::string &AkVCam::MsgFrameReady::device() const
{
    return this->d->m_device;
}

const AkVCam::VideoFrame &AkVCam::MsgFrameReady::frame() const
{
    return this->d->m_frame;
}

bool AkVCam::MsgFrameReady::isActive() const
{
    return this->d->m_isActive;
}

namespace AkVCam
{
    class MsgBroadcastPrivate
    {
        public:
            std::string m_device;
            uint64_t m_pid {0};
            VideoFrame m_frame;
    };
}

AkVCam::MsgBroadcast::MsgBroadcast():
    MsgCommons()
{
    this->d = new MsgBroadcastPrivate;
}

AkVCam::MsgBroadcast::MsgBroadcast(const std::string &device):
    MsgCommons()
{
    this->d = new MsgBroadcastPrivate;
    this->d->m_device = device;
}

AkVCam::MsgBroadcast::MsgBroadcast(const std::string &device, uint64_t pid):
    MsgCommons()
{
    this->d = new MsgBroadcastPrivate;
    this->d->m_device = device;
    this->d->m_pid = pid;
}

AkVCam::MsgBroadcast::MsgBroadcast(const std::string &device,
                                   uint64_t pid,
                                   uint64_t queryId):
    MsgCommons(queryId)
{
    this->d = new MsgBroadcastPrivate;
    this->d->m_device = device;
    this->d->m_pid = pid;
}

AkVCam::MsgBroadcast::MsgBroadcast(const std::string &device,
                                   uint64_t pid,
                                   const VideoFrame &frame):
    MsgCommons()
{
    this->d = new MsgBroadcastPrivate;
    this->d->m_device = device;
    this->d->m_pid = pid;
    this->d->m_frame = frame;
}

AkVCam::MsgBroadcast::MsgBroadcast(const std::string &device,
                                   uint64_t pid,
                                   const VideoFrame &frame,
                                   uint64_t queryId):
    MsgCommons(queryId)
{
    this->d = new MsgBroadcastPrivate;
    this->d->m_device = device;
    this->d->m_pid = pid;
    this->d->m_frame = frame;
}

AkVCam::MsgBroadcast::MsgBroadcast(const MsgBroadcast &other):
    MsgCommons(other.queryId())
{
    this->d = new MsgBroadcastPrivate;
    this->d->m_device = other.d->m_device;
    this->d->m_pid = other.d->m_pid;
    this->d->m_frame = other.d->m_frame;
}

AkVCam::MsgBroadcast::MsgBroadcast(const Message &message): //FIXNOpost2 Starts
    MsgCommons(message.queryId())
{
    this->d = new MsgBroadcastPrivate;
    if (message.id() != AKVCAM_SERVICE_MSG_BROADCAST) return;

    size_t offset = 0;
    size_t bufferSize = message.data().size();
    const char* buffer = message.data().data();

    if (bufferSize - offset < sizeof(size_t)) return;
    size_t deviceSize = 0;
    memcpy(&deviceSize, buffer + offset, sizeof(size_t));
    offset += sizeof(size_t);

    if (bufferSize - offset < deviceSize) return;
    if (deviceSize > 0) {
        this->d->m_device = std::string(buffer + offset, deviceSize);
        offset += deviceSize;
    }

    size_t fixedSize = sizeof(uint64_t) + sizeof(PixelFormat) + sizeof(int) + sizeof(int);
    if (bufferSize - offset < fixedSize) return;

    memcpy(&this->d->m_pid, buffer + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);

    PixelFormat fourcc = PixelFormat_none;
    memcpy(&fourcc, buffer + offset, sizeof(fourcc));
    offset += sizeof(fourcc);

    int width = 0;
    memcpy(&width, buffer + offset, sizeof(width));
    offset += sizeof(width);

    int height = 0;
    memcpy(&height, buffer + offset, sizeof(height));
    offset += sizeof(height);

    if (bufferSize - offset < sizeof(size_t)) return;
    size_t dataSize = 0;
    memcpy(&dataSize, buffer + offset, sizeof(size_t));
    offset += sizeof(size_t);

    if (bufferSize - offset != dataSize) return; //FIXNOCodex1

    if (dataSize > 0) {
        VideoFrame frame(VideoFormat(fourcc, width, height));

        if (dataSize != frame.size()) return;

        this->d->m_frame = frame;
        memcpy(this->d->m_frame.data(), buffer + offset, dataSize);
    } //FIXNOCodex1 ends
}//FIXNOpost2 ends

AkVCam::MsgBroadcast::~MsgBroadcast()
{
    delete this->d;
}

AkVCam::MsgBroadcast &AkVCam::MsgBroadcast::operator =(const MsgBroadcast &other)
{
    if (this != &other) {
        this->d->m_device = other.d->m_device;
        this->d->m_pid = other.d->m_pid;
        this->d->m_frame = other.d->m_frame;
        this->setQueryId(other.queryId());
    }

    return *this;
}

bool AkVCam::MsgBroadcast::operator ==(const MsgBroadcast &other) const
{
    return this->d->m_device == other.d->m_device
           && this->d->m_pid == other.d->m_pid
           && this->d->m_frame == other.d->m_frame
           && this->queryId() == other.queryId();
}

AkVCam::Message AkVCam::MsgBroadcast::toMessage() const
{
    size_t totalSize = sizeof(size_t)
                       + this->d->m_device.size()
                       + sizeof(this->d->m_pid)
                       + sizeof(PixelFormat)
                       + sizeof(int)
                       + sizeof(int)
                       + sizeof(size_t)
                       + this->d->m_frame.size();
    std::vector<char> data(totalSize);
    size_t offset = 0;

    size_t deviceSize = this->d->m_device.size();
    memcpy(data.data() + offset, &deviceSize, sizeof(size_t));
    offset += sizeof(size_t);

    if (deviceSize > 0) {
        memcpy(data.data() + offset, this->d->m_device.data(), deviceSize);
        offset += deviceSize;
    }

    memcpy(data.data() + offset, &this->d->m_pid, sizeof(this->d->m_pid));
    offset += sizeof(this->d->m_pid);

    auto fourcc = this->d->m_frame.format().format();
    memcpy(data.data() + offset, &fourcc, sizeof(fourcc));
    offset += sizeof(fourcc);

    auto width = this->d->m_frame.format().width();
    memcpy(data.data() + offset, &width, sizeof(width));
    offset += sizeof(width);

    auto height = this->d->m_frame.format().height();
    memcpy(data.data() + offset, &height, sizeof(height));
    offset += sizeof(height);

    size_t dataSize = this->d->m_frame.size();
    memcpy(data.data() + offset, &dataSize, sizeof(size_t));
    offset += sizeof(size_t);

    if (dataSize > 0)
        memcpy(data.data() + offset, this->d->m_frame.constData(), dataSize);

    return {AKVCAM_SERVICE_MSG_BROADCAST, this->queryId(), data};
}

const std::string &AkVCam::MsgBroadcast::device() const
{
    return this->d->m_device;
}

uint64_t AkVCam::MsgBroadcast::pid() const
{
    return this->d->m_pid;
}

const AkVCam::VideoFrame &AkVCam::MsgBroadcast::frame() const
{
    return this->d->m_frame;
}

namespace AkVCam
{
    class MsgListenPrivate
    {
        public:
            std::string m_device;
            uint64_t m_pid {0};
    };
}

AkVCam::MsgListen::MsgListen():
    MsgCommons()
{
    this->d = new MsgListenPrivate;
}

AkVCam::MsgListen::MsgListen(const std::string &device):
    MsgCommons()
{
    this->d = new MsgListenPrivate;
    this->d->m_device = device;
}

AkVCam::MsgListen::MsgListen(const std::string &device, uint64_t pid):
    MsgCommons()
{
    this->d = new MsgListenPrivate;
    this->d->m_device = device;
    this->d->m_pid = pid;
}

AkVCam::MsgListen::MsgListen(const std::string &device,
                             uint64_t pid,
                             uint64_t queryId):
    MsgCommons(queryId)
{
    this->d = new MsgListenPrivate;
    this->d->m_device = device;
    this->d->m_pid = pid;
}

AkVCam::MsgListen::MsgListen(const MsgListen &other):
    MsgCommons(other.queryId())
{
    this->d = new MsgListenPrivate;
    this->d->m_device = other.d->m_device;
    this->d->m_pid = other.d->m_pid;
}

AkVCam::MsgListen::MsgListen(const Message &message): //FIXNOpost2 Starts
    MsgCommons(message.queryId())
{
    this->d = new MsgListenPrivate;
    if (message.id() != AKVCAM_SERVICE_MSG_LISTEN) return;

    size_t offset = 0;
    size_t bufferSize = message.data().size();
    const char* buffer = message.data().data();

    if (bufferSize - offset < sizeof(size_t)) return;
    size_t deviceSize = 0;
    memcpy(&deviceSize, buffer + offset, sizeof(size_t));
    offset += sizeof(size_t);

    if (bufferSize - offset < deviceSize) return;
    if (deviceSize > 0) {
        this->d->m_device = std::string(buffer + offset, deviceSize);
        offset += deviceSize;
    }

    if (bufferSize - offset != sizeof(uint64_t)) return; // Strict bounds check for end of message
    memcpy(&this->d->m_pid, buffer + offset, sizeof(uint64_t));
} //FIXNOpost2 ends

AkVCam::MsgListen::~MsgListen()
{
    delete this->d;
}

AkVCam::MsgListen &AkVCam::MsgListen::operator =(const MsgListen &other)
{
    if (this != &other) {
        this->d->m_device = other.d->m_device;
        this->d->m_pid = other.d->m_pid;
        this->setQueryId(other.queryId());
    }

    return *this;
}

bool AkVCam::MsgListen::operator ==(const MsgListen &other) const
{
    return this->d->m_device == other.d->m_device
           && this->d->m_pid == other.d->m_pid
           && this->queryId() == other.queryId();
}

AkVCam::Message AkVCam::MsgListen::toMessage() const
{
    size_t totalSize = sizeof(size_t)
                       + this->d->m_device.size()
                       + sizeof(this->d->m_pid);
    std::vector<char> data(totalSize);
    size_t offset = 0;

    size_t deviceSize = this->d->m_device.size();
    memcpy(data.data() + offset, &deviceSize, sizeof(size_t));
    offset += sizeof(size_t);

    if (deviceSize > 0) {
        memcpy(data.data() + offset, this->d->m_device.data(), deviceSize);
        offset += deviceSize;
    }

    memcpy(data.data() + offset, &this->d->m_pid, sizeof(this->d->m_pid));

    return {AKVCAM_SERVICE_MSG_LISTEN, this->queryId(), data};
}

const std::string &AkVCam::MsgListen::device() const
{
    return this->d->m_device;
}

uint64_t AkVCam::MsgListen::pid() const
{
    return this->d->m_pid;
}
