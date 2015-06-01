//
// A module to check for coincidences of CRV pulses
//
// $Id: $
// $Author: ehrlich $
// $Date: 2014/08/07 01:33:40 $
// 
// Original Author: Ralf Ehrlich

#include "CosmicRayShieldGeom/inc/CosmicRayShield.hh"
#include "DataProducts/inc/CRSScintillatorBarIndex.hh"

#include "GeometryService/inc/DetectorSystem.hh"
#include "GeometryService/inc/GeomHandle.hh"
#include "GeometryService/inc/GeometryService.hh"
#include "RecoDataProducts/inc/CrvRecoPulsesCollection.hh"
#include "RecoDataProducts/inc/CrvCoincidenceCheckResult.hh"

#include "art/Persistency/Common/Ptr.h"
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "fhiclcpp/ParameterSet.h"
#include "CLHEP/Units/GlobalSystemOfUnits.h"

#include <string>

#include <TMath.h>
#include <TH2D.h>

namespace mu2e 
{
  class SimpleCrvCoincidenceCheck : public art::EDProducer 
  {
    public:
    explicit SimpleCrvCoincidenceCheck(fhicl::ParameterSet const& pset);
    void produce(art::Event& e);
    void beginJob();
    void endJob();

    private:
    bool        _storeCoincidenceCombinations;
    std::string _crvRecoPulsesModuleLabel;
    double      _PEthreshold;
    double      _maxDistance;
    double      _maxTimeDifference;
    double      _timeWindowStart;
    double      _timeWindowEnd;

    struct coincidenceStruct
    {
      double                  pos;
      CRSScintillatorBarIndex counter;
      std::vector<double>     time;
      std::vector<int>        PEs;
      std::vector<int>        SiPMs;
    };
  };

  SimpleCrvCoincidenceCheck::SimpleCrvCoincidenceCheck(fhicl::ParameterSet const& pset) :
    _storeCoincidenceCombinations(pset.get<bool>("storeCoincidenceCombinations")),
    _crvRecoPulsesModuleLabel(pset.get<std::string>("crvRecoPulsesModuleLabel")),
    _PEthreshold(pset.get<double>("PEthreshold")),
    _maxDistance(pset.get<double>("maxDistance")),
    _maxTimeDifference(pset.get<double>("maxTimeDifference")),
    _timeWindowStart(pset.get<double>("timeWindowStart")),
    _timeWindowEnd(pset.get<double>("timeWindowEnd"))
  {
    produces<CrvCoincidenceCheckResult>();
  }

  void SimpleCrvCoincidenceCheck::beginJob()
  {
  }

  void SimpleCrvCoincidenceCheck::endJob()
  {
  }

