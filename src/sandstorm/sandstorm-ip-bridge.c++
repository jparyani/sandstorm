// Sandstorm - Personal Cloud Sandbox
// Copyright (c) 2014 Sandstorm Development Group, Inc. and contributors
// All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sandstorm/sandstorm-ip-bridge.h"
#include "sandstorm/util.h"

#include <kj/debug.h>
#include <netinet/ip.h>
#include <linux/netfilter_ipv4.h>
#include <unistd.h>

namespace sandstorm {
namespace ipbridge {

class RefcountedAsyncIoStream: public kj::Refcounted {
public:
  explicit RefcountedAsyncIoStream(kj::Own<kj::AsyncIoStream>&& stream)
      : stream(kj::mv(stream)) {}
  kj::Own<kj::AsyncIoStream> stream;
};

static const uint BUFFER_SIZE = 8192;
static const uint MAX_PARALLEL_WRITES = 4;

// TCP handling
class AcceptedConnection : private kj::TaskSet::ErrorHandler {
  // This class is for handling a single TCP connection.
  // It will proxy all writes from the incoming AsyncIoStream to the TcpPort
  // Cap'n Proto interface and provides a Downstream capability that handles
  // the returning data stream
  class Downstream final: public ByteStream::Server {
  private:
    kj::Own<RefcountedAsyncIoStream> connection;
    kj::Promise<void> lastWrite;

  public:
    explicit Downstream(kj::Own<RefcountedAsyncIoStream> && connection) :
      connection(kj::mv(connection)), lastWrite(kj::READY_NOW) {}

    kj::Promise<void> write(WriteContext context) {
      // AsyncIoStream can only have 1 write in flight at a time.
      // We have to wait for the last to complete before calling write again.
      auto writePromise = lastWrite.then([this, context] () mutable {
        auto params = context.getParams();
        return connection->stream->write(params.getData().begin(), params.getData().size());
      });

      auto fork = writePromise.fork();
      lastWrite = fork.addBranch();
      return fork.addBranch();
    }

    kj::Promise<void> done(DoneContext context) {
      return lastWrite.then([this] () {
        connection->stream->shutdownWrite();
      });
    }
  };

private:
  kj::Own<RefcountedAsyncIoStream> connection;
  capnp::byte buffer[BUFFER_SIZE];
  TcpPort::Client port;
  kj::TaskSet taskSet;
  kj::Own<ByteStream::Client> upstream;
  uint numOutstandingWrites;
  kj::Own<kj::PromiseFulfiller<void>> continueReading;
  bool aborted;

  inline kj::Promise<void> write(size_t size) {
    ++numOutstandingWrites;
    auto request = upstream->writeRequest();
    // TODO(someday): do this without copying
    request.setData(kj::ArrayPtr<capnp::byte>(buffer, size));
    return request.send().then([this] (auto args) {
      if (numOutstandingWrites >= MAX_PARALLEL_WRITES) {
        continueReading->fulfill();
      }
      --numOutstandingWrites;
    });
  }

  void taskFailed(kj::Exception&& exception) override {
    // Abort the read end of the stream since we have received an error
    // upstream and no longer need to read anymore. This could be called
    // multiple times, so only abort the first. This will have a side-effect
    // of causing tryRead to fail, and erroring out messageLoop() below.
    if (!aborted) {
      connection->stream->abortRead();
      aborted = true;
    }
  }

public:
  AcceptedConnection(kj::Own<kj::AsyncIoStream>&& connectionParam, TcpPort::Client && portParam)
      : connection(kj::refcounted<RefcountedAsyncIoStream>(kj::mv(connectionParam))),
        port(kj::mv(portParam)), taskSet(*this),
        numOutstandingWrites(0), aborted(false) {
    auto request = port.connectRequest();
    request.setDownstream(kj::heap<Downstream>(kj::addRef(*connection)));
    upstream = kj::heap<ByteStream::Client>(request.send().getUpstream());

    auto fulfillerPair = kj::newPromiseAndFulfiller<void>();
    continueReading = kj::mv(fulfillerPair.fulfiller);
  }

