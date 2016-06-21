/*******************************************************************************
*   OpenHyperFLOW2D-CUDA                                                       *
*                                                                              *
*   Transient, Density based Effective Explicit Parallel Hybrid Solver         *
*   TDEEPHS (CUDA+MPI)                                                         *
*   Version  2.0.1                                                             *
*   Copyright (C)  1995-2016 by Serge A. Suchkov                               *
*   Copyright policy: LGPL V3                                                  *
*   http://github.com/sergeas67/openhyperflow2d                                *
*                                                                              *
*   deeps2d_core.cpp: OpenHyperFLOW2D solver core code....                     *
*                                                                              *
*  last update: 14/04/2016                                                     *
********************************************************************************/
#include "deeps2d_core.hpp"

#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/file.h>

#ifdef _MPI_

inline void  DEEPS2D_Stage1(UMatrix2D< FlowNode2D<FP,NUM_COMPONENTS> >*     pLJ,
                     UMatrix2D< FlowNodeCore2D<FP,NUM_COMPONENTS> >* pLC,
                     int MIN_X, int MAX_X, int MAX_Y,
                     FP dxx, FP dyy,
                     FP dtdx, FP dtdy,
                     int is_local_timestep) {

    for (int i = MIN_X;i<MAX_X;i++ ) {
       for (int j = 0;j<MAX_Y;j++ ) {

          FlowNode2D< FP,NUM_COMPONENTS >* CurrentNode=NULL;

          CurrentNode = &(pLJ->GetValue(i,j)); 

          if ( CurrentNode->isCond2D(CT_NODE_IS_SET_2D) &&
                 !CurrentNode->isCond2D(CT_SOLID_2D)
              && !CurrentNode->isCond2D(NT_FC_2D)
              ) {

              FlowNodeCore2D< FP,NUM_COMPONENTS >* NextNode=NULL;

              FlowNode2D< FP,NUM_COMPONENTS >* UpNode=NULL;          // near
              FlowNode2D< FP,NUM_COMPONENTS >* DownNode=NULL;        // nodes
              FlowNode2D< FP,NUM_COMPONENTS >* LeftNode=NULL;        // references
              FlowNode2D< FP,NUM_COMPONENTS >* RightNode=NULL;

              FP beta;  
              FP _beta; 
              FP dXX,dYY;

              int Num_Eq = FlowNode2D<FP,NUM_COMPONENTS>::NumEq-SetTurbulenceModel(CurrentNode);

              NextNode    = &(pLC->GetValue(i,j)); 

              int  n1=CurrentNode->idXl;
              int  n2=CurrentNode->idXr;
              int  n3=CurrentNode->idYu;
              int  n4=CurrentNode->idYd;

              int  N1 = i - n1;
              int  N2 = i + n2;
              int  N3 = j + n3;
              int  N4 = j - n4;

              int  n_n = max(n1+n2,1);
              int  m_m = max(n3+n4,1);

              FP  n_n_1 = 1./n_n;
              FP  m_m_1 = 1./m_m;

              UpNode    = &(pLJ->GetValue(i,N3));
              DownNode  = &(pLJ->GetValue(i,N4));
              RightNode = &(pLJ->GetValue(N2,j));
              LeftNode  = &(pLJ->GetValue(N1,j));

              // Scan equation system ... k - number of equation
              for (int k=0;k<Num_Eq;k++ ) {
                    int      c_flag = 0;
                    int      dx_flag, dx2_flag;
                    int      dy_flag, dy2_flag;

                    beta  = CurrentNode->beta[k];
                    _beta = 1. - beta;

                // Precomputed variables for current node ...
                    c_flag  = dx_flag = dy_flag = dx2_flag = dy2_flag = 0;
                    if ( k < 4 ) { // Make bit flags for future test for current equation
                        c_flag   = CT_Rho_CONST_2D     << k; 
                        dx_flag  = CT_dRhodx_NULL_2D   << k;
                        dy_flag  = CT_dRhody_NULL_2D   << k;
                        dx2_flag = CT_d2Rhodx2_NULL_2D << k;
                        dy2_flag = CT_d2Rhody2_NULL_2D << k;
                    } else if (k < (4+NUM_COMPONENTS)) {
                        c_flag   = CT_Y_CONST_2D;
                        dx_flag  = CT_dYdx_NULL_2D;
                        dy_flag  = CT_dYdy_NULL_2D;
                        dx2_flag = CT_d2Ydx2_NULL_2D;
                        dy2_flag = CT_d2Ydy2_NULL_2D;
                    } else if ((CurrentNode->isTurbulenceCond2D(TCT_k_eps_Model_2D) ||
                                CurrentNode->isTurbulenceCond2D(TCT_Spalart_Allmaras_Model_2D))) { //
                      if( k == i2d_k) {
                          c_flag   = TCT_k_CONST_2D     << (k-4-NUM_COMPONENTS); 
                          dx_flag  = TCT_dkdx_NULL_2D   << (k-4-NUM_COMPONENTS);
                          dy_flag  = TCT_dkdy_NULL_2D   << (k-4-NUM_COMPONENTS);
                          dx2_flag = TCT_d2kdx2_NULL_2D << (k-4-NUM_COMPONENTS);
                          dy2_flag = TCT_d2kdy2_NULL_2D << (k-4-NUM_COMPONENTS);
                      } else if (k == i2d_eps) {
                          c_flag   = TCT_eps_CONST_2D     << (k-4-NUM_COMPONENTS); 
                          dx_flag  = TCT_depsdx_NULL_2D   << (k-4-NUM_COMPONENTS);
                          dy_flag  = TCT_depsdy_NULL_2D   << (k-4-NUM_COMPONENTS);
                          dx2_flag = TCT_d2epsdx2_NULL_2D << (k-4-NUM_COMPONENTS);
                          dy2_flag = TCT_d2epsdy2_NULL_2D << (k-4-NUM_COMPONENTS);
                      }
                    }
                    // Check BC for current equation
                    if (k<(4+NUM_COMPONENTS)) {

                        if ( CurrentNode->isCond2D((CondType2D)c_flag) )
                            c_flag  = 0;
                        else
                            c_flag  = 1;

                        if ( CurrentNode->isCond2D((CondType2D)dx_flag) ) {
                            dx_flag = 0;
                        } else {
                            dx_flag = 1;
                        }

                        if ( CurrentNode->isCond2D((CondType2D)dy_flag) ) {
                            dy_flag = 0;
                        } else {
                            dy_flag = 1;
                        }

                        if ( CurrentNode->isCond2D((CondType2D)dx2_flag) ) {
                            dx2_flag = 1;
                        } else {
                            dx2_flag = 0;
                        }

                        if ( CurrentNode->isCond2D((CondType2D)dy2_flag) ) {
                            dy2_flag = 1;
                        } else {
                            dy2_flag = 0;
                        }
                    } else if((CurrentNode->isTurbulenceCond2D(TCT_k_eps_Model_2D) ||
                               CurrentNode->isTurbulenceCond2D(TCT_Spalart_Allmaras_Model_2D)) ) {
                        if ( CurrentNode->isTurbulenceCond2D((TurbulenceCondType2D)c_flag) )
                            c_flag  = 0;
                        else
                            c_flag  = 1;

                        if ( CurrentNode->isTurbulenceCond2D((TurbulenceCondType2D)dx_flag) ) {
                            dx_flag = 0;
                        } else {
                            dx_flag = 1;
                        }
                        if ( CurrentNode->isTurbulenceCond2D((TurbulenceCondType2D)dy_flag) ) {
                            dy_flag = 0;
                        } else {
                            dy_flag = 1;
                        }
                        if ( CurrentNode->isTurbulenceCond2D((TurbulenceCondType2D)dx2_flag) ) {
                            dx2_flag = 1;
                        } else {
                            dx2_flag = 0;
                        }
                        if ( CurrentNode->isTurbulenceCond2D((TurbulenceCondType2D)dy2_flag) ) {
                            dy2_flag = 1;
                        } else {
                            dy2_flag = 0;
                        }
                    }
                    if ( c_flag ) {
                        if ( dx_flag ) {
                            dXX = CurrentNode->dSdx[k] = (RightNode->A[k]-LeftNode->A[k])*n_n_1;
                        } else {
                            CurrentNode->S[k] = (LeftNode->S[k]*n2+RightNode->S[k]*n1)*n_n_1;
                            dXX = CurrentNode->dSdx[k] = 0.;
                        }
                        if ( dy_flag ) {
                            dYY = CurrentNode->dSdy[k] = (UpNode->B[k]-DownNode->B[k])*m_m_1;

                        } else {
                            CurrentNode->S[k] =  (UpNode->S[k]*n3+DownNode->S[k]*n4)*m_m_1;
                            dYY = CurrentNode->dSdy[k] = 0;
                        }
                        if ( dx2_flag ) {
                            dXX = (LeftNode->dSdx[k]+RightNode->dSdx[k])*0.5;
                        }
                        if ( dy2_flag ) {
                            dYY = (UpNode->dSdy[k]+DownNode->dSdy[k])*0.5;
                        }

                        if ( CurrentNode->FT ) {
                            NextNode->S[k] = CurrentNode->S[k]*beta+_beta*(dxx*(LeftNode->S[k]+RightNode->S[k])+dyy*(UpNode->S[k]+DownNode->S[k]))*0.5
                                          - (dtdx*dXX+dtdy*(dYY+CurrentNode->F[k]/(j+1))) + (CurrentNode->Src[k])*dt+CurrentNode->SrcAdd[k];
                        } else {
                            NextNode->S[k] = CurrentNode->S[k]*beta+_beta*(dxx*(LeftNode->S[k]+RightNode->S[k])+dyy*(UpNode->S[k]+DownNode->S[k]))*0.5
                                          - (dtdx*dXX+dtdy*dYY) + (CurrentNode->Src[k])*dt+CurrentNode->SrcAdd[k];
                        }
                }
            }
        }
     }
   }
}

