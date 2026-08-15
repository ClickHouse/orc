/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef CODED_STREAM_WRAPPER_HH
#define CODED_STREAM_WRAPPER_HH

#include "Adaptor.hh"

DIAGNOSTIC_PUSH

#ifdef __clang__
DIAGNOSTIC_IGNORE("-Wshorten-64-to-32")
DIAGNOSTIC_IGNORE("-Wreserved-id-macro")
#endif

#if defined(__GNUC__) || defined(__clang__)
DIAGNOSTIC_IGNORE("-Wconversion")
#endif

#include <google/protobuf/io/coded_stream.h>

DIAGNOSTIC_POP

#include <cstdint>
#include <exception>

namespace orc {
  // Matches the Java reader's InStream.PROTOBUF_MESSAGE_MAX_LIMIT (1 GB) so both
  // implementations reject oversized messages identically.
  constexpr int PROTOBUF_MESSAGE_MAX_LIMIT = 1024 << 20;

  /**
   * Adapts a stream whose methods throw to the ZeroCopyInputStream contract, which requires
   * failure to be reported by return value. An exception unwinding out of the protobuf
   * parser skips its has-bits write-back, leaving the message in a state its own destructor
   * rejects. The first exception is stored; the caller must rethrow it with
   * rethrowStoredException() once the parser has returned.
   */
  class ExceptionIsolatingInputStream : public google::protobuf::io::ZeroCopyInputStream {
   private:
    google::protobuf::io::ZeroCopyInputStream* input_;
    mutable std::exception_ptr exception_;
    mutable int64_t lastByteCount_ = 0;

    void storeException() const {
      // Keep the first one: later failures are consequences of reporting it as end of input.
      if (!exception_) {
        exception_ = std::current_exception();
      }
    }

   public:
    explicit ExceptionIsolatingInputStream(google::protobuf::io::ZeroCopyInputStream* input)
        : input_(input) {}

    bool Next(const void** data, int* size) override {
      try {
        return input_->Next(data, size);
      } catch (...) {
        storeException();
        return false;
      }
    }

    void BackUp(int count) override {
      try {
        input_->BackUp(count);
      } catch (...) {
        storeException();
      }
    }

    bool Skip(int count) override {
      try {
        return input_->Skip(count);
      } catch (...) {
        storeException();
        return false;
      }
    }

    int64_t ByteCount() const override {
      // The contract defines no failure value here, so report the last count read.
      try {
        lastByteCount_ = input_->ByteCount();
      } catch (...) {
        storeException();
      }
      return lastByteCount_;
    }

    // ReadCord is deliberately not overridden: the inherited implementation reaches the
    // wrapped stream only through Next() and BackUp() above.

    void rethrowStoredException() {
      if (exception_) {
        std::exception_ptr stored = exception_;
        exception_ = nullptr;
        std::rethrow_exception(stored);
      }
    }
  };

  // Parse a protobuf message from a ZeroCopyInputStream while enforcing the
  // total byte limit above. Use this instead of Message::ParseFromZeroCopyStream
  // for any message read from file contents.
  template <typename Message>
  inline bool parseProtobufFromStream(Message* message,
                                      google::protobuf::io::ZeroCopyInputStream* input) {
    ExceptionIsolatingInputStream guard(input);
    bool parsed;
    {
      google::protobuf::io::CodedInputStream codedStream(&guard);
#if defined(GOOGLE_PROTOBUF_VERSION) && GOOGLE_PROTOBUF_VERSION < 3006000
      // The single-argument overload was added in protobuf 3.6.0; older versions
      // require a warning threshold, where -1 disables the warning.
      codedStream.SetTotalBytesLimit(PROTOBUF_MESSAGE_MAX_LIMIT, -1);
#else
      codedStream.SetTotalBytesLimit(PROTOBUF_MESSAGE_MAX_LIMIT);
#endif
      parsed = message->ParseFromCodedStream(&codedStream);
    }
    // ~CodedInputStream calls BackUp, which can throw too, so it must run before the check.
    guard.rethrowStoredException();
    return parsed;
  }
}  // namespace orc

#endif
