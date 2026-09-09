#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstdlib>
#include <functional>
#include <iostream>

#include "radio/hamlib_rigctld_client.hpp"

namespace {

bool waitFor(const std::function<bool()>& predicate,
             const int timeout_ms = 2000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return predicate();
}

void require(const bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

class FakeRigctld final : public QObject {
 public:
  explicit FakeRigctld(QObject* parent = nullptr) : QObject(parent) {
    QObject::connect(&server_, &QTcpServer::newConnection, this, [this] {
      socket_ = server_.nextPendingConnection();
      QObject::connect(socket_, &QTcpSocket::readyRead, this, [this] {
        buffer_.append(socket_->readAll());
        qsizetype newline = -1;
        while ((newline = buffer_.indexOf('\n')) >= 0) {
          const QByteArray line = buffer_.left(newline).trimmed();
          buffer_.remove(0, newline + 1);
          commands.push_back(line);
          reply(line);
        }
      });
    });
    require(server_.listen(QHostAddress::LocalHost, 0),
            "fake rigctld did not listen");
  }

  quint16 port() const { return server_.serverPort(); }
  QList<QByteArray> commands;
  bool malformed_frequency{false};
  bool vfo_mode_enabled{true};

 private:
  void reply(const QByteArray& line) {
    const QByteArray op = line.left(1);
    if (line == "\\chk_vfo")
      socket_->write(vfo_mode_enabled ? "1\n" : "0\n");
    else if (op == "f")
      socket_->write(malformed_frequency ? "invalid\n" : "14015000\n");
    else if (op == "i") socket_->write("14018000\n");
    else if (op == "m") socket_->write("USB\n2400\n");
    else if (op == "x") socket_->write("CW\n500\n");
    else if (op == "s") socket_->write("1\nVFOB\n");
    else socket_->write("RPRT 0\n");
    socket_->flush();
  }

  QTcpServer server_;
  QTcpSocket* socket_{nullptr};
  QByteArray buffer_;
};

cwassistant::desktop::HamlibRigctldClient::Configuration configuration(
    const quint16 port, const bool writable = false) {
  return {.host = QStringLiteral("127.0.0.1"),
          .port = port,
          .rx_vfo = QStringLiteral("VFOA"),
          .tx_vfo = QStringLiteral("VFOB"),
          .writable = writable,
          .vfo_mode = true,
          .poll_interval_ms = 100,
          .request_timeout_ms = 500,
          .reconnect_interval_ms = 100};
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  using cwassistant::core::RadioMode;
  using cwassistant::core::RadioObservation;
  using cwassistant::core::RadioSplit;
  using cwassistant::desktop::HamlibRigctldClient;

  {
    FakeRigctld server;
    HamlibRigctldClient client;
    client.connectToServer(configuration(server.port()));
    require(waitFor([&] { return client.connected(); }),
            "read-only client did not publish a complete poll");
    const auto state = client.radioState();
    require(state.availability == RadioObservation::Known &&
                state.rx_frequency.hz == 14'015'000 &&
                state.tx_frequency.hz == 14'018'000 &&
                state.rx_mode.mode == RadioMode::UpperSideband &&
                state.tx_mode.mode == RadioMode::Cw &&
                state.split.split == RadioSplit::Enabled &&
                state.tx_vfo.identifier == "VFOB",
            "complete Hamlib state was parsed incorrectly");
    require(!client.canWrite() && !client.setRxFrequency(14'020'000),
            "read-only client accepted a write");
    require(server.commands.mid(0, 6) ==
                QList<QByteArray>{"\\chk_vfo", "f VFOA", "i VFOA",
                                  "m VFOA", "x VFOA", "s VFOA"},
            "poll did not target the configured VFOs exactly");
  }

  {
    FakeRigctld server;
    HamlibRigctldClient client;
    client.connectToServer(configuration(server.port(), true));
    require(waitFor([&] { return client.canWrite(); }),
            "writable client did not become ready");
    require(client.setRxFrequency(14'020'000) &&
                client.setTxFrequency(14'023'000) &&
                client.setRxMode(RadioMode::CwReverse) &&
                client.setTxMode(RadioMode::Cw) && client.setSplit(true),
            "valid Hamlib writes were rejected");
    require(waitFor([&] {
              return server.commands.contains("F VFOA 14020000") &&
                     server.commands.contains("I VFOA 14023000") &&
                     server.commands.contains("M VFOA CWR 0") &&
                     server.commands.contains("X VFOA CW 0") &&
                     server.commands.contains("S VFOA 1 VFOB");
            }),
            "writes did not preserve exact RX/TX VFO targeting");
    for (const QByteArray& command : server.commands) {
      require(!command.startsWith("T") && !command.startsWith("\\set_ptt"),
              "Hamlib adapter emitted a prohibited PTT command");
    }
  }

  {
    FakeRigctld server;
    server.malformed_frequency = true;
    HamlibRigctldClient client;
    client.connectToServer(configuration(server.port()));
    require(waitFor([&] {
              return client.statusText().contains(
                  QStringLiteral("invalid radio-state"));
            }),
            "malformed state did not fail closed");
    require(client.radioState().availability == RadioObservation::Unavailable,
            "malformed state remained authoritative");
  }

  {
    FakeRigctld server;
    server.vfo_mode_enabled = false;
    HamlibRigctldClient client;
    client.connectToServer(configuration(server.port()));
    require(waitFor([&] {
              return client.statusText().contains(QStringLiteral("--vfo"));
            }),
            "non-VFO daemon did not fail closed");
    require(client.radioState().availability == RadioObservation::Unavailable,
            "non-VFO daemon exposed authoritative state");
  }

  {
    HamlibRigctldClient client;
    auto remote = configuration(4532);
    remote.host = QStringLiteral("192.0.2.1");
    client.connectToServer(remote);
    require(client.statusText().contains(QStringLiteral("Invalid Hamlib")) &&
                !client.connected(),
            "unencrypted non-loopback rigctld endpoint was accepted");
  }

  std::cout << "hamlib rigctld client tests passed\n";
  return 0;
}
