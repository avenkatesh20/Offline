//
// This module transforms StepPointMC objects into StrawDigi objects
// It also builds the truth match map
//
// $Id: StrawDigisFromStepPointMCs_module.cc,v 1.39 2014/08/29 19:49:23 brownd Exp $
// $Author: brownd $
// $Date: 2014/08/29 19:49:23 $
//
// Original author David Brown, LBNL
//
// framework
#include "art/Framework/Principal/Event.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Principal/Handle.h"
#include "GeometryService/inc/GeomHandle.hh"
#include "art/Framework/Core/EDProducer.h"
#include "GeometryService/inc/DetectorSystem.hh"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "SeedService/inc/SeedService.hh"
#include "cetlib_except/exception.h"
// conditions
#include "ConditionsService/inc/ConditionsHandle.hh"
#include "ConditionsService/inc/AcceleratorParams.hh"
#include "GeometryService/inc/getTrackerOrThrow.hh"
#include "TTrackerGeom/inc/TTracker.hh"
#include "ConfigTools/inc/ConfigFileLookupPolicy.hh"
#include "TrackerConditions/inc/StrawElectronics.hh"
#include "TrackerConditions/inc/StrawPhysics.hh"
#include "GeometryService/inc/GeomHandle.hh"
#include "GeometryService/inc/DetectorSystem.hh"
#include "BFieldGeom/inc/BFieldManager.hh"
#include "GlobalConstantsService/inc/GlobalConstantsHandle.hh"
#include "GlobalConstantsService/inc/ParticleDataTable.hh"
// utiliities
#include "GeneralUtilities/inc/TwoLinePCA.hh"
#include "Mu2eUtilities/inc/SimParticleTimeOffset.hh"
#include "TrackerConditions/inc/DeadStrawList.hh"
#include "TrackerConditions/inc/Types.hh"
// data
#include "RecoDataProducts/inc/StrawDigiCollection.hh"
#include "MCDataProducts/inc/StepPointMCCollection.hh"
#include "MCDataProducts/inc/PtrStepPointMCVectorCollection.hh"
#include "MCDataProducts/inc/StrawDigiMCCollection.hh"
// MC structures
#include "TrackerMC/inc/StrawClusterSequencePair.hh"
#include "TrackerMC/inc/StrawWaveform.hh"
//CLHEP
#include "CLHEP/Random/RandGaussQ.h"
#include "CLHEP/Random/RandFlat.h"
#include "CLHEP/Vector/LorentzVector.h"
// root
#include "TMath.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TGraph.h"
#include "TMarker.h"
#include "TTree.h"
// C++
#include <map>
#include <algorithm>
using namespace std;

namespace mu2e {
  namespace TrackerMC {
    using namespace TrkTypes;
    struct IonCluster {  // ion charge cluster before drift or amplification
      CLHEP::Hep3Vector _pos; // position of this cluster
      double _charge; // charge of this cluster, in pC.  Note: this is pre-gain!!!
      unsigned _nion; // number of ionizations in this cluste
      IonCluster(CLHEP::Hep3Vector const& pos, double charge, unsigned nion): _pos(pos),_charge(charge), _nion(nion) {}
    };

    struct WireCharge { // charge at the wire after drift
      double _charge; // charge at the wire, in units of pC
      double _time; // relative time at the wire, relative to ionzation time (ns)
      double _dd;  // transverse distance drifted to the wrie
      double _wpos; // position long the wire, WRT the wire center, signed by the wire direction
    };

    struct WireEndCharge { // charge at one end of the wire after propagation
      double _charge; // charge at the wire, in units of pC
      double _time; // time at the wire end, relative to the time the charge reached the wire (ns)
      double _wdist; // propagation distance from the point of collection to the end
    };

    class StrawDigisFromStepPointMCs : public art::EDProducer {

      public:

	typedef map<StrawIndex,StrawClusterSequencePair> StrawClusterMap;  // clusts by straw
	typedef vector<art::Ptr<StepPointMC> > StrawSPMCPV; // vector of associated StepPointMCs for a single straw/particle
	typedef list<WFX> WFXList;
	typedef vector<WFXList::const_iterator> WFXP;

	explicit StrawDigisFromStepPointMCs(fhicl::ParameterSet const& pset);
	// Accept compiler written d'tor.

	virtual void beginJob();
	virtual void beginRun( art::Run& run );
	virtual void produce( art::Event& e);

      private:

	// Diagnostics level.
	int _diagLevel, _printLevel;
	unsigned _maxhist;
	bool _xtalkhist,_noxhist;
	int _minnxinghist;
	// Limit on number of events for which there will be full printout.
	int _maxFullPrint;
	// Name of the tracker StepPoint collection
	string _trackerStepPoints;

	// Parameters
	bool   _addXtalk; // should we add cross talk hits?
	double _ctMinCharge; // minimum charge to add cross talk (for performance issues)
	bool   _addNoise; // should we add noise hits?
	double _preampxtalk, _postampxtalk; // x-talk parameters; these should come from conditions, FIXME!!
	double _highdEdx; // cut dividing highly-ionizing steps from low
	string _g4ModuleLabel;  // Nameg of the module that made these hits.
	double _mbtime; // period of 1 microbunch
	double _mbbuffer; // buffer on that for ghost clusts (for waveform)
	double _steptimebuf; // buffer for MC step point times
	// models of straw response to stimuli
	ConditionsHandle<StrawPhysics> _strawphys;
	ConditionsHandle<StrawElectronics> _strawele;
	SimParticleTimeOffset _toff;
	TrkTypes::Path _diagpath; // electronics path for waveform diagnostics
	// Random number distributions
	art::RandomNumberGenerator::base_engine_t& _engine;
	CLHEP::RandGaussQ _randgauss;
	CLHEP::RandFlat _randflat;
	// A category for the error logger.
	const string _messageCategory;
	// Give some informationation messages only on the first event.
	bool _firstEvent;
	// record the BField at the tracker center
	double _bz;
	// List of dead straws as a parameter set; needed at beginRun time.
	fhicl::ParameterSet _deadStraws;
	DeadStrawList _strawStatus;
	// diagnostics
	TTree* _swdiag;
	Int_t _splane, _spanel, _slayer, _sstraw;
	Int_t _nclust,_iclust;
	Float_t _hqsum, _vmax, _tvmax, _sesum;
	Int_t _wmcpdg, _wmcproc, _nxing;
	Float_t _mce, _slen, _sedep;
	Int_t _nsteppoint;
	Int_t _npart;
	Float_t _tmin, _tmax, _txing, _xddist, _xwdist, _xpdist;
	Bool_t _wfxtalk;
	TTree* _sddiag;
	Int_t _sdplane, _sdpanel, _sdlayer, _sdstraw;
	Int_t _nend, _nstep;
	Float_t _xtime0, _xtime1, _htime0, _htime1, _charge0, _charge1, _ddist0, _ddist1, _wdist0, _wdist1, _vstart0, _vstart1, _vcross0, _vcross1;
	Float_t _mctime, _mcenergy, _mctrigenergy, _mcthreshenergy;
	Int_t _mcthreshpdg, _mcthreshproc, _mcnstep;
	Float_t _mcdca;
	Int_t _dmcpdg, _dmcproc, _dmcgen;
	Float_t _dmcmom;
	Bool_t _xtalk;
	vector<unsigned> _adc;
	Int_t _tdccal, _tdchv;
	Int_t _totcal, _tothv;
	TTree* _sdiag;
	Float_t _steplen, _stepE, _qsum, _partP;
	Int_t _nsubstep, _niontot, _partPDG;
	TTree* _cdiag;
	Float_t _gain, _cq;
	Int_t _nion;

