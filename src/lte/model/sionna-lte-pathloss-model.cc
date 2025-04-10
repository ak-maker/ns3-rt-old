#include "sionna-lte-pathloss-model.h"
#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/mobility-model.h"
#include "ns3/simulator.h"
#include "ns3/spectrum-signal-parameters.h"

// your helper to talk to Python
#include "sionna-helper.h"

#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SionnaLtePathlossModel");
NS_OBJECT_ENSURE_REGISTERED(SionnaLtePathlossModel);

TypeId
SionnaLtePathlossModel::GetTypeId()
{
  static TypeId tid = TypeId("ns3::SionnaLtePathlossModel")
    .SetParent<SpectrumPropagationLossModel>()
    .SetGroupName("Spectrum")
    .AddConstructor<SionnaLtePathlossModel>();
  return tid;
}

SionnaLtePathlossModel::SionnaLtePathlossModel()
{
  NS_LOG_FUNCTION(this);
}

SionnaLtePathlossModel::~SionnaLtePathlossModel()
{
  NS_LOG_FUNCTION(this);
}

Ptr<SpectrumValue>
SionnaLtePathlossModel::DoCalcRxPowerSpectralDensity(
    Ptr<const SpectrumSignalParameters> params,
    Ptr<const MobilityModel> aMob,
    Ptr<const MobilityModel> bMob) const
{
  // (1) 获取发射接收位置
  Vector posA = aMob->GetPosition();
  Vector posB = bMob->GetPosition();

  // (2) 调用 Sionna 来算 pathLoss (dB)
  double pathLossDb = GetPathLossFromSionna(posA, posB);
  double pathLossLin = std::pow(10.0, -pathLossDb / 10.0);

  // (3) 复制原先 PSD => 保留 SpectrumModel
  Ptr<const SpectrumValue> txPsd = params->psd;
  Ptr<SpectrumValue> rxPsd = Copy<SpectrumValue>(txPsd);

  // (4) 乘以线性衰减
  (*rxPsd) *= pathLossLin;

  // (5) 返回
  return rxPsd;
}

double
SionnaLtePathlossModel::GetPathLossFromSionna(const Vector &txPos, const Vector &rxPos) const
{
  // 这里通过 sionna-helper 跟 Python 端通信
  double plDb = SionnaHelper::GetPathLossFromSionna(txPos, rxPos);
  NS_LOG_DEBUG("Sionna-lte-pathloss: posA(" << txPos.x << "," << txPos.y << ") -> ("
               << rxPos.x << "," << rxPos.y << ") = " << plDb << " dB");
  return plDb;
}

} // namespace ns3
