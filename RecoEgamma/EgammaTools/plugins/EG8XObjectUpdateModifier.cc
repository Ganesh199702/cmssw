#include "CommonTools/CandAlgos/interface/ModifyObjectValueBase.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DataFormats/Common/interface/Handle.h"
#include "Geometry/CaloTopology/interface/CaloTopology.h"
#include "Geometry/Records/interface/CaloGeometryRecord.h"
#include "Geometry/CaloEventSetup/interface/CaloTopologyRecord.h"
#include "RecoEcal/EgammaCoreTools/interface/EcalClusterTools.h"
#include "DataFormats/EcalRecHit/interface/EcalRecHitCollections.h"
#include "DataFormats/EcalRecHit/interface/EcalRecHit.h"
#include "DataFormats/EcalDetId/interface/EcalSubdetector.h"

#include <vdt/vdtMath.h>

//this modifier fills variables where not present in CMSSW_8X
//use case is when reading older 80X samples in newer releases, aka legacy

class EG8XObjectUpdateModifier : public ModifyObjectValueBase {
public:
  EG8XObjectUpdateModifier(const edm::ParameterSet& conf, edm::ConsumesCollector& cc);
  ~EG8XObjectUpdateModifier() override {}

  void setEvent(const edm::Event&) final;
  void setEventContent(const edm::EventSetup&) final;

  void modifyObject(reco::GsfElectron& ele) const final;
  void modifyObject(reco::Photon& pho) const final;

  void modifyObject(pat::Electron& ele) const final { return modifyObject(static_cast<reco::GsfElectron&>(ele)); }
  void modifyObject(pat::Photon& pho) const final { return modifyObject(static_cast<reco::Photon&>(pho)); }

private:
  std::pair<int, bool> getSaturationInfo(const reco::SuperCluster& superClus) const;

  CaloTopology const* caloTopo_ = nullptr;
  EcalRecHitCollection const* ecalRecHitsEB_ = nullptr;
  EcalRecHitCollection const* ecalRecHitsEE_ = nullptr;

  edm::ESGetToken<CaloTopology, CaloTopologyRecord> caloTopoToken_;
  edm::EDGetTokenT<EcalRecHitCollection> ecalRecHitsEBToken_;
  edm::EDGetTokenT<EcalRecHitCollection> ecalRecHitsEEToken_;
};

EG8XObjectUpdateModifier::EG8XObjectUpdateModifier(const edm::ParameterSet& conf, edm::ConsumesCollector& cc)
    : ModifyObjectValueBase(conf),
      caloTopoToken_{cc.esConsumes()},
      ecalRecHitsEBToken_(cc.consumes(conf.getParameter<edm::InputTag>("ecalRecHitsEB"))),
      ecalRecHitsEEToken_(cc.consumes(conf.getParameter<edm::InputTag>("ecalRecHitsEE"))) {}

void EG8XObjectUpdateModifier::setEvent(const edm::Event& iEvent) {
  ecalRecHitsEB_ = &iEvent.get(ecalRecHitsEBToken_);
  ecalRecHitsEE_ = &iEvent.get(ecalRecHitsEEToken_);
}

void EG8XObjectUpdateModifier::setEventContent(const edm::EventSetup& iSetup) {
  caloTopo_ = &iSetup.getData(caloTopoToken_);
}

void EG8XObjectUpdateModifier::modifyObject(reco::GsfElectron& ele) const {
  const reco::CaloCluster& seedClus = *(ele.superCluster()->seed());
  const EcalRecHitCollection* ecalRecHits = ele.isEB() ? ecalRecHitsEB_ : ecalRecHitsEE_;

  auto full5x5ShowerShapes = ele.full5x5_showerShape();
  full5x5ShowerShapes.e2x5Left = noZS::EcalClusterTools::e2x5Left(seedClus, ecalRecHits, caloTopo_);
  full5x5ShowerShapes.e2x5Right = noZS::EcalClusterTools::e2x5Right(seedClus, ecalRecHits, caloTopo_);
  full5x5ShowerShapes.e2x5Top = noZS::EcalClusterTools::e2x5Top(seedClus, ecalRecHits, caloTopo_);
  full5x5ShowerShapes.e2x5Bottom = noZS::EcalClusterTools::e2x5Bottom(seedClus, ecalRecHits, caloTopo_);
  ele.full5x5_setShowerShape(full5x5ShowerShapes);

  auto showerShapes = ele.showerShape();
  showerShapes.e2x5Left = EcalClusterTools::e2x5Left(seedClus, ecalRecHits, caloTopo_);
  showerShapes.e2x5Right = EcalClusterTools::e2x5Right(seedClus, ecalRecHits, caloTopo_);
  showerShapes.e2x5Top = EcalClusterTools::e2x5Top(seedClus, ecalRecHits, caloTopo_);
  showerShapes.e2x5Bottom = EcalClusterTools::e2x5Bottom(seedClus, ecalRecHits, caloTopo_);
  ele.setShowerShape(showerShapes);

  reco::GsfElectron::SaturationInfo eleSatInfo;
  auto satInfo = getSaturationInfo(*ele.superCluster());
  eleSatInfo.nSaturatedXtals = satInfo.first;
  eleSatInfo.isSeedSaturated = satInfo.second;
  ele.setSaturationInfo(eleSatInfo);
}

void EG8XObjectUpdateModifier::modifyObject(reco::Photon& pho) const {
  reco::Photon::SaturationInfo phoSatInfo;
  auto satInfo = getSaturationInfo(*pho.superCluster());
  phoSatInfo.nSaturatedXtals = satInfo.first;
  phoSatInfo.isSeedSaturated = satInfo.second;
  pho.setSaturationInfo(phoSatInfo);
}

std::pair<int, bool> EG8XObjectUpdateModifier::getSaturationInfo(const reco::SuperCluster& superClus) const {
  bool isEB = superClus.seed()->seed().subdetId() == EcalBarrel;
  const auto& ecalRecHits = isEB ? *ecalRecHitsEB_ : *ecalRecHitsEE_;

  int nrSatCrys = 0;
  bool seedSaturated = false;
  const auto& hitsAndFractions = superClus.seed()->hitsAndFractions();
  for (const auto& hitFractionPair : hitsAndFractions) {
    auto ecalRecHitIt = ecalRecHits.find(hitFractionPair.first);
    if (ecalRecHitIt != ecalRecHits.end() && ecalRecHitIt->checkFlag(EcalRecHit::Flags::kSaturated)) {
      nrSatCrys++;
      if (hitFractionPair.first == superClus.seed()->seed())
        seedSaturated = true;
    }
  }
  return {nrSatCrys, seedSaturated};
}

DEFINE_EDM_PLUGIN(ModifyObjectValueFactory, EG8XObjectUpdateModifier, "EG8XObjectUpdateModifier");