	//    vector<TGraph*> _waveforms;
	vector<TH1F*> _waveforms;
	//  helper functions
	void fillClusterMap(art::Event const& event, StrawClusterMap & hmap);
	void addStep(art::Ptr<StepPointMC> const& spmcptr, Straw const& straw, StrawClusterSequencePair& shsp);
	void divideStep(StepPointMC const& step, vector<IonCluster>& clusters);
	void driftCluster(Straw const& straw, IonCluster const& cluster, WireCharge& wireq);
	void propagateCharge(Straw const& straw, WireCharge const& wireq, StrawEnd end, WireEndCharge& weq);
	double microbunchTime(double globaltime) const;
	void addGhosts(StrawCluster const& clust,StrawClusterSequence& shs);
	void addNoise(StrawClusterMap& hmap);
	void findThresholdCrossings(StrawWaveform const& swf, WFXList& xings);
	void createDigis(StrawClusterSequencePair const& hsp,
	    XTalk const& xtalk,
	    StrawDigiCollection* digis, StrawDigiMCCollection* mcdigis,
	    PtrStepPointMCVectorCollection* mcptrs );
	void fillDigis(WFXList const& xings,const StrawWaveform wf[2] , StrawIndex index,
	    StrawDigiCollection* digis, StrawDigiMCCollection* mcdigis,
	    PtrStepPointMCVectorCollection* mcptrs );
	void createDigi(WFXP const& xpair, const StrawWaveform wf[2], StrawIndex index, StrawDigiCollection* digis);
	void findCrossTalkStraws(Straw const& straw,vector<XTalk>& xtalk);
	// diagnostic functions
	void waveformDiag(const StrawWaveform wf[2], WFXList const& xings);
	void digiDiag(WFXP const& xpair, StrawDigi const& digi,StrawDigiMC const& mcdigi);
    };

    StrawDigisFromStepPointMCs::StrawDigisFromStepPointMCs(fhicl::ParameterSet const& pset) :

      // diagnostic parameters
      _diagLevel(pset.get<int>("diagLevel",0)),
      _printLevel(pset.get<int>("printLevel",0)),
      _maxhist(pset.get<unsigned>("MaxHist",100)),
      _xtalkhist(pset.get<bool>("CrossTalkHist",false)),
      _noxhist(pset.get<bool>("NoCrossingHist",false)),
      _minnxinghist(pset.get<int>("MinNXingHist",0)),
      // Parameters
      _maxFullPrint(pset.get<int>("maxFullPrint",2)),
      _trackerStepPoints(pset.get<string>("trackerStepPoints","tracker")),
      _addXtalk(pset.get<bool>("addCrossTalk",false)),
      _ctMinCharge(pset.get<double>("xtalkMinimumCharge",0)),
      _addNoise(pset.get<bool>("addNoise",false)),
      _preampxtalk(pset.get<double>("preAmplificationCrossTalk",0.0)),
      _postampxtalk(pset.get<double>("postAmplificationCrossTalk",0.02)), // dimensionless relative coupling
      _highdEdx(pset.get<double>("HighlyIonizingdEdx",0.001)), // MeV/mm
      _g4ModuleLabel(pset.get<string>("g4ModuleLabel")),
      _steptimebuf(pset.get<double>("StepPointMCTimeBuffer",100.0)), // nsec
      _toff(pset.get<fhicl::ParameterSet>("TimeOffsets", fhicl::ParameterSet())),
      _diagpath(static_cast<TrkTypes::Path>(pset.get<int>("WaveformDiagPath",TrkTypes::thresh))),
      // Random number distributions
      _engine(createEngine( art::ServiceHandle<SeedService>()->getSeed())),
      _randgauss( _engine ),
      _randflat( _engine ),

      _messageCategory("HITS"),

