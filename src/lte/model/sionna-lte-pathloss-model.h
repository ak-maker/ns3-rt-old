#ifndef SIONNA_LTE_PATHLOSS_MODEL_H
#define SIONNA_LTE_PATHLOSS_MODEL_H

#include "ns3/spectrum-propagation-loss-model.h"
#include "ns3/vector.h"

namespace ns3 {

/**
 * \brief A custom SpectrumPropagationLossModel that queries Sionna for pathloss (dB).
 */
class SionnaLtePathlossModel : public SpectrumPropagationLossModel
{
public:
  static TypeId GetTypeId();

  SionnaLtePathlossModel();
  virtual ~SionnaLtePathlossModel();

protected:
  // override the parent's pure virtual function
  virtual Ptr<SpectrumValue> DoCalcRxPowerSpectralDensity(
      Ptr<const SpectrumSignalParameters> params,
      Ptr<const MobilityModel> aMob,
      Ptr<const MobilityModel> bMob) const override;

private:
  double GetPathLossFromSionna(const Vector &txPos, const Vector &rxPos) const;
};

} // namespace ns3

#endif
