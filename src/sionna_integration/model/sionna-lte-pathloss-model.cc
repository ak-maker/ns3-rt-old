/* sionna-lte-pathloss-model.cc
 *
 * 实现前面声明的模型
 */

#include "sionna-lte-pathloss-model.h"
#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/mobility-model.h"
#include "ns3/simulator.h"
#include <cmath>

// ★ 需要包含 sionna-helper.h，然后调用 SionnaHelper::GetPathLossFromSionna()
#include "sionna-helper.h"

// ★ 需要包含 spectrum-signal-parameters.h 来访问 params->txPsd
#include "ns3/spectrum-signal-parameters.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("SionnaLtePathlossModel");
NS_OBJECT_ENSURE_REGISTERED (SionnaLtePathlossModel);

TypeId
SionnaLtePathlossModel::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SionnaLtePathlossModel")
    .SetParent<SpectrumPropagationLossModel> ()
    .SetGroupName ("Lte")
    .AddConstructor<SionnaLtePathlossModel> ();
  return tid;
}

SionnaLtePathlossModel::SionnaLtePathlossModel ()
{
  NS_LOG_FUNCTION (this);
}

SionnaLtePathlossModel::~SionnaLtePathlossModel ()
{
  NS_LOG_FUNCTION (this);
}

Ptr<SpectrumValue>
SionnaLtePathlossModel::DoCalcRxPowerSpectralDensity(
    Ptr<const SpectrumSignalParameters> params,
    Ptr<const MobilityModel> aMob,
    Ptr<const MobilityModel> bMob
) const
{
  NS_LOG_FUNCTION (this << params << aMob << bMob);

  // 1) 发射机 & 接收机位置
  Vector posA = aMob->GetPosition ();
  Vector posB = bMob->GetPosition ();

  // 2) 计算 pathloss(dB)
  double pathLossDb = GetPathLossFromSionna (posA, posB);
  double pathLossLin = std::pow(10.0, -pathLossDb/10.0);

  // 3) 从 params->txPsd 取得Tx功率谱
  //    SpectrumSignalParameters 里还可能有其他字段: txAntenna, duration等
  Ptr<const SpectrumValue> txPsd = params->txPsd;
  if (!txPsd)
    {
      NS_LOG_WARN("No txPsd in SpectrumSignalParameters, returning null");
      return Create<SpectrumValue> ();
    }

  // 4) 复制 Tx PSD 并乘以衰减
  Ptr<SpectrumValue> rxPsd = Copy<SpectrumValue>(txPsd);
  *rxPsd *= pathLossLin;

  // 5) 返回衰减后的 PSD
  return rxPsd;
}

double
SionnaLtePathlossModel::GetPathLossFromSionna(const Vector &txPos, const Vector &rxPos) const
{
  // 这里就是你原先写的SionnaHelper::GetPathLossFromSionna(...)
  double plDb = SionnaHelper::GetPathLossFromSionna(txPos, rxPos);
  NS_LOG_DEBUG("Sionna-lte-pathloss: posA(" << txPos.x << "," << txPos.y << ")"
               << " -> posB(" << rxPos.x << "," << rxPos.y << ") => "
               << plDb << " dB");
  return plDb;
}

} // namespace ns3