      // Control some information messages.
      _firstEvent(true),
      _deadStraws(pset.get<fhicl::ParameterSet>("deadStrawList", fhicl::ParameterSet())),
      _strawStatus(pset.get<fhicl::ParameterSet>("deadStrawList", fhicl::ParameterSet()))
      {
	// Tell the framework what we make.
	produces<StrawDigiCollection>();
	produces<PtrStepPointMCVectorCollection>();
	produces<StrawDigiMCCollection>();
      }
    void StrawDigisFromStepPointMCs::beginJob(){

      if(_diagLevel > 0){

	art::ServiceHandle<art::TFileService> tfs;
	_sdiag =tfs->make<TTree>("sdiag","Step diagnostics");
	_sdiag->Branch("steplen",&_steplen,"steplen/F");
	_sdiag->Branch("stepE",&_stepE,"stepE/F");
	_sdiag->Branch("partP",&_partP,"partP/F");
	_sdiag->Branch("qsum",&_qsum,"qsum/F");
	_sdiag->Branch("nsubstep",&_nsubstep,"nsubstep/I");
	_sdiag->Branch("niontot",&_niontot,"niontot/I");
	_sdiag->Branch("partPDG",&_partPDG,"partPDG/I");

	_cdiag =tfs->make<TTree>("cdiag","Cluster diagnostics");
	_cdiag->Branch("gain",&_gain,"gain/F");
	_cdiag->Branch("charge",&_cq,"charge/F");
	_cdiag->Branch("nion",&_nion,"nion/I");

	_swdiag =tfs->make<TTree>("swdiag","StrawWaveform diagnostics");
	_swdiag->Branch("plane",&_splane,"plane/I");
	_swdiag->Branch("panel",&_spanel,"panel/I");
	_swdiag->Branch("layer",&_slayer,"layer/I");
	_swdiag->Branch("straw",&_sstraw,"straw/I");
	_swdiag->Branch("nclust",&_nclust,"nclust/I");
	_swdiag->Branch("iclust",&_iclust,"iclust/I");
	_swdiag->Branch("hqsum",&_hqsum,"hqsum/F");
	_swdiag->Branch("vmax",&_vmax,"vmax/F");
	_swdiag->Branch("tvmax",&_tvmax,"tvmax/F");
	_swdiag->Branch("mcpdg",&_wmcpdg,"mcpdg/I");
	_swdiag->Branch("mcproc",&_wmcproc,"mcproc/I");
	_swdiag->Branch("mce",&_mce,"mce/F");
	_swdiag->Branch("slen",&_slen,"slen/F");
	_swdiag->Branch("sedep",&_sedep,"sedep/F");
	_swdiag->Branch("nxing",&_nxing,"nxing/I");
	_swdiag->Branch("nstep",&_nsteppoint,"nstep/I");
	_swdiag->Branch("sesum",&_sesum,"sesum/F");
	_swdiag->Branch("npart",&_npart,"npart/I");
	_swdiag->Branch("tmin",&_tmin,"tmin/F");
	_swdiag->Branch("tmax",&_tmax,"tmax/F");
	_swdiag->Branch("txing",&_txing,"txing/F");
	_swdiag->Branch("xddist",&_xddist,"xddist/F");
	_swdiag->Branch("xwdist",&_xwdist,"xwdist/F");
	_swdiag->Branch("xpdist",&_xpdist,"xpdist/F");
	_swdiag->Branch("xtalk",&_wfxtalk,"xtalk/B");

	if(_diagLevel > 1){
	  _sddiag =tfs->make<TTree>("sddiag","StrawDigi diagnostics");
	  _sddiag->Branch("plane",&_sdplane,"plane/I");
	  _sddiag->Branch("panel",&_sdpanel,"panel/I");
	  _sddiag->Branch("layer",&_sdlayer,"layer/I");
	  _sddiag->Branch("straw",&_sdstraw,"straw/I");
	  _sddiag->Branch("nend",&_nend,"nend/I");
	  _sddiag->Branch("nstep",&_nstep,"nstep/I");
	  _sddiag->Branch("xtime0",&_xtime0,"xtime0/F");
	  _sddiag->Branch("xtime1",&_xtime1,"xtime1/F");
	  _sddiag->Branch("htime0",&_htime0,"htime0/F");
	  _sddiag->Branch("htime1",&_htime1,"htime1/F");
	  _sddiag->Branch("charge0",&_charge0,"charge0/F");
	  _sddiag->Branch("charge1",&_charge1,"charge1/F");
	  _sddiag->Branch("wdist0",&_wdist0,"wdist0/F");
	  _sddiag->Branch("wdist1",&_wdist1,"wdist1/F");
	  _sddiag->Branch("vstart0",&_vstart0,"vstart0/F");
	  _sddiag->Branch("vstart1",&_vstart1,"vstart1/F");
	  _sddiag->Branch("vcross0",&_vcross0,"vcross0/F");
	  _sddiag->Branch("vcross1",&_vcross1,"vcross1/F");
	  _sddiag->Branch("ddist0",&_ddist0,"ddist0/F");
	  _sddiag->Branch("ddist1",&_ddist1,"ddist1/F");
	  _sddiag->Branch("tdccal",&_tdccal,"tdccal/I");
	  _sddiag->Branch("tdchv",&_tdchv,"tdchv/I");
          _sddiag->Branch("totcal",&_totcal,"totcal/I");
          _sddiag->Branch("tothv",&_tothv,"tothv/I");
	  _sddiag->Branch("adc",&_adc);
	  _sddiag->Branch("mctime",&_mctime,"mctime/F");
	  _sddiag->Branch("mcenergy",&_mcenergy,"mcenergy/F");
	  _sddiag->Branch("mctrigenergy",&_mctrigenergy,"mctrigenergy/F");
	  _sddiag->Branch("mcthreshenergy",&_mcthreshenergy,"mcthreshenergy/F");
	  _sddiag->Branch("mcthreshpdg",&_mcthreshpdg,"mcthreshpdg/I");
	  _sddiag->Branch("mcthreshproc",&_mcthreshproc,"mcthreshproc/I");
	  _sddiag->Branch("mcnstep",&_mcnstep,"mcnstep/I");
	  _sddiag->Branch("mcdca",&_mcdca,"mcdca/F");
	  _sddiag->Branch("mcpdg",&_dmcpdg,"mcpdg/I");
	  _sddiag->Branch("mcproc",&_dmcproc,"mcproc/I");
	  _sddiag->Branch("mcgen",&_dmcgen,"mcgen/I");
	  _sddiag->Branch("mcmom",&_dmcmom,"mcmom/F");
	  _sddiag->Branch("xtalk",&_xtalk,"xtalk/B");
	}
      }
    }

    void StrawDigisFromStepPointMCs::beginRun( art::Run& run ){
      _strawStatus.reset(_deadStraws);
      // field at the center of the tracker
      // GeomHandle<BFieldManager> bfmgr;
      //  GeomHandle<DetectorSystem> det;
      //  CLHEP::Hep3Vector vpoint_mu2e = det->toMu2e(Hep3Vector(0.0,0.0,0.0));
      // scale the field for the curvature
      //  _bz = BField::mmTeslaToMeVc*bfmgr->getBField(vpoint_mu2e).z();
    }

