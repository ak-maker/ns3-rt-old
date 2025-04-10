/* sionna-lte-pathloss-model.h
 *
 * 自定义一个继承自 ns-3 的 SpectrumPropagationLossModel，
 * 让它在计算接收功率时主动调用 SionnaHelper::GetPathLossFromSionna() 取得 dB。
 */

#ifndef SIONNA_LTE_PATHLOSS_MODEL_H
#define SIONNA_LTE_PATHLOSS_MODEL_H

#include "ns3/spectrum-propagation-loss-model.h"
#include "ns3/vector.h"
#include "ns3/mobility-model.h"
#include "ns3/spectrum-signal-parameters.h" // ★ 需要这个以使用 SpectrumSignalParameters

namespace ns3 {

/**
 * \ingroup lte
 * \brief A custom SpectrumPropagationLossModel that queries Sionna for pathloss (dB).
 */
class SionnaLtePathlossModel : public SpectrumPropagationLossModel
{
public:
  static TypeId GetTypeId (void);

  SionnaLtePathlossModel ();
  virtual ~SionnaLtePathlossModel ();

protected:
  /**
   * \brief 覆盖父类纯虚函数：CalcRxPowerSpectralDensity时，会调用此方法
   *
   * 父类定义为：
   *  virtual Ptr<SpectrumValue> DoCalcRxPowerSpectralDensity(
   *      Ptr<const SpectrumSignalParameters> params,
   *      Ptr<const MobilityModel> a,
   *      Ptr<const MobilityModel> b
   *  ) const = 0;
   */
  virtual Ptr<SpectrumValue> DoCalcRxPowerSpectralDensity(
      Ptr<const SpectrumSignalParameters> params,  // ★ 改成父类的 SpectrumSignalParameters
      Ptr<const MobilityModel> aMob,
      Ptr<const MobilityModel> bMob
  ) const override;

private:
  double GetPathLossFromSionna (const Vector &txPos, const Vector &rxPos) const;
};

} // namespace ns3

#endif // SIONNA_LTE_PATHLOSS_MODEL_H
