#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace orc {
  class InputStream;

  struct CacheOptions {
    /// The maximum distance in bytes between two consecutive
    /// ranges; beyond this value, ranges are not combined
    int64_t hole_size_limit;

    /// The maximum size in bytes of a combined range; if
    /// combining two consecutive ranges would produce a range of a
    /// size greater than this, they are not combined
    int64_t range_size_limit;
  };

  struct ReadRange {
    int64_t offset;
    int64_t length;

    friend bool operator==(const ReadRange& left, const ReadRange& right) {
      return (left.offset == right.offset && left.length == right.length);
    }
    friend bool operator!=(const ReadRange& left, const ReadRange& right) {
      return !(left == right);
    }

    bool contains(const ReadRange& other) const {
      return (offset <= other.offset && offset + length >= other.offset + other.length);
    }
  };

  /// A read cache designed to hide IO latencies when reading.
  ///
  /// This class takes multiple byte ranges that an application expects to read, and
  /// coalesces them into fewer, larger read requests, which benefits performance on some
  /// filesystems, particularly remote ones like Amazon S3. By default, it also issues
  /// these read requests in parallel up front.
  ///
  /// To use:
  /// 1. Cache() the ranges you expect to read in the future. Ideally, these ranges have
  ///    the exact offset and length that will later be read. The cache will combine those
  ///    ranges according to parameters (see constructor).
  ///
  ///    By default, the cache will also start fetching the combined ranges in parallel in
  ///    the background, unless CacheOptions.lazy is set.
  ///
  /// 2. Call WaitFor() to be notified when the given ranges have been read. If
  ///    CacheOptions.lazy is set, I/O will be triggered in the background here instead.
  ///    This can be done in parallel (e.g. if parsing a file, call WaitFor() for each
  ///    chunk of the file that can be parsed in parallel).
  ///
  /// 3. Call Read() to retrieve the actual data for the given ranges.
  ///    A synchronous application may skip WaitFor() and just call Read() - it will still
  ///    benefit from coalescing and parallel fetching.
  class ReadRangeCache {
   public:
    static constexpr int64_t kDefaultHoleSizeLimit = 8192;
    static constexpr int64_t kDefaultRangeSizeLimit = 32 * 1024 * 1024;

    /// Construct a read cache with given options
    explicit ReadRangeCache(InputStream* _stream, CacheOptions _options)
        : stream(_stream), options(std::move(_options)) {}

    ~ReadRangeCache();

    /// Cache the given ranges in the background.
    ///
    /// The caller must ensure that the ranges do not overlap with each other,
    /// nor with previously cached ranges.  Otherwise, behaviour will be undefined.
    void cache(std::vector<ReadRange> ranges);

    /// \brief Read a range previously given to Cache().
    Result<std::shared_ptr<Buffer>> read(ReadRange range);

private:
    InputStream * stream;
    CacheOptions options;
  };

}  // namespace orc