    void StrawDigisFromStepPointMCs::produce(art::Event& event) {
      if ( _printLevel > 0 ) cout << "StrawDigisFromStepPointMCs: produce() begin; event " << event.id().event() << endl;
      static int ncalls(0);
      ++ncalls;
      // update conditions caches.
      ConditionsHandle<AcceleratorParams> accPar("ignored");
      _mbtime = accPar->deBuncherPeriod;
      _toff.updateMap(event);
      _strawele = ConditionsHandle<StrawElectronics>("ignored");
      _strawphys = ConditionsHandle<StrawPhysics>("ignored");
      const Tracker& tracker = getTrackerOrThrow();
      // make the microbunch buffer long enough to get the full waveform
      _mbbuffer = _strawele->nADCSamples()*_strawele->adcPeriod();
      // Containers to hold the output information.
      unique_ptr<StrawDigiCollection> digis(new StrawDigiCollection);
      unique_ptr<StrawDigiMCCollection> mcdigis(new StrawDigiMCCollection);
      unique_ptr<PtrStepPointMCVectorCollection> mcptrs(new PtrStepPointMCVectorCollection);
      // create the StrawCluster map
      StrawClusterMap hmap;
      // fill this from the event
      fillClusterMap(event,hmap);
      // add noise clusts
      if(_addNoise)addNoise(hmap);
      // loop over the clust sequences
      for(auto ihsp=hmap.begin();ihsp!= hmap.end();++ihsp){
	StrawClusterSequencePair const& hsp = ihsp->second;
	// create primary digis from this clust sequence
	XTalk self(hsp.strawIndex()); // this object represents the straws coupling to itself, ie 100%
	createDigis(hsp,self,digis.get(),mcdigis.get(),mcptrs.get());
	// if we're applying x-talk, look for nearby coupled straws
	if(_addXtalk) {
	  // only apply if the charge is above a threshold
	  double totalCharge = 0;
	  for(auto ih=hsp.clustSequence(TrkTypes::cal).clustList().begin();ih!= hsp.clustSequence(TrkTypes::cal).clustList().end();++ih){
	    totalCharge += ih->charge();
	  }
	  if( totalCharge > _ctMinCharge){
	    vector<XTalk> xtalk;
	    Straw const& straw = tracker.getStraw(hsp.strawIndex());
	    findCrossTalkStraws(straw,xtalk);
	    for(auto ixtalk=xtalk.begin();ixtalk!=xtalk.end();++ixtalk){
	      createDigis(hsp,*ixtalk,digis.get(),mcdigis.get(),mcptrs.get());
	    }
	  }
	}
      }
      // store the digis in the event
      event.put(move(digis));
      // store MC truth match
      event.put(move(mcdigis));
      event.put(move(mcptrs));
      if ( _printLevel > 0 ) cout << "StrawDigisFromStepPointMCs: produce() end" << endl;
      // Done with the first event; disable some messages.
      _firstEvent = false;
    } // end produce

    void StrawDigisFromStepPointMCs::createDigis(StrawClusterSequencePair const& hsp,
	XTalk const& xtalk,
	StrawDigiCollection* digis, StrawDigiMCCollection* mcdigis,
	PtrStepPointMCVectorCollection* mcptrs ) {
      // instantiate waveforms for both ends of this straw
      StrawWaveform waveforms[2] {StrawWaveform(hsp.clustSequence(TrkTypes::cal),_strawele,xtalk),
	StrawWaveform(hsp.clustSequence(TrkTypes::hv),_strawele,xtalk) };
      // find the threshold crossing points for these waveforms
      WFXList xings;
      // loop over the ends of this straw
      for(size_t iend=0;iend<2;++iend){
	findThresholdCrossings(waveforms[iend],xings);
      }
      // convert the crossing points into digis, and add them to the event data.  Require both ends to have threshold
      // crossings
      if(xings.size() > 1){
	// fill digis from these crossings
	fillDigis(xings,waveforms,xtalk._dest,digis,mcdigis,mcptrs);
	// diagnostics
      }
      if(_diagLevel > 0 && waveforms[0].clusts().clustList().size() > 0)waveformDiag(waveforms,xings);
    }

    void StrawDigisFromStepPointMCs::fillClusterMap(art::Event const& event, StrawClusterMap & hmap){
	// get conditions
	const TTracker& tracker = static_cast<const TTracker&>(getTrackerOrThrow());
	// Get all of the tracker StepPointMC collections from the event:
	typedef vector< art::Handle<StepPointMCCollection> > HandleVector;
	// This selector will select only data products with the given instance name.
	art::ProductInstanceNameSelector selector(_trackerStepPoints);
	HandleVector stepsHandles;
	event.getMany( selector, stepsHandles);
	// Informational message on the first event.
	if ( _firstEvent ) {
	  mf::LogInfo log(_messageCategory);
	  log << "StrawDigisFromStepPointMCs::fillHitMap will use StepPointMCs from: \n";
	  for ( HandleVector::const_iterator i=stepsHandles.begin(), e=stepsHandles.end();
	      i != e; ++i ){
	    art::Provenance const& prov(*(i->provenance()));
	    log  << "   " << prov.branchName() << "\n";
	  }
	}
	if(stepsHandles.empty()){
	  throw cet::exception("SIM")<<"mu2e::StrawDigisFromStepPointMCs: No StepPointMC collections found for tracker" << endl;
	}
	// Loop over StepPointMC collections
	for ( HandleVector::const_iterator ispmcc=stepsHandles.begin(), espmcc=stepsHandles.end();ispmcc != espmcc; ++ispmcc ){
	  art::Handle<StepPointMCCollection> const& handle(*ispmcc);
	  StepPointMCCollection const& steps(*handle);
	  // Loop over the StepPointMCs in this collection
	  for (size_t ispmc =0; ispmc<steps.size();++ispmc){
	    // find straw index
	    StrawIndex const & strawind = steps[ispmc].strawIndex();
	    // Skip dead straws, and straws that don't exist
	    if(tracker.strawExists(strawind)) {
	      // lookup straw here, to avoid having to find the tracker for every step
	      Straw const& straw = tracker.getStraw(strawind);
	      // Skip steps that occur in the deadened region near the end of each wire,
	      // or in dead regions of the straw
	      double wpos = fabs((steps[ispmc].position()-straw.getMidPoint()).dot(straw.getDirection()));
	      if(wpos <  straw.getDetail().activeHalfLength() &&
		  _strawStatus.isAlive(strawind,wpos) ){
		// create ptr to MC truth, used for references
		art::Ptr<StepPointMC> spmcptr(handle,ispmc);
		// create a clust from this step, and add it to the clust map
		addStep(spmcptr,straw,hmap[strawind]);
	      }
	    }
	  }
	}
      }

    void
      StrawDigisFromStepPointMCs::addStep(art::Ptr<StepPointMC> const& spmcptr,
	  Straw const& straw,
	  StrawClusterSequencePair& shsp) {
	StepPointMC const& step = *spmcptr;
	StrawIndex const & strawind = step.strawIndex();
	// Subdivide the StepPointMC into ionization clusters
	vector<IonCluster> clusters;
	divideStep(step,clusters);
	// get time offset for this step
	double tstep = _toff.timeWithOffsetsApplied(step);
	// test if this microbunch is worth simulating
	double mbtime = microbunchTime(tstep);
	if( mbtime > _strawele->flashEnd()-_steptimebuf
	    || mbtime <  _strawele->flashStart() ) {
	  // drift these clusters to the wire, and record the charge at the wire
	  for(auto iclu=clusters.begin(); iclu != clusters.end(); ++iclu){
	    WireCharge wireq;
	    driftCluster(straw,*iclu,wireq);
	    // propagate this charge to each end of the wire
	    for(size_t iend=0;iend<2;++iend){
	      StrawEnd end(static_cast<TrkTypes::End>(iend));
	      // compute the longitudinal propagation effects
	      WireEndCharge weq;
	      propagateCharge(straw,wireq,end,weq);
	      // compute the total time, modulo the microbunch
	      double gtime = tstep + wireq._time + weq._time;
	      double htime = microbunchTime(gtime);
	      // create the clust
	      StrawCluster clust(StrawCluster::primary,strawind,end,htime,weq._charge,wireq._dd,weq._wdist,
		  spmcptr,CLHEP::HepLorentzVector(iclu->_pos,mbtime));
	      // add the clusts to the appropriate sequence.
	      shsp.clustSequence(end).insert(clust);
	      // if required, add a 'ghost' copy of this clust
	      addGhosts(clust,shsp.clustSequence(end));
	    }
	  }
	}
      }

