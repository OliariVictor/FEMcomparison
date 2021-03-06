//
//  ProblemConfig.h
//  ErrorEstimation
//
//  Created by Philippe Devloo on 09/05/18.
//

#ifndef ProblemConfig_h
#define ProblemConfig_h

#include <set>
#include "TPZAnalyticSolution.h"

/// class to guide the error estimator
struct ProblemConfig
{
    
    /// geometric mesh on which the computational meshes are based
    TPZGeoMesh *gmesh = 0;
    /// polynomial order of the original mesh
    int k = -1;
    int n = -1;

    /// option to compute the error based on continuous pressures or not
    bool makepressurecontinuous = true;
    
    /// number of uniform refinements applied to the mesh
    int ndivisions = -1;
    int adaptivityStep = -1;
    int dimension = 0;
    bool prefine = false;
    bool steklovexample = false;
    bool GalvisExample = false;
    bool TensorNonConst = false;
    bool MeshNonConvex = false;
    
    STATE alpha=1;
    STATE Km = 0.;
    STATE coefG = 0.;
    /// directory where the files will be stored
    std::string dir_name = ".";
    /// name identifying the problem
    std::string problemname;
    /// set of materialids in the mesh
    std::set<int> materialids;
    /// set of boundary condition material ids
    std::set<int> bcmaterialids;
    /// exact solution
    TPZAutoPointer<TLaplaceExample1> exact;

    ProblemConfig() = default;

    ProblemConfig(const ProblemConfig &cp) = default;

    ProblemConfig &operator=(const ProblemConfig &cp) = default;
};

struct PreConfig{
    std::ofstream Erro, timer;
    TPZVec<REAL> *rate, *Log;
    int refLevel = -1;

    int k = 1;
    int n = 1;
    std::string problem;
    std::string approx;

    REAL perm_Q1 = 5;
    REAL perm_Q2 = 1;

    REAL hLog = -1, h = -1000;
    int numErrors = 4;

    std::string plotfile;
    int mode = -1;           // 0 = "H1"; 1 = "Hybrid"; 2 = "Mixed";
    int argc = 1;
    int type= -1;

    bool debugger = true;
    int exp = 2; // Initial exponent of mesh refinement (numElem = 2*2^exp)
};

#endif /* ProblemConfig_h */
