//
//  Collection of tools useful for dealing with various tracking functions
//  not specific to a module or task.
//  Original Author Dave Brown (LBNL) 26 Aug. 2016
//
#ifndef TrkReco_TrkUtilities_HH
#define TrkReco_TrkUtilities_HH
#include "CLHEP/Vector/ThreeVector.h"
#include <vector>

class HelixTraj;
class BbrVectorErr;
class KalRep;
namespace mu2e {
  class RobustHelix;
  class KalSegment;
  class TrkStrawHitSeed;
  namespace TrkUtilities {
  // convert the robust helix format into the BaBar format HelixTraj.  This requires
  // the sign of the angular momentum about the z axis, as the BaBar rep os semi-kinematic
    bool RobustHelix2Traj (RobustHelix const& helix, HelixTraj &traj, float amsign);
  // create a robust helix from raw particle informaiton.  This is useful for MC comparison
    void RobustHelixFromMom(CLHEP::Hep3Vector const& pos, CLHEP::Hep3Vector const& mom, double charge, double Bz, RobustHelix& helix);
    // create a KalSegment (helix segment) from a HelixTraj
    void fillSegment(HelixTraj const& htraj, BbrVectorErr const& momerr, KalSegment& kseg);
    // create HitSeeds from the TrkStrawHits in a KalRep
    void fillHitSeeds(const KalRep* krep, std::vector<TrkStrawHitSeed>& hitseeds);
  }
}
#endif
