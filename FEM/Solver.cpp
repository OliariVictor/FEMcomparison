//
// Created by victor on 02/09/2020.
//

#include "Solver.h"
#include <TPZMultiphysicsCompMesh.h>
#include "pzanalysis.h"
#include "DataStructure.h"
#include "MeshInit.h"
#include "TPZCompMeshTools.h"
#include "TPZCreateMultiphysicsSpace.h"
#include "TPZSSpStructMatrix.h"
#include "TPZSSpStructMatrix.h"
#include "TPZParFrontStructMatrix.h"
#include "pzskylstrmatrix.h"
#include "pzstepsolver.h"
#include "Tools.h"
#include "Output.h"

void Solve(ProblemConfig &config, PreConfig &preConfig){

    TPZCompMesh *cmesh = InsertCMeshH1(config,preConfig);
    TPZMultiphysicsCompMesh *multiCmesh = new TPZMultiphysicsCompMesh(config.gmesh);
    int interfaceMatID = -10;
    int hybridLevel = 1;

    const clock_t start = clock();

    switch(preConfig.mode){
        case 0: //H1
            TPZCompMeshTools::CreatedCondensedElements(cmesh, false, false);
            SolveH1Problem(cmesh, config, preConfig);
            break;
        case 1: //Hybrid
            CreateHybridH1ComputationalMesh(multiCmesh, interfaceMatID,preConfig, config,hybridLevel);
            SolveHybridH1Problem(multiCmesh, interfaceMatID, config, preConfig,hybridLevel);
            break;
        case 2: //Mixed
            CreateMixedComputationalMesh(multiCmesh, preConfig, config);
            SolveMixedProblem(multiCmesh, config, preConfig);
            break;
        default:
            DebugStop();
            break;
    }
    FlushTime(preConfig,start);

    if(preConfig.debugger) DrawMesh(config,preConfig,cmesh,multiCmesh);
}

void DrawMesh(ProblemConfig &config, PreConfig &preConfig, TPZCompMesh *cmesh, TPZMultiphysicsCompMesh *multiCmesh) {

    std::stringstream ref;
    ref << "_ref-" << 1/preConfig.h <<" x " << 1/preConfig.h;
    std::string refinement =  ref.str();

    std::ofstream out(preConfig.plotfile + "/gmesh"+ refinement + ".vtk");
    std::ofstream out2(preConfig.plotfile + "/gmesh"+ refinement + "txt");
    std::ofstream out3(preConfig.plotfile + "/cmesh.txt");

    TPZVTKGeoMesh::PrintGMeshVTK(config.gmesh, out);
    config.gmesh->Print(out2);

    if (preConfig.mode == 0) cmesh->Print(out3);
    else multiCmesh->Print(out3);
}

void CreateMixedComputationalMesh(TPZMultiphysicsCompMesh *cmesh_Mixed, PreConfig &pConfig, ProblemConfig &config){

    int matID = 1;
    int dim = config.gmesh->Dimension();

    //Flux mesh creation
    TPZCompMesh *cmesh_flux = new TPZCompMesh(config.gmesh);
    BuildFluxMesh(cmesh_flux, config,pConfig);

    //Potential mesh creation
    TPZCompMesh *cmesh_p = new TPZCompMesh(config.gmesh);
    BuildPotentialMesh(cmesh_p, config, pConfig);

    //Multiphysics mesh build
    InsertMaterialMixed(cmesh_Mixed, config,pConfig);
    TPZManVector<int> active(2, 1);
    TPZManVector<TPZCompMesh *, 2> meshvector(2);
    meshvector[0] = cmesh_flux;
    meshvector[1] = cmesh_p;
    TPZCompMeshTools::AdjustFluxPolynomialOrders(meshvector[0], config.n); //Increases internal flux order by "hdivmais"
    TPZCompMeshTools::SetPressureOrders(meshvector[0], meshvector[1]);//Set the pressure order the same as the internal flux

    cmesh_Mixed->BuildMultiphysicsSpace(active,meshvector);
    bool keeponelagrangian = true, keepmatrix = false;
    TPZCompMeshTools::CreatedCondensedElements(cmesh_Mixed, keeponelagrangian, keepmatrix);
    cmesh_Mixed->LoadReferences();
    cmesh_Mixed->InitializeBlock();
}