    void StrawDigisFromStepPointMCs::divideStep(StepPointMC const& step,
	vector<IonCluster>& clusters) {
      // calculate the total # of electrons the step energy corresponds to.  We will maintain
      // this, as it includes all the fluctuations G4 has already made
      unsigned nele = max(unsigned(1),unsigned(rint(step.ionizingEdep()/_strawphys->ionizationEnergy())));
      // if the step is already smaller than the mean free path, don't subdivide
      if(step.stepLength() > _strawphys->meanFreePath()) {
	// if this isn't a highly-ionizing step, use the best statistics
	if(step.ionizingEdep()/step.stepLength() < _highdEdx){
	  // Calculate the total number of ionization electrons corresponding to this energy
	  // create clusters until all the electrons are used up
	  unsigned niontot(0);
	  CLHEP::Hep3Vector dir = step.momentum().unit();
	  while(niontot < nele){
	    unsigned nion = _strawphys->nIons(_randflat.fire());
	    // truncate if necessary
	    if(niontot + nion > nele) nion = nele-niontot;
	    double qc = _strawphys->ionizationCharge(nion*_strawphys->ionizationEnergy());
	    // place the cluster at a random position along the step.
	    // This works for high-momentum particles, otherwise I should use a helix, FIXME!!!
	    double length = _randflat.fire(0.0,step.stepLength());
	    CLHEP::Hep3Vector pos = step.position() + length*dir;
	    IonCluster cluster(pos,qc,nion);
	    clusters.push_back(cluster);
	    niontot += nion;
	  }
	} else {
	  // if this is a highly-ionizing particle divide the # of electrons evenly by the mean free path
	  int nion = max(1,int(rint(nele*_strawphys->meanFreePath()/step.stepLength())));
	  unsigned niontot(0);
	  CLHEP::Hep3Vector dir = step.momentum().unit();
	  while(niontot < nele){
	    // truncate if necessary
	    if(niontot + nion > nele) nion = nele-niontot;
	    double qc = _strawphys->ionizationCharge(nion*_strawphys->ionizationEnergy());
	    // place the cluster at a random position along the step.
	    // This works for high-momentum particles, otherwise I should use a helix, FIXME!!!
	    double length = _randflat.fire(0.0,step.stepLength());
	    CLHEP::Hep3Vector pos = step.position() + length*dir;
	    IonCluster cluster(pos,qc,nion);
	    clusters.push_back(cluster);
	    niontot += nion;
	  }
	}
      } else {
	// for short steps put all the charge into 1 cluster
	double qstep = _strawphys->ionizationCharge(step.ionizingEdep());
	IonCluster cluster(step.position(),qstep,nele);
	clusters.push_back(cluster);
      }
      if(_diagLevel > 0){
	_steplen = step.stepLength();
	_stepE = step.ionizingEdep();
	_partP = step.momentum().mag();
	_partPDG = step.simParticle()->pdgId();
	_nsubstep = clusters.size();
	_niontot = 0;
	_qsum = 0.0;
	for(auto iclust=clusters.begin();iclust != clusters.end();++iclust){
	  _niontot += iclust->_nion;
	  _qsum += iclust->_charge;
	}
	_sdiag->Fill();
      }
    }

    void StrawDigisFromStepPointMCs::driftCluster(Straw const& straw,
	IonCluster const& cluster, WireCharge& wireq) {
      // Compute the vector from the cluster to the wire
      CLHEP::Hep3Vector cpos = cluster._pos-straw.getMidPoint();
      // drift distance perp to wire, and angle WRT magnetic field (for Lorentz effect)
      double dd = min(cpos.perp(straw.getDirection()),straw.getDetail().innerRadius());
      // for now ignore Lorentz effects FIXME!!!
      double dphi = 0.0;
      // sample the gain for this cluster 
      double gain = _strawphys->clusterGain(_randgauss, _randflat, cluster._nion);
      wireq._charge = cluster._charge*(gain);
      // smear drift time
      wireq._time = _randgauss.fire(_strawphys->driftDistanceToTime(dd,dphi),
	  _strawphys->driftTimeSpread(dd,dphi));
      wireq._dd = dd;
      // position along wire
      // need to add Lorentz effects, this should be in StrawPhysics, FIXME!!!
      wireq._wpos = cpos.dot(straw.getDirection());
      if(_diagLevel > 0){
	_gain = gain;
	_cq = cluster._charge;
	_nion = cluster._nion;
	_cdiag->Fill(); 
      }
    }

    void StrawDigisFromStepPointMCs::propagateCharge(Straw const& straw,
	WireCharge const& wireq, StrawEnd end, WireEndCharge& weq) {
      // compute distance to the appropriate end
      double wlen = straw.getDetail().halfLength(); // use the full length, not the active length
      // NB: the following assumes the straw direction points in increasing azimuth.  FIXME!
      if(end == TrkTypes::hv)
	weq._wdist = wlen - wireq._wpos;
      else
	weq._wdist = wlen + wireq._wpos;
      // split the charge, and attenuate it according to the distance
      weq._charge = 0.5*wireq._charge*_strawphys->propagationAttenuation(weq._wdist);    // linear time propagation.  Dispersion is handled elsewhere
      weq._time = _strawphys->propagationTime(weq._wdist);
    }

    double StrawDigisFromStepPointMCs::microbunchTime(double globaltime) const {
      // fold time relative to MB frequency
      return fmod(globaltime,_mbtime);
    }

    void StrawDigisFromStepPointMCs::addGhosts(StrawCluster const& clust,StrawClusterSequence& shs) {
      // add enough buffer to cover both the flash blanking and the ADC waveform
      if(clust.time() < _strawele->flashStart()+_mbbuffer)
	shs.insert(StrawCluster(clust,_mbtime));
    }

