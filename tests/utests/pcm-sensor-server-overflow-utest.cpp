// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2025, Intel Corporation

// Regression test for the out-of-bounds write in
// basic_socketbuf::overflow() (src/pcm-sensor-server.cpp, lines 1226-1237).
//
// The vulnerable overflow() writes the incoming character into *pptr()
// *before* flushing the full put-area to the socket. When the put area is
// full, pptr() == epptr(), i.e. it points one past the end of outputBuffer_,
// which is the first byte of the adjacent inputBuffer_. This test drives the
// real basic_socketbuf template through a socketpair, fills the put area to
// the brim, triggers overflow(), and verifies that inputBuffer_[0] is not
// corrupted.
//
// With the bug in place the assertion on inputBuffer_[0] fails (and, when the
// binary is built with AddressSanitizer via -DPCM_NO_ASAN=OFF, the underlying
// intra-object OOB write / OOB read in send() is also detected). Once
// overflow() is fixed to flush first and then store the character into the
// emptied buffer, the test passes.

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

// Pull the real basic_socketbuf template out of pcm-sensor-server.cpp without
// bringing in its main(). The same mechanism is already used by
// tests/pcm-sensor-server-fuzz.cpp.
#define UNIT_TEST 1
#include "../../src/pcm-sensor-server.cpp"
#undef UNIT_TEST

#include <gtest/gtest.h>

namespace {

// SIZE matches the instantiation used by basic_socketstream::buf_type
// (see pcm-sensor-server.cpp line 1363).
constexpr std::size_t kSocketBufSize = 16385;

// Subclass that exposes the protected buffer members so the test can inspect
// inputBuffer_[0] after triggering overflow().
class ProbeSocketBuf : public basic_socketbuf<kSocketBufSize, char> {
public:
    using basic_socketbuf<kSocketBufSize, char>::inputBuffer_;
    using basic_socketbuf<kSocketBufSize, char>::outputBuffer_;
};

// Drain the peer end of a socketpair in a background thread so that the
// server-side send() inside writeToSocket() never blocks, regardless of what
// the kernel's default socket buffer sizes happen to be on the CI runner.
class SocketDrainer {
public:
    explicit SocketDrainer(int fd) : fd_(fd), stop_(false) {
        thread_ = std::thread([this]() {
            char buf[4096];
            while (!stop_.load()) {
                ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
                if (n <= 0) {
                    break;
                }
                received_.insert(received_.end(), buf, buf + n);
            }
        });
    }

    ~SocketDrainer() {
        stop_.store(true);
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    const std::vector<char>& data() const { return received_; }

private:
    int fd_;
    std::atomic<bool> stop_;
    std::thread thread_;
    std::vector<char> received_;
};

} // namespace

TEST(PcmSensorServerOverflowTest, OverflowDoesNotWritePastOutputBuffer)
{
    int sv[2];
    ASSERT_EQ(0, ::socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
        << "socketpair failed: " << std::strerror(errno);

    // Peer drains whatever the socketbuf sends.
    SocketDrainer drainer(sv[1]);

    auto buf = std::make_unique<ProbeSocketBuf>();
    buf->setSocket(sv[0]);

    // Plant a distinctive sentinel in inputBuffer_[0]. With the vulnerable
    // overflow(), this byte is the one clobbered by "*pptr() = ch" when the
    // put area is full.
    constexpr unsigned char kSentinel = 0xAA;
    constexpr unsigned char kOverflowChar = 0x5A; // 'Z'
    static_assert(kSentinel != kOverflowChar,
                  "sentinel and overflow byte must differ to detect the OOB write");
    buf->inputBuffer_[0] = static_cast<char>(kSentinel);

    // Fill the put area completely. After exactly SIZE sputc() calls,
    // pptr() == epptr() but overflow() has not yet been invoked.
    for (std::size_t i = 0; i < kSocketBufSize; ++i) {
        ASSERT_NE(std::char_traits<char>::eof(), buf->sputc('X'))
            << "sputc failed while filling the put area at index " << i;
    }

    // The next sputc() must trigger overflow(kOverflowChar). With the
    // vulnerable implementation this is where *pptr() = ch writes past the
    // end of outputBuffer_ and into inputBuffer_[0].
    ASSERT_NE(std::char_traits<char>::eof(),
              buf->sputc(static_cast<char>(kOverflowChar)))
        << "sputc returned eof when triggering overflow()";

    // After overflow() returns, the put area should have been flushed to the
    // socket and the sentinel in inputBuffer_[0] must still be intact.
    EXPECT_EQ(kSentinel, static_cast<unsigned char>(buf->inputBuffer_[0]))
        << "basic_socketbuf::overflow() corrupted inputBuffer_[0]: "
        << "wrote ch=0x" << std::hex << static_cast<int>(kOverflowChar)
        << " past the end of outputBuffer_ (SIZE=" << std::dec
        << kSocketBufSize << "). See pcm-sensor-server.cpp "
        << "basic_socketbuf::overflow() (lines 1226-1237): the character "
        << "must be stored only *after* the full put-area has been flushed "
        << "and the put pointers reset.";

    // Release the socket before the buf destructor runs sync(); this keeps
    // the test output stable regardless of whether the drainer has already
    // exited. Resetting buf closes sv[0].
    buf.reset();
    // sv[1] is closed by the drainer's shutdown.
}