void CreateHybridH1ComputationalMesh(TPZMultiphysicsCompMesh *cmesh_H1Hybrid,int &interFaceMatID , PreConfig &pConfig, ProblemConfig &config,int hybridLevel){
    auto spaceType = TPZCreateMultiphysicsSpace::EH1Hybrid;
    if(hybridLevel == 2) {
        spaceType = TPZCreateMultiphysicsSpace::EH1HybridSquared;
    }
    else if(hybridLevel != 1) {
        DebugStop();
    }

    TPZCreateMultiphysicsSpace createspace(config.gmesh, spaceType);
    //TPZCreateMultiphysicsSpace createspace(config.gmesh);
    std::cout << cmesh_H1Hybrid->NEquations();

    createspace.SetMaterialIds({1,2,3}, {-6,-5,-2,-1});
    createspace.fH1Hybrid.fHybridizeBCLevel = 1;//opcao de hibridizar o contorno
    createspace.ComputePeriferalMaterialIds();

    TPZManVector<TPZCompMesh *> meshvec;

    int pOrder = config.n+config.k;
    createspace.CreateAtomicMeshes(meshvec,pOrder,config.k);

    InsertMaterialHybrid(cmesh_H1Hybrid, config,pConfig);
    createspace.InsertPeriferalMaterialObjects(cmesh_H1Hybrid);
    cmesh_H1Hybrid->BuildMultiphysicsSpace(meshvec);
    createspace.InsertLagranceMaterialObjects(cmesh_H1Hybrid);

    createspace.AddInterfaceElements(cmesh_H1Hybrid);
    createspace.GroupandCondenseElements(cmesh_H1Hybrid);

    cmesh_H1Hybrid->InitializeBlock();
    cmesh_H1Hybrid->ComputeNodElCon();

    interFaceMatID = createspace.fH1Hybrid.fLagrangeMatid.first;

}

void SolveH1Problem(TPZCompMesh *cmeshH1,struct ProblemConfig &config, struct PreConfig &pConfig){

    config.exact.operator*().fSignConvention = -1;

    std::cout << "Solving H1 " << std::endl;

    TPZAnalysis an(cmeshH1);

#ifdef USING_MKL
    TPZSymetricSpStructMatrix strmat(cmeshH1);
    strmat.SetNumThreads(8);
    //        strmat.SetDecomposeType(ELDLt);
#else
    TPZParFrontStructMatrix<TPZFrontSym<STATE> > strmat(cmeshH1);
    strmat.SetNumThreads(0);
    //        TPZSkylineStructMatrix strmat3(cmesh_HDiv);
    //        strmat3.SetNumThreads(8);
#endif

    std::set<int> matids;
    for (auto matid : config.materialids) matids.insert(matid);

    for(auto mat:config.bcmaterialids){
        matids.insert(mat);
    }

    strmat.SetMaterialIds(matids);
    an.SetStructuralMatrix(strmat);

    TPZStepSolver<STATE> *direct = new TPZStepSolver<STATE>;
    direct->SetDirect(ELDLt);
    an.SetSolver(*direct);
    delete direct;
    direct = 0;
    an.Assemble();
    an.Solve();//resolve o problema misto ate aqui

    int64_t nelem = cmeshH1->NElements();
    cmeshH1->LoadSolution(cmeshH1->Solution());
    cmeshH1->ExpandSolution();
    cmeshH1->ElementSolution().Redim(nelem, 10);

    ////Calculo do erro
    std::cout << "Computing Error H1 " << std::endl;

    an.SetExact(config.exact.operator*().ExactSolution());

    StockErrorsH1(an,cmeshH1,pConfig.Erro,pConfig.Log,pConfig);

    ////PostProcess
    if(pConfig.debugger) {
        TPZStack<std::string> scalnames, vecnames;
        scalnames.Push("Solution");
        vecnames.Push("Derivative");
        scalnames.Push("ExactSolution");

        int dim = cmeshH1->Reference()->Dimension();

        std::string plotname;
        {
            std::stringstream out;
            out << pConfig.plotfile /* << config.dir_name*/ << "/" << "H1_Problem" << config.k << "_" << dim
                << "D_" << config.problemname << "Ndiv_ " << config.ndivisions << ".vtk";
            plotname = out.str();
        }
        int resolution=0;
        an.DefineGraphMesh(dim, scalnames, vecnames, plotname);
        an.PostProcess(resolution,dim);
    }

    std::cout << "FINISHED!" << std::endl;
}