    void StrawDigisFromStepPointMCs::findThresholdCrossings(StrawWaveform const& swf, WFXList& xings){
      // start when the electronics becomes enabled:
      WFX wfx(swf,_strawele->flashEnd());
      //randomize the threshold to account for electronics noise
      double threshold = _randgauss.fire(_strawele->threshold(),_strawele->analogNoise(TrkTypes::thresh));
      // iterate sequentially over clusts inside the sequence.  Note we fold
      // the flash blanking to AFTER the end of the microbunch
      while( wfx._time < _mbtime+_strawele->flashStart() &&
	  swf.crossesThreshold(threshold,wfx) ){
	// keep these in time-order
	auto iwfxl = xings.begin();
	while(iwfxl != xings.end() && iwfxl->_time < wfx._time)
	  ++iwfxl;
	xings.insert(iwfxl,wfx);
	// insure a minimum time buffer between crossings
	wfx._time += _strawele->deadTimeAnalog();
	if(wfx._time >_mbtime+_strawele->flashStart())
	  break;
	// skip to the next clust
	++(wfx._iclust);
	// update threshold
	threshold = _randgauss.fire(_strawele->threshold(),_strawele->analogNoise(TrkTypes::thresh));
      }
    }

    void StrawDigisFromStepPointMCs::fillDigis(WFXList const& xings, const StrawWaveform wf[2],
	StrawIndex index,
	StrawDigiCollection* digis, StrawDigiMCCollection* mcdigis,
	PtrStepPointMCVectorCollection* mcptrs ){
      // loop over crossings
      auto first_wfx = xings.begin();
      while (first_wfx != xings.end()){
        auto second_wfx = first_wfx; ++second_wfx;
        bool found_coincidence = false;
        while (second_wfx != xings.end()){
          // if we are past coincidence window, stop
          if (!_strawele->combineEnds(first_wfx->_time,second_wfx->_time))
            break;

          // if these are crossings on different ends of the straw
          if (first_wfx->_iclust->strawEnd() != second_wfx->_iclust->strawEnd())
          {
            found_coincidence = true;
            WFXP xpair;
            xpair.push_back(first_wfx);
            xpair.push_back(second_wfx);
            // create a digi from this pair
            createDigi(xpair,wf,index,digis);
            // fill associated MC truth matching. Only count the same step once
            set<art::Ptr<StepPointMC> > xmcsp;
            double wetime[2] = {-100.,-100.};
            CLHEP::HepLorentzVector cpos[2];
            art::Ptr<StepPointMC> stepMC[2];

            for (auto ixp=xpair.begin();ixp!=xpair.end();++ixp){
              StrawCluster const& sh=*((*ixp)->_iclust);
              xmcsp.insert(sh.stepPointMC());
              size_t iend = sh.strawEnd();
              wetime[iend] = sh.time();
              cpos[iend] = sh.clusterPosition();
              stepMC[iend] = sh.stepPointMC();
            }
            // choose the minimum time from either end, as the ADC sums both
            double ptime = 1.0e10;
            for (size_t iend=0;iend<2;++iend){
              if (wetime[iend] > 0)
                ptime = std::min(ptime,wetime[iend]);
            }
            // subtract a small buffer
            ptime -= 0.01*_strawele->adcPeriod();
            // pickup all StepPointMCs associated with clusts inside the time window of the ADC digitizations (after the threshold)
            set<art::Ptr<StepPointMC> > spmcs;
            for (auto ih=wf[0].clusts().clustList().begin();ih!=wf[0].clusts().clustList().end();++ih){
              if (ih->time() >= ptime && ih->time() < ptime +
                  ( _strawele->nADCSamples()-_strawele->nADCPreSamples())*_strawele->adcPeriod())
                spmcs.insert(ih->stepPointMC());
            }
            vector<art::Ptr<StepPointMC> > stepMCs;
            stepMCs.reserve(spmcs.size());
            for(auto ispmc=spmcs.begin(); ispmc!= spmcs.end(); ++ispmc){
              stepMCs.push_back(*ispmc);
            }
            PtrStepPointMCVector mcptr;
            for(auto ixmcsp=xmcsp.begin();ixmcsp!=xmcsp.end();++ixmcsp)
              mcptr.push_back(*ixmcsp);
            mcptrs->push_back(mcptr);
            mcdigis->push_back(StrawDigiMC(index,wetime,cpos,stepMC,stepMCs));
            // diagnostics
            if(_diagLevel > 1)
              digiDiag(xpair,digis->back(),mcdigis->back());

            // we are done looking for a coincidence with this first wfx
            break;
          }

          ++second_wfx;
        }

        // now find next crossing to start looking from for coincidence
        if (found_coincidence){
          first_wfx = second_wfx;
          ++first_wfx;
          // move past deadtime for event readout
          while (first_wfx != xings.end()){
            if (first_wfx->_time > second_wfx->_time + _strawele->deadTimeDigital())
              break;
            ++first_wfx;
          }
        }else{
          // analog deadtime between non triggering crossings enforced when finding crossings
          ++first_wfx;
        }
      }
    }

    void StrawDigisFromStepPointMCs::createDigi(WFXP const& xpair, const StrawWaveform waveform[2],
	StrawIndex index, StrawDigiCollection* digis){
      // storage for MC match can be more than 1 StepPointMCs
      set<art::Ptr<StepPointMC>> mcmatch;
      // initialize the float variables that we later digitize
      TDCTimes xtimes = {0.0,0.0}; 
      TrkTypes::TOTValues tot;
      // smear (coherently) both times for the TDC clock jitter
      double dt = _randgauss.fire(0.0,_strawele->clockJitter());
      // loop over the associated crossings
      for(auto iwfx = xpair.begin();iwfx!= xpair.end();++iwfx){
	WFX const& wfx = **iwfx;
	size_t index = wfx._iclust->strawEnd();
	// record the crossing time for this end, including clock jitter  These already include noise effects
	xtimes[index] = wfx._time+dt;
	// record MC match if it isn't already recorded
	mcmatch.insert(wfx._iclust->stepPointMC());
        // find TOT
        double threshold = _randgauss.fire(_strawele->threshold(),_strawele->analogNoise(TrkTypes::thresh));
        tot[index] = waveform[index].digitizeTOT(threshold,wfx._time + dt);
      }
      //  sums voltages from both waveforms for ADC
      ADCVoltages wf[2];
      // get the sample times from the electroincs
      TrkTypes::ADCTimes adctimes;
      _strawele->adcTimes(xpair[0]->_time,adctimes);
      // sample the waveform from both ends at these times
      for(size_t iend=0;iend<2;++iend){
	waveform[iend].sampleWaveform(TrkTypes::adc,adctimes,wf[iend]);
      }
      // add ends and add noise
      ADCVoltages wfsum; wfsum.reserve(adctimes.size());
      for(unsigned isamp=0;isamp<adctimes.size();++isamp){
	wfsum.push_back(wf[0][isamp]+wf[1][isamp]+_randgauss.fire(0.0,_strawele->analogNoise(TrkTypes::adc)));
      }
      // digitize
      TrkTypes::ADCWaveform adc;
      _strawele->digitizeWaveform(wfsum,adc);
      TrkTypes::TDCValues tdc;
      _strawele->digitizeTimes(xtimes,tdc);
      // create the digi from this
      digis->push_back(StrawDigi(index,tdc,tot,adc));
    }