#ifdef _MPI_
void DEEPS2D_Run(ofstream* f_stream
#ifdef _MPI
                ,UMatrix2D< FlowNode2D<FP,NUM_COMPONENTS> >*     pJ,
                 UMatrix2D< FlowNodeCore2D<FP,NUM_COMPONENTS> >* pC,
                 int rank, int last_rank
#endif //_MPI
                 ) {

   // local variables
    FP   n_n,m_m,n_n_1,m_m_1;
    FP   dXX,dYY,AAA;
    int      n1,n2,n3,n4;
    unsigned int j,k;
    unsigned int StartXLocal,MaxXLocal;
    FP   dtdx;
    FP   dtdy;
    FP   dyy;
    FP   dxx;
    FP   dx_1,dy_1; // 1/dx, 1/dy
    FP   d_time;
    FP   t,VCOMP;
    timeval  start, stop, mark1, mark2;
    int      N1,N2,N3,N4;
#ifdef _OPEN_MP
    unsigned int i_max,j_max,k_max;
#endif //_OPEN_MP
#ifdef _MPI
    unsigned int i_max,j_max,k_max;
#endif //_MPI
    FP   dt_min_local;
    FP   _beta[6+NUM_COMPONENTS];
    FP   DD_local[FlowNode2D<FP,NUM_COMPONENTS>::NumEq];
#ifdef _RMS_
    FP   max_RMS;
    int      k_max_RMS;
#endif //_RMS_
#ifndef _MPI
    UMatrix2D< FlowNode2D<FP,NUM_COMPONENTS> >*     pJ=NULL;
    UMatrix2D< FlowNodeCore2D<FP,NUM_COMPONENTS> >* pC=NULL;
    FP*   dt_min;
    int*  i_c;
    int*  j_c;
    FP    dtmin=1.0;
    FP    DD[FlowNode2D<FP,NUM_COMPONENTS>::NumEq];
#ifdef _RMS_
    FP    sum_RMS[FlowNode2D<FP,NUM_COMPONENTS>::NumEq];
    unsigned long sum_iRMS[FlowNode2D<FP,NUM_COMPONENTS>::NumEq];
    UMatrix2D<FP> RMS(FlowNode2D<FP,NUM_COMPONENTS>::NumEq,SubDomainArray->GetNumElements());
    UMatrix2D<int>    iRMS(FlowNode2D<FP,NUM_COMPONENTS>::NumEq,SubDomainArray->GetNumElements());
#endif //_RMS_
    UMatrix2D<FP> DD_max(FlowNode2D<FP,NUM_COMPONENTS>::NumEq,SubDomainArray->GetNumElements());
#else
#ifdef _RMS_
    FP   RMS[FlowNode2D<FP,NUM_COMPONENTS>::NumEq];
    unsigned long iRMS[FlowNode2D<FP,NUM_COMPONENTS>::NumEq];
#endif //_RMS_
    Var_pack DD_max[last_rank+1];
#ifdef _MPI_NB
    MPI::Request  HaloExchange[4];
    MPI::Request  DD_Exchange[2*last_rank];
#endif //_MPI_NB
    unsigned int r_Overlap, l_Overlap;
#endif // _MPI
    int      AddEq=0;

    isScan = 0;
    dyy    = dx/(dx+dy);
    dxx    = dy/(dx+dy);
    dxdy   = dx*dy;
    dx2    = dx*dx;
    dy2    = dy*dy;
    t      = 0;
    n1 = n2 = n3 = n4 = 1;
    d_time = 0.;
#ifndef _MPI
    dt_min = new FP[SubDomainArray->GetNumElements()];
    i_c    = new int[SubDomainArray->GetNumElements()];
    j_c    = new int[SubDomainArray->GetNumElements()];
#endif // _MPI

#ifndef _MPI
    for(int ii=0;ii<(int)SubDomainArray->GetNumElements();ii++) {
        dt_min[ii] = dtmin = dt;
        i_c[ii] = j_c[ii] = 0;
     }
#ifdef _RMS_
    snprintf(RMSFileName,255,"RMS-%s",OutFileName);
    CutFile(RMSFileName);
    pRMS_OutFile = OpenData(RMSFileName);
    SaveRMSHeader(pRMS_OutFile);
#endif // _RMS_

#else
    MPI::COMM_WORLD.Barrier();
    MPI::COMM_WORLD.Bcast(&dt,1,MPI::DOUBLE,0);
#ifdef _RMS_
    if( rank == 0 ) {
        char    RMSFileName[255];
        snprintf(RMSFileName,255,"RMS-%s",OutFileName);
        CutFile(RMSFileName);
        pRMS_OutFile = OpenData(RMSFileName);
        SaveRMSHeader(pRMS_OutFile);
    }
#endif // _RMS_

    for (int ii=0;(int)ii<last_rank+1;ii++)
         DD_max[ii].dt_min=dt;

#endif // _MPI
#ifdef _DEBUG_0
          ___try {
#endif  // _DEBUG_0

                    I = 0;
                    isRun = 1;
#ifdef _MPI
                    if( rank == 0) {
                        StartXLocal = l_Overlap = 0;
                    } else {
                        StartXLocal = l_Overlap = 1;
                    }

                    if( rank == last_rank) {
                        MaxXLocal=pJ->GetX();
                        r_Overlap = 0;
                    } else {
                        MaxXLocal=pJ->GetX()-1;
                        r_Overlap = 1;
                    }
#else
                    pJ    = J;

                    StartXLocal = 0;
                    MaxXLocal=pJ->GetX();
#endif // _MPI
             do {

                  gettimeofday(&mark2,NULL);
                  gettimeofday(&start,NULL);
                  
                  if( AddSrcStartIter < iter + last_iter){
                   FlowNode2D<FP,NUM_COMPONENTS>::isSrcAdd = 1;
                  } else {
                   FlowNode2D<FP,NUM_COMPONENTS>::isSrcAdd = 0;
                  }
#ifdef _OPENMP
                  n_s = (int)SubDomainArray->GetNumElements();

#pragma omp parallel shared(f_stream,CoreSubDomainArray, SubDomainArray, chemical_reactions,Y_mix,\
                            Cp,i_max,j_max,k_max,Tg,beta0,CurrentTimePart,DD,dx,dy,MaxX,MaxY,dt_min, dtmin,DD_max,i_c,j_c,n_s) \
                     private(iter,j,k,n1,n2,n3,n4,N1,N2,N3,N4,n_n,m_m,pC,pJ,err_i,err_j,\
                             beta,_beta,AddEq,dXX,dYY,DD_local,makeZero,AAA,StartXLocal,MaxXLocal,\
                             dtdx,dtdy,dt)
#endif //_OPENMP

#ifdef _RMS_                           
                            //sum_RMS, sum_iRMS, RMS, iRMS,
#endif // _RMS_
                  iter = 0;
                  do {
#ifdef _MPI
// MPI version      
#ifdef _RMS_
                  if(rank == 0 ) {
                    if(MonitorNumber < 5)
                      max_RMS =  0.5*ExitMonitorValue;
                    else
                      max_RMS = 0.;
                  } else {
                    if(MonitorNumber < 5)
                       max_RMS =  2*ExitMonitorValue;
                    else
                       max_RMS = 0.;
                  }

                  k_max_RMS = -1;
#endif // _RMS_
                  for (int kk=0;kk<FlowNode2D<FP,NUM_COMPONENTS>::NumEq;kk++ ) {
#ifdef _RMS_
                      RMS[kk] = 0.;                                           // Clean sum residual
                      iRMS[kk]= 0;                                            // num involved nodes
                      
                      DD_max[rank].DD[kk].RMS  = 0.;                          // sum residual per rank
                      DD_max[rank].DD[kk].iRMS = 0;                           // num involved nodes per rank
#endif // _RMS_
                      
                      DD_max[rank].DD[kk].DD   = 0.;                          // max residual per rank
                      DD_max[rank].DD[kk].i    = 0;                           // max residual x-coord
                      DD_max[rank].DD[kk].j    = 0;                           // max residual y-coord
                      DD_local[kk]             = 0.;                          // local residual
                    }
#else
#ifdef _OPENMP
//#pragma omp single
#endif //_OPENMP
                   {
#ifdef _RMS_
                       max_RMS = 0.5 * ExitMonitorValue;
                       k_max_RMS = -1;
#endif // _RMS_
                       for ( k=0;k<(int)FlowNode2D<FP,NUM_COMPONENTS>::NumEq;k++ ) {
#ifdef _RMS_
                           for(int ii=0;ii<(int)SubDomainArray->GetNumElements();ii++) {
                               sum_RMS[k]  = RMS(k,ii)  = 0.;    // Clean sum residual
                               sum_iRMS[k] = iRMS(k,ii) = 0;     // num involved nodes
                           }
#endif // _RMS_
                           DD[k]      = 0.;
                       }
                   }
#endif //_MPI

#ifdef _OPENMP
#pragma omp barrier
#pragma omp for private(CurrentNode,NextNode,UpNode,DownNode,LeftNode,RightNode)  schedule(dynamic) ordered nowait
                   for(int ii=0;ii<n_s;ii++) {  // OpenMP version
#endif //_OPENMP

#ifndef _MPI
#ifndef _OPENMP
                    int ii = 0;                                                    // Single thread version
                    dt = dt_min[0];
#endif //_OPENMP
#else
                     dt  = 1.;
                     for (int ii=0;(int)ii<last_rank+1;ii++) {                     // Choose
                         dt  = min(DD_max[ii].dt_min,dt);                          // minimal
                    }

#endif // _MPI

#ifdef _OPENMP
//#pragma omp critical
                   dtmin = min(dt_min[ii],dtmin);
                   
                   dt    = dtmin;
                   i_c[ii] = j_c[ii] = 0;
#endif //_OPENMP

#ifndef _MPI
                    pJ = SubDomainArray->GetElement(ii);
                    pC = CoreSubDomainArray->GetElement(ii);
#endif // _MPI

#ifdef _MPI

#else
                    for ( k=0;k<(int)FlowNode2D<FP,NUM_COMPONENTS>::NumEq;k++ ) {
                       DD_max(k,ii) =  0.;
                      }

                    if( ii == 0)
                       StartXLocal=0;
                    else
                       StartXLocal=1;
                    if( ii == (int)SubDomainArray->GetNumElements()-1) {
                        MaxXLocal=pJ->GetX();
                    } else {
                        MaxXLocal=pJ->GetX()-1;
                    }
#endif // _MPI
                    dx_1 = 1.0/dx;
                    dy_1 = 1.0/dy;

                    dtdx = dt/dx;
                    dtdy = dt/dy;

#ifdef _OPENMP
                    dt_min[ii]   = 1.;
#else
#ifdef _MPI
                    DD_max[rank].dt_min = dt_min_local = 1.;
#endif // _MPI
#endif // _OPENMP
                    DEEPS2D_Stage1(pJ,pC,StartXLocal,MaxXLocal,MaxY,dxx,dyy,dtdx,dtdy);
#ifdef _OPENMP
           }
#endif // _OPENMP
        
#ifdef _OPENMP
#pragma omp barrier
#pragma omp for private(CurrentNode,NextNode,UpNode,DownNode,LeftNode,RightNode) schedule(dynamic) ordered nowait
             for(int ii=0;ii<n_s;ii++) {

                    pJ = SubDomainArray->GetElement(ii);
                    pC = CoreSubDomainArray->GetElement(ii);

                    if( ii == 0)
                      StartXLocal=0;
                    else
                      StartXLocal=1;
                    if( ii == (int)SubDomainArray->GetNumElements()-1) {
                        MaxXLocal=pJ->GetX();
                    } else {
                        MaxXLocal=pJ->GetX()-1;
                    }
#endif //_OPENMP
#ifdef _MPI                    
                    DD_max[rank].dt_min = min(DD_max[rank].dt_min,
#else
                    dt_min[ii] = min(dt_min[ii],

#endif //_MPI 
                    DEEPS2D_Stage2(pJ,pC,StartXLocal,
                                   MaxXLocal,MaxY,beta0,
                                   bFF,&chemical_reactions,
                                   iter,last_iter,TurbStartIter,
                                   SigW,SigF,dx_1,dy_1,delta_bl,
                                   min(CFL0,CFL_Scenario->GetVal(iter+last_iter)),
                                   beta_Scenario->GetVal(iter+last_iter),
#ifdef _RMS_        
#ifdef _MPI
                                   DD_max, rank,
#else
                                   RMS, iRMS, DD_max, i_c, j_c, ii,
#endif //_MPI 
#endif // _RMS_
                                   (TurbulenceExtendedModel)TurbExtModel));
                    
#ifdef _MPI
                    if(DD_max[rank].dt_min == 0.0) {
                        *f_stream << "\nERROR: Computational unstability  on iteration " << iter+last_iter<< " in subdomain no " << rank << "\n";
#else
                    if(dt_min[ii] == 0.0) {
                        *f_stream << "\nERROR: Computational unstability  on iteration " << iter+last_iter<< " in subdomain no " << ii << "\n";
#endif //_MPI 
                        Abort_OpenHyperFLOW2D();
                    }
#ifdef _MPI
// --- Halo exchange ---
                 if(rank < last_rank) {
// Send Tail
                 void*  tmp_SendPtr  = (void*)((ulong)pJ->GetMatrixPtr()+
                                                     (pJ->GetMatrixSize()-2*pJ->GetColSize()));
                 u_long tmp_SendSize = pJ->GetColSize();
#ifdef _MPI_NB
                 HaloExchange[0] = MPI::COMM_WORLD.Isend(tmp_SendPtr,
                                                         tmp_SendSize,
                                                         MPI::BYTE,rank+1,
                                                         tag_MatrixTail);
#else
                 MPI::COMM_WORLD.Send(tmp_SendPtr,
                                      tmp_SendSize,
                                      MPI::BYTE,rank+1,tag_MatrixTail);
#endif //_MPI_NB

// Recive Head
                 void*  tmp_RecvPtr  = (void*)((ulong)pJ->GetMatrixPtr()+
                                                     (pJ->GetMatrixSize()-
                                                      pJ->GetColSize()));
                 u_long tmp_RecvSize = pJ->GetColSize();
#ifdef _MPI_NB
                 HaloExchange[1] = MPI::COMM_WORLD.Irecv(tmp_RecvPtr,
                                                         tmp_RecvSize,
                                                         MPI::BYTE,rank+1,
                                                         tag_MatrixHead);
#else
                 MPI::COMM_WORLD.Recv(tmp_RecvPtr,
                                      tmp_RecvSize,
                                      MPI::BYTE,rank+1,tag_MatrixHead);
#endif //_MPI_NB

             }
               if(rank > 0) {
// Recive Tail
                 void*  tmp_RecvPtr  = (void*)(pJ->GetMatrixPtr());
                 u_long tmp_RecvSize = pJ->GetColSize();
#ifdef _MPI_NB
                 HaloExchange[3] = MPI::COMM_WORLD.Irecv(tmp_RecvPtr,
                                                         tmp_RecvSize,
                                                         MPI::BYTE,rank-1,
                                                         tag_MatrixTail);
#else
                 MPI::COMM_WORLD.Recv(tmp_RecvPtr,
                                      tmp_RecvSize,
                                      MPI::BYTE,rank-1,tag_MatrixTail);
#endif //_MPI_NB

//Send  Head
                 void*  tmp_SendPtr  = (void*)((ulong)pJ->GetMatrixPtr()+
                                                      pJ->GetColSize());
                 u_long tmp_SendSize = pJ->GetColSize();
#ifdef _MPI_NB
                 HaloExchange[2] = MPI::COMM_WORLD.Isend(tmp_SendPtr,
                                                         tmp_SendSize,
                                                         MPI::BYTE,rank-1,tag_MatrixHead);
#else
                 MPI::COMM_WORLD.Send(tmp_SendPtr,
                                      tmp_SendSize,
                                      MPI::BYTE,rank-1,tag_MatrixHead);
#endif //_MPI_NB

             }
#endif // _MPI

             if(!isAdiabaticWall)
                CalcHeatOnWallSources(pJ,dx,dy,dt
#ifdef _MPI
                                      ,rank,last_rank
#else
                                      ,ii,(int)SubDomainArray->GetNumElements()-1 
#endif // _MPI
                                   );
#ifdef _OPENMP
 for (DD_max_var = 1.,k=0;k<(int)FlowNode2D<FP,NUM_COMPONENTS>::NumEq;k++ ) {
    DD_max_var = DD[k] = max(DD[k],DD_max(k,ii));
    if(DD[k] == DD_max(k,ii)) {
       i_max =  i_c[ii];
       j_max =  j_c[ii];
       k_max =  k;
     }
  }
#pragma omp critical
     dtmin = min(dt_min[ii],dtmin); 
     dt    = dtmin;
#endif // _OPENMP

#ifdef _MPI
#ifdef _MPI_NB
        if(rank < last_rank) {
           HaloExchange[0].Wait();
           HaloExchange[1].Wait();
        }
        if(rank > 0) {
           HaloExchange[3].Wait();
           HaloExchange[2].Wait();
        }
       // MPI::Request::Waitall(4, HaloExchange);
#endif //_MPI_NB

       if(rank > 0) {
#ifdef _MPI_NB
           DD_Exchange[rank-1] = MPI::COMM_WORLD.Isend(&DD_max[rank],sizeof(Var_pack),MPI::BYTE,0,tag_DD);
#else
           MPI::COMM_WORLD.Send(&DD_max[rank],sizeof(Var_pack),MPI::BYTE,0,tag_DD);
#endif //_MPI_NB

       } else {
         for(int ii=1;ii<last_rank+1;ii++) {
#ifdef _MPI_NB
           DD_Exchange[last_rank+ii] = MPI::COMM_WORLD.Irecv(&DD_max[ii],sizeof(Var_pack),MPI::BYTE,ii,tag_DD);
#else
           MPI::COMM_WORLD.Recv(&DD_max[ii],sizeof(Var_pack),MPI::BYTE,ii,tag_DD);
#endif //_MPI_NB

         }
       }

#ifdef _MPI_NB
       if(rank > 0) {
          DD_Exchange[rank-1].Wait();
       } else {
         for(int ii=1;ii<last_rank+1;ii++) {
          DD_Exchange[last_rank+ii].Wait();  
         }
       }
       //MPI::Request::Waitall(2*(last_rank), DD_Exchange);
#endif //_MPI_NB

       if(rank == 0) {
           for(int ii=0,DD_max_var=0.;ii<last_rank+1;ii++) {
               for (k=0;k<(int)(FlowNode2D<FP,NUM_COMPONENTS>::NumEq);k++ ) {
                  DD_max_var = max(DD_max[ii].DD[k].DD,DD_max_var);
#ifdef _RMS_
                  RMS[k] += DD_max[ii].DD[k].RMS;
                  iRMS[k]+= DD_max[ii].DD[k].iRMS;
#endif // _RMS_
                  if(DD_max[ii].DD[k].DD == DD_max_var) {
                   i_max = DD_max[ii].DD[k].i;
                   j_max = DD_max[ii].DD[k].j;
                   k_max = k;
                 }
             }
           }
#ifdef _RMS_               
           for (k=0;k<(int)(FlowNode2D<FP,NUM_COMPONENTS>::NumEq);k++ ) {
                   if(iRMS[k] > 0) {
                     RMS[k] = sqrt(RMS[k]/iRMS[k]);
                     if(MonitorNumber == 0 || MonitorNumber > 4) {
                        max_RMS = max(RMS[k],max_RMS);
                        if(max_RMS == RMS[k])
                           k_max_RMS = k;
                     } else {
                        max_RMS = max(RMS[MonitorNumber-1],max_RMS);
                        if(max_RMS == RMS[MonitorNumber-1])
                           k_max_RMS = k;
                     }

                    }
            }
#endif // _RMS_
#else
#ifdef _OPENMP
 }

#pragma omp single
#endif // _OPENMP
    {
#ifdef _RMS_
        for(k=0;k<(int)(FlowNode2D<FP,NUM_COMPONENTS>::NumEq);k++ ) {
         for(int ii=0;ii<(int)SubDomainArray->GetNumElements();ii++) {
              if(iRMS(k,ii) > 0) {
                 sum_RMS[k]  += RMS(k,ii);
                 sum_iRMS[k] += iRMS(k,ii);
             }
           }

           if(sum_iRMS[k] != 0)
              sum_RMS[k] = sum_RMS[k]/sum_iRMS[k];
           else
              sum_RMS[k] = 0;

           if( MonitorNumber == 0 || MonitorNumber > 4) {
               max_RMS = max(sum_RMS[k],max_RMS);
               if(max_RMS == sum_RMS[k])
                  k_max_RMS = k;
           } else {
               max_RMS = max(sum_RMS[MonitorNumber-1],max_RMS);
               if(max_RMS == sum_RMS[MonitorNumber-1])
                  k_max_RMS = MonitorNumber-1;
           }

         }
#endif // _RMS_

#endif // _MPI
        CurrentTimePart += dt;
         if ( isVerboseOutput && iter/NOutStep*NOutStep == iter ) {
             gettimeofday(&mark1,NULL);
             d_time = (FP)(mark1.tv_sec-mark2.tv_sec)+(FP)(mark1.tv_usec-mark2.tv_usec)*1.e-6; 

             if(d_time > 0.)
                 VCOMP = (FP)(NOutStep)/d_time;
             else
                 VCOMP = 0.;
             memcpy(&mark2,&mark1,sizeof(mark1));
#ifdef _RMS_
             SaveRMS(pRMS_OutFile,last_iter+iter,
#ifdef  _MPI
             RMS);
#else
             sum_RMS);
#endif // _MPI

             if(k_max_RMS == i2d_nu_t)
                k_max_RMS +=turb_mod_name_index;

             if(k_max_RMS != -1 && (MonitorNumber == 0 || MonitorNumber == 5))
             *f_stream << "Step No " << iter+last_iter << " maxRMS["<< RMS_Name[k_max_RMS] << "]="<< (FP)(max_RMS*100.) \
                        <<  " % step_time=" << (FP)d_time << " sec (" << (FP)VCOMP <<" step/sec) dt="<< dt <<"\n" << flush;
             else if(MonitorNumber > 0 &&  MonitorNumber < 5 )
                 *f_stream << "Step No " << iter+last_iter << " maxRMS["<< RMS_Name[MonitorNumber-1] << "]="<< (FP)(max_RMS*100.) \
                  <<  " % step_time=" << (FP)d_time << " sec (" << (FP)VCOMP <<" step/sec) dt="<< dt <<"\n" << flush;
             else
             *f_stream << "Step No " << iter+last_iter << " maxRMS["<< k_max_RMS << "]="<< (FP)(max_RMS*100.) \
                        <<  " % step_time=" << (FP)d_time << " sec (" << (FP)VCOMP <<" step/sec) dt="<< dt <<"\n" << flush;
#else
             *f_stream << "Step No " << iter+last_iter <<  " step_time=" << (FP)d_time << " sec (" << (FP)VCOMP <<" step/sec) dt="<< dt <<"\n" << flush;
#endif // _RMS_
             f_stream->flush();
            }
#ifndef _MPI
#endif // _MPI
        }
     iter++;
   } while((int)iter < Nstep);
#ifdef _OPENMP
#pragma omp single 
          {
#endif //  _OPENMP

#ifdef _MPI
     // Collect all SubDomain
        if(rank>0) {
        void*  tmp_SendPtr=(void*)((u_long)(pJ->GetMatrixPtr())+
                                            pJ->GetColSize()*l_Overlap);
        u_long tmp_SendSize=pJ->GetMatrixSize()-
                            pJ->GetColSize()*(l_Overlap);
#ifdef _IMPI_
        LongMatrixSend(0, tmp_SendPtr, tmp_SendSize);  // Low Mem Send subdomain
#else
        MPI::COMM_WORLD.Send(tmp_SendPtr,
                             tmp_SendSize,
                             MPI::BYTE,0,tag_Matrix); 
#endif // _IMPI_
        } else {
        void*  tmp_RecvPtr;
        u_long tmp_RecvSize;
        for(int ii=1;ii<last_rank+1;ii++) {
           tmp_RecvPtr=(void*)((u_long)(ArraySubDomain->GetElement(ii)->GetMatrixPtr())+
                                        ArraySubDomain->GetElement(ii)->GetColSize()*(r_Overlap+
                                                                                      l_Overlap));
           tmp_RecvSize=ArraySubDomain->GetElement(ii)->GetMatrixSize()-
                        ArraySubDomain->GetElement(ii)->GetColSize()*(r_Overlap-l_Overlap);
#ifdef _IMPI_
           LongMatrixRecv(ii, tmp_RecvPtr, tmp_RecvSize);  // Low Mem Send subdomain
#else
           MPI::COMM_WORLD.Recv(tmp_RecvPtr,
                                tmp_RecvSize,
                                MPI::BYTE,ii,tag_Matrix); 
#endif // _IMPI_
        }
#ifdef _PARALLEL_RECALC_Y_PLUS_
        if(WallNodes && !WallNodesUw_2D && J) 
             WallNodesUw_2D = GetWallFrictionVelocityArray2D(J,WallNodes);
         else if(WallNodes && J)
             RecalcWallFrictionVelocityArray2D(J,WallNodesUw_2D,WallNodes);
#endif // _PARALLEL_RECALC_Y_PLUS_
    }

#ifdef _PARALLEL_RECALC_Y_PLUS_
    if( rank == 0 ) {
        *f_stream << "Parallel recalc y+...";
        for(int ii = 1; (int)ii < last_rank + 1; ii++ ) {
            MPI::COMM_WORLD.Send(WallNodesUw_2D->GetArrayPtr(),
                                 WallNodesUw_2D->GetNumElements()*WallNodesUw_2D->GetElementSize(),
                                 MPI::BYTE,ii,tag_WallFrictionVelocity);
        }
    } else {
            MPI::COMM_WORLD.Recv(WallNodesUw_2D->GetArrayPtr(),
                                 WallNodesUw_2D->GetNumElements()*WallNodesUw_2D->GetElementSize(),
                                 MPI::BYTE,0,tag_WallFrictionVelocity);
    }
    ParallelRecalc_y_plus(pJ,WallNodes,WallNodesUw_2D,x0);
#endif // _PARALLEL_RECALC_Y_PLUS_

    if( rank == 0) {
#ifndef _PARALLEL_RECALC_Y_PLUS_
        *f_stream << "Recalc y+...";
         Recalc_y_plus(J,WallNodes);
#endif // _PARALLEL_RECALC_Y_PLUS_
#else
#ifdef _PARALLEL_RECALC_Y_PLUS_
         *f_stream << "Parallel recalc y+...";
         ParallelRecalc_y_plus(pJ,WallNodes,WallNodesUw_2D,x0);
#else
   *f_stream << "Recalc y+...";
   Recalc_y_plus(J,WallNodes);
#endif // _PARALLEL_RECALC_Y_PLUS_
#endif //  _MPI
         
        if ( isGasSource && SrcList) {
              *f_stream << "\nSet gas sources...";
              SrcList->SetSources2D();
              *f_stream << "OK" << endl;
         }
        if ( XCutArray->GetNumElements() > 0 ) {
            for(int i_xcut = 0; i_xcut<(int)XCutArray->GetNumElements(); i_xcut++) {
                XCut* TmpXCut = XCutArray->GetElementPtr(i_xcut);
                *f_stream << "Cut(" << i_xcut + 1  <<") X=" << TmpXCut->x0 << " Y=" << TmpXCut->y0 <<
                   " dY=" << TmpXCut->dy << " MassFlow="<< CalcMassFlowRateX2D(J,TmpXCut->x0,TmpXCut->y0,TmpXCut->dy) << "  (kg/sec*m)" << endl;
            }
        }
        gettimeofday(&stop,NULL);
#ifdef  _GNUPLOT_
        *f_stream << "\nSave current results in file " << OutFileName << "...\n" << flush; 
        DataSnapshot(OutFileName,WM_REWRITE);
#endif // _GNUPLOT_
        if ( (I/NSaveStep)*NSaveStep == I ) {
#ifdef  _TECPLOT_
             *f_stream << "Add current results to transient solution file " << TecPlotFileName << "...\n" << flush; 
             DataSnapshot(TecPlotFileName,WM_APPEND); 
#endif // _TECPLOT_
         }
         I++;
         d_time = (FP)(stop.tv_sec-start.tv_sec)+(FP)(stop.tv_usec-start.tv_usec)*1.e-6; 
         *f_stream << "HyperFLOW/DEEPS computation cycle time=" << (FP)d_time << " sec ( average  speed " << (FP)(Nstep/d_time) <<" step/sec).       \n" << flush;
         f_stream->flush();
         last_iter  += iter;
         iter = 0;
         GlobalTime += CurrentTimePart;
         CurrentTimePart  = 0.;

         if(isOutHeatFluxX) {
          char HeatFluxXFileName[255];
          snprintf(HeatFluxXFileName,255,"HeatFlux-X-%s",OutFileName);
          CutFile(HeatFluxXFileName);
          pHeatFlux_OutFile = OpenData(HeatFluxXFileName);
          SaveXHeatFlux2D(pHeatFlux_OutFile,J,Ts0);
          pHeatFlux_OutFile->close();
         }

         if(isOutHeatFluxY) {
          char HeatFluxYFileName[255];
          snprintf(HeatFluxYFileName,255,"HeatFlux-Y-%s",OutFileName);
          CutFile(HeatFluxYFileName);
          pHeatFlux_OutFile = OpenData(HeatFluxYFileName);
          SaveYHeatFlux2D(pHeatFlux_OutFile,J,Ts0);
          pHeatFlux_OutFile->close();
         }
// Sync swap files...
//  Gas area
                     if ( GasSwapData ) {
                       if(isVerboseOutput)
                        *f_stream << "\nSync swap file for gas..." << flush;
#ifdef  _NO_MMAP_
#ifdef _WRITE_LARGE_FILE_
//#warning "Sync file >2Gb..."
                        lseek(fd_g,0,SEEK_SET);
                        ssize_t max_write = 1024L*1024L*1024L;
                        ssize_t one_write = 0L;
                        ssize_t len  = 0L;
                        off_t  off = 0L;
                        char* TmpPtr=(char*)J->GetMatrixPtr();
                        if(J->GetMatrixSize() > max_write) {
                           for(off = 0L,one_write = max_write; len < J->GetMatrixSize(); off += max_write) {
                           len += pwrite64(fd_g,TmpPtr+off,one_write,off);
                           if(J->GetMatrixSize() - len < max_write)
                              one_write = J->GetMatrixSize() - len;
                            }
                        if(len != J->GetMatrixSize())
                           *f_stream << "Error: len(" << len << ") != FileSize(" << J->GetMatrixSize() << ") " << endl << flush;
                        } else {
                           len = pwrite64(fd_g,J->GetMatrixPtr(),J->GetMatrixSize(),0L);
                        }
#else
                        lseek(fd_g,0,SEEK_SET);
                        write(fd_g,J->GetMatrixPtr(),J->GetMatrixSize());
#endif // _WRITE_LARGE_FILE_
#else
                        msync(J->GetMatrixPtr(),J->GetMatrixSize(),MS_SYNC);
#endif //  _GPFS
                        *f_stream << "OK" << endl;
                     }
#ifdef _MPI
      }
// Get Data back
    MPI::COMM_WORLD.Bcast(&GlobalTime,1,MPI::DOUBLE,0);
    if(rank>0) {
        void*  tmp_RecvPtr=(void*)((u_long)(pJ->GetMatrixPtr())+pJ->GetColSize()*l_Overlap);
        u_long tmp_RecvSize=pJ->GetMatrixSize()-pJ->GetColSize()*(l_Overlap);
#ifdef _IMPI_
        LongMatrixRecv(0, tmp_RecvPtr, tmp_RecvSize);  // Low Mem Send subdomain
#else
        MPI::COMM_WORLD.Recv(tmp_RecvPtr,
                             tmp_RecvSize,
                             MPI::BYTE,0,tag_Matrix);
#endif //_IMPI_
#ifdef _PARALLEL_RECALC_Y_PLUS_
        if(!WallNodesUw_2D)
            WallNodesUw_2D = new UArray<FP>(NumWallNodes,-1);

        MPI::COMM_WORLD.Recv(WallNodesUw_2D->GetArrayPtr(),
                             NumWallNodes*sizeof(FP),
                             MPI::BYTE,0,tag_WallFrictionVelocity);
#endif // _PARALLEL_RECALC_Y_PLUS_
    } else {
      void*  tmp_SendPtr;
      u_long tmp_SendSize;
      for(int ii=1;ii<last_rank+1;ii++) {
          tmp_SendPtr=(void*)((u_long)(ArraySubDomain->GetElement(ii)->GetMatrixPtr())+
          ArraySubDomain->GetElement(ii)->GetColSize()*(r_Overlap+l_Overlap));
          tmp_SendSize=ArraySubDomain->GetElement(ii)->GetMatrixSize()-
          ArraySubDomain->GetElement(ii)->GetColSize()*(r_Overlap-l_Overlap);
#ifdef _IMPI_
          LongMatrixSend(ii, tmp_SendPtr, tmp_SendSize);  // Low Mem Send subdomain
#else
          MPI::COMM_WORLD.Send(tmp_SendPtr,
                               tmp_SendSize,
                               MPI::BYTE,ii,tag_Matrix);
#endif //_IMPI_ 
#ifdef _PARALLEL_RECALC_Y_PLUS_
          MPI::COMM_WORLD.Send(WallNodesUw_2D->GetArrayPtr(),
                               NumWallNodes*sizeof(FP),
                               MPI::BYTE,ii,tag_WallFrictionVelocity);
#endif // _PARALLEL_RECALC_Y_PLUS_
      }
    }
     MPI::COMM_WORLD.Bcast(&last_iter,1,MPI::INT,0);
     MPI::COMM_WORLD.Bcast(&isRun,1,MPI::INT,0);
     if(!isRun)
         Abort_OpenHyperFLOW2D();
#endif //  _MPI
#ifdef _PROFILE_
 Abort_OpenHyperFLOW2D();
#endif // _PROFILE_
#ifdef _MPI
 MPI::COMM_WORLD.Bcast(&isRun,1,MPI::INT,0);
#endif //  _MPI
#ifdef _OPENMP
          }
#pragma omp barrier
#endif //  _OPENMP

if(MonitorNumber < 5) {
#ifdef _RMS_
    if( max_RMS > ExitMonitorValue )
     MonitorCondition = 1;
   else
     MonitorCondition = 0;
#endif // _RMS_
} else {
   if( GlobalTime <  ExitMonitorValue )
     MonitorCondition = 1;
   else
     MonitorCondition = 0;
}
}while( MonitorCondition );
//---   Save  results ---
#ifdef _MPI
if (rank == 0) {
#endif //  _MPI

#ifdef _DEBUG_0
                ___try {
#endif  // _DEBUG_0
#ifdef  _GNUPLOT_
                    DataSnapshot(OutFileName,WM_REWRITE);
#endif //  _GNUPLOT_
#ifdef _DEBUG_0
                } __except(SysException e) {
                    if ( e == SIGINT ) {
                        *f_stream << "Interrupted by user\n" <<  flush;
                    } else {
                        *f_stream << "Save error data !\n";
                        f_stream->flush();
                        if ( GasSwapData!=0 ) {
#ifdef _REMOVE_SWAPFILE_
                            if ( e != SIGINT ) isDel=1;
#endif //_REMOVE_SWAPFILE_
#ifdef _REMOVE_SWAPFILE_
                                 CloseSwapFile(GasSwapFileName,
                                               GasSwapData,
                                               FileSizeGas,
                                               fd_g,
                                               isDel);
#endif // _REMOVE_SWAPFILE_
                        }
                          Abort_OpenHyperFLOW2D();
                    }
                }
                __end_except;
#endif  // _DEBUG_0
//--- End Save results ---
                *f_stream << "\nResults saved in file \"" << OutFileName << "\".\n" << flush;
                f_stream->flush();
#ifdef _MPI
                isRun = 0;
                Exit_OpenHyperFLOW2D();
              }
#endif //  _MPI
#ifdef _DEBUG_0
            }__except( UMatrix2D<FP>*  m) {
                *f_stream << "\n";
                *f_stream << "Error in UMatrix2D<FP> ("<< err_i << "," << err_j <<")." << "\n" << flush;
                f_stream->flush();

                if ( GasSwapData!=0 ) {
#ifdef _REMOVE_SWAPFILE_
                    CloseSwapFile(GasSwapFileName,
                                  GasSwapData,
                                  FileSizeGas,
                                  fd_g,
                                  1);
#endif // _REMOVE_SWAPFILE_
                    useSwapFile = 0;
                } else {
                    delete J;
                }
                Abort_OpenHyperFLOW2D();
            } __except( ComputationalMatrix2D*  m) {
                *f_stream << "\n";
                *f_stream << "Error in UMatrix2D< FlowNode2D<FP,NUM_COMPONENTS> ("<< err_i << "," << err_j <<")->";
                if(m->GetMatrixState() == MXS_ERR_OUT_OF_INDEX){
                *f_stream << "MXS_ERR_OUT_OF_INDEX"  << "\n" << flush; 
                } else if(m->GetMatrixState() == MXS_ERR_MEM) {
                *f_stream << "MXS_ERR_MEM"  << "\n" << flush;
                }else if(m->GetMatrixState() == MXS_ERR_MAP) {
                *f_stream << "MXS_ERR_MAP"  << "\n" << flush;
                }
                f_stream->flush();
                if ( GasSwapData!=0 ) {
#ifdef _REMOVE_SWAPFILE_
                    CloseSwapFile(GasSwapFileName,
                                  GasSwapData,
                                  FileSizeGas,
                                  fd_g,
                                  1);
#endif // _REMOVE_SWAPFILE_
                    useSwapFile=0;
                } else {
                    delete J;
                }
                Abort_OpenHyperFLOW2D();
            }__except(SysException e) {
                //int isDel=0;
                if ( e == SIGINT ) {
                    *f_stream << "\n";
                    *f_stream << "Interrupted by user\n" <<  flush;
                } else   *f_stream << "Handled system signal: " << (int)e << "\n" << flush;
                f_stream->flush();
                if ( GasSwapData!=0 ) {
#ifdef _REMOVE_SWAPFILE_
                    if ( e != SIGINT ) isDel=1;
#endif //_REMOVE_SWAPFILE_
#ifdef _REMOVE_SWAPFILE_
             CloseSwapFile(GasSwapFileName,
                           GasSwapData,
                           FileSizeGas,
                           fd_g,
                           isDel);
#endif // _REMOVE_SWAPFILE_
                } else {
                  delete J;
                }
                  Abort_OpenHyperFLOW2D();
            }__except(void*) {
                *f_stream << "\n";
                *f_stream << "Unknown error  in ("<< err_i << "," << err_j <<")." << "\n" << flush;
                f_stream->flush();
                if ( GasSwapData!=0 ) {
#ifdef _REMOVE_SWAPFILE_
                    CloseSwapFile(GasSwapFileName,
                                  GasSwapData,
                                  FileSizeGas,
                                  fd_g,
                                  1);
#endif // _REMOVE_SWAPFILE_
                    useSwapFile=0;
                } else {
                  delete J;
                }
                  Abort_OpenHyperFLOW2D();
            }
            __end_except;
#endif // _DEBUG_0

#ifdef _MPI
           if(rank == 0)
#endif // _MPI
             *f_stream << "\nReady. Computation finished.\n"  << flush;

           Exit_OpenHyperFLOW2D();
};
#endif // _MPI_



#ifdef _PARALLEL_RECALC_Y_PLUS_
void ParallelRecalc_y_plus(ComputationalMatrix2D* pJ, 
                           UArray< XY<int> >* WallNodes,
                           UArray<FP>* WallFrictionVelocity2D,
                           FP x0) {
#ifdef _OPEN_MP
#pragma omp parallel for
#endif //_OPEN_MP
        for (int i=0;i<(int)pJ->GetX();i++ ) {
            for (int j=0;j<(int)pJ->GetY();j++ ) {
                 if ( pJ->GetValue(i,j).isCond2D(CT_NODE_IS_SET_2D) &&
                      !pJ->GetValue(i,j).isCond2D(CT_SOLID_2D)) {

                        for (int ii=0;ii<(int)WallNodes->GetNumElements();ii++) {

                             unsigned int iw,jw;
                             FP U_w,x,y,wx,wy;

                             iw = WallNodes->GetElementPtr(ii)->GetX();
                             jw = WallNodes->GetElementPtr(ii)->GetY();

                             U_w   =  WallFrictionVelocity2D->GetElement(ii);

                             x = x0 + i * FlowNode2D<FP,NUM_COMPONENTS>::dx;
                             y = j * FlowNode2D<FP,NUM_COMPONENTS>::dy;

                             wx = iw * FlowNode2D<FP,NUM_COMPONENTS>::dx;
                             wy = jw * FlowNode2D<FP,NUM_COMPONENTS>::dy;

                             if(x == wx && y == wy ) {
                                 pJ->GetValue(i,j).y_plus = U_w*min((FlowNode2D<FP,NUM_COMPONENTS>::dx),
                                                                    (FlowNode2D<FP,NUM_COMPONENTS>::dy))*pJ->GetValue(i,j).S[i2d_Rho]/pJ->GetValue(i,j).mu;
                             } else {
                                 if(pJ->GetValue(i,j).l_min == min(pJ->GetValue(i,j).l_min,
                                                                   sqrt((x-wx)*(x-wx) + (y-wy)*(y-wy))))
                                 pJ->GetValue(i,j).y_plus = U_w*pJ->GetValue(i,j).l_min*pJ->GetValue(i,j).S[i2d_Rho]/pJ->GetValue(i,j).mu;
                             }
                        }
                 }
            }
        }
}
#else
void Recalc_y_plus(ComputationalMatrix2D* pJ, UArray< XY<int> >* WallNodes) {
    unsigned int iw,jw;
    FP tau_w, U_w;

    for (int ii=0;ii<(int)WallNodes->GetNumElements();ii++) { 

        iw = WallNodes->GetElementPtr(ii)->GetX();
        jw = WallNodes->GetElementPtr(ii)->GetY();

        tau_w = (fabs(pJ->GetValue(iw,jw).dUdy) + 
                 fabs(pJ->GetValue(iw,jw).dVdx)) * pJ->GetValue(iw,jw).mu;

        U_w   = sqrt(tau_w/pJ->GetValue(iw,jw).S[i2d_Rho]+1e-30);

        for (int i=0;i<(int)pJ->GetX();i++ ) {
               for (int j=0;j<(int)pJ->GetY();j++ ) {

                       if (pJ->GetValue(i,j).isCond2D(CT_NODE_IS_SET_2D) &&
                           !pJ->GetValue(i,j).isCond2D(CT_SOLID_2D)) {
                               if ( i == (int)iw && j == (int)jw) {
                                     pJ->GetValue(i,j).y_plus = U_w*min(dx,dy)/pJ->GetValue(i,j).mu;
                               } else { 
                                   if(pJ->GetValue(i,j).l_min == max(min(dx,dy),sqrt((i-iw)*(i-iw)*dx*dx + 
                                                                                     (j-jw)*(j-jw)*dy*dy)))
                                      pJ->GetValue(i,j).y_plus = U_w*pJ->GetValue(i,j).l_min*pJ->GetValue(i,j).S[i2d_Rho]/pJ->GetValue(i,j).mu;
                               }
                         }
                       }
               }
    }
}
#endif // _PARALLEL_RECALC_Y_PLUS_


inline  void CalcHeatOnWallSources(UMatrix2D< FlowNode2D<FP,NUM_COMPONENTS> >* F, FP dx, FP dy, FP dt, int rank, int last_rank) {

        unsigned int StartXLocal,MaxXLocal;
        FP dx_local, dy_local;

        if( rank == 0) {
            StartXLocal = 0;
        } else {
            StartXLocal = 1;
        }

        if( rank == last_rank) {
            MaxXLocal=F->GetX();
        } else {
            MaxXLocal=F->GetX()-1;
        }
        // Clean Q
        for (unsigned int i=StartXLocal;i<MaxXLocal;i++ )
            for ( unsigned int j=0;j<F->GetY();j++ ) {
                 F->GetValue(i,j).Q_conv = 0.;
            }

        for (unsigned int i=StartXLocal;i<MaxXLocal;i++ )
            for ( unsigned int j=0;j<F->GetY();j++ ) {

                FlowNode2D< FP,NUM_COMPONENTS >* CurrentNode=NULL;
                FlowNode2D< FP,NUM_COMPONENTS >* UpNode=NULL;
                FlowNode2D< FP,NUM_COMPONENTS >* DownNode=NULL;
                FlowNode2D< FP,NUM_COMPONENTS >* LeftNode=NULL;
                FlowNode2D< FP,NUM_COMPONENTS >* RightNode=NULL;


                CurrentNode = &F->GetValue(i,j);

                if(!CurrentNode->isCond2D(CT_SOLID_2D)) { 
                    
                    dx_local = dx;
                    dy_local = dy;

                    if(j < F->GetY()-1)
                      UpNode    = &(F->GetValue(i,j+1));
                    else
                      UpNode    = NULL;

                    if(j>0)
                      DownNode  = &(F->GetValue(i,j-1));
                    else
                      DownNode  = NULL;

                    if(i>0)
                      LeftNode  = &(F->GetValue(i-1,j));
                    else
                      LeftNode  = NULL;

                    if(i < F->GetX()-1)
                      RightNode = &(F->GetValue(i+1,j));
                    else
                      RightNode = NULL;

                if ( CurrentNode->isCond2D(CT_WALL_LAW_2D) || 
                     CurrentNode->isCond2D(CT_WALL_NO_SLIP_2D)) { 
                    FP lam_eff = 0;
                    int num_near_nodes = 0;
                    
                    
                    if ( DownNode && DownNode->isCond2D(CT_SOLID_2D) ) {
                        
                        num_near_nodes = 1;
                        lam_eff = CurrentNode->lam+CurrentNode->lam_t; 
                        
                        if (CurrentNode->UpNode) {
                            lam_eff +=CurrentNode->UpNode->lam + CurrentNode->UpNode->lam_t;
                            num_near_nodes++;
                        }
                        
                        lam_eff = lam_eff/num_near_nodes;
                        
                        if(DownNode->Q_conv > 0.)
                           DownNode->Q_conv = (DownNode->Q_conv - lam_eff*(DownNode->Tg - CurrentNode->Tg)/dy_local)*0.5;
                        else
                           DownNode->Q_conv = -lam_eff*(DownNode->Tg - CurrentNode->Tg)/dy_local;
                        
                        CurrentNode->SrcAdd[i2d_RhoE] = -dt*DownNode->Q_conv/dy;
                    }
                    
                    if ( UpNode && UpNode->isCond2D(CT_SOLID_2D) ) {
                        
                        num_near_nodes = 1;
                        lam_eff = CurrentNode->lam+CurrentNode->lam_t; 
                        
                        if (CurrentNode->DownNode) {
                            lam_eff +=CurrentNode->DownNode->lam + CurrentNode->DownNode->lam_t;
                            num_near_nodes++;
                        }
                        
                        lam_eff = lam_eff/num_near_nodes;
                        
                        if(UpNode->Q_conv > 0.)
                           UpNode->Q_conv = (UpNode->Q_conv - lam_eff*(UpNode->Tg - CurrentNode->Tg)/dy_local)*0.5;
                        else
                           UpNode->Q_conv = -lam_eff*(UpNode->Tg - CurrentNode->Tg)/dy_local;
                        
                        CurrentNode->SrcAdd[i2d_RhoE] = -dt*UpNode->Q_conv/dy;
                    }
                    
                    if ( LeftNode && LeftNode->isCond2D(CT_SOLID_2D) ) {
                        
                        num_near_nodes = 1;
                        lam_eff = CurrentNode->lam+CurrentNode->lam_t; 
                        
                        if (CurrentNode->RightNode) {
                            lam_eff +=CurrentNode->RightNode->lam + CurrentNode->RightNode->lam_t;
                            num_near_nodes++;
                        }
                        
                        lam_eff = lam_eff/num_near_nodes;
                        
                        if(LeftNode->Q_conv > 0.)
                           LeftNode->Q_conv = (LeftNode->Q_conv - lam_eff*(LeftNode->Tg - CurrentNode->Tg)/dx_local)*0.5;
                        else
                           LeftNode->Q_conv = -lam_eff*(LeftNode->Tg - CurrentNode->Tg)/dx_local;
                        
                        CurrentNode->SrcAdd[i2d_RhoE] = -dt*LeftNode->Q_conv/dx;
                    }
                    
                    if ( RightNode && RightNode->isCond2D(CT_SOLID_2D) ) {
                        
                        num_near_nodes = 1;
                        lam_eff = CurrentNode->lam+CurrentNode->lam_t; 
                        
                        if (CurrentNode->LeftNode) {
                            lam_eff +=CurrentNode->LeftNode->lam + CurrentNode->LeftNode->lam_t;
                            num_near_nodes++;
                        }
                        
                        lam_eff = lam_eff/num_near_nodes;
                        
                        if(RightNode->Q_conv > 0.)
                           RightNode->Q_conv = (RightNode->Q_conv - lam_eff*(RightNode->Tg - CurrentNode->Tg)/dx_local)*0.5;
                        else
                           RightNode->Q_conv = -lam_eff*(RightNode->Tg - CurrentNode->Tg)/dx_local;
                        
                        CurrentNode->SrcAdd[i2d_RhoE] = -dt*RightNode->Q_conv/dx;
                    }
                }
            }
         }
}
void SetMinDistanceToWall2D(ComputationalMatrix2D* pJ2D,
                            UArray< XY<int> >* WallNodes2D
                            ,FP x0
                            ) {

FP  min_l_min = min((FlowNode2D<FP,NUM_COMPONENTS>::dx),
                        (FlowNode2D<FP,NUM_COMPONENTS>::dy));
#ifdef _OPEN_MP
#pragma omp parallel  for
#endif //_OPEN_MP

for (int i=0;i<(int)pJ2D->GetX();i++ ) {
    for (int j=0;j<(int)pJ2D->GetY();j++ ) {
           if (pJ2D->GetValue(i,j).isCond2D(CT_NODE_IS_SET_2D) &&
              !pJ2D->GetValue(i,j).isCond2D(CT_SOLID_2D)) {
              if(pJ2D->GetValue(i,j).Tg != 0 && pJ2D->GetValue(i,j).p == 0.) {
                 pJ2D->GetValue(i,j).SetCond2D(CT_SOLID_2D);
             } else {   
                        unsigned int iw,jw;
                        FP wx, wy;
                        FP x, y;

                        pJ2D->GetValue(i,j).l_min = max((x0+FlowNode2D<FP,NUM_COMPONENTS>::dx*pJ2D->GetX()),
                                                           (FlowNode2D<FP,NUM_COMPONENTS>::dy*pJ2D->GetY()));
                        
                        x = x0 + i * FlowNode2D<FP,NUM_COMPONENTS>::dx;
                        y = j * FlowNode2D<FP,NUM_COMPONENTS>::dy;

                        for (int ii=0;ii<(int)WallNodes2D->GetNumElements();ii++) {

                             iw = WallNodes2D->GetElementPtr(ii)->GetX();
                             jw = WallNodes2D->GetElementPtr(ii)->GetY();

                             wx = iw * FlowNode2D<FP,NUM_COMPONENTS>::dx;
                             wy = jw * FlowNode2D<FP,NUM_COMPONENTS>::dy;

                             pJ2D->GetValue(i,j).l_min = min(pJ2D->GetValue(i,j).l_min,
                                                             sqrt((x-wx)*(x-wx) + (y-wy)*(y-wy)));
                             pJ2D->GetValue(i,j).l_min =  max(min_l_min,pJ2D->GetValue(i,j).l_min);
                        }
             }
         }
      }
   }
}


#ifdef _IMPI_
void LongSend(int rank, void* src,  size_t len, int data_tag) {
const size_t fix_buf_size=1024*1024; // 1M
char  TmpBuff[fix_buf_size+1];
int   num;
off_t offset=0L;

      if(len <= fix_buf_size) {
                MPI::COMM_WORLD.Send(src,
                                     len,
                                     MPI::BYTE,rank,data_tag);
      } else {
       num = len/fix_buf_size;
       for(int i=0;i<num;i++) {
                memcpy(TmpBuff,src+offset,fix_buf_size);
                MPI::COMM_WORLD.Send(TmpBuff,
                                     fix_buf_size,
                                     MPI::BYTE,rank,data_tag);
                offset += fix_buf_size;
       }

       if(len - num*fix_buf_size > 0L) {
          memcpy(TmpBuff,src+offset,len - num*fix_buf_size);
          MPI::COMM_WORLD.Send(TmpBuff,
                              len - num*fix_buf_size,
                              MPI::BYTE,rank,data_tag);
       }
     }
}

void LongRecv(int rank, void* dst,  size_t len, int data_tag) {
const size_t fix_buf_size=1024*1024; // 1M
char  TmpBuff[fix_buf_size+1];
int   num;
off_t offset=0L;
      if(len <= fix_buf_size) {
            MPI::COMM_WORLD.Recv(dst,
                                 len,
                                 MPI::BYTE,rank,data_tag);
      } else {
       num = len/fix_buf_size;
       for(int i=0;i<num;i++) {
                MPI::COMM_WORLD.Recv(TmpBuff,
                                     fix_buf_size,
                                     MPI::BYTE,rank,data_tag);
                memcpy(dst+offset,TmpBuff,fix_buf_size);
                offset += fix_buf_size;
       }

       if(len - num*fix_buf_size > 0L) {
          MPI::COMM_WORLD.Recv(TmpBuff,
                              len - num*fix_buf_size,
                              MPI::BYTE,rank,data_tag);
          memcpy(dst+offset, TmpBuff, len - num*fix_buf_size);
       }
      }
}

void LongMatrixSend(int rank, void* src,  size_t len) {
const size_t fix_buf_size=1024*1024; // 1M
char  TmpBuff[fix_buf_size+1];
int   num;
off_t offset=0L;

      if(len <= fix_buf_size) {
                MPI::COMM_WORLD.Send(src,
                                     len,
                                     MPI::BYTE,rank,tag_Matrix);
      } else {
       num = len/fix_buf_size;
       for(int i=0;i<num;i++) {
                memcpy(TmpBuff,src+offset,fix_buf_size);
                MPI::COMM_WORLD.Send(TmpBuff,
                                     fix_buf_size,
                                     MPI::BYTE,rank,tag_Matrix);
                offset += fix_buf_size;
       }

       if(len - num*fix_buf_size > 0L) {
          memcpy(TmpBuff,src+offset,len - num*fix_buf_size);
          MPI::COMM_WORLD.Send(TmpBuff,
                              len - num*fix_buf_size,
                              MPI::BYTE,rank,tag_Matrix);
       }
     }
}

void LongMatrixRecv(int rank, void* dst,  size_t len) {
const size_t fix_buf_size=1024*1024; // 1M
char  TmpBuff[fix_buf_size+1];
int   num;
off_t offset=0L;
      if(len <= fix_buf_size) {
            MPI::COMM_WORLD.Recv(dst,
                                 len,
                                 MPI::BYTE,rank,tag_Matrix);
      } else {
       num = len/fix_buf_size;
       for(int i=0;i<num;i++) {
                MPI::COMM_WORLD.Recv(TmpBuff,
                                     fix_buf_size,
                                     MPI::BYTE,rank,tag_Matrix);
                memcpy(dst+offset,TmpBuff,fix_buf_size);
                offset += fix_buf_size;
       }

       if(len - num*fix_buf_size > 0L) {
          MPI::COMM_WORLD.Recv(TmpBuff,
                              len - num*fix_buf_size,
                              MPI::BYTE,rank,tag_Matrix);
          memcpy(dst+offset, TmpBuff, len - num*fix_buf_size);
       }
      }
}
#endif // _IMPI_

inline FP DEEPS2D_Stage2(UMatrix2D< FlowNode2D<FP,NUM_COMPONENTS> >*     pLJ,
                         UMatrix2D< FlowNodeCore2D<FP,NUM_COMPONENTS> >* pLC,
                         int MIN_X, int MAX_X, int MAX_Y,
                         FP  beta0, int b_FF, ChemicalReactionsModelData2D* pCRMD,
                         int iter, int last_iter, int TurbStartIter,
                         FP SigW, FP SigF, FP dx_1, FP dy_1, FP delta_bl,
                         FP CFL,FP beta_init,
#ifdef _RMS_
#ifdef _MPI
                         Var_pack* DD_max, int rank,
#else
                         UMatrix2D<FP>& RMS, 
                         UMatrix2D<int>&    iRMS,
                         UMatrix2D<FP>& DD_max,
                         int* i_c, int* j_c,
                         int ii,
#endif //_MPI 
#endif // _RMS_
                         TurbulenceExtendedModel TurbExtModel) {

    FP dt_min_local;
    FP beta_min;

    beta_min = min(beta0,beta_init);

    for (int i = MIN_X;i<MAX_X;i++ ) {
       for (int j = 0;j<MAX_Y;j++ ) {

           FlowNode2D< FP,NUM_COMPONENTS >* CurrentNode=NULL;

           CurrentNode = &(pLJ->GetValue(i,j)); 

           if (CurrentNode->isCond2D(CT_NODE_IS_SET_2D) &&
               !CurrentNode->isCond2D(CT_SOLID_2D) &&
               !CurrentNode->isCond2D(NT_FC_2D)) {

               FlowNodeCore2D< FP,NUM_COMPONENTS >* NextNode=NULL;

               FlowNode2D< FP,NUM_COMPONENTS >* UpNode=NULL;          // near
               FlowNode2D< FP,NUM_COMPONENTS >* DownNode=NULL;        // nodes
               FlowNode2D< FP,NUM_COMPONENTS >* LeftNode=NULL;        // references
               FlowNode2D< FP,NUM_COMPONENTS >* RightNode=NULL;
               
               FP DD_local[NUM_COMPONENTS+6];

               int Num_Eq = FlowNode2D<FP,NUM_COMPONENTS>::NumEq-SetTurbulenceModel(CurrentNode);

               NextNode    = &(pLC->GetValue(i,j)); 

               int  n1=CurrentNode->idXl;
               int  n2=CurrentNode->idXr;
               int  n3=CurrentNode->idYu;
               int  n4=CurrentNode->idYd;

               int  N1 = i - n1;
               int  N2 = i + n2;
               int  N3 = j + n3;
               int  N4 = j - n4;

               int  n_n = max(n1+n2,1);
               int  m_m = max(n3+n4,1);

               FP  n_n_1 = 1./n_n;
               FP  m_m_1 = 1./m_m;

               UpNode    = &(pLJ->GetValue(i,N3));
               DownNode  = &(pLJ->GetValue(i,N4));
               RightNode = &(pLJ->GetValue(N2,j));
               LeftNode  = &(pLJ->GetValue(N1,j));

               // Scan equation system ... k - number of equation
               for (int k=0;k<Num_Eq;k++ ) {

                   int c_flag = 0;

                   if ( k < 4 ) // Make bit flags for future test for current equation // FlowNode2D<FP,NUM_COMPONENTS>::NumEq-AddEq-NUM_COMPONENTS ?
                       c_flag  = CT_Rho_CONST_2D   << k;
                   else if (k<(4+NUM_COMPONENTS))  // 7 ?
                       c_flag  = CT_Y_CONST_2D;
                   else if((CurrentNode->isTurbulenceCond2D(TCT_k_eps_Model_2D) ||
                            CurrentNode->isTurbulenceCond2D(TCT_Spalart_Allmaras_Model_2D) )) 
                       c_flag  = TCT_k_CONST_2D << (k-4-NUM_COMPONENTS); 

                   DD_local[k] = 0;

                   if ( !CurrentNode->isCond2D((CondType2D)c_flag) && 
                         CurrentNode->S[k] != 0. ) {

                         FP Tmp;

                         if(k == i2d_RhoU && k == i2d_RhoV ) {
                             Tmp = sqrt(CurrentNode->S[i2d_RhoU]*CurrentNode->S[i2d_RhoU]+
                                        CurrentNode->S[i2d_RhoV]*CurrentNode->S[i2d_RhoV]+1.e-30); // Flux
                         } else {
                             Tmp = CurrentNode->S[k];
                         }

                         if(fabs(Tmp) > 1.e-15)
                            DD_local[k] = fabs((NextNode->S[k]-CurrentNode->S[k])/Tmp);
                         else
                            DD_local[k] = 0.0;

                         if( b_FF == BFF_L) {
                         //LINEAR locally adopted blending factor function  (LLABFF)
                           CurrentNode->beta[k] = min(beta_min,(beta_min*beta_min)/(beta_min+DD_local[k]));
                         } else if( b_FF == BFF_LR) {
                         //LINEAR locally adopted blending factor function with relaxation (LLABFFR)
                           CurrentNode->beta[k] = min((beta_min+CurrentNode->beta[k])*0.5,(beta_min*beta_min)/(beta_min+DD_local[k]));
                         } else if( b_FF == BFF_S) {
                         //SQUARE locally adopted blending factor function (SLABF)
                           CurrentNode->beta[k] = min(beta_min,(beta_min*beta_min)/(beta_min+DD_local[k]*DD_local[k]));
                         } else if (b_FF == BFF_SR) {
                         //SQUARE locally adopted blending factor function with relaxation (SLABFFR)
                           CurrentNode->beta[k] = min((beta_min+CurrentNode->beta[k])*0.5,(beta_min*beta_min)/(beta_min+DD_local[k]*DD_local[k]));
                         } else if( b_FF == BFF_SQR) {
                         //SQRT() locally adopted blending factor function (SQRLABF)
                           CurrentNode->beta[k] = min(beta_min,(beta_min*beta_min)/(beta_min+sqrt(DD_local[k])));
                         } else if( b_FF == BFF_SQRR) {
                         //SQRT() locally adopted blending factor function with relaxation (SQRLABFFR)
                           CurrentNode->beta[k] = min((beta_min+CurrentNode->beta[k])*0.5,(beta_min*beta_min)/(beta_min+sqrt(DD_local[k]))); 
                         } else if( b_FF == BFF_LG ) {
                           FP LGAF = sqrt(CurrentNode->dSdx[k]*CurrentNode->dSdx[k] +
                                              CurrentNode->dSdy[k]*CurrentNode->dSdy[k] + 1.0e-30) * (dx+dy)/CurrentNode->S[k];
                           CurrentNode->beta[k] = min(beta_min,(beta_min*beta_min)/(beta_min+LGAF));
                         } else if (b_FF == BFF_MACH) {
                           //Mach number depended locally adapted blending factor function  (MLABFF)
                           CurrentNode->beta[k] = min(beta0,(beta_min*beta_min)/(beta_min+pow(DD_local[k],1.0/(1.0+Mach)))); // 1/(1+M) or 1/M ???
                         } else if (b_FF == BFF_HYBRID) {
                           // Hybrid BFF
                           FP LGAF = sqrt(CurrentNode->dSdx[k]*CurrentNode->dSdx[k] +
                                              CurrentNode->dSdy[k]*CurrentNode->dSdy[k] + 1.0e-30) * (dx+dy)/CurrentNode->S[k];
                           CurrentNode->beta[k] = min(beta_min,(beta_min*beta_min)/(beta_min+(LGAF+
                                                                               DD_local[k]*DD_local[k]+
                                                                               pow(DD_local[k],1.0/(1.0+Mach)))/3.));
                         } else if( b_FF == BFF_MIXED ) {
                           FP LGAF = sqrt(CurrentNode->dSdx[k]*CurrentNode->dSdx[k] +
                                              CurrentNode->dSdy[k]*CurrentNode->dSdy[k] + 1.0e-30) * (dx+dy)/CurrentNode->S[k];
                           CurrentNode->beta[k] = min(beta_min,(beta_min*beta_min)/(beta_min+(LGAF+DD_local[k]*DD_local[k])*0.5));
                         } else if (b_FF == BFF_SR_LIMITED) {
                           //LIMITED locally adopted blending factor function with relaxation (SLLABFFR)
                           CurrentNode->beta[k] = min((beta_min+CurrentNode->beta[k])*0.5,beta_min/(1.0+DD_local[k]));
                         } else {
                           // Default->SQRLABF
                           CurrentNode->beta[k] = min(beta_min,(beta_min*beta_min)/(beta_min+sqrt(DD_local[k])));
                         }
#ifdef _RMS_
                         RMS(k,ii) += DD_local[k];
                         iRMS(k,ii)++;
                         DD_max(k,ii) = max(DD_max(k,ii),DD_local[k]);

                         if ( DD_max(k,ii) == DD_local[k] ) {
                              i_c[ii] = i;
                              j_c[ii] = j;
                         }
#endif // RMS
                   }
                   if (k<(4+NUM_COMPONENTS)) {
                       if ( !CurrentNode->isCond2D((CondType2D)c_flag) )
                             CurrentNode->S[k]   = NextNode->S[k];
                   } else if ((CurrentNode->isTurbulenceCond2D(TCT_k_eps_Model_2D) ||
                               CurrentNode->isTurbulenceCond2D(TCT_Spalart_Allmaras_Model_2D)) ){
                       if ( !CurrentNode->isTurbulenceCond2D((TurbulenceCondType2D)c_flag) )
                             CurrentNode->S[k]   =  NextNode->S[k];
                   }
               }

               CurrentNode->droYdx[NUM_COMPONENTS]=CurrentNode->droYdy[NUM_COMPONENTS]=0.;

               for (int k=4;k<FlowNode2D<FP,NUM_COMPONENTS>::NumEq-2;k++ ) {
                   if ( !CurrentNode->isCond2D(CT_dYdx_NULL_2D) ) {
                       CurrentNode->droYdx[k-4]=(RightNode->S[k]-LeftNode->S[k])*dx_1*0.5;
                       CurrentNode->droYdx[NUM_COMPONENTS]+=(RightNode->S[k]-LeftNode->S[k])*dx_1*0.5;
                   }
                   if ( !CurrentNode->isCond2D(CT_dYdy_NULL_2D) ) {
                         CurrentNode->droYdy[k-4]=(UpNode->S[k]-DownNode->S[k])*dy_1*0.5;
                         CurrentNode->droYdy[NUM_COMPONENTS]+=(DownNode->S[k]-UpNode->S[k])*dy_1*0.5;
                   }
               }

               if (CurrentNode->isCond2D(CT_WALL_NO_SLIP_2D) || CurrentNode->isCond2D(CT_WALL_LAW_2D) )  {
                   CurrentNode->dUdx=(RightNode->U*n1-LeftNode->U*n2)*dx_1*n_n_1;
                   CurrentNode->dVdx=(RightNode->V*n1-LeftNode->V*n2)*dx_1*n_n_1;

                   CurrentNode->dUdy=(UpNode->U*n3-DownNode->U*n4)*dy_1*m_m_1;
                   CurrentNode->dVdy=(UpNode->V*n3-DownNode->V*n4)*dy_1*m_m_1;

                   if(CurrentNode->isTurbulenceCond2D(TCT_k_eps_Model_2D)){
                     CurrentNode->dkdx   =(RightNode->S[i2d_k]*n1-LeftNode->S[i2d_k]*n2)*dx_1/CurrentNode->S[i2d_Rho]*n_n_1;
                     CurrentNode->depsdx =(RightNode->S[i2d_eps]*n1-LeftNode->S[i2d_eps]*n2)*dx_1/CurrentNode->S[i2d_Rho]*n_n_1;

                     CurrentNode->dkdy   =(UpNode->S[i2d_k]*n3-DownNode->S[i2d_k]*n4)*dy_1/CurrentNode->S[i2d_Rho]*m_m_1;
                     CurrentNode->depsdy =(UpNode->S[i2d_eps]*n3-DownNode->S[i2d_eps]*n4)*dy_1/CurrentNode->S[i2d_Rho]*m_m_1;
                   } else if (CurrentNode->isTurbulenceCond2D(TCT_Spalart_Allmaras_Model_2D)) {
                              CurrentNode->dkdx   =(RightNode->S[i2d_k]*n1-LeftNode->S[i2d_k]*n2)*dx_1/CurrentNode->S[i2d_Rho]*n_n_1;
                              CurrentNode->dkdy   =(UpNode->S[i2d_k]*n3-DownNode->S[i2d_k]*n4)*dy_1/CurrentNode->S[i2d_Rho]*m_m_1;
                   }
               } else {
                   CurrentNode->dUdx   =(RightNode->U-LeftNode->U)*dx_1*n_n_1;
                   CurrentNode->dVdx   =(RightNode->V-LeftNode->V)*dx_1*n_n_1;

                   CurrentNode->dUdy   =(UpNode->U-DownNode->U)*dy_1*m_m_1;
                   CurrentNode->dVdy   =(UpNode->V-DownNode->V)*dy_1*m_m_1;
                   if(CurrentNode->isTurbulenceCond2D(TCT_k_eps_Model_2D)){
                     CurrentNode->dkdx   =(RightNode->S[i2d_k]-LeftNode->S[i2d_k])*dx_1/CurrentNode->S[i2d_Rho]*n_n_1;
                     CurrentNode->depsdx =(RightNode->S[i2d_eps]-LeftNode->S[i2d_eps])*dx_1/CurrentNode->S[i2d_Rho]*n_n_1;

                     CurrentNode->dkdy   =(UpNode->S[i2d_k]-DownNode->S[i2d_k])*dy_1/CurrentNode->S[i2d_Rho]*m_m_1;
                     CurrentNode->depsdy =(UpNode->S[i2d_eps]-DownNode->S[i2d_eps])*dy_1/CurrentNode->S[i2d_Rho]*m_m_1;
                   } else if (CurrentNode->isTurbulenceCond2D(TCT_Spalart_Allmaras_Model_2D)) {
                              CurrentNode->dkdx   =(RightNode->S[i2d_k]-LeftNode->S[i2d_k])*dx_1/CurrentNode->S[i2d_Rho]*n_n_1;
                              CurrentNode->dkdy   =(UpNode->S[i2d_k]-DownNode->S[i2d_k])*dy_1/CurrentNode->S[i2d_Rho]*m_m_1;
                   }
               }

               CurrentNode->dTdx=(RightNode->Tg-LeftNode->Tg)*dx_1*n_n_1;
               CurrentNode->dTdy=(UpNode->Tg-DownNode->Tg)*dy_1*m_m_1;

               CalcChemicalReactions(CurrentNode,CRM_ZELDOVICH, (void*)(pCRMD));

               if((int)(iter+last_iter) < TurbStartIter) {
                  CurrentNode->FillNode2D(0,1,SigW,SigF,(TurbulenceExtendedModel)TurbExtModel,delta_bl);
               } else {
                  CurrentNode->FillNode2D(1,0,SigW,SigF,(TurbulenceExtendedModel)TurbExtModel,delta_bl);
               }

               if( CurrentNode->Tg < 0. ) {
                   *f_stream << "\nTg=" << CurrentNode->Tg << " K. p=" << CurrentNode->p <<" Pa dt=" << dt << " sec.\n" << flush;
                   *f_stream << "\nERROR: Computational unstability in UMatrix2D< FlowNode2D<FP,NUM_COMPONENTS> >(" << i <<","<< j <<") on iteration " << iter+last_iter<< "...\n";
                   return 0.0;
               }  else {
                   FP AAA          = sqrt(CurrentNode->k*CurrentNode->R*CurrentNode->Tg); 
                   dt_min_local    = CFL*min(dx/(AAA+fabs(CurrentNode->U)),dy/(AAA+fabs(CurrentNode->V)));
               }
        }
      }
    }
 return dt_min_local;
}

#endif // _MPI_
