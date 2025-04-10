/*  sionna-helper.h
 *
 *  声明一些辅助函数，用于给 Sionna 服务器发送/接收消息：
 *   - updateLocationInSionna(...) 发送 LOC_UPDATE:...
 *   - getPathLossFromSionna(...)  发送 CALC_REQUEST_PATHGAIN:... 并返回 dB
 *   - 如果需要也可以加 getDelayFromSionna(...) 等
 *
 *  要点：
 *    - 这里写最简单的 UDP 通信，真正的 Socket 连接可以另行封装。
 *    - 用 static 函数 方便直接调用。
 */

#ifndef SIONNA_HELPER_H
#define SIONNA_HELPER_H

#include "ns3/vector.h"
#include <string>

namespace ns3 {

/**
 * \brief SionnaHelper 用于封装跟 Python 端的通信
 *
 * 这里提供静态函数，供其他地方直接用：
 *   - updateLocationInSionna(objId, pos, velocity)
 *   - getPathLossFromSionna(txPos, rxPos)
 *   - shutdownSionna()
 */
class SionnaHelper
{
public:
  /**
   * \brief 发送 "LOC_UPDATE:objX, x, y, z, angle" 到 Sionna 服务器
   */
  static void UpdateLocationInSionna (const std::string &objName,
                                      const Vector &pos,
                                      const Vector &vel);

  /**
   * \brief 发送 "CALC_REQUEST_PATHGAIN:objA,objB" 并等待 "CALC_DONE_PATHGAIN:xx"
   * \return 路径损耗(dB)，如 83.27
   */
  static double GetPathLossFromSionna (const Vector &txPos, const Vector &rxPos);

  /**
   * \brief 发送 "SHUTDOWN_SIONNA" 关闭 Python 端(可选)
   */
  static void ShutdownSionna ();

  // 你可以在此设置服务器 IP/端口，也可以写成 SetServerIp(...) / SetServerPort(...)
  // 这里为简单起见，直接写成 static 变量
  static std::string m_serverIp;     // e.g. "127.0.0.1"
  static int         m_serverPort;   // e.g. 8103
};

} // namespace ns3

#endif // SIONNA_HELPER_H