    // find straws which couple to the given one, and record them and their couplings in XTalk objects.
    // For now, this is just a fixed number for adjacent straws,
    // the couplings and straw identities should eventually come from a database, FIXME!!!
    void StrawDigisFromStepPointMCs::findCrossTalkStraws(Straw const& straw, vector<XTalk>& xtalk) {
      StrawIndex selfind = straw.index();
      xtalk.clear();
      // find straws sensitive to straw-to-straw cross talk
      vector<StrawIndex> const& strawNeighbors = straw.nearestNeighboursByIndex();
      // find straws sensitive to electronics cross talk
      vector<StrawIndex> const& preampNeighbors = straw.preampNeighboursByIndex();
      // convert these to cross-talk
      for(auto isind=strawNeighbors.begin();isind!=strawNeighbors.end();++isind){
	xtalk.push_back(XTalk(selfind,*isind,_preampxtalk,0));
      }
      for(auto isind=preampNeighbors.begin();isind!=preampNeighbors.end();++isind){
	xtalk.push_back(XTalk(selfind,*isind,0,_postampxtalk));
      }
    }

    // functions that need implementing:: FIXME!!!!!!
    void StrawDigisFromStepPointMCs::addNoise(StrawClusterMap& hmap){
      // create random noise clusts and add them to the sequences of random straws.
    }
    // diagnostic functions
    void StrawDigisFromStepPointMCs::waveformDiag(const StrawWaveform wfs[2],
	WFXList const& xings) {
      const Tracker& tracker = getTrackerOrThrow();
      const Straw& straw = tracker.getStraw( wfs[0].clusts().strawIndex() );
      _splane = straw.id().getPlane();
      _spanel = straw.id().getPanel();
      _slayer = straw.id().getLayer();
      _sstraw = straw.id().getStraw();
      ClusterList const& clusts = wfs[0].clusts().clustList();
      _nclust = clusts.size();
      _iclust = -1;
      set<art::Ptr<StepPointMC> > steps;
      set<art::Ptr<SimParticle> > parts;
      _nxing = xings.size();
      _txing = _mbtime+_mbbuffer;
      _xddist = _xwdist = _xpdist = -1.0;
      if(_nxing > 0){
	for(auto ixing=xings.begin();ixing!=xings.end();++ixing){
	  _txing = min(_txing,static_cast<float_t>(ixing->_time));
	  // find the clust index of the 1st crossing
	  for(auto ih=clusts.begin();ih!= clusts.end();++ih){
	    if(ih == ixing->_iclust)break;
	    ++_iclust;
	  }
	  _xddist = ixing->_iclust->driftDistance();
	  _xwdist = ixing->_iclust->wireDistance();
	  // compute direction perpendicular to wire and momentum
	  art::Ptr<StepPointMC> const& spp = ixing->_iclust->stepPointMC();
	  if(!spp.isNull()){
	    CLHEP::Hep3Vector pdir = straw.getDirection().cross(spp->momentum()).unit();
	    // project the differences in position to get the perp distance
	    _xpdist = pdir.dot(spp->position()-straw.getMidPoint());
	  }
	}
      } else {
	// no xings: just take the 1st clust
	if(_nclust > 0 ){
	  _iclust = 0;
	  _xddist = clusts.front().driftDistance();
	  _xwdist = clusts.front().wireDistance();
	  art::Ptr<StepPointMC> const& spp = clusts.front().stepPointMC();
	  if(!spp.isNull()){
	    CLHEP::Hep3Vector pdir = straw.getDirection().cross(spp->momentum()).unit();
	    // project the differences in position to get the perp distance
	    _xpdist = pdir.dot(spp->position()-straw.getMidPoint());
	  }
	}
      }
      _hqsum = 0.0;
      _vmax = _tvmax = 0.0;
      _wmcpdg=0;
      _wmcproc=0;
      for(auto ihitl=clusts.begin();ihitl!=clusts.end();++ihitl){
	if(ihitl->stepPointMC().isNonnull()){
	  steps.insert(ihitl->stepPointMC());
	  parts.insert(ihitl->stepPointMC()->simParticle());
	  _hqsum += ihitl->charge();
	  double htime = ihitl->time()+_strawele->maxResponseTime(_diagpath);
	  double vout = wfs[0].sampleWaveform(_diagpath,htime);
	  if(vout > _vmax){
	    _vmax = vout;
	    _tvmax = htime;
	    _wmcpdg = ihitl->stepPointMC()->simParticle()->pdgId();
	    _wmcproc = ihitl->stepPointMC()->simParticle()->creationCode();
	    _mce = ihitl->stepPointMC()->simParticle()->startMomentum().e();
	    _slen = ihitl->stepPointMC()->stepLength();
	    _sedep = ihitl->stepPointMC()->ionizingEdep();
	  }
	}
      }
      _nsteppoint = steps.size();
      _npart = parts.size();
      _sesum = 0.0;
      for(auto istep=steps.begin();istep!=steps.end();++istep)
	_sesum += (*istep)->ionizingEdep();
      _tmin = clusts.begin()->time();
      _tmax = clusts.rbegin()->time();
      _wfxtalk = !wfs[0].xtalk().self();
      _swdiag->Fill();
      if(_diagLevel > 2) {
	// histogram individual waveforms
	static unsigned nhist(0);// maximum number of histograms per job!
	bool histthis = _noxhist && _nxing==0;
	histthis |= _xtalkhist && _wfxtalk;
	histthis |= (!_xtalkhist) && (!_noxhist);
	histthis &=  _nxing >= _minnxinghist;
	// histogram the waveforms
	if(nhist < _maxhist && histthis ) {
	  static const double tstep(0.1); // 0.1 ns
	  static const double nfall(5.0); // 5 lambda past last fall time
	  double tstart = clusts.begin()->time()-tstep;
	  double tfall = _strawele->fallTime(_diagpath);
	  double tend = clusts.rbegin()->time() + nfall*tfall;
	  ADCTimes times;
	  ADCVoltages volts;
	  times.reserve(size_t(rint(tend-tstart)/tstep));
	  volts.reserve(size_t(rint(tend-tstart)/tstep));
	  double t = tstart;
	  while(t<tend){
	    times.push_back(t);
	    t += tstep;
	  }
	  wfs[0].sampleWaveform(_diagpath,times,volts);
	  ++nhist;
	  art::ServiceHandle<art::TFileService> tfs;
	  char name[60];
	  char title[100];
	  snprintf(name,60,"SWF%i_%i",wfs[0].clusts().strawIndex().asInt(),nhist);
	  snprintf(title,100,"Electronic output for straw %i event %i;time (nSec);mVolts",wfs[0].clusts().strawIndex().asInt(),nhist);
	  TH1F* wfh = tfs->make<TH1F>(name,title,volts.size(),times.front(),times.back());
	  for(size_t ibin=0;ibin<times.size();++ibin)
	    wfh->SetBinContent(ibin+1,volts[ibin]);
	  TList* flist = wfh->GetListOfFunctions();
	  for(auto ixing=xings.begin();ixing!=xings.end();++ixing){
	    if(ixing->_iclust->strawEnd() == wfs[0].strawEnd()){
	      TMarker* smark = new TMarker(ixing->_time,ixing->_vcross,8);
	      smark->SetMarkerColor(kGreen);
	      smark->SetMarkerSize(2);
	      flist->Add(smark);
	    }
	  }
	  _waveforms.push_back(wfh);
	}
      }
    }