void SolveHybridH1Problem(TPZMultiphysicsCompMesh *cmesh_H1Hybrid,int InterfaceMatId, struct ProblemConfig config,struct PreConfig &pConfig,int hybridLevel){

    config.exact.operator*().fSignConvention = 1;

    std::cout << "Solving HYBRID_H1 " << std::endl;

    TPZAnalysis an(cmesh_H1Hybrid);

#ifdef USING_MKL
    TPZSymetricSpStructMatrix strmat(cmesh_H1Hybrid);
    strmat.SetNumThreads(0);
    //        strmat.SetDecomposeType(ELDLt);
#else
    //    TPZFrontStructMatrix<TPZFrontSym<STATE> > strmat(Hybridmesh);
    //    strmat.SetNumThreads(2);
    //    strmat.SetDecomposeType(ELDLt);
    TPZSkylineStructMatrix strmat(cmesh_H1Hybrid);
    strmat.SetNumThreads(0);
#endif
    std::set<int> matIds;
    for (auto matid : config.materialids) matIds.insert(matid);
    for (auto matidbc : config.bcmaterialids) matIds.insert(matidbc);

    matIds.insert(InterfaceMatId);
    strmat.SetMaterialIds(matIds);
    an.SetStructuralMatrix(strmat);

    TPZStepSolver<STATE>* direct = new TPZStepSolver<STATE>;
    direct->SetDirect(ELDLt);
    an.SetSolver(*direct);
    delete direct;
    direct = 0;
    an.Assemble();
    an.Solve();

    int64_t nelem = cmesh_H1Hybrid->NElements();
    cmesh_H1Hybrid->LoadSolution(cmesh_H1Hybrid->Solution());
    cmesh_H1Hybrid->ExpandSolution();
    cmesh_H1Hybrid->ElementSolution().Redim(nelem, 5);

    ////Calculo do erro
    std::cout << "Computing Error HYBRID_H1 " << std::endl;

    an.SetExact(config.exact.operator*().ExactSolution());

    std::cout << "DOF = " << cmesh_H1Hybrid->NEquations() << std::endl;

   StockErrors(an,cmesh_H1Hybrid,pConfig.Erro,pConfig.Log,pConfig);

    ////PostProcess

    if(pConfig.debugger) {
        TPZStack<std::string> scalnames, vecnames;
        scalnames.Push("Pressure");
        scalnames.Push("PressureExact");
        vecnames.Push("Flux");

        int dim = 2;
        std::string plotname;
        {
            std::stringstream out;
            out << pConfig.plotfile /* << config.dir_name*/ << "/"
                << config.problemname << "_k-" << config.k
                << "_n-" << config.n << "_ref_" << 1/pConfig.h << " x " << 1/pConfig.h <<".vtk";
            plotname = out.str();
        }
        int resolution = 0;
        an.DefineGraphMesh(dim, scalnames, vecnames, plotname);
        an.PostProcess(resolution, dim);
    }
}

