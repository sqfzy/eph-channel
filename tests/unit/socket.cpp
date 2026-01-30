#include "eph/core/socket.hpp"
#include <gtest/gtest.h>
#include <netinet/tcp.h>

using namespace eph::imc;


class TcpSocketTest : public ::testing::Test {
protected:
  static constexpr uint16_t TEST_PORT = 19999;
  static constexpr const char *TEST_IP = "127.0.0.1";
};

TEST_F(TcpSocketTest, Creation) {
  Socket sock(SOCK_STREAM);

  EXPECT_TRUE(sock);
  EXPECT_GE(sock.fd(), 0);
}

TEST_F(TcpSocketTest, BindOperation) {
  Socket sock(SOCK_STREAM);

  EXPECT_NO_THROW({ sock.bind(TEST_IP, TEST_PORT); });
}

TEST_F(TcpSocketTest, SetSockOpt) {
  Socket sock(SOCK_STREAM);

  int reuse = 1;
  EXPECT_EQ(sock.set_opt(SOL_SOCKET, SO_REUSEADDR, reuse), 0);

  // 验证选项已设置
  int value = 0;
  socklen_t len = sizeof(value);
  getsockopt(sock.fd(), SOL_SOCKET, SO_REUSEADDR, &value, &len);
  EXPECT_EQ(value, 1);
}


class UdpSocketTest : public ::testing::Test {
protected:
  static constexpr uint16_t TEST_PORT = 20000;
  static constexpr const char *TEST_IP = "127.0.0.1";
};

TEST_F(UdpSocketTest, Creation) {
  Socket sock(SOCK_DGRAM);

  EXPECT_TRUE(sock);
  EXPECT_GE(sock.fd(), 0);
}

TEST_F(UdpSocketTest, BindAndConnect) {
  Socket receiver(SOCK_DGRAM);
  Socket sender(SOCK_DGRAM);

  // Receiver bind
  EXPECT_NO_THROW({ receiver.bind(TEST_IP, TEST_PORT); });

  // Sender connect
  EXPECT_NO_THROW({ sender.connect(TEST_IP, TEST_PORT); });
}


TEST(SocketMoveTest, MoveConstructor) {
  Socket sock1(SOCK_STREAM);
  int fd1 = sock1.fd();

  Socket sock2(std::move(sock1));

  EXPECT_FALSE(sock1); // 已移动
  EXPECT_TRUE(sock2);
  EXPECT_EQ(sock2.fd(), fd1);
}

TEST(SocketMoveTest, MoveAssignment) {
  Socket sock1(SOCK_STREAM);
  Socket sock2(SOCK_STREAM);

  int fd1 = sock1.fd();

  sock2 = std::move(sock1);

  EXPECT_FALSE(sock1);
  EXPECT_EQ(sock2.fd(), fd1);
}


TEST_F(UdpSocketTest, ActualCommunication) {
  Socket receiver(SOCK_DGRAM);
  Socket sender(SOCK_DGRAM);

  // 设置选项
  int reuse = 1;
  receiver.set_opt(SOL_SOCKET, SO_REUSEADDR, reuse);
  receiver.set_opt(SOL_SOCKET, SO_REUSEPORT, reuse);

  receiver.bind(TEST_IP, TEST_PORT);
  sender.connect(TEST_IP, TEST_PORT);

  // 发送数据
  const char *msg = "Hello, Socket!";
  ssize_t sent = sender.send(msg, strlen(msg) + 1);
  EXPECT_GT(sent, 0);

  // 接收数据
  char buffer[128] = {0};

  bool received = false;
  for (int i = 0; i < 100; ++i) {
    ssize_t n = receiver.recv(buffer, sizeof(buffer));
    if (n > 0) {
      received = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EXPECT_TRUE(received);
  EXPECT_STREQ(buffer, msg);
}


TEST_F(UdpSocketTest, BufferSizeConfiguration) {
  Socket sock(SOCK_DGRAM);

  int sndbuf = 262144; // 256KB
  int rcvbuf = 262144;

  EXPECT_EQ(sock.set_opt(SOL_SOCKET, SO_SNDBUF, sndbuf), 0);
  EXPECT_EQ(sock.set_opt(SOL_SOCKET, SO_RCVBUF, rcvbuf), 0);

  // 验证设置（内核可能调整大小）
  int actual_sndbuf = 0;
  socklen_t len = sizeof(actual_sndbuf);
  getsockopt(sock.fd(), SOL_SOCKET, SO_SNDBUF, &actual_sndbuf, &len);
  EXPECT_GE(actual_sndbuf, sndbuf / 2); // 允许内核调整
}


TEST(SocketCleanupTest, AutoClose) {
  int fd;
  {
    Socket sock(SOCK_STREAM);
    fd = sock.fd();
    EXPECT_GE(fd, 0);
  } // sock 析构

  // 验证 fd 已关闭（write 应该失败）
  char buf[1] = {0};
  EXPECT_EQ(::write(fd, buf, 1), -1);
  EXPECT_EQ(errno, EBADF);
}

TEST(SocketCleanupTest, ManualClose) {
  Socket sock(SOCK_STREAM);
  int fd = sock.fd();

  sock.close();

  EXPECT_FALSE(sock);
  EXPECT_EQ(sock.fd(), -1);

  // 再次关闭应该安全
  EXPECT_NO_THROW(sock.close());
}
