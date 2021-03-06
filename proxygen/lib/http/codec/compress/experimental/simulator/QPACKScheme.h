/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "proxygen/lib/http/codec/compress/experimental/simulator/CompressionScheme.h"
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKCodec.h>
#include <proxygen/lib/http/codec/compress/NoPathIndexingStrategy.h>

namespace proxygen {  namespace compress {

class QPACKScheme : public CompressionScheme {
 public:
  explicit QPACKScheme(CompressionSimulator* sim)
      : CompressionScheme(sim) {
    client_.setHeaderIndexingStrategy(NoPathIndexingStrategy::getInstance());
    server_.setHeaderIndexingStrategy(NoPathIndexingStrategy::getInstance());
  }

  struct QPACKAck : public CompressionScheme::Ack {
    explicit QPACKAck(uint16_t n, std::unique_ptr<folly::IOBuf> inAcks)
        : seqn(n),
          acks(std::move(inAcks)) {}
    uint16_t seqn;
    std::unique_ptr<folly::IOBuf> acks;
  };
  std::unique_ptr<Ack> getAck(uint16_t seqn) override {
    auto acks = server_.moveAcks();
    if (!acks) {
      return nullptr;
    }
    VLOG(4) << "Sending ack for seqn=" << seqn;
    return std::make_unique<QPACKAck>(seqn, std::move(acks));
  }
  void recvAck(std::unique_ptr<Ack> ack) override {
    CHECK(ack);
    auto qpackAck = dynamic_cast<QPACKAck*>(ack.get());
    CHECK_NOTNULL(qpackAck);
    VLOG(4) << "Received qpack-ack";
    client_.applyAcks(qpackAck->acks.get());
  }

  std::pair<bool, std::unique_ptr<folly::IOBuf>> encode(
    std::vector<compress::Header> allHeaders,
    SimStats& stats) override {
    index++;
    auto block = client_.encode(allHeaders);
    stats.uncompressed += client_.getEncodedSize().uncompressed;
    stats.compressed += client_.getEncodedSize().compressed;
    // OOO is always allowed
    return {true, std::move(block)};
  }

  void decode(bool allowOOO, std::unique_ptr<folly::IOBuf> encodedReq,
              SimStats& stats, SimStreamingCallback& callback) override {
    VLOG(1) << "Decoding request=" << callback.requestIndex << " allowOOO="
            << uint32_t(allowOOO);
    folly::io::Cursor c(encodedReq.get());
    server_.decodeStreaming(c, c.totalLength(), &callback);
    callback.maybeMarkHolDelay();
    if (server_.getQueuedBytes() > stats.maxQueueBufferBytes) {
      stats.maxQueueBufferBytes = server_.getQueuedBytes();
    }
  }

  uint32_t getHolBlockCount() const override {
    return server_.getHolBlockCount();
  }

  QPACKCodec client_;
  QPACKCodec server_;
};
}}