void SolveMixedProblem(TPZMultiphysicsCompMesh *cmesh_Mixed,struct ProblemConfig config,struct PreConfig &pConfig) {

    config.exact.operator*().fSignConvention = 1;
    bool optBW = true;

    std::cout << "Solving Mixed " << std::endl;
    TPZAnalysis an(cmesh_Mixed, optBW); //Cria objeto de análise que gerenciará a analise do problema

    //MKL solver
#ifdef USING_MKL
    TPZSymetricSpStructMatrix strmat(cmesh_Mixed);
    strmat.SetNumThreads(8);
#else
    TPZSkylineStructMatrix strmat(cmesh_H1Hybrid);
    strmat.SetNumThreads(0);
#endif
    an.SetStructuralMatrix(strmat);

    TPZStepSolver<STATE>* direct = new TPZStepSolver<STATE>;
    direct->SetDirect(ELDLt);
    an.SetSolver(*direct);
    delete direct;
    direct = 0;
    an.Assemble();
    an.Solve();

    ////Calculo do erro
    std::cout << "Computing Error MIXED " << std::endl;

    an.SetExact(config.exact.operator*().ExactSolution());

    std::cout << "DOF = " << cmesh_Mixed->NEquations() << std::endl;

    StockErrors(an,cmesh_Mixed,pConfig.Erro,pConfig.Log,pConfig);

    ////PostProcess
    if(pConfig.debugger) {

        int dim = config.gmesh->Dimension();
        std::string plotname;
        {
            std::stringstream out;
            out << pConfig.plotfile  << "/"
                << config.problemname << "_Mixed_k-" << config.k
                << "_n-" << config.n << "_ref-" << 1/pConfig.h <<" x " << 1/pConfig.h << ".vtk";
            plotname = out.str();
        }

        TPZStack<std::string> scalnames, vecnames;
        scalnames.Push("Pressure");
        scalnames.Push("ExactPressure");
        vecnames.Push("Flux");
        vecnames.Push("ExactFlux");

        int resolution = 0;
        an.DefineGraphMesh(dim, scalnames, vecnames, plotname);
        an.PostProcess(resolution, dim);
    }
}

void StockErrorsH1(TPZAnalysis &an,TPZCompMesh *cmesh, ofstream &Erro, TPZVec<REAL> *Log,PreConfig &pConfig){

    TPZManVector<REAL,6> Errors;
    Errors.resize(pConfig.numErrors);
    bool store_errors = false;

    an.PostProcessError(Errors, store_errors, Erro);

    if ((*Log)[0] != -1) {
        for (int j = 0; j < 3; j++) {
            (*pConfig.rate)[j] =
                    (log10(Errors[j]) - log10((*Log)[j])) /
                    (log10(pConfig.h) - log10(pConfig.hLog));
            Erro << "rate " << j << ": " << (*pConfig.rate)[j] << std::endl;
        }
    }

    Erro << "h = " << pConfig.h << std::endl;
    Erro << "DOF = " << cmesh->NEquations() << std::endl;
    for (int i = 0; i < pConfig.numErrors; i++)
        (*Log)[i] = Errors[i];
    Errors.clear();
}

void StockErrors(TPZAnalysis &an,TPZMultiphysicsCompMesh *cmesh, ofstream &Erro, TPZVec<REAL> *Log,PreConfig &pConfig){

    TPZManVector<REAL,6> Errors;
    Errors.resize(pConfig.numErrors);
    bool store_errors = false;

    an.PostProcessError(Errors, store_errors, Erro);

    if ((*Log)[0] != -1) {
        for (int j = 0; j < 3; j++) {
            (*pConfig.rate)[j] =
                    (log10(Errors[j]) - log10((*Log)[j])) /
                    (log10(pConfig.h) - log10(pConfig.hLog));
            Erro << "rate " << j << ": " << (*pConfig.rate)[j] << std::endl;
        }
    }

    Erro << "h = " << pConfig.h << std::endl;
    Erro << "DOF = " << cmesh->NEquations() << std::endl;
    for (int i = 0; i < pConfig.numErrors; i++)
        (*Log)[i] = Errors[i];
    Errors.clear();
}