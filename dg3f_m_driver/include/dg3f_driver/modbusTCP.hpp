#pragma once

// Modbus TCP client over POSIX sockets.
//
// Previously implemented on boost::asio. Boost is no longer required anywhere in
// this workspace, and the two APIs this used (io_service, resolver::query) were
// deprecated in Boost 1.66 and removed in the Boost shipped with newer Ubuntu,
// which broke the build outright. This implementation uses the same plain
// sockets as delto_tcp_comm.
//
// Framing notes: every response is read using the MBAP length field rather than
// an assumed fixed size, so a Modbus exception response (3 payload bytes) is
// parsed and reported instead of stalling until the read timeout. Any bytes left
// queued from a previous timed-out transaction are discarded before a new
// request goes out, and the transaction id is verified on every reply.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class ModbusClient {
 public:
  explicit ModbusClient(const std::string& host, int port)
      : host_(host),
        port_(static_cast<uint16_t>(port)),
        sockfd_(-1),
        timeout_ms_(200),
        transaction_id_(0) {}

  virtual ~ModbusClient() { disconnect(); }

  // Default read/write timeout for subsequent transactions, in milliseconds.
  void setTimeout(int timeout_ms) {
    if (timeout_ms > 0) {
      timeout_ms_ = timeout_ms;
    }
  }

  bool connect(int timeout_seconds = 5) {
    std::lock_guard<std::mutex> lock(mutex_);
    return connectLocked(timeout_seconds);
  }

  bool reconnect(int max_attempts = 3) {
    for (int i = 0; i < max_attempts; ++i) {
      std::cout << "Reconnection attempt " << (i + 1) << "/" << max_attempts
                << std::endl;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        disconnectLocked();
        if (connectLocked()) {
          return true;
        }
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
  }

  void disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnectLocked();
  }

  bool isConnected() const { return sockfd_ >= 0; }

  // Discards anything still queued on the socket.
  void clearBuffers() {
    std::lock_guard<std::mutex> lock(mutex_);
    drainLocked();
  }

  std::vector<int16_t> readHoldingRegisters(uint16_t start_address,
                                           uint16_t quantity,
                                           unsigned int timeout_ms = 0) {
    return readRegisters(0x03, start_address, quantity, timeout_ms);
  }

  std::vector<int16_t> readInputRegisters(uint16_t start_address,
                                         uint16_t quantity,
                                         unsigned int timeout_ms = 200) {
    return readRegisters(0x04, start_address, quantity, timeout_ms);
  }

  void writeSingleCoil(uint16_t address, bool value) {
    std::vector<uint8_t> pdu = {0x05,
                               static_cast<uint8_t>(address >> 8),
                               static_cast<uint8_t>(address & 0xFF),
                               static_cast<uint8_t>(value ? 0xFF : 0x00),
                               0x00};
    transact("writeSingleCoil", pdu, 0x05, 0);
  }

  void writeMultiCoils(uint16_t address, const std::vector<bool>& values) {
    if (values.empty()) {
      throw std::runtime_error("writeMultiCoils: no values");
    }
    const std::size_t num_bytes = (values.size() + 7) / 8;
    std::vector<uint8_t> packed(num_bytes, 0x00);
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (values[i]) {
        packed[i / 8] = static_cast<uint8_t>(packed[i / 8] | (1u << (i % 8)));
      }
    }

    std::vector<uint8_t> pdu = {0x0F,
                               static_cast<uint8_t>(address >> 8),
                               static_cast<uint8_t>(address & 0xFF),
                               static_cast<uint8_t>(values.size() >> 8),
                               static_cast<uint8_t>(values.size() & 0xFF),
                               static_cast<uint8_t>(num_bytes)};
    pdu.insert(pdu.end(), packed.begin(), packed.end());
    transact("writeMultiCoils", pdu, 0x0F, 0);
  }

  void writeSingleRegister(uint16_t address, uint16_t value) {
    std::vector<uint8_t> pdu = {0x06,
                               static_cast<uint8_t>(address >> 8),
                               static_cast<uint8_t>(address & 0xFF),
                               static_cast<uint8_t>(value >> 8),
                               static_cast<uint8_t>(value & 0xFF)};
    transact("writeSingleRegister", pdu, 0x06, 0);
  }

  void writeMultiRegisters(uint16_t address,
                           const std::vector<uint16_t>& values) {
    if (values.empty()) {
      throw std::runtime_error("writeMultiRegisters: no values");
    }
    if (values.size() > 123) {  // Modbus caps FC16 at 123 registers
      throw std::runtime_error("writeMultiRegisters: " +
                               std::to_string(values.size()) +
                               " registers exceeds the Modbus limit of 123");
    }

    const std::size_t values_bytes = values.size() * 2;
    std::vector<uint8_t> pdu = {0x10,
                               static_cast<uint8_t>(address >> 8),
                               static_cast<uint8_t>(address & 0xFF),
                               static_cast<uint8_t>(values.size() >> 8),
                               static_cast<uint8_t>(values.size() & 0xFF),
                               static_cast<uint8_t>(values_bytes)};
    for (const uint16_t v : values) {
      pdu.push_back(static_cast<uint8_t>(v >> 8));
      pdu.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    transact("writeMultiRegisters", pdu, 0x10, 0);
  }

 private:
  // MBAP: transaction(2) protocol(2) length(2) unit(1)
  static constexpr std::size_t MBAP_SIZE = 7;
  static constexpr uint8_t UNIT_ID = 0x01;
  // Guards against a corrupt length field turning into a huge allocation.
  static constexpr uint16_t MAX_PDU_LENGTH = 260;

  std::string host_;
  uint16_t port_;
  int sockfd_;
  int timeout_ms_;
  std::atomic<uint16_t> transaction_id_;
  mutable std::mutex mutex_;

  // -------------------------------------------------------------------------
  // Connection
  // -------------------------------------------------------------------------

  void disconnectLocked() {
    if (sockfd_ >= 0) {
      ::close(sockfd_);
      sockfd_ = -1;
    }
  }

  bool connectLocked(int timeout_seconds = 5) {
    disconnectLocked();

    sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
      std::cerr << "Socket open error: " << std::strerror(errno) << std::endl;
      return false;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
      std::cerr << "Invalid host address: " << host_ << std::endl;
      disconnectLocked();
      return false;
    }

    // Non-blocking connect with an explicit deadline: a blocking connect() to
    // an unreachable host sits in the kernel SYN retry for about two minutes.
    const int flags = ::fcntl(sockfd_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK) < 0) {
      std::cerr << "fcntl error: " << std::strerror(errno) << std::endl;
      disconnectLocked();
      return false;
    }

    int rc = ::connect(sockfd_, reinterpret_cast<struct sockaddr*>(&addr),
                       sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
      std::cerr << "Connection error: " << std::strerror(errno) << std::endl;
      disconnectLocked();
      return false;
    }
    if (rc < 0) {
      struct pollfd pfd;
      pfd.fd = sockfd_;
      pfd.events = POLLOUT;
      const int ret = ::poll(&pfd, 1, timeout_seconds * 1000);
      if (ret <= 0) {
        std::cerr << "Connection to " << host_ << ":" << port_
                  << (ret == 0 ? " timed out" : " failed in poll") << std::endl;
        disconnectLocked();
        return false;
      }
      int soerr = 0;
      socklen_t len = sizeof(soerr);
      if (::getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &soerr, &len) < 0 ||
          soerr != 0) {
        std::cerr << "Connection error: "
                  << std::strerror(soerr != 0 ? soerr : errno) << std::endl;
        disconnectLocked();
        return false;
      }
    }
    if (::fcntl(sockfd_, F_SETFL, flags) < 0) {
      std::cerr << "fcntl restore error: " << std::strerror(errno) << std::endl;
      disconnectLocked();
      return false;
    }

    const int nodelay = 1;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    std::cout << "Connected to " << host_ << ":" << port_ << std::endl;
    return true;
  }

  // -------------------------------------------------------------------------
  // Low-level I/O
  // -------------------------------------------------------------------------

  bool sendAllLocked(const uint8_t* data, std::size_t len, int timeout_ms) {
    if (sockfd_ < 0) return false;
    std::size_t sent = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (sent < len) {
      const int remaining = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              deadline - std::chrono::steady_clock::now()).count());
      if (remaining <= 0) return false;

      struct pollfd pfd;
      pfd.fd = sockfd_;
      pfd.events = POLLOUT;
      const int ret = ::poll(&pfd, 1, remaining);
      if (ret < 0) {
        if (errno == EINTR) continue;
        return false;
      }
      if (ret == 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        return false;
      }

      const ssize_t n = ::send(sockfd_, data + sent, len - sent, MSG_NOSIGNAL);
      if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
        return false;
      }
      if (n == 0) return false;
      sent += static_cast<std::size_t>(n);
    }
    return true;
  }

  // Accumulates until `len` bytes have arrived; a short read is normal on TCP.
  bool recvAllLocked(uint8_t* data, std::size_t len, int timeout_ms) {
    if (sockfd_ < 0) return false;
    std::size_t received = 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (received < len) {
      const int remaining = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              deadline - std::chrono::steady_clock::now()).count());
      if (remaining <= 0) return false;

      struct pollfd pfd;
      pfd.fd = sockfd_;
      pfd.events = POLLIN;
      const int ret = ::poll(&pfd, 1, remaining);
      if (ret < 0) {
        if (errno == EINTR) continue;
        return false;
      }
      if (ret == 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        return false;
      }

      const ssize_t n = ::recv(sockfd_, data + received, len - received, 0);
      if (n < 0) {
        if (errno == EINTR) continue;
        return false;
      }
      if (n == 0) return false;  // peer closed
      received += static_cast<std::size_t>(n);
    }
    return true;
  }

  void drainLocked() {
    if (sockfd_ < 0) return;
    uint8_t scratch[256];
    std::size_t dropped = 0;
    while (true) {
      const ssize_t n = ::recv(sockfd_, scratch, sizeof(scratch), MSG_DONTWAIT);
      if (n <= 0) break;
      dropped += static_cast<std::size_t>(n);
      if (dropped > 64 * 1024) break;
    }
    if (dropped > 0) {
      std::cerr << "Discarded " << dropped
                << " stale bytes before request (stream was desynced)"
                << std::endl;
    }
  }

  // -------------------------------------------------------------------------
  // Transaction
  // -------------------------------------------------------------------------

  // Sends one PDU and returns the response PDU (function code first).
  // Throws on timeout, framing error, transaction-id mismatch or a Modbus
  // exception response.
  std::vector<uint8_t> transact(const char* what,
                                const std::vector<uint8_t>& pdu,
                                uint8_t expected_fc,
                                unsigned int timeout_ms) {
    const int tmo = timeout_ms > 0 ? static_cast<int>(timeout_ms) : timeout_ms_;
    std::lock_guard<std::mutex> lock(mutex_);

    if (sockfd_ < 0) {
      throw std::runtime_error(std::string(what) + ": not connected");
    }

    // A reply left over from a previously timed-out transaction would otherwise
    // be read as the answer to this one.
    drainLocked();

    const uint16_t txn = transaction_id_++;
    const std::size_t length_field = pdu.size() + 1;  // unit id + PDU

    std::vector<uint8_t> request;
    request.reserve(MBAP_SIZE + pdu.size());
    request.push_back(static_cast<uint8_t>(txn >> 8));
    request.push_back(static_cast<uint8_t>(txn & 0xFF));
    request.push_back(0x00);
    request.push_back(0x00);
    request.push_back(static_cast<uint8_t>(length_field >> 8));
    request.push_back(static_cast<uint8_t>(length_field & 0xFF));
    request.push_back(UNIT_ID);
    request.insert(request.end(), pdu.begin(), pdu.end());

    if (!sendAllLocked(request.data(), request.size(), tmo)) {
      disconnectLocked();
      throw std::runtime_error(std::string(what) + ": send failed");
    }

    uint8_t mbap[MBAP_SIZE];
    if (!recvAllLocked(mbap, MBAP_SIZE, tmo)) {
      throw std::runtime_error(std::string(what) + ": header read timeout");
    }

    const uint16_t rx_txn = static_cast<uint16_t>((mbap[0] << 8) | mbap[1]);
    const uint16_t protocol = static_cast<uint16_t>((mbap[2] << 8) | mbap[3]);
    const uint16_t rx_length = static_cast<uint16_t>((mbap[4] << 8) | mbap[5]);

    if (protocol != 0x0000) {
      disconnectLocked();
      throw std::runtime_error(std::string(what) + ": invalid protocol id");
    }
    // rx_length covers the unit id plus the PDU, so it must be at least 2
    // (unit + function code).
    if (rx_length < 2 || rx_length > MAX_PDU_LENGTH) {
      disconnectLocked();
      throw std::runtime_error(std::string(what) + ": invalid length field " +
                               std::to_string(rx_length));
    }

    std::vector<uint8_t> body(static_cast<std::size_t>(rx_length) - 1);
    if (!recvAllLocked(body.data(), body.size(), tmo)) {
      throw std::runtime_error(std::string(what) + ": body read timeout");
    }

    if (rx_txn != txn) {
      throw std::runtime_error(std::string(what) +
                               ": transaction id mismatch (expected " +
                               std::to_string(txn) + ", got " +
                               std::to_string(rx_txn) + ")");
    }

    const uint8_t fc = body[0];
    if (fc == static_cast<uint8_t>(expected_fc | 0x80)) {
      // Exception response: unit(1) fc(1) code(1), i.e. rx_length == 3.
      handleModbusException(body.size() > 1 ? body[1] : 0x00);
    }
    if (fc != expected_fc) {
      throw std::runtime_error(std::string(what) + ": unexpected function code " +
                               std::to_string(static_cast<int>(fc)));
    }
    return body;
  }

  std::vector<int16_t> readRegisters(uint8_t fc, uint16_t start_address,
                                    uint16_t quantity,
                                    unsigned int timeout_ms) {
    if (quantity == 0 || quantity > 125) {
      throw std::runtime_error("readRegisters: quantity " +
                               std::to_string(quantity) + " out of range 1..125");
    }

    const std::vector<uint8_t> pdu = {
        fc,
        static_cast<uint8_t>(start_address >> 8),
        static_cast<uint8_t>(start_address & 0xFF),
        static_cast<uint8_t>(quantity >> 8),
        static_cast<uint8_t>(quantity & 0xFF)};

    const std::vector<uint8_t> body =
        transact("readRegisters", pdu, fc, timeout_ms);

    // body: fc(1) byte_count(1) data(2N)
    if (body.size() < 2) {
      throw std::runtime_error("readRegisters: truncated response");
    }
    const uint8_t byte_count = body[1];
    if (byte_count != quantity * 2 ||
        body.size() < static_cast<std::size_t>(2) + byte_count) {
      throw std::runtime_error(
          "readRegisters: byte count " + std::to_string(byte_count) +
          " does not match the requested " + std::to_string(quantity * 2) +
          " bytes");
    }

    std::vector<int16_t> registers;
    registers.reserve(quantity);
    for (std::size_t i = 0; i < quantity; ++i) {
      registers.push_back(static_cast<int16_t>(
          (static_cast<uint16_t>(body[2 + i * 2]) << 8) | body[3 + i * 2]));
    }
    return registers;
  }

  void handleModbusException(uint8_t exception_code) {
    std::string error;
    switch (exception_code) {
      case 0x01: error = "Illegal function"; break;
      case 0x02: error = "Illegal data address"; break;
      case 0x03: error = "Illegal data value"; break;
      case 0x04: error = "Slave device failure"; break;
      case 0x05: error = "Acknowledge"; break;
      case 0x06: error = "Slave device busy"; break;
      case 0x08: error = "Memory parity error"; break;
      case 0x0A: error = "Gateway path unavailable"; break;
      case 0x0B: error = "Gateway target device failed to respond"; break;
      default:   error = "Unknown exception code"; break;
    }
    throw std::runtime_error("Modbus exception 0x" +
                             std::to_string(static_cast<int>(exception_code)) +
                             ": " + error);
  }
};
