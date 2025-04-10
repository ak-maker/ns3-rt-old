/*  sionna-helper.cc
 *
 *  这里实现跟 Python 端的 UDP 通信逻辑
 *  假设 Python 端在  (m_serverIp, m_serverPort) = ("127.0.0.1", 8103) 监听
 */

#include "sionna-helper.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/system-thread.h"

#include <sys/socket.h>   // socket(), sendto(), recvfrom(), etc. (POSIX)
#include <arpa/inet.h>    // sockaddr_in
#include <unistd.h>       // close()
#include <cstring>        // memset(), strlen(), etc.
#include <cstdio>         // sprintf

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SionnaHelper");

// 初始化静态变量
std::string SionnaHelper::m_serverIp   = "127.0.0.1";
int         SionnaHelper::m_serverPort = 8103;

////////////////////////////////////////////////////////////
static int CreateUdpSocket(const std::string &ip, int port)
{
  // 创建 UDP 套接字
  int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    NS_LOG_ERROR("SionnaHelper: Failed to create socket");
    return -1;
  }

  // 服务器地址
  struct sockaddr_in servAddr;
  memset(&servAddr, 0, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  servAddr.sin_port   = htons(port);
  if (inet_pton(AF_INET, ip.c_str(), &servAddr.sin_addr) <= 0) {
    NS_LOG_ERROR("SionnaHelper: invalid IP address " << ip);
    ::close(sock);
    return -1;
  }

  // 不绑定客户端本地端口，让系统自动分配
  // (若你要手动bind可以再改)

  // 连接其实对UDP不是必须，但有时可以简化send/recv
  // 这里不执行 connect()，后面用 sendto/recvfrom

  return sock;
}