  kj::Promise<void> messageLoop() {
    return connection->stream->tryRead(buffer, 1, BUFFER_SIZE)
          .then([this] (size_t size) -> kj::Promise<void> {
      if (size < 1) {  // EOF
        return upstream->doneRequest().send().then([] (auto args) {});
      }

      if (numOutstandingWrites < MAX_PARALLEL_WRITES) {
        taskSet.add(write(size));
        return messageLoop();
      } else {
        // Make a new promiseFulfillerPair and wait for one of the previous writes to
        // call fulfill(). Since we wait immediately after we reach
        // numOutstandingWrites == MAX_PARALLEL_WRITES, we are guaranteed to have less
        // than MAX_PARALLEL_WRITES after any of the writes finish, and we can start our
        // next write immediately.
        auto fulfillerPair = kj::newPromiseAndFulfiller<void>();
        continueReading = kj::mv(fulfillerPair.fulfiller);
        return fulfillerPair.promise.then([this, size] () {
          taskSet.add(write(size));
          return messageLoop();
        });
      }
    });
  }
};

TcpPort::Client getTcpClient(kj::Own<kj::AsyncIoStream>& connection,
                             HackSessionContext::Client& session) {
  auto request = session.getIpNetworkRequest().send().getNetwork().getRemoteHostRequest();

  struct sockaddr destaddr;
  uint size = sizeof(destaddr);
  connection->getsockopt(SOL_IP, SO_ORIGINAL_DST, &destaddr, &size);

  // TODO(someday): handle ipv6
  struct sockaddr_in * destaddr_ipv4 = reinterpret_cast<sockaddr_in *>(&destaddr);
  request.getAddress().setLower64(0x0000FFFF00000000 | ntohl(destaddr_ipv4->sin_addr.s_addr));
  auto portRequest = request.send().getHost().getTcpPortRequest();

  portRequest.setPortNum(ntohs(destaddr_ipv4->sin_port));

  return portRequest.send().getPort();
}

kj::Promise<void> runTcpBridge(kj::ConnectionReceiver& serverPort,
                             kj::TaskSet& taskSet, HackSessionContext::Client& session) {
  return serverPort.accept().then([&](kj::Own<kj::AsyncIoStream>&& connection) {
    auto connectionState = kj::heap<AcceptedConnection>(kj::mv(connection),
                                                        getTcpClient(connection, session));
    auto promise = connectionState->messageLoop();
    taskSet.add(promise.attach(kj::mv(connectionState)));
    return runTcpBridge(serverPort, taskSet, session);
  });
}

// UDP handling
// This is mostly non-working at the moment. It requires some re-working using tproxy
class AcceptedUdpConnection {
private:
  class ReturnPort final: public UdpPort::Server {
  private:
    kj::Promise<kj::Own<kj::NetworkAddress>> src;
    kj::Own<kj::NetworkAddress> dest;
  public:
    ReturnPort(kj::Promise<kj::Own<kj::NetworkAddress > >&& src, kj::NetworkAddress& dest) :
      src(kj::mv(src)), dest(dest.clone()) {}

    kj::Promise<void> send(SendContext context) {
      return src.then([&] (auto srcAddress) {
        auto port = srcAddress->bindDatagramPort();
        // TODO(soon): deal with context's returnPort
        auto data = context.getParams().getMessage();
        return port->send((void *)data.begin(), data.size(), *dest)
              .then([this] (auto args) { });
      });
    }
  };

  kj::Own<kj::DatagramReceiver> receiver;

public:
  explicit AcceptedUdpConnection(kj::DatagramPort& serverPort, kj::TaskSet& taskSet,
                                 HackSessionContext::Client& session, kj::Network& network) {
    receiver = serverPort.makeReceiver({65536, sizeof(sockaddr_in) + 128});
  }

  kj::Promise<void> loop() {
    return receiver->receive().then([this] () {
      return messageLoop();
    });
  }

  kj::Promise<void> start() {
    return loop();
  }

  kj::Promise<void> messageLoop() {
    KJ_SYSCALL(write(STDERR_FILENO, "Unhandled UDP packet received by sandstorm-ip-bridge\n",
               sizeof("Unhandled UDP packet received by sandstorm-ip-bridge\n")));

    return loop();
  }
};

kj::Promise<void> runUdpBridge(kj::DatagramPort& serverPort, kj::TaskSet& taskSet,
                               HackSessionContext::Client& session, kj::Network& network) {
  auto connectionState = kj::heap<AcceptedUdpConnection>(serverPort, taskSet, session, network);
  auto promise = connectionState->start();
  taskSet.add(promise.attach(kj::mv(connectionState)));
  return kj::READY_NOW;
}

}  // namespace ipbridge
}  // namespace sandstorm