  void SimpleCrvCoincidenceCheck::produce(art::Event& event) 
  {
    std::unique_ptr<CrvCoincidenceCheckResult> crvCoincidenceCheckResult(new CrvCoincidenceCheckResult);

    GeomHandle<CosmicRayShield> CRS;

    art::Handle<CrvRecoPulsesCollection> crvRecoPulsesCollection;
    event.getByLabel(_crvRecoPulsesModuleLabel,"",crvRecoPulsesCollection);

    std::map<int, std::vector<coincidenceStruct> > coincidenceMap;

    for(CrvRecoPulsesCollection::const_iterator iter=crvRecoPulsesCollection->begin(); 
        iter!=crvRecoPulsesCollection->end(); iter++)
    {
      const CRSScintillatorBarIndex &barIndex = iter->first;
      const CRSScintillatorBar &CRSbar = CRS->getBar(barIndex);
      int   moduleNumber=CRSbar.id().getShieldNumber();

      const CrvRecoPulses &crvRecoPulses = iter->second;
      for(int SiPM=0; SiPM<2; SiPM++)
      {
        coincidenceStruct c;
        c.counter=barIndex;
        int coincidenceGroup = 0;
//FIXME: DO NOT HARDCODE THIS
        switch (moduleNumber)
        {
          case 0: 
          case 3: 
          case 4: 
          case 5: if(SiPM==0) {coincidenceGroup = 1; c.pos=CRSbar.getPosition().z();}
                  if(SiPM==1) {coincidenceGroup = 2; c.pos=CRSbar.getPosition().z();}
                  break;
          case 1: if(SiPM==1) {coincidenceGroup = 2; c.pos=CRSbar.getPosition().z();}
                  break;
          case 2: if(SiPM==0) {coincidenceGroup = 1; c.pos=CRSbar.getPosition().z();}
                  if(SiPM==1) {coincidenceGroup = 3; c.pos=CRSbar.getPosition().z();}
                  break;
          case 6: 
          case 7: 
          case 8: if(SiPM==0) {coincidenceGroup = 4; c.pos=CRSbar.getPosition().z();}
                  if(SiPM==1) {coincidenceGroup = 5; c.pos=CRSbar.getPosition().z();}
                  break;
          case 9: if(SiPM==0) {coincidenceGroup = 2; c.pos=CRSbar.getPosition().z();}
                  break;
         case 10: 
         case 11: 
         case 12: if(SiPM==0) {coincidenceGroup = 2; c.pos=CRSbar.getPosition().z();}
                  if(SiPM==1) {coincidenceGroup = 5; c.pos=CRSbar.getPosition().z();}
                  break;
         case 13: if(SiPM==0) {coincidenceGroup = 6; c.pos=CRSbar.getPosition().y();}
                  if(SiPM==1) {coincidenceGroup = 7; c.pos=CRSbar.getPosition().y();}
                  break;
         case 14: if(SiPM==0) {coincidenceGroup = 8; c.pos=CRSbar.getPosition().y();}
                  break;
         case 15: if(SiPM==0) {coincidenceGroup = 9; c.pos=CRSbar.getPosition().x();}
                  break;
         case 16: if(SiPM==0) {coincidenceGroup = 10; c.pos=CRSbar.getPosition().x();}
                  break;
         case 17: if(SiPM==0) {coincidenceGroup = 11; c.pos=CRSbar.getPosition().x();}
                  break;
        };

        if(coincidenceGroup==0) continue;

        for(int SiPMtmp=SiPM; SiPMtmp<4; SiPMtmp+=2)  //the reco pulse times of both SiPMs on the same side are put into the same c to avoid that 
                                                      //pulses from the same counter can be used to satisfy the coincidence condition
        {
          const std::vector<CrvRecoPulses::CrvSingleRecoPulse> &pulseVector = crvRecoPulses.GetRecoPulses(SiPMtmp);
          for(unsigned int i = 0; i<pulseVector.size(); i++) 
          {
            const CrvRecoPulses::CrvSingleRecoPulse &pulse = pulseVector[i];
            if(pulse._PEs>=_PEthreshold && 
               pulse._leadingEdge>=_timeWindowStart && 
               pulse._leadingEdge<=_timeWindowEnd)
               {
                 c.time.push_back(pulse._leadingEdge);
                 c.PEs.push_back(pulse._PEs);
                 c.SiPMs.push_back(SiPMtmp);
//std::cout<<"coincidence group: "<<coincidenceGroup<<"   barIndex: "<<barIndex<<"  SiPM: "<<SiPMtmp<<std::endl;
//std::cout<<"  PEs: "<<pulse._PEs<<"   LE: "<<pulse._leadingEdge<<"   pos: "<<c.pos<<std::endl;
               }
          }
        }
        if(c.time.size()>0) coincidenceMap[coincidenceGroup].push_back(c);
      }
    }

//std::cout<<"comparing all reco hits ..."<<std::endl;
    bool foundCoincidence=false;
    std::map<int, std::vector<coincidenceStruct> >::const_iterator iterC;
    for(iterC = coincidenceMap.begin(); iterC!=coincidenceMap.end() && (!foundCoincidence || _storeCoincidenceCombinations); iterC++)
    {
      const std::vector<coincidenceStruct> &vectorC = iterC->second;
      unsigned int n=vectorC.size();
//std::cout<<"coincidence map #"<<iterC->first<<": "<<n<<" entries"<<std::endl;
      for(unsigned int i1=0; i1<n && (!foundCoincidence || _storeCoincidenceCombinations); i1++) 
      for(unsigned int i2=i1+1; i2<n && (!foundCoincidence || _storeCoincidenceCombinations); i2++) 
      for(unsigned int i3=i2+1; i3<n && (!foundCoincidence || _storeCoincidenceCombinations); i3++)
      {
        double pos[3]={vectorC[i1].pos,vectorC[i2].pos,vectorC[i3].pos};
        double posMin = *std::min_element(pos,pos+3);
        double posMax = *std::max_element(pos,pos+3);
        if(posMax-posMin>_maxDistance) continue;

        const std::vector<double> &vectorTime1 = vectorC[i1].time;
        const std::vector<double> &vectorTime2 = vectorC[i2].time;
        const std::vector<double> &vectorTime3 = vectorC[i3].time;
        for(unsigned int j1=0; j1<vectorTime1.size() && (!foundCoincidence || _storeCoincidenceCombinations); j1++)
        for(unsigned int j2=0; j2<vectorTime2.size() && (!foundCoincidence || _storeCoincidenceCombinations); j2++)
        for(unsigned int j3=0; j3<vectorTime3.size() && (!foundCoincidence || _storeCoincidenceCombinations); j3++)
        {
          double time[3]={vectorTime1[j1],vectorTime2[j2],vectorTime3[j3]};
          double timeMin = *std::min_element(time,time+3);
          double timeMax = *std::max_element(time,time+3);
          if(timeMax-timeMin<_maxTimeDifference) 
          {
            foundCoincidence=true;
            if(_storeCoincidenceCombinations)
            {
              CrvCoincidenceCheckResult::CoincidenceCombination combination;
              for(int k=0; k<3; k++) combination._time[k] = time[k];
              combination._PEs[0] = vectorC[i1].PEs[j1];
              combination._PEs[1] = vectorC[i2].PEs[j2];
              combination._PEs[2] = vectorC[i3].PEs[j3];
              combination._counters[0] = vectorC[i1].counter;
              combination._counters[1] = vectorC[i2].counter;
              combination._counters[2] = vectorC[i3].counter;
              combination._SiPMs[0] = vectorC[i1].SiPMs[j1];
              combination._SiPMs[1] = vectorC[i2].SiPMs[j2];
              combination._SiPMs[2] = vectorC[i3].SiPMs[j3];
              crvCoincidenceCheckResult->GetCoincidenceCombinations().push_back(combination);
              std::cout<<"Coincidence times/counters/SiPMs/PEs: "<<std::endl;
              for(int k=0; k<3; k++)
              {
                std::cout<<"   "<<time[k]<<" / "<<combination._counters[k]<<" / "<<combination._SiPMs[k]<<" / "<<combination._PEs[k]<<std::endl;
              }
            }
          }
        }
      } 
    }
    crvCoincidenceCheckResult->SetCoincidence(foundCoincidence);

    std::cout<<"run "<<event.id().run()<<"  subrun "<<event.id().subRun()<<"  event "<<event.id().event()<<"    ";
    std::cout<<(foundCoincidence?"Coincidence satisfied":"No coincidence found")<<std::endl;

    event.put(std::move(crvCoincidenceCheckResult));

  } // end produce

} // end namespace mu2e

using mu2e::SimpleCrvCoincidenceCheck;
DEFINE_ART_MODULE(SimpleCrvCoincidenceCheck)