static bool UdpSendAndRecv(int sock, const std::string &ip, int port,
                           const std::string &msg,
                           std::string &response,
                           double timeoutSec = 2.0)
{
  // server地址
  struct sockaddr_in servAddr;
  memset(&servAddr, 0, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  servAddr.sin_port   = htons(port);
  inet_pton(AF_INET, ip.c_str(), &servAddr.sin_addr);

  // 发送
  ssize_t sent = ::sendto(sock, msg.c_str(), msg.size(), 0,
                          (struct sockaddr*)&servAddr, sizeof(servAddr));
  if (sent < 0) {
    NS_LOG_ERROR("SionnaHelper: sendto() failed for msg=" << msg);
    return false;
  }

  // 设置一个阻塞接收，带超时
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(sock, &fds);

  struct timeval tv;
  tv.tv_sec  = (int)timeoutSec;
  tv.tv_usec = (int)((timeoutSec - (int)timeoutSec)*1000000);

  int ret = select(sock+1, &fds, NULL, NULL, &tv);
  if (ret <= 0) { // 0=timeout, -1=error
    NS_LOG_ERROR("SionnaHelper: recv select timeout or error. msg=" << msg);
    return false;
  }

  // 接收
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  struct sockaddr_in fromAddr;
  socklen_t fromLen = sizeof(fromAddr);
  ssize_t recvd = ::recvfrom(sock, buf, sizeof(buf)-1, 0,
                             (struct sockaddr*)&fromAddr, &fromLen);
  if (recvd < 0) {
    NS_LOG_ERROR("SionnaHelper: recvfrom() failed. msg=" << msg);
    return false;
  }
  buf[recvd] = '\0';
  response = buf;
  //NS_LOG_INFO("SionnaHelper: got response=" << response);
  return true;
}
////////////////////////////////////////////////////////////

void
SionnaHelper::UpdateLocationInSionna(const std::string &objName,
                                     const Vector &pos,
                                     const Vector &vel)
{
  // 这里随意把 angle 设置为0，或者根据vel算个朝向
  // 对应 python 里 manage_location_message() 的解析
  double angle = 0.0;
  if (vel.x != 0.0 || vel.y != 0.0) {
    angle = std::atan2(vel.y, vel.x)*180.0/M_PI;
  }

  // 拼接 LOC_UPDATE 消息
  // e.g. "LOC_UPDATE:obj3, 200.000000, 10.000000, 1.500000, 30.0"
  char msg[128];
  sprintf(msg, "LOC_UPDATE:%s,%.6f,%.6f,%.6f,%.6f",
          objName.c_str(), pos.x, pos.y, pos.z, angle);

  // 打开socket -> send -> recv -> 关闭 (最简单的做法)
  int sock = CreateUdpSocket(m_serverIp, m_serverPort);
  if (sock < 0) return;
  std::string response;
  if (!UdpSendAndRecv(sock, m_serverIp, m_serverPort, msg, response)) {
    NS_LOG_WARN("SionnaHelper: UpdateLocationInSionna no response for " << msg);
  } else {
    NS_LOG_INFO("SionnaHelper: got ack " << response);
  }
  ::close(sock);
}

double
SionnaHelper::GetPathLossFromSionna(const Vector &txPos, const Vector &rxPos)
{
  // 为了跟 python 对接，我们需要把 txPos/rxPos 对应 "objX" "objY"
  // 但这里没有 NodeId -> "objId" 的映射，这里做个简化：把 txPos, rxPos 通过 "origin" or something
  // 你也可以在本地保存一张表 (Node->objId)
  // 这里只是演示；真正精确做法需先 "SendLocUpdateToSionna" 设置位置，再 "CALC_REQUEST_PATHGAIN:objA,objB"

  // 这里偷个懒：假设 Python 端 scene.xml 里 eNB= obj0, UE= obj1,2,3...
  // 但你没有 nodeId => objId.
  // **若你想严格匹配，你要在 node 安装时就 "SendLocUpdateToSionna" 并记下 NodeId->"objId"。**
  // 这里只演示 "CALC_REQUEST_PATHGAIN: origin, origin" 也行，Python 里会返回 0
  // 其实你肯定要更细地写.

  // ------- 重要：如果想真正关联pos到Python，最好的方法： -------
  //  1) 在每次位置变化时: UpdateLocationInSionna("objX", pos, vel)
  //  2) 这里 只发送  "CALC_REQUEST_PATHGAIN:objX,objY"
  //  python 脚本就能根据 sionna_structure["sionna_location_db"] 里的坐标 进行射线追踪
  //
  // 为简化，这里直接把传进来的 pos 用 "obj999" 之类临时插入 Python 端 -> 再算...
  // 下面给你一个“完整做法”：假设 tx 用 obj0, rx 用 obj1 ...
  // -----------------------------------------------------

  // 先把 txPos 写到 "obj0"
  {
    char objName[32];
    sprintf(objName, "obj%d", 0);
    // 先发 LOC_UPDATE
    int sock1 = CreateUdpSocket(m_serverIp, m_serverPort);
    if (sock1 >= 0) {
      char msg[128];
      // angle=0
      sprintf(msg, "LOC_UPDATE:%s,%.6f,%.6f,%.6f,%.6f", objName, txPos.x, txPos.y, txPos.z, 0.0);
      std::string resp;
      UdpSendAndRecv(sock1, m_serverIp, m_serverPort, msg, resp);
      ::close(sock1);
    }
  }

  // 再把 rxPos 写到 "obj1"
  {
    char objName[32];
    sprintf(objName, "obj%d", 1);
    // 先发 LOC_UPDATE
    int sock2 = CreateUdpSocket(m_serverIp, m_serverPort);
    if (sock2 >= 0) {
      char msg[128];
      // angle=0
      sprintf(msg, "LOC_UPDATE:%s,%.6f,%.6f,%.6f,%.6f", objName, rxPos.x, rxPos.y, rxPos.z, 0.0);
      std::string resp;
      UdpSendAndRecv(sock2, m_serverIp, m_serverPort, msg, resp);
      ::close(sock2);
    }
  }

  // 现在 python 端 scene 里 "car_0" & "car_1"（对应 obj0, obj1）位置都更新
  // 然后发 "CALC_REQUEST_PATHGAIN:obj0,obj1"
  double pathLossDb = 300.0; // a large default
  {
    int sock3 = CreateUdpSocket(m_serverIp, m_serverPort);
    if (sock3 < 0)
      return pathLossDb;

    char msg[64];
    sprintf(msg, "CALC_REQUEST_PATHGAIN:obj%d,obj%d", 0, 1);
    std::string resp;
    bool ok = UdpSendAndRecv(sock3, m_serverIp, m_serverPort, msg, resp);
    ::close(sock3);

    if (ok && resp.find("CALC_DONE_PATHGAIN:") == 0) {
      // 形如 "CALC_DONE_PATHGAIN:83.2764"
      std::string valStr = resp.substr(strlen("CALC_DONE_PATHGAIN:"));
      try {
        pathLossDb = std::stod(valStr);
      } catch(...) {
        NS_LOG_ERROR("SionnaHelper: parse pathLossDb error: " << valStr);
      }
    } else {
      NS_LOG_ERROR("SionnaHelper: invalid pathgain response: " << resp);
    }
  }

  return pathLossDb;
}

void
SionnaHelper::ShutdownSionna()
{
  // 发送 SHUTDOWN_SIONNA，让 python 脚本退出
  int sock = CreateUdpSocket(m_serverIp, m_serverPort);
  if (sock < 0) return;
  std::string resp;
  UdpSendAndRecv(sock, m_serverIp, m_serverPort, "SHUTDOWN_SIONNA", resp);
  ::close(sock);

  NS_LOG_INFO("SionnaHelper: asked python server to shutdown");
}

} // namespace ns3
