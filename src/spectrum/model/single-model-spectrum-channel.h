///*
// * Copyright (c) 2009 CTTC
// *
// * This program is free software; you can redistribute it and/or modify
// * it under the terms of the GNU General Public License version 2 as
// * published by the Free Software Foundation;
// *
// * This program is distributed in the hope that it will be useful,
// * but WITHOUT ANY WARRANTY; without even the implied warranty of
// * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// * GNU General Public License for more details.
// *
// * You should have received a copy of the GNU General Public License
// * along with this program; if not, write to the Free Software
// * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// *
// * Author: Nicola Baldo <nbaldo@cttc.es>
// */
//
//#ifndef SINGLE_MODEL_SPECTRUM_CHANNEL_H
//#define SINGLE_MODEL_SPECTRUM_CHANNEL_H
//
//#include "spectrum-channel.h"
//#include "spectrum-model.h"
//
//#include <ns3/traced-callback.h>
//
//namespace ns3
//{
//
///**
// * \ingroup spectrum
// *
// * \brief SpectrumChannel implementation which handles a single spectrum model
// *
// * All SpectrumPhy layers attached to this SpectrumChannel
// */
//class SingleModelSpectrumChannel : public SpectrumChannel
//{
//  public:
//    SingleModelSpectrumChannel();
//
//    /**
//     * \brief Get the type ID.
//     * \return the object TypeId
//     */
//    static TypeId GetTypeId();
//
//    // inherited from SpectrumChannel
//    void RemoveRx(Ptr<SpectrumPhy> phy) override;
//    void AddRx(Ptr<SpectrumPhy> phy) override;
//    void StartTx(Ptr<SpectrumSignalParameters> params) override;
//
//    // inherited from Channel
//    std::size_t GetNDevices() const override;
//    Ptr<NetDevice> GetDevice(std::size_t i) const override;
//
//    /// Container: SpectrumPhy objects
//    typedef std::vector<Ptr<SpectrumPhy>> PhyList;
//
//  private:
//    void DoDispose() override;
//
//    /**
//     * Used internally to reschedule transmission after the propagation delay.
//     *
//     * \param params
//     * \param receiver
//     */
//    void StartRx(Ptr<SpectrumSignalParameters> params, Ptr<SpectrumPhy> receiver);
//
//    /**
//     * List of SpectrumPhy instances attached to the channel.
//     */
//    PhyList m_phyList;
//
//    /**
//     * SpectrumModel that this channel instance is supporting.
//     */
//    Ptr<const SpectrumModel> m_spectrumModel;
//};
//
//} // namespace ns3
//
//#endif /* SINGLE_MODEL_SPECTRUM_CHANNEL_H */
#ifndef SINGLE_MODEL_SPECTRUM_CHANNEL_H
#define SINGLE_MODEL_SPECTRUM_CHANNEL_H

#include "spectrum-channel.h"
#include "spectrum-model.h"

// 需要包含
#include "spectrum-propagation-loss-model.h"

#include <ns3/traced-callback.h>
#include <ns3/ptr.h>

namespace ns3 {

/**
 * \ingroup spectrum
 *
 * \brief SpectrumChannel implementation which handles a single spectrum model
 */
class SingleModelSpectrumChannel : public SpectrumChannel
{
public:
  SingleModelSpectrumChannel();
  static TypeId GetTypeId();

  // inherited from SpectrumChannel
  void RemoveRx(Ptr<SpectrumPhy> phy) override;
  void AddRx(Ptr<SpectrumPhy> phy) override;
  void StartTx(Ptr<SpectrumSignalParameters> params) override;

  // inherited from Channel
  std::size_t GetNDevices() const override;
  Ptr<NetDevice> GetDevice(std::size_t i) const override;

private:
  void DoDispose() override;
  void StartRx(Ptr<SpectrumSignalParameters> params, Ptr<SpectrumPhy> receiver);

  typedef std::vector<Ptr<SpectrumPhy>> PhyList;
  PhyList m_phyList;
  Ptr<const SpectrumModel> m_spectrumModel;

  // 新增: 用来存储衰落模型指针
  Ptr<SpectrumPropagationLossModel> m_spectrumLoss;
};

} // namespace ns3

#endif /* SINGLE_MODEL_SPECTRUM_CHANNEL_H */