    void StrawDigisFromStepPointMCs::digiDiag(WFXP const& xpair, StrawDigi const& digi,StrawDigiMC const& mcdigi) {
      const Tracker& tracker = getTrackerOrThrow();
      const Straw& straw = tracker.getStraw( digi.strawIndex() );
      _sdplane = straw.id().getPlane();
      _sdpanel = straw.id().getPanel();
      _sdlayer = straw.id().getLayer();
      _sdstraw = straw.id().getStraw();
      _xtime0 = _xtime1 = -1000.0;
      _htime0 = _htime1 = -1000.0;
      _charge0 = _charge1 = -1000.0;
      _wdist0 = _wdist1 = -1000.0;
      _ddist0 = _ddist1 = -1000.0;
      _vstart0 = _vstart1 = -1000.0;
      _vcross0 = _vcross1 = -1000.0;
      _nend = xpair.size();
      for(auto ixp=xpair.begin();ixp!=xpair.end();++ixp){
	if((*ixp)->_iclust->strawEnd() == TrkTypes::cal) {
	  _xtime0 =(*ixp)->_time;
	  _htime0 = (*ixp)->_iclust->time();
	  _charge0 = (*ixp)->_iclust->charge();
	  _ddist0 = (*ixp)->_iclust->driftDistance();
	  _wdist0 = (*ixp)->_iclust->wireDistance();
	  _vstart0 = (*ixp)->_vstart;
	  _vcross0 = (*ixp)->_vcross;
	} else {
	  _xtime1 =(*ixp)->_time;
	  _htime1 = (*ixp)->_iclust->time();
	  _charge1 = (*ixp)->_iclust->charge();
	  _ddist1 = (*ixp)->_iclust->driftDistance();
	  _wdist1 = (*ixp)->_iclust->wireDistance();
	  _vstart1 = (*ixp)->_vstart;
	  _vcross1 = (*ixp)->_vcross;
	}
      }
      if(xpair.size() < 2 || xpair[0]->_iclust->stepPointMC() == xpair[1]->_iclust->stepPointMC())
	_nstep = 1;
      else
	_nstep = 2;
      _tdccal = digi.TDC(TrkTypes::cal);
      _tdchv = digi.TDC(TrkTypes::hv);
      _totcal = digi.TOT(TrkTypes::cal);
      _tothv = digi.TOT(TrkTypes::hv);
      _adc.clear();
      for(auto iadc=digi.adcWaveform().begin();iadc!=digi.adcWaveform().end();++iadc){
	_adc.push_back(*iadc);
      }
      // mc truth information
      _dmcpdg = _dmcproc = _dmcgen = 0;
      _dmcmom = -1.0;
      _mctime = _mcenergy = _mctrigenergy = _mcthreshenergy = _mcdca = -1000.0;
      _mcthreshpdg = _mcthreshproc = _mcnstep = 0;
      art::Ptr<StepPointMC> const& spmc = xpair[0]->_iclust->stepPointMC();
      if(!spmc.isNull()){
	_mctime = _toff.timeWithOffsetsApplied(*spmc);
	// compute the doca for this step
	TwoLinePCA pca( straw.getMidPoint(), straw.getDirection(),
	    spmc->position(), spmc->momentum().unit() );
	_mcdca = pca.dca();
	if(!spmc->simParticle().isNull()){
	  _dmcpdg = spmc->simParticle()->pdgId();
	  _dmcproc = spmc->simParticle()->creationCode();
	  if(spmc->simParticle()->genParticle().isNonnull())
	    _dmcgen = spmc->simParticle()->genParticle()->generatorId().id();
	  _dmcmom = spmc->momentum().mag();
	}
      }
      _mcenergy = mcdigi.energySum();
      _mctrigenergy = mcdigi.triggerEnergySum(TrkTypes::cal);
      // sum the energy from the explicit trigger particle, and find it's releationship
      _mcthreshenergy = 0.0;
      _mcnstep = mcdigi.stepPointMCs().size();
      art::Ptr<StepPointMC> threshpart = mcdigi.stepPointMC(TrkTypes::cal);
      if(threshpart.isNull()) threshpart = mcdigi.stepPointMC(TrkTypes::hv);
      for(auto imcs = mcdigi.stepPointMCs().begin(); imcs!= mcdigi.stepPointMCs().end(); ++ imcs){
	// if the SimParticle for this step is the same as the one which fired the discrim, add the energy
	if( (*imcs)->simParticle() == threshpart->simParticle() )
	  _mcthreshenergy += (*imcs)->eDep();
      }
      _mcthreshpdg = threshpart->simParticle()->pdgId();
      _mcthreshproc = threshpart->simParticle()->creationCode();

      // check for x-talk
      _xtalk = digi.strawIndex() != spmc->strawIndex();
      // fill the tree entry
      _sddiag->Fill();
    }
  } // end namespace trackermc
} // end namespace mu2e

using mu2e::TrackerMC::StrawDigisFromStepPointMCs;
DEFINE_ART_MODULE(StrawDigisFromStepPointMCs);


