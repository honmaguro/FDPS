///////////////////////////////////////////////
// implementaion of methods of TreeForForce ///

namespace ParticleSimulator{
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    template<class Tep2, class Tep3>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>
    ::scatterEP(S32 n_send[],
                S32 n_send_disp[],
                S32 n_recv[],
                S32 n_recv_disp[],
                ReallocatableArray<Tep2> & ep_send,  // send buffer
                ReallocatableArray<Tep2> & ep_recv,  // recv buffer
                const ReallocatableArray<Tep3> & ep_org, // original
                const DomainInfo & dinfo){
        static bool first = true;
        static ReallocatableArray<Tep2> * ep_send_buf;
        static ReallocatableArray<F64vec> * shift_image_domain;
        if(first){
            const S32 n_thread = Comm::getNumberOfThread();
            ep_send_buf = new ReallocatableArray<Tep2>[n_thread];
            shift_image_domain = new ReallocatableArray<F64vec>[n_thread];
            for(S32 i=0; i<n_thread; i++){
	        //ep_send_buf[i].reserve(1000);
                ep_send_buf[i].reserve(n_surface_for_comm_);
                shift_image_domain[i].reserve(5*5*5);
            }
            first = false;
        }
        const S32 n_proc = Comm::getNumberOfProc();
        const S32 my_rank = Comm::getRank();
        const F64ort outer_boundary_of_my_tree = tc_loc_[0].mom_.vertex_out_;

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"outer_boundary_of_my_tree="<<outer_boundary_of_my_tree<<std::endl;
#endif

        wtime_walk_LET_1st_ = GetWtime();

#pragma omp parallel
        {
            const S32 ith = Comm::getThreadNum();
            S32 n_proc_cum = 0;
            bool pa[DIMENSION];
            dinfo.getPeriodicAxis(pa);
            ep_send_buf[ith].clearSize();
//#pragma omp for schedule(dynamic, 4)
#pragma omp for schedule(dynamic, 1)
            for(S32 ib=0; ib<n_proc; ib++){
                n_send[ib] = n_recv[ib] = 0;
                shift_image_domain[ith].clearSize();
                if(dinfo.getBoundaryCondition() == BOUNDARY_CONDITION_OPEN){
                    shift_image_domain[ith].push_back( (F64vec)(0.0));
                }
                else{
                    CalcNumberAndShiftOfImageDomain
                        (shift_image_domain[ith], 
                         dinfo.getPosRootDomain().getFullLength(),
                         outer_boundary_of_my_tree, 
                         dinfo.getPosDomain(ib), 
                         pa);
                }
                S32 n_ep_offset = ep_send_buf[ith].size();
                const S32 n_image = shift_image_domain[ith].size();
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
                PARTICLE_SIMULATOR_PRINT_LINE_INFO();
                std::cout<<"n_image="<<n_image<<std::endl;
                std::cout<<"dinfo.getPosDomain(ib)="<<dinfo.getPosDomain(ib)<<std::endl;
#endif
                for(S32 ii=0; ii<n_image; ii++){
                    if(my_rank == ib && ii == 0) continue; // skip self image
                    F64ort pos_domain = dinfo.getPosDomain(ib).shift(shift_image_domain[ith][ii]);
                    const S32 adr_tc_tmp = tc_loc_[0].adr_tc_;

                    if( !tc_loc_[0].isLeaf(n_leaf_limit_) ){
                        MakeListUsingOuterBoundary
                            (tc_loc_.getPointer(),  adr_tc_tmp,
                             ep_org.getPointer(),   ep_send_buf[ith],
                             pos_domain,            n_leaf_limit_,
                             -shift_image_domain[ith][ii]);
                    }
                    else{
                        const S32 n_tmp = tc_loc_[0].n_ptcl_;
                        S32 adr_tmp = tc_loc_[0].adr_ptcl_;
                        for(S32 iii=0; iii<n_tmp; iii++, adr_tmp++){

                            const F64vec pos_tmp = ep_org[adr_tmp].getPos();
                            const F64 size_tmp = ep_org[adr_tmp].getRSearch();
                            const F64 dis_sq_tmp = pos_domain.getDistanceMinSQ(pos_tmp);
                            if(dis_sq_tmp > size_tmp*size_tmp) continue;
                            ep_send_buf[ith].increaseSize();
                            ep_send_buf[ith].back() = ep_org[adr_tmp];
                            const F64vec pos_new = ep_send_buf[ith].back().getPos() - shift_image_domain[ith][ii];
                            ep_send_buf[ith].back().setPos(pos_new);
                        }
                    }
                }
                //n_ep_send_[ib] = ep_send_buf[ith].size() - n_ep_offset;
                n_send[ib] = ep_send_buf[ith].size() - n_ep_offset;
                id_proc_send_[ith][n_proc_cum++] = ib;  // new
            } // omp for
#pragma omp single
            {
                n_send_disp[0] = 0;
                for(S32 i=0; i<n_proc; i++){
                    n_send_disp[i+1] = n_send_disp[i] + n_send[i];
                }
                ep_send.resizeNoInitialize(n_send_disp[n_proc]);
            }
            S32 n_ep_cnt = 0;
            for(S32 ib=0; ib<n_proc_cum; ib++){
                const S32 id = id_proc_send_[ith][ib];
                const S32 adr_ep_tmp = n_send_disp[id];
                const S32 n_ep_tmp = n_send[id];
                for(int ip=0; ip<n_ep_tmp; ip++){
                    ep_send[adr_ep_tmp+ip] = ep_send_buf[ith][n_ep_cnt++];
                }
            }
        } // omp parallel scope

        wtime_walk_LET_1st_ = GetWtime() - wtime_walk_LET_1st_;

        Tcomm_scatterEP_tmp_ = GetWtime();
        Comm::allToAll(n_send, 1, n_recv); // TEST
        n_recv_disp[0] = 0;
        for(S32 i=0; i<n_proc; i++){
            n_recv_disp[i+1] = n_recv_disp[i] + n_recv[i];
        }
        ep_recv.resizeNoInitialize( n_recv_disp[n_proc] );
        Comm::allToAllV(ep_send.getPointer(), n_send, n_send_disp,
                        ep_recv.getPointer(), n_recv, n_recv_disp);
        Tcomm_scatterEP_tmp_ = GetWtime() - Tcomm_scatterEP_tmp_;

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"ep_send.size()="<<ep_send.size()<<std::endl;
        std::cout<<"ep_recv.size()="<<ep_recv.size()<<std::endl;
#endif
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    size_t TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>
    ::getMemSizeUsed() const {
	return tp_buf_.getMemSize() + tp_loc_.getMemSize() + tp_glb_.getMemSize()
	    + tc_loc_.getMemSize() + tc_glb_.getMemSize()
	    + epi_sorted_.getMemSize() + epi_org_.getMemSize()
	    + epj_sorted_.getMemSize() + epj_org_.getMemSize()
	    + spj_sorted_.getMemSize() + spj_org_.getMemSize()
	    + ipg_.getMemSize()
	    + epj_send_.getMemSize() + epj_recv_.getMemSize()
	    + spj_send_.getMemSize() + spj_recv_.getMemSize();
    }
    

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    initialize(const U64 n_glb_tot,
               const F64 theta,
               const U32 n_leaf_limit,
               const U32 n_group_limit){
        if(is_initialized_ == true){
            PARTICLE_SIMULATOR_PRINT_ERROR("Do not initialize the tree twice");
            std::cerr<<"SEARCH_MODE: "<<typeid(TSM).name()<<std::endl;
            std::cerr<<"Force: "<<typeid(Tforce).name()<<std::endl;
            std::cerr<<"EPI: "<<typeid(Tepi).name()<<std::endl;
            std::cerr<<"EPJ: "<<typeid(Tepj).name()<<std::endl;
            std::cerr<<"SPJ: "<<typeid(Tspj).name()<<std::endl;
            Abort(-1);
        }
        is_initialized_ = true;
        n_glb_tot_ = n_glb_tot;
        theta_ = theta;
        n_leaf_limit_ = n_leaf_limit;
        n_group_limit_ = n_group_limit;
        lev_max_ = 0;
        const S32 n_thread = Comm::getNumberOfThread();
        //std::cerr<<"n_thread="<<n_thread<<std::endl;
        const S64 n_proc = Comm::getNumberOfProc();
        //std::cerr<<"n_proc="<<n_proc<<std::endl;
        const S64 np_ave = (n_glb_tot_ / n_proc);
        //std::cerr<<"np_ave="<<np_ave<<std::endl;

        const F64 np_one_dim = pow( ((F64)np_ave)*1.0001, 1.0/DIMENSION) + 4;
#ifdef PARTICLE_SIMULATOR_TWO_DIMENSION
        n_surface_for_comm_ = (4*np_one_dim)*6;
#else
        n_surface_for_comm_ = (6*np_one_dim*np_one_dim+8*np_one_dim)*6;
#endif
        //std::cerr<<"n_surface_for_comm_="<<n_surface_for_comm_<<std::endl;
        epi_org_.reserve( np_ave*4 + 100 );
        epi_sorted_.reserve( epi_org_.capacity() );

        bool err = false;
        if(n_leaf_limit_ <= 0){
            err = true;
            PARTICLE_SIMULATOR_PRINT_ERROR("The limit number of the particles in the leaf cell must be > 0");
            std::cout<<"n_leaf_limit_= "<<n_leaf_limit_<<std::endl;
        }
        if(n_group_limit_ < n_leaf_limit_){
            err = true;
            PARTICLE_SIMULATOR_PRINT_ERROR("The limit number of particles in ip graoups msut be >= that in leaf cells");
            std::cout<<"n_group_limit_= "<<n_group_limit_<<std::endl;
            std::cout<<"n_leaf_limit_= "<<n_leaf_limit_<<std::endl;
        }
        if( typeid(TSM) == typeid(SEARCH_MODE_LONG) || 
            typeid(TSM) == typeid(SEARCH_MODE_LONG_CUTOFF) ){
            if(theta_ < 0.0){
                err = true;
                //PARTICLE_SIMULATOR_PRINT_ERROR("theta must be >= 0.0");
                PARTICLE_SIMULATOR_PRINT_ERROR("The opening criterion of the tree must be >= 0.0");
                std::cout<<"theta_= "<<theta_<<std::endl;
            }
        }
        if(err){
            std::cout<<"SEARCH_MODE: "<<typeid(TSM).name()<<std::endl;
            std::cout<<"Force: "<<typeid(Tforce).name()<<std::endl;
            std::cout<<"EPI: "<<typeid(Tepi).name()<<std::endl;
            std::cout<<"SPJ: "<<typeid(Tspj).name()<<std::endl;
            ParticleSimulator::Abort(-1);
        }

        if( typeid(TSM) == typeid(SEARCH_MODE_LONG) || 
            typeid(TSM) == typeid(SEARCH_MODE_LONG_CUTOFF) ){
            if(theta_ > 0.0){
                const S32 n_tmp = epi_org_.capacity() + 2000 * pow((0.5 / theta_), DIMENSION);
                const S32 n_new = std::min( std::min(n_tmp, n_glb_tot_+100), 20000);
                epj_org_.reserve( n_new );
                epj_sorted_.reserve( n_new );
                spj_org_.reserve( epj_org_.capacity() );
                spj_sorted_.reserve( epj_sorted_.capacity() );

            }
            else if(theta == 0.0) {
                epj_org_.reserve(n_glb_tot_ + 100);
                epj_sorted_.reserve(n_glb_tot_ + 100);
                spj_org_.reserve(n_glb_tot_ + 100);
                spj_sorted_.reserve(n_glb_tot_ + 100);
            }

            id_ep_send_buf_ = new ReallocatableArray<S32>[n_thread];
            id_sp_send_buf_ = new ReallocatableArray<S32>[n_thread];
            for(S32 i=0; i<n_thread; i++){
                id_ep_send_buf_[i].reserve(100);
                id_sp_send_buf_[i].reserve(100);
            }
            spj_send_.reserve(n_proc+1000);
            spj_recv_.reserve(n_proc+1000);
            n_sp_send_ = new S32[n_proc];
            n_sp_recv_ = new S32[n_proc];
            n_sp_send_disp_ = new S32[n_proc+1];
            n_sp_recv_disp_ = new S32[n_proc+1];
            epj_for_force_ = new ReallocatableArray<Tepj>[n_thread];
            spj_for_force_ = new ReallocatableArray<Tspj>[n_thread];
            for(S32 i=0; i<n_thread; i++){
                epj_for_force_[i].reserve(epj_org_.size() * 2 / n_thread);
                spj_for_force_[i].reserve(spj_org_.size() * 2 / n_thread);
            }
        }
        else{
            // FOR SHORT MODE
            epj_org_.reserve( epi_org_.capacity() + n_surface_for_comm_ );
            epj_sorted_.reserve( epj_org_.capacity() );
            S32 n_thread = Comm::getNumberOfThread();
            id_ep_send_buf_ = new ReallocatableArray<S32>[n_thread];
            for(S32 i=0; i<n_thread; i++){
                //id_ep_send_buf_[i].reserve(100);
                id_ep_send_buf_[i].reserve(n_surface_for_comm_ * 2 / n_thread);
            }
            epj_for_force_ = new ReallocatableArray<Tepj>[n_thread];
            spj_for_force_ = new ReallocatableArray<Tspj>[n_thread];
            for(S32 i=0; i<n_thread; i++){
                epj_for_force_[i].reserve(n_surface_for_comm_ * 2 / n_thread);
                spj_for_force_[i].reserve(1);
            }
        }

        tp_loc_.reserve( epi_org_.capacity() );
        tp_glb_.reserve( epj_org_.capacity() );
        tp_buf_.reserve( epj_org_.capacity() );
        //std::cout<<"tp_loc_.capacity()="<<tp_loc_.capacity()<<std::endl;
        //std::cout<<"tp_glb_.capacity()="<<tp_glb_.capacity()<<std::endl;
        tc_loc_.reserve( tp_loc_.capacity() / n_leaf_limit_ * N_CHILDREN );
        //std::cout<<"tc_loc_.capacity()="<<tc_loc_.capacity()<<std::endl;
        tc_glb_.reserve( tp_glb_.capacity() / n_leaf_limit_ * N_CHILDREN );
        //std::cout<<"tc_glb_.capacity()="<<tc_glb_.capacity()<<std::endl;
        ipg_.reserve( std::min(epi_org_.capacity()/n_group_limit_*4, epi_org_.capacity()) );
        id_proc_send_ = new S32*[n_thread];
        for(S32 i=0; i<n_thread; i++) id_proc_send_[i] = new S32[n_proc];

        epj_send_.reserve(n_surface_for_comm_);
        epj_recv_.reserve(n_surface_for_comm_);

        n_ep_send_ = new S32[n_proc];
        n_ep_recv_ = new S32[n_proc];
        n_ep_send_disp_ = new S32[n_proc+1];
        n_ep_recv_disp_ = new S32[n_proc+1];

        force_org_.reserve(epi_org_.capacity());
        force_sorted_.reserve(epi_org_.capacity());

    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    template<class Tpsys>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    setParticleLocalTree(const Tpsys & psys,
                         const bool clear){
        const S32 nloc = psys.getNumberOfParticleLocal();
        if(clear){ n_loc_tot_ = 0;}
        const S32 offset = 0;
        n_loc_tot_ += nloc;
        epi_org_.resizeNoInitialize(n_loc_tot_);
        epj_org_.resizeNoInitialize(n_loc_tot_);
        if(clear){
#pragma omp parallel for
            for(S32 i=0; i<nloc; i++){
                epi_org_[i].copyFromFP( psys[i] );
                epj_org_[i].copyFromFP( psys[i] );
            }
        }
        else{
#pragma omp parallel for
            for(S32 i=0; i<nloc; i++){
                epi_org_[i+offset].copyFromFP( psys[i] );
                epj_org_[i+offset].copyFromFP( psys[i] );
            }
        }
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
            PARTICLE_SIMULATOR_PRINT_LINE_INFO();
            std::cout<<"nloc="<<nloc<<std::endl;
            std::cout<<"n_loc_tot_="<<n_loc_tot_<<std::endl;
#endif
    }
    
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    setRootCell(const DomainInfo & dinfo){
        if(dinfo.getBoundaryCondition() == BOUNDARY_CONDITION_OPEN){
            calcCenterAndLengthOfRootCellOpenImpl(typename TSM::search_type());
        }
        else{
            calcCenterAndLengthOfRootCellPeriodicImpl(typename TSM::search_type());
        }
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
            PARTICLE_SIMULATOR_PRINT_LINE_INFO();
            std::cout<<"length_="<<length_<<" center_="<<center_<<std::endl;
            std::cout<<"pos_root_cell_="<<pos_root_cell_<<std::endl;
#endif
    }

// new function by M.I.
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    template<class Ttree>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    copyRootCell(const Ttree & tree){
        center_ = tree.center_;
        length_ = tree.length_;
        pos_root_cell_ = tree.pos_root_cell_;
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    setRootCell(const F64 l, const F64vec & c){
        center_ = c;
        length_ = l;
        pos_root_cell_.low_ = center_ - F64vec(length_*0.5);
        pos_root_cell_.high_ = center_ + F64vec(length_*0.5);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    mortonSortLocalTreeOnly(){
        tp_loc_.resizeNoInitialize(n_loc_tot_);
        tp_buf_.resizeNoInitialize(n_loc_tot_);
        epi_sorted_.resizeNoInitialize(n_loc_tot_);
        epj_sorted_.resizeNoInitialize(n_loc_tot_);
        MortonKey::initialize( length_ * 0.5, center_);
#pragma omp parallel for
        for(S32 i=0; i<n_loc_tot_; i++){
            tp_loc_[i].setFromEP(epj_org_[i], i);
        }
        rs_.lsdSort(tp_loc_.getPointer(), tp_buf_.getPointer(), 0, n_loc_tot_-1);
#pragma omp parallel for
        for(S32 i=0; i<n_loc_tot_; i++){
            const S32 adr = tp_loc_[i].adr_ptcl_;
            epi_sorted_[i] = epi_org_[adr];
            epj_sorted_[i] = epj_org_[adr];
        }
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
            PARTICLE_SIMULATOR_PRINT_LINE_INFO();
            std::cout<<"tp_loc_.size()="<<tp_loc_.size()<<" tp_buf_.size()="<<tp_buf_.size()<<std::endl;
            std::cout<<"epi_sorted_.size()="<<epi_sorted_.size()<<" epj_sorted_.size()="<<epj_sorted_.size()<<std::endl;
#endif
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    linkCellLocalTreeOnly(){
        LinkCell(tc_loc_, adr_tc_level_partition_, tp_loc_.getPointer(), lev_max_, n_loc_tot_, n_leaf_limit_);
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"tc_loc_.size()="<<tc_loc_.size()<<std::endl;
        std::cout<<"lev_max_="<<lev_max_<<std::endl;
#endif
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentLocalTreeOnly(){
        calcMomentLocalTreeOnlyImpl(typename TSM::search_type());
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
#endif
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentLocalTreeOnlyImpl(TagSearchLong){
        CalcMoment(adr_tc_level_partition_, tc_loc_.getPointer(),
                   epj_sorted_.getPointer(), lev_max_, n_leaf_limit_);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentLocalTreeOnlyImpl(TagSearchLongCutoff){
        CalcMoment(adr_tc_level_partition_, tc_loc_.getPointer(),
                   epj_sorted_.getPointer(), lev_max_, n_leaf_limit_);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentLocalTreeOnlyImpl(TagSearchShortScatter){
        CalcMoment(adr_tc_level_partition_, tc_loc_.getPointer(),
                   epj_sorted_.getPointer(), lev_max_, n_leaf_limit_);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentLocalTreeOnlyImpl(TagSearchShortGather){
	CalcMoment(adr_tc_level_partition_, tc_loc_.getPointer(),
		   epi_sorted_.getPointer(), lev_max_, n_leaf_limit_);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentLocalTreeOnlyImpl(TagSearchShortSymmetry){
        CalcMoment(adr_tc_level_partition_, tc_loc_.getPointer(),
                   epi_sorted_.getPointer(), lev_max_, n_leaf_limit_);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    exchangeLocalEssentialTree(const DomainInfo & dinfo){
        if(typeid(TSM) == typeid(SEARCH_MODE_LONG)
           && dinfo.getBoundaryCondition() != BOUNDARY_CONDITION_OPEN){
            PARTICLE_SIMULATOR_PRINT_ERROR("The forces w/o cutoff can be evaluated only under the open boundary condition");
            Abort(-1);
        }
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        if(tc_loc_[0].n_ptcl_ <= 0){
            std::cout<<"The number of particles of this process is 0."<<std::endl;
            std::cout<<"tc_loc_[0].n_ptcl_="<<tc_loc_[0].n_ptcl_<<std::endl;
        }
#endif
        exchangeLocalEssentialTreeImpl(typename TSM::search_type(), dinfo);
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"epj_send_.size()="<<epj_send_.size()<<" spj_send_.size()="<<spj_send_.size()<<std::endl;
        std::cout<<"epj_recv_.size()="<<epj_recv_.size()<<" spj_recv_.size()="<<spj_recv_.size()<<std::endl;
#endif
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    exchangeLocalEssentialTreeImpl(TagSearchLong, const DomainInfo & dinfo){
        const S32 n_proc = Comm::getNumberOfProc();
        const S32 my_rank = Comm::getRank();
#pragma omp parallel
        {
            const S32 ith = Comm::getThreadNum();
            S32 n_proc_cum = 0;
            id_ep_send_buf_[ith].reserve(1000);
            id_sp_send_buf_[ith].reserve(1000);
            id_ep_send_buf_[ith].resizeNoInitialize(0);
            id_sp_send_buf_[ith].resizeNoInitialize(0);
            const F64 r_crit_sq = (length_ * length_) / (theta_ * theta_) * 0.25;
            const S32 adr_tc_tmp = tc_loc_[0].adr_tc_;
#pragma omp for schedule(dynamic, 4)
            for(S32 ib=0; ib<n_proc; ib++){
                n_ep_send_[ib] = n_sp_send_[ib] = n_ep_recv_[ib] = n_sp_recv_[ib] = 0;
                if(my_rank == ib) continue;
                const F64ort pos_target_domain = dinfo.getPosDomain(ib);
                const S32 n_ep_cum = id_ep_send_buf_[ith].size();
                const S32 n_sp_cum = id_sp_send_buf_[ith].size();
                if(!tc_loc_[0].isLeaf(n_leaf_limit_)){
                    SearchSendParticleLong<TreeCell<Tmomloc>, Tepj>
                        (tc_loc_,               adr_tc_tmp,    epj_sorted_,
                         id_ep_send_buf_[ith],  id_sp_send_buf_[ith],
                         pos_target_domain,  r_crit_sq,   n_leaf_limit_);
                }
                else{
                    F64vec pos_tmp = tc_loc_[0].mom_.getPos();
                    if( (pos_target_domain.getDistanceMinSQ(pos_tmp) <= r_crit_sq * 4.0) ){
                        const S32 n_tmp = tc_loc_[0].n_ptcl_;
                        S32 adr_tmp = tc_loc_[0].adr_ptcl_;
                        for(S32 ip=0; ip<n_tmp; ip++){
                            id_ep_send_buf_[ith].push_back(adr_tmp++);
                        }
                    }
                    else{
                        id_sp_send_buf_[ith].push_back(0); // set root
                    }
                }

                n_ep_send_[ib] = id_ep_send_buf_[ith].size() - n_ep_cum;
                n_sp_send_[ib] = id_sp_send_buf_[ith].size() - n_sp_cum;
                id_proc_send_[ith][n_proc_cum++] = ib;
            }
#pragma omp single
            {
                n_ep_send_disp_[0] = n_sp_send_disp_[0] = 0;
                for(S32 ib=0; ib<n_proc; ib++){
                    n_ep_send_disp_[ib+1] = n_ep_send_disp_[ib] + n_ep_send_[ib];
                    n_sp_send_disp_[ib+1] = n_sp_send_disp_[ib] + n_sp_send_[ib];
                }
                epj_send_.resizeNoInitialize( n_ep_send_disp_[n_proc] );
                spj_send_.resizeNoInitialize( n_sp_send_disp_[n_proc] );
            }
            S32 n_ep_cnt = 0;
            S32 n_sp_cnt = 0;
            for(S32 ib=0; ib<n_proc_cum; ib++){
                const S32 id = id_proc_send_[ith][ib];
                S32 adr_ep_tmp = n_ep_send_disp_[id];
                const S32 n_ep_tmp = n_ep_send_[id];
                for(int ip=0; ip<n_ep_tmp; ip++){
                    const S32 id_ep = id_ep_send_buf_[ith][n_ep_cnt++];
                    epj_send_[adr_ep_tmp++] = epj_sorted_[id_ep];
                }
                S32 adr_sp_tmp = n_sp_send_disp_[id];
                const S32 n_sp_tmp = n_sp_send_[id];
                for(int ip=0; ip<n_sp_tmp; ip++){
                    const S32 id_sp = id_sp_send_buf_[ith][n_sp_cnt++];
                    spj_send_[adr_sp_tmp++].copyFromMoment(tc_loc_[id_sp].mom_);
                }
            }
        } // omp parallel scope

        Tcomm_tmp_ = GetWtime();

        Comm::allToAll(n_ep_send_, 1, n_ep_recv_); // TEST
        Comm::allToAll(n_sp_send_, 1, n_sp_recv_); // TEST
        n_ep_recv_disp_[0] = n_sp_recv_disp_[0] = 0;
        for(S32 i=0; i<n_proc; i++){
            n_ep_recv_disp_[i+1] = n_ep_recv_disp_[i] + n_ep_recv_[i];
            n_sp_recv_disp_[i+1] = n_sp_recv_disp_[i] + n_sp_recv_[i];
        }
        epj_recv_.resizeNoInitialize( n_ep_recv_disp_[n_proc] );
        spj_recv_.resizeNoInitialize( n_sp_recv_disp_[n_proc] );
        Comm::allToAllV(epj_send_.getPointer(), n_ep_send_, n_ep_send_disp_,
                        epj_recv_.getPointer(), n_ep_recv_, n_ep_recv_disp_);
        Comm::allToAllV(spj_send_.getPointer(), n_sp_send_, n_sp_send_disp_,
                        spj_recv_.getPointer(), n_sp_recv_, n_sp_recv_disp_);
        Tcomm_tmp_ = GetWtime() - Tcomm_tmp_;
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    exchangeLocalEssentialTreeImpl(TagSearchLongCutoff, const DomainInfo & dinfo){
        //std::cout<<"SEARCH_MODE_LONG_CUTOFF"<<std::endl;
        const S32 n_proc = Comm::getNumberOfProc();
        const S32 my_rank = Comm::getRank();
        static bool first = true;
        static ReallocatableArray<Tepj> * epj_send_buf;
        static ReallocatableArray<Tspj> * spj_send_buf;
        if(first){
            const S32 n_thread = Comm::getNumberOfThread();
            epj_send_buf = new ReallocatableArray<Tepj>[n_thread];
            spj_send_buf = new ReallocatableArray<Tspj>[n_thread];
            for(S32 i=0; i<n_thread; i++){
                epj_send_buf[i].reserve(1000);
                spj_send_buf[i].reserve(1000);
            }
            first = false;
        }
#pragma omp parallel
        {
            const S32 ith = Comm::getThreadNum();
            S32 n_proc_cum = 0;
            epj_send_buf[ith].clearSize();
            spj_send_buf[ith].clearSize();
            S32 n_epj_cum = 0;
            S32 n_spj_cum = 0;
            const F64 r_crit_sq = (length_ * length_) / (theta_ * theta_) * 0.25;
            const S32 adr_tc_tmp = tc_loc_[0].adr_tc_;
            const F64 r_cut_sq  = epj_sorted_[0].getRSearch() * epj_sorted_[0].getRSearch();
            ReallocatableArray<F64vec> shift_image_domain(5*5*5);
            bool periodic_axis[DIMENSION];
            dinfo.getPeriodicAxis(periodic_axis);        
            const F64ort outer_boundary_of_my_tree = tc_loc_[0].mom_.vertex_out_;
#pragma omp for schedule(dynamic, 4)
            for(S32 i=0; i<n_proc; i++){
                n_ep_send_[i] = n_sp_send_[i] = n_ep_recv_[i] = n_sp_recv_[i] = 0;
                shift_image_domain.clearSize();
                if(dinfo.getBoundaryCondition() == BOUNDARY_CONDITION_OPEN){
                    shift_image_domain.push_back( F64vec(0.0) );
                }
                else{
                    CalcNumberAndShiftOfImageDomain
                        (shift_image_domain, dinfo.getPosRootDomain().getFullLength(),
                         outer_boundary_of_my_tree, dinfo.getPosDomain(i), periodic_axis);
                }
                S32 n_image = shift_image_domain.size();
                for(S32 j = 0; j < n_image; j++) {
                    if(my_rank == i && j == 0) continue;
                    const F64ort pos_target_domain = dinfo.getPosDomain(i).shift(shift_image_domain[j]);
                    const F64ort cell_box = pos_root_cell_;

                    if( !tc_loc_[0].isLeaf(n_leaf_limit_) ){
                        SearchSendParticleLongCutoff<TreeCell<Tmomloc>, Tepj, Tspj>
                            (tc_loc_,       adr_tc_tmp,
                             epj_sorted_,   epj_send_buf[ith],
                             spj_send_buf[ith],   cell_box,
                             pos_target_domain,   r_crit_sq,
                             r_cut_sq,            n_leaf_limit_,
                             - shift_image_domain[j]);
                    }
                    else{
                        const F64 dis_sq_cut = pos_target_domain.getDistanceMinSQ(cell_box);
                        if(dis_sq_cut <= r_cut_sq){
                            const F64vec pos_tmp = tc_loc_[0].mom_.getPos();
                            const F64 dis_sq_crit = pos_target_domain.getDistanceMinSQ(pos_tmp);
                            if(dis_sq_crit <= r_crit_sq*4.0){
                                const S32 n_tmp = tc_loc_[0].n_ptcl_;
                                S32 adr_ptcl_tmp = tc_loc_[0].adr_ptcl_;
                                for(S32 ip=0; ip<n_tmp; ip++){
                                    epj_send_buf[ith].push_back(epj_sorted_[adr_ptcl_tmp++]);
                                    const F64vec pos_new = epj_send_buf[ith].back().getPos() - shift_image_domain[j];
                                    epj_send_buf[ith].back().setPos(pos_new);
                                }
                            }
                            else{
                                spj_send_buf[ith].increaseSize();
                                spj_send_buf[ith].back().copyFromMoment(tc_loc_[0].mom_);
                                const F64vec pos_new = spj_send_buf[ith].back().getPos() - shift_image_domain[j];
                                spj_send_buf[ith].back().setPos(pos_new);
                            }
                        }
                    }

                }
                n_ep_send_[i] = epj_send_buf[ith].size() - n_epj_cum;
                n_sp_send_[i] = spj_send_buf[ith].size() - n_spj_cum;
                n_epj_cum = epj_send_buf[ith].size();
                n_spj_cum = spj_send_buf[ith].size();
                id_proc_send_[ith][n_proc_cum++] = i;
            } // end of for loop
#pragma omp single
            {
                n_ep_send_disp_[0] = n_sp_send_disp_[0] = 0;
                for(S32 ib=0; ib<n_proc; ib++){
                    n_ep_send_disp_[ib+1] = n_ep_send_disp_[ib] + n_ep_send_[ib];
                    n_sp_send_disp_[ib+1] = n_sp_send_disp_[ib] + n_sp_send_[ib];
                }
                epj_send_.resizeNoInitialize( n_ep_send_disp_[n_proc] );
                spj_send_.resizeNoInitialize( n_sp_send_disp_[n_proc] );
            }
            S32 n_ep_cnt = 0;
            S32 n_sp_cnt = 0;
            for(S32 ib=0; ib<n_proc_cum; ib++){
                const S32 id = id_proc_send_[ith][ib];
                S32 adr_ep_tmp = n_ep_send_disp_[id];
                const S32 n_ep_tmp = n_ep_send_[id];
                for(int ip=0; ip<n_ep_tmp; ip++){
                    epj_send_[adr_ep_tmp++] = epj_send_buf[ith][n_ep_cnt++];
                }
                S32 adr_sp_tmp = n_sp_send_disp_[id];
                const S32 n_sp_tmp = n_sp_send_[id];
                for(int ip=0; ip<n_sp_tmp; ip++){
                    spj_send_[adr_sp_tmp++] = spj_send_buf[ith][n_sp_cnt++];
                }
            }
        } // omp parallel scope
        Comm::allToAll(n_ep_send_, 1, n_ep_recv_); // TEST
        Comm::allToAll(n_sp_send_, 1, n_sp_recv_); // TEST
        n_ep_recv_disp_[0] = n_sp_recv_disp_[0] = 0;
        for(S32 i=0; i<n_proc; i++){
            n_ep_recv_disp_[i+1] = n_ep_recv_disp_[i] + n_ep_recv_[i];
            n_sp_recv_disp_[i+1] = n_sp_recv_disp_[i] + n_sp_recv_[i];
        }
        epj_recv_.resizeNoInitialize( n_ep_recv_disp_[n_proc] );
        spj_recv_.resizeNoInitialize( n_sp_recv_disp_[n_proc] );
        Comm::allToAllV(epj_send_.getPointer(), n_ep_send_, n_ep_send_disp_,
                        epj_recv_.getPointer(), n_ep_recv_, n_ep_recv_disp_); // TEST
        Comm::allToAllV(spj_send_.getPointer(), n_sp_send_, n_sp_send_disp_, 
                        spj_recv_.getPointer(), n_sp_recv_, n_sp_recv_disp_); // TEST
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    exchangeLocalEssentialTreeImpl(TagSearchShortScatter, const DomainInfo & dinfo){
        scatterEP(n_ep_send_,  n_ep_send_disp_,
                  n_ep_recv_,  n_ep_recv_disp_, 
                  epj_send_,   epj_recv_,
                  epj_sorted_, dinfo);	
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"epj_send_.size()="<<epj_send_.size()<<" spj_send_.size()="<<spj_send_.size()<<std::endl;
        std::cout<<"epj_recv_.size()="<<epj_recv_.size()<<" spj_recv_.size()="<<spj_recv_.size()<<std::endl;
#endif	    
    }


    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    exchangeLocalEssentialTreeImpl(TagSearchShortGather, const DomainInfo & dinfo){
        exchangeLocalEssentialTreeGatherImpl(typename HasRSearch<Tepj>::type(), dinfo);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    exchangeLocalEssentialTreeGatherImpl(TagRSearch, const DomainInfo & dinfo){
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        std::cout<<"EPJ has RSearch"<<std::endl;
#endif
        exchangeLocalEssentialTreeImpl(TagSearchShortSymmetry(), dinfo);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    exchangeLocalEssentialTreeGatherImpl(TagNoRSearch, const DomainInfo & dinfo){
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        std::cout<<"EPJ dose not have RSearch"<<std::endl;
#endif
        const S32 n_proc = Comm::getNumberOfProc();
        static ReallocatableArray<EPXROnly> ep_x_r_send;
        static ReallocatableArray<EPXROnly> ep_x_r_recv;
        static ReallocatableArray<Tepj> * epj_send_buf; // for 1st communication
        static ReallocatableArray<Tepj> epj_recv_buf; // for 2nd communication
        static S32 * n_ep_recv_1st;
        static S32 * n_ep_recv_disp_1st;
        static S32 * n_ep_recv_2nd;
        static S32 * n_ep_recv_disp_2nd;
        static S32 * id_proc_src;
        static S32 * id_proc_dest;
        static ReallocatableArray<S32> * id_ptcl_send;
#ifdef PARTICLE_SIMULATOR_MPI_PARALLEL
        const S32 my_rank = Comm::getRank();
        static MPI::Request * req_send;
        static MPI::Request * req_recv;
#endif
        static bool first = true;
        if(first){
            const S32 n_thread = Comm::getNumberOfThread();
            //ep_x_r_send.reserve(1000);
            //ep_x_r_recv.reserve(1000);
            ep_x_r_send.reserve(n_surface_for_comm_);
            ep_x_r_recv.reserve(n_surface_for_comm_);
            n_ep_recv_1st = new S32[n_proc];
            n_ep_recv_disp_1st = new S32[n_proc+1];
            n_ep_recv_2nd = new S32[n_proc];
            n_ep_recv_disp_2nd = new S32[n_proc+1];
            id_proc_src = new S32[n_proc];
            id_proc_dest = new S32[n_proc];
            id_ptcl_send = new ReallocatableArray<S32>[n_thread];
            epj_send_buf = new ReallocatableArray<Tepj>[n_thread];
            for(S32 i=0; i<n_thread; i++){
	        //id_ptcl_send[i].reserve(1000);
	        //epj_send_buf[i].reserve(1000);
	        id_ptcl_send[i].reserve(n_surface_for_comm_ * 2 / n_thread);
	        epj_send_buf[i].reserve(n_surface_for_comm_ * 2 / n_thread);
            }
            //epj_recv_buf.reserve(1000);
	    epj_recv_buf.reserve(n_surface_for_comm_);
#ifdef PARTICLE_SIMULATOR_MPI_PARALLEL
            req_send = new MPI::Request[n_proc];
            req_recv = new MPI::Request[n_proc];
#endif
            first = false;
        }
        S32 n_proc_src_1st = 0;
        S32 n_proc_dest_1st = 0;
        ////////////
        // 1st STEP
        // GATHER
        // NORMAL VERSION

        TexLET0_ = GetWtime();

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
#endif
        scatterEP(n_ep_send_,    n_ep_send_disp_,
                  n_ep_recv_1st,     n_ep_recv_disp_1st,
                  ep_x_r_send,       ep_x_r_recv,
                  epi_sorted_, dinfo);
        assert(ep_x_r_recv.size() == n_ep_recv_disp_1st[n_proc]);

        TexLET0_ = GetWtime() - TexLET0_;

        n_ep_send_1st_ = ep_x_r_send.size();
        n_ep_recv_1st_ = ep_x_r_recv.size();

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"ep_x_r_send.size()="<<ep_x_r_send.size()
                 <<" ep_x_r_recv.size()="<<ep_x_r_recv.size()<<std::endl;
#endif

        TexLET1_ = GetWtime();

        ////////////
        // 2nd step (send j particles)
        // GATHER
        // NORMAL VERSION
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
#endif
        n_proc_src_1st = n_proc_dest_1st = 0;
        for(S32 i=0; i<n_proc; i++){
            if(n_ep_send_[i] > 0){
                id_proc_dest[n_proc_dest_1st++] = i;
            }
            if(n_ep_recv_1st[i] > 0){
                id_proc_src[n_proc_src_1st++] = i;
            }
        }
        for(S32 i=0; i<n_proc; i++) n_ep_send_[i] = 0;
#pragma omp parallel
        {
            const S32 ith = Comm::getThreadNum();
            S32 n_proc_src_2nd = 0;
            //ReallocatableArray<F64ort> pos_image_box(5*5*5);
            ReallocatableArray<F64vec> shift_image_box(5*5*5);
            ReallocatableArray<S32> ip_disp(3*3*3);
            bool pa[DIMENSION];
            dinfo.getPeriodicAxis(pa);
            epj_send_buf[ith].clearSize();
            S32 n_ep_send_cum_old = 0;
//#pragma omp for schedule(dynamic, 4)
#pragma omp for schedule(dynamic, 1)
            for(S32 ib=0; ib<n_proc_src_1st; ib++){
                const S32 id_proc_tmp = id_proc_src[ib];
                const S32 i_head = n_ep_recv_disp_1st[id_proc_tmp];
                const S32 i_tail = n_ep_recv_disp_1st[id_proc_tmp+1];
                const F64ort pos_root_domain = dinfo.getPosRootDomain();
                const F64vec size_root_domain = pos_root_domain.getFullLength();
                S32vec id_image_new;
                S32vec id_image_old = -9999;
                //pos_image_box.clearSize();
                shift_image_box.clearSize();
                ip_disp.clearSize();
                if(dinfo.getBoundaryCondition() == BOUNDARY_CONDITION_OPEN){
                    shift_image_box.push_back( F64vec(0.0) );
                    ip_disp.push_back(i_head);
                    ip_disp.push_back(i_tail);
                    //pos_image_box.push_back( F64ort(9999.9, -9999.9) );
		    /*
                    for(S32 ip=i_head; ip<i_tail; ip++){
                        const F64vec pos_target = epj_recv_1st_buf[ip].getPos();
                        const F64 len_target = epj_recv_1st_buf[ip].getRSearch();
                        pos_image_box.back().merge(pos_target, len_target);
                    }
		    */
                }
                else{
                    for(S32 ip=i_head; ip<i_tail; ip++){
                        const F64vec pos_target = ep_x_r_recv[ip].getPos();
                        id_image_new = CalcIDOfImageDomain(pos_root_domain, pos_target, pa);
                        if(id_image_old != id_image_new){
                            ip_disp.push_back(ip);
#ifdef PARTICLE_SIMULATOR_TWO_DIMENSION				    
                            shift_image_box.push_back(F64vec(id_image_new.x*size_root_domain.x, id_image_new.y*size_root_domain.y));
#else
                            shift_image_box.push_back(F64vec(id_image_new.x*size_root_domain.x, id_image_new.y*size_root_domain.y, id_image_new.z*size_root_domain.z));
#endif
                            //pos_image_box.push_back( F64ort(9999.9, -9999.9) );
                            id_image_old = id_image_new;
                        }
                        //const F64 len_target = epj_recv_1st_buf[ip].getRSearch();
                        //pos_image_box.back().merge(pos_target, len_target);
                    }
                    ip_disp.push_back(i_tail);
                }
                const S32 n_image = shift_image_box.size();
                for(S32 ii=0; ii<n_image; ii++){
                    const F64vec shift = shift_image_box[ii];
                    const S32 adr_tc_tmp = tc_loc_[0].adr_tc_;
#if 1
#ifdef UNORDERED_SET 
                    std::unordered_set<S32> id;
                    typedef std::unordered_set<S32>::iterator SetItr;
#else
                    std::set<S32> id;
                    typedef std::set<S32>::iterator SetItr;
#endif
                    for(S32 ip=ip_disp[ii]; ip<ip_disp[ii+1]; ip++){
                        const F64vec pos_target = ep_x_r_recv[ip].getPos();
                        const F64 len_target = ep_x_r_recv[ip].getRSearch();
                        const F64ort particle_box(pos_target, len_target);
                        id_ptcl_send[ith].clearSize();
                        if( !tc_loc_[0].isLeaf(n_leaf_limit_) ){
                            MakeListUsingInnerBoundaryForGatherModeNormalMode
                                (tc_loc_.getPointer(),      adr_tc_tmp,
                                 epi_sorted_.getPointer(),  id_ptcl_send[ith],
                                 particle_box,              n_leaf_limit_);
                        }
                        else{
                            S32 n_ptcl_tmp = tc_loc_[0].n_ptcl_;
                            S32 adr_ptcl_tmp = tc_loc_[0].adr_ptcl_;
                            for(S32 ip=0; ip<n_ptcl_tmp; ip++, adr_ptcl_tmp++){
                                const F64vec pos_tmp = epi_sorted_[adr_ptcl_tmp].getPos();
                                if(particle_box.overlapped(pos_tmp)){
                                    id_ptcl_send[ith].push_back(adr_ptcl_tmp);
                                }
                            }
                        }
                        for(S32 i2=0; i2<id_ptcl_send[ith].size(); i2++) id.insert(id_ptcl_send[ith][i2]);
                    }
                    id_ptcl_send[ith].reserveAtLeast( id.size() ); // TODO: fix here! 
                    id_ptcl_send[ith].clearSize();
                    //for(std::set<S32>::iterator itr = id.begin(); itr != id.end(); ++itr){
                    //for(std::unordered_set<S32>::iterator itr = id.begin(); itr != id.end(); ++itr){
                    for(SetItr itr = id.begin(); itr != id.end(); ++itr){
                        id_ptcl_send[ith].pushBackNoCheck(*itr);
                    }
                    assert( id_ptcl_send[ith].size() == (S32)id.size());
#elif 0
                    // sort version
                    id_ptcl_send[ith].clearSize();
                    for(S32 ip=ip_disp[ii]; ip<ip_disp[ii+1]; ip++){
                        const F64vec pos_target = ep_x_r_recv[ip].getPos();
                        const F64 len_target = ep_x_r_recv[ip].getRSearch();
                        const F64ort particle_box(pos_target, len_target);

                        if( !tc_loc_[0].isLeaf(n_leaf_limit_) ){
                            MakeListUsingInnerBoundaryForGatherModeNormalMode
                                (tc_loc_.getPointer(),      adr_tc_tmp,
                                 epi_sorted_.getPointer(),  id_ptcl_send[ith],
                                 particle_box,              n_leaf_limit_);
                        }
                        else{
                            S32 n_ptcl_tmp = tc_loc_[0].n_ptcl_;
                            S32 adr_ptcl_tmp = tc_loc_[0].adr_ptcl_;
                            for(S32 ip=0; ip<n_ptcl_tmp; ip++, adr_ptcl_tmp++){
                                const F64vec pos_tmp = epi_sorted_[adr_ptcl_tmp].getPos();
                                if(particle_box.overlapped(pos_tmp)){
                                    id_ptcl_send[ith].push_back(adr_ptcl_tmp);
                                }
                            }
                        }
                    }
                    std::sort(id_ptcl_send[ith].getPointer(), id_ptcl_send[ith].getPointer(id_ptcl_send[ith].size()));
                    S32 * adr_end = std::unique(id_ptcl_send[ith].getPointer(), id_ptcl_send[ith].getPointer(id_ptcl_send[ith].size()));
                    id_ptcl_send[ith].resizeNoInitialize( adr_end - id_ptcl_send[ith].getPointer() );
#else
                    id_ptcl_send[ith].clearSize();
                    MakeListUsingInnerBoundaryForGatherModeNormalMode
                        (tc_loc_.getPointer(),     adr_tc_tmp,
                         epi_sorted_.getPointer(), id_ptcl_send[ith],
                         pos_image_box[ii],      n_leaf_limit_);
#endif
                    const S32 n_ep_per_image = id_ptcl_send[ith].size();
                    for(S32 jp=0; jp<n_ep_per_image; jp++){
                        const S32 id_j = id_ptcl_send[ith][jp];
                        const F64vec pos_j = epi_sorted_[id_j].getPos(); // NOTE: not have to shift, because epj_recv have shifted vale.
                        for(S32 ip=ip_disp[ii]; ip<ip_disp[ii+1]; ip++){
                            const F64vec pos_i = ep_x_r_recv[ip].getPos();
                            const F64 len_sq_i = ep_x_r_recv[ip].getRSearch() * ep_x_r_recv[ip].getRSearch();
                            if(pos_j.getDistanceSQ(pos_i) <= len_sq_i){
                                epj_send_buf[ith].push_back( epj_sorted_[id_j] );
                                epj_send_buf[ith].back().setPos(pos_j-shift);
                                break;
                            }
                        }
                    }
                }
                n_ep_send_[id_proc_tmp] = epj_send_buf[ith].size() - n_ep_send_cum_old;
                n_ep_send_cum_old = epj_send_buf[ith].size();
                if(n_ep_send_[id_proc_tmp] > 0){
                    id_proc_send_[ith][n_proc_src_2nd++] = id_proc_tmp;
                }
            }// end of pragma omp for
#pragma omp single
            {
                n_ep_send_disp_[0] = 0;
                for(S32 i=0; i<n_proc; i++){
                    n_ep_send_disp_[i+1] = n_ep_send_disp_[i] + n_ep_send_[i];
                }
                epj_send_.resizeNoInitialize( n_ep_send_disp_[n_proc] );
            }
            S32 n_ep_cnt = 0;
            for(S32 ib=0; ib<n_proc_src_2nd; ib++){
                const S32 id = id_proc_send_[ith][ib];
                S32 adr_ep_tmp = n_ep_send_disp_[id];
                const S32 n_ep_tmp = n_ep_send_[id];
                for(int ip=0; ip<n_ep_tmp; ip++){
                    epj_send_[adr_ep_tmp++] = epj_send_buf[ith][n_ep_cnt++];
                }
            }
        }// omp parallel scope

        TexLET1_ = GetWtime() - TexLET1_;
        wtime_walk_LET_2nd_ = TexLET1_;

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"epj_send_.size()="<<epj_send_.size()<<std::endl;
#endif

        TexLET2_ = GetWtime();

#ifdef PARTICLE_SIMULATOR_MPI_PARALLEL 
        /////////////////////
        // 3rd STEP
        // exchange # of particles
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
#endif
        for(S32 i=0; i<n_proc; i++){ n_ep_recv_2nd[i] = 0; }
        for(S32 i=0; i<n_proc_src_1st; i++){
            S32 id_proc = id_proc_src[i];
            S32 tag = (my_rank < id_proc) ? my_rank : id_proc;
            req_send[i] = MPI::COMM_WORLD.Isend(n_ep_send_+id_proc, 1, GetDataType<S32>(), id_proc, tag);
        }
        for(S32 i=0; i<n_proc_dest_1st; i++){
            S32 id_proc = id_proc_dest[i];
            S32 tag = (my_rank < id_proc) ? my_rank : id_proc;
            req_recv[i] = MPI::COMM_WORLD.Irecv( n_ep_recv_2nd+id_proc, 1, GetDataType<S32>(), id_proc, tag);
        }
        MPI::Request::Waitall(n_proc_src_1st, req_send);
        MPI::Request::Waitall(n_proc_dest_1st, req_recv);
#else
        n_ep_recv_2nd[0] = n_ep_send_[0];
#endif
        n_ep_recv_disp_2nd[0] = 0;
        for(S32 i=0; i<n_proc; i++){
            n_ep_recv_disp_2nd[i+1] = n_ep_recv_disp_2nd[i] + n_ep_recv_2nd[i];
        }
        for(S32 i=0; i<n_proc; i++){
            n_ep_recv_[i] = n_ep_recv_2nd[i];
            n_ep_recv_disp_[i] = n_ep_recv_disp_2nd[i];
        }
        n_ep_recv_disp_[n_proc] = n_ep_recv_disp_2nd[n_proc];

        TexLET2_ = GetWtime() - TexLET2_;

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"n_ep_recv_disp_[n_proc]="<<n_ep_recv_disp_[n_proc]<<std::endl;
#endif

        TexLET3_ = GetWtime();

        /////////////////////
        // 4th STEP
        // exchange EPJ
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
#endif
        epj_recv_buf.resizeNoInitialize( n_ep_recv_disp_2nd[n_proc] );
#ifdef PARTICLE_SIMULATOR_MPI_PARALLEL 
        S32 n_cnt_send = 0;
        S32 n_cnt_recv = 0;
        for(S32 i=0; i<n_proc_src_1st; i++){
            const S32 id_proc = id_proc_src[i];
            if(n_ep_send_[id_proc] > 0){
                S32 adr = n_ep_send_disp_[id_proc];
                S32 tag = (my_rank < id_proc) ? my_rank : id_proc;
                req_send[n_cnt_send++] = MPI::COMM_WORLD.Isend
                    (epj_send_.getPointer()+adr, n_ep_send_[id_proc],
                     GetDataType<Tepj>(), id_proc, tag);
            }
        }
        for(S32 i=0; i<n_proc_dest_1st; i++){
            const S32 id_proc = id_proc_dest[i];
            if(n_ep_recv_2nd[id_proc] > 0){
                S32 adr = n_ep_recv_disp_2nd[id_proc];
                S32 tag = (my_rank < id_proc) ? my_rank : id_proc;
                req_recv[n_cnt_recv++] = MPI::COMM_WORLD.Irecv
                    (epj_recv_buf.getPointer()+adr, n_ep_recv_2nd[id_proc],
                     GetDataType<Tepj>(), id_proc, tag);
            }
        }
        MPI::Request::Waitall(n_cnt_send, req_send);
        MPI::Request::Waitall(n_cnt_recv, req_recv);
#else
        S32 adr_send = n_ep_send_disp_[0];
        S32 adr_recv = n_ep_recv_disp_2nd[0];
        for(S32 i=0; i<n_ep_send_[0]; i++){
            epj_recv_buf[adr_recv++] = epj_send_[adr_send++];
        }
#endif

        n_ep_send_2nd_ = epj_send_.size();
        n_ep_recv_2nd_ = epj_recv_buf.size();

        TexLET3_ = GetWtime() - TexLET3_;


#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
#endif

        TexLET4_ = GetWtime();

        /////////////////////
        // 5th STEP
        // set epj_recv_
        epj_recv_.resizeNoInitialize( n_ep_recv_disp_2nd[n_proc] );
#pragma omp parallel for
        for(S32 i=0; i<n_proc; i++){
            S32 n_cnt = n_ep_recv_disp_[i];
            const S32 j_head_2nd = n_ep_recv_disp_2nd[i];
            const S32 j_tail_2nd = n_ep_recv_disp_2nd[i+1];
            for(S32 j=j_head_2nd; j<j_tail_2nd; j++){
                epj_recv_[n_cnt++] = epj_recv_buf[j];
            }
        }
        TexLET4_ = GetWtime() - TexLET4_;
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    exchangeLocalEssentialTreeImpl(TagSearchShortSymmetry, const DomainInfo & dinfo){
        const S32 n_proc = Comm::getNumberOfProc();
        static ReallocatableArray<Tepj> epj_recv_1st_buf;
        static ReallocatableArray<Tepj> epj_recv_2nd_buf;
        static ReallocatableArray<Tepj> * epj_send_buf; // for 1st communication
        static S32 * n_ep_recv_1st;
        static S32 * n_ep_recv_disp_1st;
        static S32 * n_ep_recv_2nd;
        static S32 * n_ep_recv_disp_2nd;
        static S32 * id_proc_src;
        static S32 * id_proc_dest;
        static ReallocatableArray<S32> * id_ptcl_send;
#ifdef PARTICLE_SIMULATOR_MPI_PARALLEL
        static MPI::Request * req_send;
        static MPI::Request * req_recv;
        const S32 my_rank = Comm::getRank();
#endif
        static bool first = true;
        if(first){
            const S32 n_thread = Comm::getNumberOfThread();
            //epj_recv_1st_buf.reserve(1000);
            //epj_recv_2nd_buf.reserve(1000);
            epj_recv_1st_buf.reserve(n_surface_for_comm_);
            epj_recv_2nd_buf.reserve(n_surface_for_comm_);
            n_ep_recv_1st = new S32[n_proc];
            n_ep_recv_disp_1st = new S32[n_proc+1];
            n_ep_recv_2nd = new S32[n_proc];
            n_ep_recv_disp_2nd = new S32[n_proc+1];
            id_proc_src = new S32[n_proc];
            id_proc_dest = new S32[n_proc];
            epj_send_buf = new ReallocatableArray<Tepj>[n_thread];
            id_ptcl_send = new ReallocatableArray<S32>[n_thread];
            for(S32 i=0; i<n_thread; i++){
                //id_ptcl_send[i].reserve(1000);
                //epj_send_buf[i].reserve(1000);
                id_ptcl_send[i].reserve(n_surface_for_comm_);
                epj_send_buf[i].reserve(n_surface_for_comm_);
            }
#ifdef PARTICLE_SIMULATOR_MPI_PARALLEL
            req_send = new MPI::Request[n_proc];
            req_recv = new MPI::Request[n_proc];
#endif
            first = false;
        }
        S32 n_proc_src_1st = 0;
        S32 n_proc_dest_1st = 0;

        TexLET0_ = GetWtime();

        ////////////
        // 1st STEP (send j particles)
        // SYMMETRY 
        // FAST VERSION
        scatterEP(n_ep_send_,  n_ep_send_disp_, 
                  n_ep_recv_1st,     n_ep_recv_disp_1st,
                  epj_send_,   epj_recv_1st_buf,
                  epj_sorted_, dinfo);
        assert(epj_recv_1st_buf.size() == n_ep_recv_disp_1st[n_proc]);

        TexLET0_ = GetWtime() - TexLET0_;

        n_ep_send_1st_ = epj_send_.size();
        n_ep_recv_1st_ = epj_recv_1st_buf.size();

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"epj_recv_1st_buf.size()="<<epj_recv_1st_buf.size()<<" epj_send_size()="<<epj_send_.size()<<std::endl;
#endif

        TexLET1_ = GetWtime();

        ////////////
        // 2nd step (send j particles)
        // SYMMETRY
        // FAST VERSION
        n_proc_src_1st = n_proc_dest_1st = 0;
        for(S32 i=0; i<n_proc; i++){
            if(n_ep_send_[i] > 0){
                id_proc_dest[n_proc_dest_1st++] = i;
            }
            if(n_ep_recv_1st[i] > 0){
                id_proc_src[n_proc_src_1st++] = i;
            }
        }
        for(S32 i=0; i<n_proc; i++) n_ep_send_[i] = 0;

#pragma omp parallel
        {
            const S32 ith = Comm::getThreadNum();
            S32 n_proc_src_2nd = 0;
            //ReallocatableArray<F64ort> pos_image_box(5*5*5);
            ReallocatableArray<F64vec> shift_image_box(5*5*5);
            ReallocatableArray<S32> ip_disp(3*3*3);
            bool pa[DIMENSION];
            dinfo.getPeriodicAxis(pa);
            epj_send_buf[ith].clearSize();
            S32 n_ep_send_cum_old = 0;
//#pragma omp for schedule(dynamic, 4)
#pragma omp for schedule(dynamic, 1)
            for(S32 ib=0; ib<n_proc_src_1st; ib++){
                const S32 id_proc_tmp = id_proc_src[ib];
                const S32 i_head = n_ep_recv_disp_1st[id_proc_tmp];
                const S32 i_tail = n_ep_recv_disp_1st[id_proc_tmp+1];
                const F64ort pos_root_domain = dinfo.getPosRootDomain();
                const F64vec size_root_domain = pos_root_domain.getFullLength();
                S32vec id_image_new;
                S32vec id_image_old = -9999;
                //pos_image_box.clearSize();
                shift_image_box.clearSize();
                ip_disp.clearSize();
                if(dinfo.getBoundaryCondition() == BOUNDARY_CONDITION_OPEN){
                    shift_image_box.push_back( F64vec(0.0) );
                    //pos_image_box.push_back( F64ort(9999.9, -9999.9) );
                    ip_disp.push_back(i_head);
                    ip_disp.push_back(i_tail);
/*
                    for(S32 ip=i_head; ip<i_tail; ip++){
                        const F64vec pos_target = epj_recv_1st_buf[ip].getPos();
                        const F64 len_target = epj_recv_1st_buf[ip].getRSearch();
                        pos_image_box.back().merge(pos_target, len_target);
                    }
*/
                }
                else{
                    for(S32 ip=i_head; ip<i_tail; ip++){
                        const F64vec pos_target = epj_recv_1st_buf[ip].getPos();
                        id_image_new = CalcIDOfImageDomain(pos_root_domain, pos_target, pa);
                        if(id_image_old != id_image_new){
                            ip_disp.push_back(ip);
#ifdef PARTICLE_SIMULATOR_TWO_DIMENSION
                            shift_image_box.push_back(F64vec(id_image_new.x*size_root_domain.x, id_image_new.y*size_root_domain.y));
#else
                            shift_image_box.push_back(F64vec(id_image_new.x*size_root_domain.x, id_image_new.y*size_root_domain.y, id_image_new.z*size_root_domain.z));
#endif
                            //pos_image_box.push_back( F64ort(9999.9, -9999.9) );
                            id_image_old = id_image_new;
                        }
                        //const F64 len_target = epj_recv_1st_buf[ip].getRSearch();
                        //pos_image_box.back().merge(pos_target, len_target);
                    }
                    ip_disp.push_back(i_tail);
                }

                const S32 n_image = shift_image_box.size();
                for(S32 ii=0; ii<n_image; ii++){
                    const F64vec shift = shift_image_box[ii];
                    const S32 adr_tc_tmp = tc_loc_[0].adr_tc_;
                    const F64ort pos_domain = dinfo.getPosDomain(id_proc_tmp).shift(shift);
#if 1
                    // use std::set
#ifdef UNORDERED_SET 
                    std::unordered_set<S32> id;
                    typedef std::unordered_set<S32>::iterator SetItr;
#else
                    std::set<S32> id;
                    typedef std::set<S32>::iterator SetItr;
#endif
                    for(S32 ip=ip_disp[ii]; ip<ip_disp[ii+1]; ip++){
                        const F64vec pos_target = epj_recv_1st_buf[ip].getPos();
                        const F64 len_target = epj_recv_1st_buf[ip].getRSearch();
                        const F64ort particle_box(pos_target, len_target);
                        id_ptcl_send[ith].clearSize();

                        if( !tc_loc_[0].isLeaf(n_leaf_limit_) ){
                            MakeListUsingInnerBoundaryForSymmetryExclusive
                                (tc_loc_.getPointer(),     adr_tc_tmp,
                                 epj_sorted_.getPointer(), id_ptcl_send[ith],
                                 particle_box,                   pos_domain,
                                 n_leaf_limit_);
                        }
                        else{
                            const S32 n_tmp = tc_loc_[0].n_ptcl_;
                            //id_ptcl_send[ith].reserve( id_ptcl_send[ith].size() + n_tmp );
                            id_ptcl_send[ith].reserveEmptyAreaAtLeast( n_tmp );
                            S32 adr_ptcl_tmp = tc_loc_[0].adr_ptcl_;
                            for(S32 ip=0; ip<n_tmp; ip++, adr_ptcl_tmp++){
                                // NOTE: need to be concistent with MakeListUsingOuterBoundary()
                                const F64vec pos_tmp = epj_sorted_[adr_ptcl_tmp].getPos();
                                const F64 len_sq = epj_sorted_[adr_ptcl_tmp].getRSearch() * epj_sorted_[adr_ptcl_tmp].getRSearch();
                                const F64 dis_sq0 = particle_box.getDistanceMinSQ(pos_tmp);
                                const F64 dis_sq1 = pos_domain.getDistanceMinSQ(pos_tmp);
                                if(dis_sq0 <= len_sq && dis_sq1 > len_sq){
                                    id_ptcl_send[ith].pushBackNoCheck(adr_ptcl_tmp);
                                }
                            }
                        }
                        for(S32 i2=0; i2<id_ptcl_send[ith].size(); i2++){
                            id.insert(id_ptcl_send[ith][i2]);
                        }
                    }
                    id_ptcl_send[ith].reserveAtLeast( id.size() );
                    id_ptcl_send[ith].clearSize();
                    //for(std::set<S32>::iterator itr = id.begin(); itr != id.end(); ++itr){
                    //for(std::unordered_set<S32>::iterator itr = id.begin(); itr != id.end(); ++itr){
                    for(SetItr itr = id.begin(); itr != id.end(); ++itr){
                        id_ptcl_send[ith].pushBackNoCheck(*itr);
                    }
                    assert( id_ptcl_send[ith].size() == (S32)id.size());
#elif 0
                    // sort version
                    id_ptcl_send[ith].clearSize();
                    for(S32 ip=ip_disp[ii]; ip<ip_disp[ii+1]; ip++){
                        const F64vec pos_target = epj_recv_1st_buf[ip].getPos();
                        const F64 len_target = epj_recv_1st_buf[ip].getRSearch();
                        const F64ort particle_box(pos_target, len_target);

                        if( !tc_loc_[0].isLeaf(n_leaf_limit_) ){
                            MakeListUsingInnerBoundaryForSymmetryExclusive
                                (tc_loc_.getPointer(),     adr_tc_tmp,
                                 epj_sorted_.getPointer(), id_ptcl_send[ith],
                                 particle_box,                   pos_domain,
                                 n_leaf_limit_);
                        }
                        else{
                            const S32 n_tmp = tc_loc_[0].n_ptcl_;
                            //id_ptcl_send[ith].reserve( id_ptcl_send[ith].size() + n_tmp );
                            id_ptcl_send[ith].reserveEmptyAreaAtLeast( n_tmp );
                            S32 adr_ptcl_tmp = tc_loc_[0].adr_ptcl_;
                            for(S32 ip=0; ip<n_tmp; ip++, adr_ptcl_tmp++){
                                // NOTE: need to be concistent with MakeListUsingOuterBoundary()
                                const F64vec pos_tmp = epj_sorted_[adr_ptcl_tmp].getPos();
                                const F64 len_sq = epj_sorted_[adr_ptcl_tmp].getRSearch() * epj_sorted_[adr_ptcl_tmp].getRSearch();
                                const F64 dis_sq0 = particle_box.getDistanceMinSQ(pos_tmp);
                                const F64 dis_sq1 = pos_domain.getDistanceMinSQ(pos_tmp);
                                if(dis_sq0 <= len_sq && dis_sq1 > len_sq){
                                    id_ptcl_send[ith].pushBackNoCheck(adr_ptcl_tmp);
                                }
                            }
                        }
                    }
                    std::sort(id_ptcl_send[ith].getPointer(), id_ptcl_send[ith].getPointer(id_ptcl_send[ith].size()));
                    S32 * adr_end = std::unique(id_ptcl_send[ith].getPointer(), id_ptcl_send[ith].getPointer(id_ptcl_send[ith].size()));
                    id_ptcl_send[ith].resizeNoInitialize( adr_end - id_ptcl_send[ith].getPointer() );
#else
                    id_ptcl_send[ith].clearSize();
                    MakeListUsingInnerBoundaryForSymmetryExclusive
                        (tc_loc_.getPointer(),     adr_tc_tmp,
                         epj_sorted_.getPointer(), id_ptcl_send[ith],
                         pos_image_box[ii],      pos_domain,
                         n_leaf_limit_);
#endif
                    const S32 n_ep_per_image = id_ptcl_send[ith].size();
                    for(S32 jp=0; jp<n_ep_per_image; jp++){
                        const S32 id_j = id_ptcl_send[ith][jp];
                        const F64vec pos_j = epj_sorted_[id_j].getPos(); // NOTE: not have to shift, because epj_recv have shifted vale.
                        for(S32 ip=ip_disp[ii]; ip<ip_disp[ii+1]; ip++){
                            const F64vec pos_i = epj_recv_1st_buf[ip].getPos();
                            const F64 len_sq_i = epj_recv_1st_buf[ip].getRSearch() * epj_recv_1st_buf[ip].getRSearch();
                            if(pos_j.getDistanceSQ(pos_i) <= len_sq_i){
                                epj_send_buf[ith].push_back(epj_sorted_[id_j]);
                                epj_send_buf[ith].back().setPos(pos_j-shift);
                                break;
                            }
                        }
                    }
                }
                n_ep_send_[id_proc_tmp] = epj_send_buf[ith].size() - n_ep_send_cum_old;
                n_ep_send_cum_old = epj_send_buf[ith].size();
                if(n_ep_send_[id_proc_tmp] > 0){
                    id_proc_send_[ith][n_proc_src_2nd++] = id_proc_tmp;
                }
            } // end of pragma omp for
#pragma omp single
            {
                n_ep_send_disp_[0] = 0;
                for(S32 i=0; i<n_proc; i++){
                    n_ep_send_disp_[i+1] = n_ep_send_disp_[i] + n_ep_send_[i];
                }
                epj_send_.resizeNoInitialize( n_ep_send_disp_[n_proc] );
            }
            S32 n_ep_cnt = 0;
            for(S32 ib=0; ib<n_proc_src_2nd; ib++){
                const S32 id = id_proc_send_[ith][ib];
                S32 adr_ep_tmp = n_ep_send_disp_[id];
                const S32 n_ep_tmp = n_ep_send_[id];
                for(int ip=0; ip<n_ep_tmp; ip++){
                    epj_send_[adr_ep_tmp++] = epj_send_buf[ith][n_ep_cnt++];
                }
            }
        }// omp parallel scope

        TexLET1_ = GetWtime() - TexLET1_;

        wtime_walk_LET_2nd_ = TexLET1_;

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"epj_send_.size()="<<epj_send_.size()<<std::endl;
#endif

        TexLET2_ = GetWtime();

        /////////////////////
        // 3rd STEP
        // exchange # of particles
        for(S32 i=0; i<n_proc; i++){
            // probably not needed
            n_ep_recv_2nd[i] = 0;
        }
#ifdef PARTICLE_SIMULATOR_MPI_PARALLEL
        for(S32 i=0; i<n_proc_src_1st; i++){
            S32 id_proc = id_proc_src[i];
            S32 tag = (my_rank < id_proc) ? my_rank : id_proc;
            req_send[i] = MPI::COMM_WORLD.Isend(n_ep_send_+id_proc, 1, GetDataType<S32>(), id_proc, tag);
        }
        for(S32 i=0; i<n_proc_dest_1st; i++){
            S32 id_proc = id_proc_dest[i];
            S32 tag = (my_rank < id_proc) ? my_rank : id_proc;
            req_recv[i] = MPI::COMM_WORLD.Irecv(n_ep_recv_2nd+id_proc, 1, GetDataType<S32>(), id_proc, tag);
        }
        MPI::Request::Waitall(n_proc_src_1st, req_send);
        MPI::Request::Waitall(n_proc_dest_1st, req_recv);
#else
        n_ep_recv_2nd[0] = n_ep_send_[0];
#endif
        n_ep_recv_disp_2nd[0] = 0;
        for(S32 i=0; i<n_proc; i++){
            n_ep_recv_disp_2nd[i+1] = n_ep_recv_disp_2nd[i] + n_ep_recv_2nd[i];
        }
        for(S32 i=0; i<n_proc; i++){
            n_ep_recv_[i] = n_ep_recv_1st[i] + n_ep_recv_2nd[i];
            n_ep_recv_disp_[i] = n_ep_recv_disp_1st[i] + n_ep_recv_disp_2nd[i];
        }
        n_ep_recv_disp_[n_proc] = n_ep_recv_disp_1st[n_proc] + n_ep_recv_disp_2nd[n_proc];

        TexLET2_ = GetWtime() - TexLET2_;
	

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"n_ep_recv_disp_[n_proc]="<<n_ep_recv_disp_[n_proc]<<std::endl;
#endif

        TexLET3_ = GetWtime();

        /////////////////////
        // 4th STEP
        // exchange EPJ
        epj_recv_2nd_buf.resizeNoInitialize( n_ep_recv_disp_2nd[n_proc] );
#ifdef PARTICLE_SIMULATOR_MPI_PARALLEL
        S32 n_cnt_send = 0;
        S32 n_cnt_recv = 0;
        for(S32 i=0; i<n_proc_src_1st; i++){
            const S32 id_proc = id_proc_src[i];
            if(n_ep_send_[id_proc] > 0){
                S32 adr = n_ep_send_disp_[id_proc];
                S32 tag = (my_rank < id_proc) ? my_rank : id_proc;
                req_send[n_cnt_send++] = MPI::COMM_WORLD.Isend
                    (epj_send_.getPointer()+adr, n_ep_send_[id_proc],
                     GetDataType<Tepj>(), id_proc, tag);
            }
        }
        for(S32 i=0; i<n_proc_dest_1st; i++){
            const S32 id_proc = id_proc_dest[i];
            if(n_ep_recv_2nd[id_proc] > 0){
                S32 adr = n_ep_recv_disp_2nd[id_proc];
                S32 tag = (my_rank < id_proc) ? my_rank : id_proc;
                req_recv[n_cnt_recv++] = MPI::COMM_WORLD.Irecv
                    (epj_recv_2nd_buf.getPointer()+adr, n_ep_recv_2nd[id_proc],
                     GetDataType<Tepj>(), id_proc, tag);
            }
        }
        MPI::Request::Waitall(n_cnt_send, req_send);
        MPI::Request::Waitall(n_cnt_recv, req_recv);
#else
        S32 adr_send = n_ep_send_disp_[0];
        S32 adr_recv = n_ep_recv_disp_2nd[0];
        for(S32 i=0; i<n_ep_send_[0]; i++){
            epj_recv_2nd_buf[adr_recv++] = epj_send_[adr_send++];
        }
#endif

        TexLET3_ = GetWtime() - TexLET3_;

        n_ep_send_2nd_ = epj_send_.size();
        n_ep_recv_2nd_ = epj_recv_2nd_buf.size();


#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"epj_recv_2nd_buf.size()="<<epj_recv_2nd_buf.size()<<std::endl;
#endif

        TexLET4_ = GetWtime();

        /////////////////////
        // 5th STEP
        // set epj_recv_
        epj_recv_.resizeNoInitialize(n_ep_recv_disp_1st[n_proc]+n_ep_recv_disp_2nd[n_proc]);
#pragma omp parallel for
        for(S32 i=0; i<n_proc; i++){
            S32 n_cnt = n_ep_recv_disp_[i];
            const S32 j_head_1st = n_ep_recv_disp_1st[i];
            const S32 j_tail_1st = n_ep_recv_disp_1st[i+1];
            for(S32 j=j_head_1st; j<j_tail_1st; j++){
                epj_recv_[n_cnt++] = epj_recv_1st_buf[j];
            }
            const S32 j_head_2nd = n_ep_recv_disp_2nd[i];
            const S32 j_tail_2nd = n_ep_recv_disp_2nd[i+1];
            for(S32 j=j_head_2nd; j<j_tail_2nd; j++){
                epj_recv_[n_cnt++] = epj_recv_2nd_buf[j];
            }
        }

        TexLET4_ = GetWtime() - TexLET4_;

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"n_ep_recv_disp_1st[n_proc]="<<n_ep_recv_disp_1st[n_proc]<<" n_ep_recv_disp_2nd[n_proc]"<<std::endl;
        std::cout<<"epj_recv_.size()="<<epj_recv_.size()<<std::endl;
#endif
    }
    
    //////////////////////////////
    /// SET LET TO GLOBAL TREE ///
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    setLocalEssentialTreeToGlobalTree(){
        setLocalEssentialTreeToGlobalTreeImpl(typename TSM::force_type());
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"n_loc_tot_="<<n_loc_tot_<<" n_glb_tot_="<<n_glb_tot_<<std::endl;
        std::cout<<"epj_org_.size()="<<epj_org_.size()<<" spj_org_.size()="<<spj_org_.size()<<" tp_glb_.size()="<<tp_glb_.size()<<std::endl;
#endif
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    setLocalEssentialTreeToGlobalTreeImpl(TagForceShort){
        const S32 n_proc = Comm::getNumberOfProc();
        const S32 offset = this->n_loc_tot_;
        const S32 n_ep_add = this->n_ep_recv_disp_[n_proc];
        this->n_glb_tot_ = this->n_loc_tot_ + n_ep_add;
        this->tp_glb_.resizeNoInitialize( this->n_glb_tot_ );
        this->epj_org_.resizeNoInitialize( this->n_glb_tot_ );
#pragma omp parallel
        {
#pragma omp for
            for(S32 i=0; i<offset; i++){
                this->tp_glb_[i] = this->tp_loc_[i]; // NOTE: need to keep tp_loc_[]?
            }
#pragma omp for
            for(S32 i=0; i<n_ep_add; i++){
                this->epj_org_[offset+i] = this->epj_recv_[i];
                this->tp_glb_[offset+i].setFromEP(this->epj_recv_[i], offset+i);
            }
        }
// debug
/*
        for(S32 i=0; i<n_ep_add; i++){
            std::cout<<"epj_org_[offset+i].pos="<<epj_org_[offset+i].pos<<std::endl;
            std::cout<<"epj_recv_[i].pos="<<epj_recv_[i].pos<<std::endl;
        }
*/
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    setLocalEssentialTreeToGlobalTreeImpl(TagForceLong){
        const S32 n_proc = Comm::getNumberOfProc();
        const S32 offset = this->n_loc_tot_;
        const S32 n_ep_add = this->n_ep_recv_disp_[n_proc];
        const S32 n_sp_add = this->n_sp_recv_disp_[n_proc];
        const S32 offset2 = this->n_loc_tot_ + n_ep_add;
        this->n_glb_tot_ = this->n_loc_tot_ + n_ep_add + n_sp_add;
        this->tp_glb_.resizeNoInitialize( this->n_glb_tot_ );
        this->epj_org_.resizeNoInitialize( offset2 );
        this->spj_org_.resizeNoInitialize( this->n_glb_tot_ );
#pragma omp parallel
        {
#pragma omp for
            for(S32 i=0; i<offset; i++){
                this->tp_glb_[i] = this->tp_loc_[i]; // NOTE: need to keep tp_loc_[]?
            }
#pragma omp for
            for(S32 i=0; i<n_ep_add; i++){
                this->epj_org_[offset+i] = this->epj_recv_[i];
                this->tp_glb_[offset+i].setFromEP(this->epj_recv_[i], offset+i);
            }
#pragma omp for
            for(S32 i=0; i<n_sp_add; i++){
                this->spj_org_[offset2+i] = this->spj_recv_[i];
                this->tp_glb_[offset2+i].setFromSP(this->spj_recv_[i], offset2+i);
            }
        }
    }

    ///////////////////////////////
    /// morton sort global tree ///
    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    mortonSortGlobalTreeOnly(){
        tp_glb_.resizeNoInitialize(n_glb_tot_);
        tp_buf_.resizeNoInitialize(n_glb_tot_);
        rs_.lsdSort(tp_glb_.getPointer(), tp_buf_.getPointer(), 0, n_glb_tot_-1);
        epj_sorted_.resizeNoInitialize( n_glb_tot_ );
        if( typeid(TSM) == typeid(SEARCH_MODE_LONG)
            || typeid(TSM) == typeid(SEARCH_MODE_LONG_CUTOFF) ){
            spj_sorted_.resizeNoInitialize( spj_org_.size() );
#pragma omp parallel for
            for(S32 i=0; i<n_glb_tot_; i++){
                const U32 adr = tp_glb_[i].adr_ptcl_;
                if( GetMSB(adr) == 0){
                    epj_sorted_[i] = epj_org_[adr];
                }
                else{
                    spj_sorted_[i] = spj_org_[ClearMSB(adr)];
                }
            }
        }
        else{
#pragma omp parallel for
            for(S32 i=0; i<n_glb_tot_; i++){
                const U32 adr = tp_glb_[i].adr_ptcl_;
                epj_sorted_[i] = epj_org_[adr];
            }
        }

#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
            PARTICLE_SIMULATOR_PRINT_LINE_INFO();
            std::cout<<"tp_glb_.size()="<<tp_glb_.size()<<" tp_buf_.size()="<<tp_buf_.size()<<std::endl;
            std::cout<<"epj_sorted_.size()="<<epj_sorted_.size()<<" spj_sorted_.size()="<<spj_sorted_.size()<<std::endl;
#endif
    }

    /////////////////////////////
    /// link cell global tree ///
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    linkCellGlobalTreeOnly(){
        LinkCell(tc_glb_, adr_tc_level_partition_,
                 tp_glb_.getPointer(), lev_max_, n_glb_tot_, n_leaf_limit_);
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"tc_glb_.size()="<<tc_glb_.size()<<std::endl;
        std::cout<<"lev_max_="<<lev_max_<<std::endl;
#endif
    }

    
    //////////////////////////
    // CALC MOMENT GLOBAL TREE
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentGlobalTreeOnly(){
        calcMomentGlobalTreeOnlyImpl(typename TSM::search_type());
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
#endif
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentGlobalTreeOnlyImpl(TagSearchLong){
	CalcMomentLongGlobalTree
	    (adr_tc_level_partition_,  tc_glb_.getPointer(),
	     tp_glb_.getPointer(),     epj_sorted_.getPointer(),
	     spj_sorted_.getPointer(), lev_max_,
	     n_leaf_limit_);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentGlobalTreeOnlyImpl(TagSearchLongCutoff){
	CalcMomentLongCutoffGlobalTree
	    (adr_tc_level_partition_,  tc_glb_.getPointer(),
	     tp_glb_.getPointer(),     epj_sorted_.getPointer(),
	     spj_sorted_.getPointer(), lev_max_,
	     n_leaf_limit_);
    }
    
    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentGlobalTreeOnlyImpl(TagSearchShortScatter){
	CalcMoment(adr_tc_level_partition_, tc_glb_.getPointer(),
		   epj_sorted_.getPointer(), lev_max_, n_leaf_limit_);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentGlobalTreeOnlyImpl(TagSearchShortGather){
	CalcMoment(adr_tc_level_partition_, tc_glb_.getPointer(),
		   epj_sorted_.getPointer(), lev_max_, n_leaf_limit_);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcMomentGlobalTreeOnlyImpl(TagSearchShortSymmetry){
	CalcMoment(adr_tc_level_partition_, tc_glb_.getPointer(),
		   epj_sorted_.getPointer(), lev_max_, n_leaf_limit_);
    }

    ////////////////////
    /// MAKE IPGROUP ///
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    makeIPGroup(){
        ipg_.clearSize();
        makeIPGroupImpl(typename TSM::force_type());
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"ipg_.size()="<<ipg_.size()<<std::endl;
#endif
    }
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    makeIPGroupImpl(TagForceLong){
        MakeIPGroupLong(ipg_, tc_loc_, epi_sorted_, 0, n_group_limit_);
    }
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    makeIPGroupImpl(TagForceShort){
        MakeIPGroupShort(ipg_, tc_loc_, epi_sorted_, 0, n_group_limit_);
    }

    /////////////////////////////
    /// MAKE INTERACTION LIST ///
    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    makeInteractionList(const S32 adr_ipg){
        makeInteractionListImpl(typename TSM::search_type(), adr_ipg);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    makeInteractionListImpl(TagSearchLong, const S32 adr_ipg){
        const S32 ith = Comm::getThreadNum();
        const F64ort pos_target_box = (ipg_[adr_ipg]).vertex_;
        const F64 r_crit_sq = (length_ * length_) / (theta_ * theta_) * 0.25;
        epj_for_force_[ith].clearSize();
        spj_for_force_[ith].clearSize();
        if( !tc_glb_[0].isLeaf(n_leaf_limit_) ){
            MakeInteractionListLongEPSP
                (tc_glb_, tc_glb_[0].adr_tc_, 
                 tp_glb_, epj_sorted_, 
                 epj_for_force_[ith],
                 spj_sorted_, spj_for_force_[ith],
                 pos_target_box, r_crit_sq, n_leaf_limit_);
        }
        else{
            const F64vec pos_tmp = tc_glb_[0].mom_.getPos();
            if( pos_target_box.getDistanceMinSQ(pos_tmp) <= r_crit_sq*4.0 ){
                const S32 n_tmp = tc_glb_[0].n_ptcl_;
                S32 adr_ptcl_tmp = tc_glb_[0].adr_ptcl_;
                //epj_for_force_[ith].reserve( epj_for_force_[ith].size() + n_tmp );
                //spj_for_force_[ith].reserve( spj_for_force_[ith].size() + n_tmp );
                epj_for_force_[ith].reserveEmptyAreaAtLeast( n_tmp );
                spj_for_force_[ith].reserveEmptyAreaAtLeast( n_tmp );
                for(S32 ip=0; ip<n_tmp; ip++){
                    if( GetMSB(tp_glb_[adr_ptcl_tmp].adr_ptcl_) == 0){
                        epj_for_force_[ith].pushBackNoCheck(epj_sorted_[adr_ptcl_tmp++]);
                    }
                    else{
                        spj_for_force_[ith].pushBackNoCheck(spj_sorted_[adr_ptcl_tmp++]);
                    }
                }
            }
        }

    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    makeInteractionListImpl(TagSearchLongCutoff, const S32 adr_ipg){
        const S32 ith = Comm::getThreadNum();
        const F64ort pos_target_box = (ipg_[adr_ipg]).vertex_;
        const F64 r_crit_sq = (length_ * length_) / (theta_ * theta_) * 0.25;
        const F64 r_cut_sq  = epj_sorted_[0].getRSearch() * epj_sorted_[0].getRSearch();
        epj_for_force_[ith].clearSize();
        spj_for_force_[ith].clearSize();
        const F64ort cell_box = pos_root_cell_;
        if( !tc_glb_[0].isLeaf(n_leaf_limit_) ){
            MakeInteractionListLongCutoffEPSP
                (tc_glb_, tc_glb_[0].adr_tc_, tp_glb_, 
                 epj_sorted_, epj_for_force_[ith],
                 spj_sorted_, spj_for_force_[ith],
                 cell_box,
                 pos_target_box, r_crit_sq, r_cut_sq, n_leaf_limit_); 
        }
        else{
            if(pos_target_box.getDistanceMinSQ(cell_box) <= r_cut_sq){
                const F64vec pos_tmp = tc_glb_[0].mom_.getPos();
                if( pos_target_box.getDistanceMinSQ(pos_tmp) <= r_crit_sq * 4.0){
                    const S32 n_tmp = tc_glb_[0].n_ptcl_;
                    S32 adr_ptcl_tmp = tc_glb_[0].adr_ptcl_;
                    //epj_for_force_[ith].reserve( epj_for_force_[ith].size() + n_tmp );
                    //spj_for_force_[ith].reserve( spj_for_force_[ith].size() + n_tmp );
                    epj_for_force_[ith].reserveEmptyAreaAtLeast( n_tmp );
                    spj_for_force_[ith].reserveEmptyAreaAtLeast( n_tmp );
                    for(S32 ip=0; ip<n_tmp; ip++){
                        if( GetMSB(tp_glb_[adr_ptcl_tmp].adr_ptcl_) == 0){
                            epj_for_force_[ith].pushBackNoCheck(epj_sorted_[adr_ptcl_tmp++]);
                        }
                        else{
                            spj_for_force_[ith].pushBackNoCheck(spj_sorted_[adr_ptcl_tmp++]);
                        }
                    }
                }
                else{
                    spj_for_force_[ith].increaseSize();
                    spj_for_force_[ith].back().copyFromMoment(tc_glb_[0].mom_);
                }
            }
        }
    }


    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    makeInteractionListImpl(TagSearchShortScatter, const S32 adr_ipg){
        const S32 ith = Comm::getThreadNum();
        const F64ort pos_target_box = (ipg_[adr_ipg]).vertex_;
        epj_for_force_[ith].clearSize();
        if( !tc_glb_[0].isLeaf(n_leaf_limit_) ){
            MakeListUsingOuterBoundary
                (tc_glb_.getPointer(),     tc_glb_[0].adr_tc_,
                 epj_sorted_.getPointer(), epj_for_force_[ith], 
                 pos_target_box,   n_leaf_limit_);
        }
        else{
            if( pos_target_box.overlapped( tc_glb_[0].mom_.getVertexOut()) ){
                S32 adr_ptcl_tmp = tc_glb_[0].adr_ptcl_;
                const S32 n_tmp = tc_glb_[0].n_ptcl_;
                //epj_for_force_[ith].reserve( epj_for_force_[ith].size() + n_tmp );
                epj_for_force_[ith].reserveEmptyAreaAtLeast( n_tmp );
                for(S32 ip=0; ip<n_tmp; ip++, adr_ptcl_tmp++){
                    const F64vec pos_tmp = epj_sorted_[adr_ptcl_tmp].getPos();
                    const F64 size_tmp = epj_sorted_[adr_ptcl_tmp].getRSearch();
                    const F64 dis_sq_tmp = pos_target_box.getDistanceMinSQ(pos_tmp);
                    if(dis_sq_tmp > size_tmp*size_tmp) continue;
                    //epj_for_force_[ith].increaseSize();
                    //epj_for_force_[ith].back() = epj_sorted_[adr_ptcl_tmp];
                    //epj_for_force_[ith].push_back(epj_sorted_[adr_ptcl_tmp]);
                    epj_for_force_[ith].pushBackNoCheck(epj_sorted_[adr_ptcl_tmp]);
                    const F64vec pos_new = epj_for_force_[ith].back().getPos();
                    epj_for_force_[ith].back().setPos(pos_new);
                }
            }
        }
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    makeInteractionListImpl(TagSearchShortGather, const S32 adr_ipg){
        const S32 ith = Comm::getThreadNum();
        const F64ort pos_target_box = (ipg_[adr_ipg]).vertex_;
        epj_for_force_[ith].clearSize();
        if( !tc_glb_[0].isLeaf(n_leaf_limit_) ){
            MakeListUsingInnerBoundary
                (tc_glb_.getPointer(),     tc_glb_[0].adr_tc_,
                 epj_sorted_.getPointer(), epj_for_force_[ith], 
                 pos_target_box,                 n_leaf_limit_);
        }
        else{
            if( pos_target_box.overlapped( tc_glb_[0].mom_.getVertexIn()) ){
                S32 adr_ptcl_tmp = tc_glb_[0].adr_ptcl_;
                const S32 n_tmp = tc_glb_[0].n_ptcl_;
                //epj_for_force_[ith].reserve( epj_for_force_[ith].size() + n_tmp );
                epj_for_force_[ith].reserveEmptyAreaAtLeast( n_tmp );
                for(S32 ip=0; ip<n_tmp; ip++, adr_ptcl_tmp++){
                    const F64vec pos_tmp = epj_sorted_[adr_ptcl_tmp].getPos();
                    if( pos_target_box.overlapped( pos_tmp) ){
                        //epj_for_force_[ith].push_back(epj_sorted_[adr_ptcl_tmp]);
                        epj_for_force_[ith].pushBackNoCheck(epj_sorted_[adr_ptcl_tmp]);
                        const F64vec pos_new = epj_for_force_[ith].back().getPos();
                        epj_for_force_[ith].back().setPos(pos_new);
                    }
                }
            }
        }

    }
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    makeInteractionListImpl(TagSearchShortSymmetry, const S32 adr_ipg){
        const S32 ith = Comm::getThreadNum();
#if 0
        const F64ort pos_target_box = (ipg_[adr_ipg]).vertex_;
        epj_for_force_[ith].clearSize();
        if(tc_glb_[0].n_ptcl_ > 0){
            MakeListUsingOuterBoundary
                (tc_glb_.getPointer(),     tc_glb_[0].adr_tc_,
                 epj_sorted_.getPointer(), epj_for_force_[ith], 
                 pos_target_box,   n_leaf_limit_);
        }
#else
// modified by M.I.
        const F64ort pos_target_box_out = (ipg_[adr_ipg]).vertex_;
        const F64ort pos_target_box_in = (ipg_[adr_ipg]).vertex_in;
        epj_for_force_[ith].clearSize();
        if( !tc_glb_[0].isLeaf(n_leaf_limit_) ){
            MakeListUsingOuterBoundaryAndInnerBoundary
                (tc_glb_.getPointer(),     tc_glb_[0].adr_tc_,
                 epj_sorted_.getPointer(), epj_for_force_[ith], 
                 pos_target_box_out, pos_target_box_in, n_leaf_limit_);
        }
        else{
            if( pos_target_box_out.overlapped(tc_glb_[0].mom_.getVertexIn()) 
                || pos_target_box_in.overlapped(tc_glb_[9].mom_.getVertexOut()) ){
                S32 adr_ptcl_tmp = tc_glb_[0].adr_ptcl_;
                const S32 n_tmp = tc_glb_[0].n_ptcl_;
                epj_for_force_[ith].reserveAtLeast( n_tmp );
                for(S32 ip=0; ip<n_tmp; ip++, adr_ptcl_tmp++){
                    const F64vec pos_tmp = epj_sorted_[adr_ptcl_tmp].getPos();
                    const F64 size_tmp = epj_sorted_[adr_ptcl_tmp].getRSearch();
                    const F64 dis_sq_tmp = pos_target_box_in.getDistanceMinSQ(pos_tmp);
                    if( pos_target_box_out.notOverlapped(pos_tmp) && dis_sq_tmp > size_tmp*size_tmp) continue;
                    //ep_list.increaseSize();
                    //ep_list.back() = ep_first[adr_ptcl_tmp];
                    //epj_for_force_[ith].push_back( epj_sorted_[adr_ptcl_tmp] );
                    epj_for_force_[ith].pushBackNoCheck( epj_sorted_[adr_ptcl_tmp] );
                    const F64vec pos_new = epj_for_force_[ith].back().getPos();
                    epj_for_force_[ith].back().setPos(pos_new);
                }
            }

        }

#endif
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    template<class Tep2>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcCenterAndLengthOfRootCellOpenNoMargenImpl(const Tep2 ep[]){
        const F64ort min_box  = GetMinBox(ep, n_loc_tot_);
        //std::cerr<<"n_loc_tot_="<<n_loc_tot_<<std::endl;
        //std::cerr<<"min_box="<<min_box<<std::endl;
        center_ = min_box.getCenter();
        const F64 tmp0 = (min_box.high_ - center_).getMax();
        const F64 tmp1 = (center_ - min_box.low_).getMax();
        length_ = std::max(tmp0, tmp1) * 2.0 * 1.001;
        pos_root_cell_.low_ = center_ - F64vec(length_*0.5);
        pos_root_cell_.high_ = center_ + F64vec(length_*0.5);
    }
    
    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    template<class Tep2>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcCenterAndLengthOfRootCellOpenWithMargenImpl(const Tep2 ep[]){
	const F64ort min_box  = GetMinBoxWithMargen(ep, n_loc_tot_);
	center_ = min_box.getCenter();
	const F64 tmp0 = (min_box.high_ - center_).getMax();
	const F64 tmp1 = (center_ - min_box.low_).getMax();
	length_ = std::max(tmp0, tmp1) * 2.0 * 1.001;
	pos_root_cell_.low_ = center_ - F64vec(length_*0.5);
	pos_root_cell_.high_ = center_ + F64vec(length_*0.5);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    template<class Tep2>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcCenterAndLengthOfRootCellPeriodicImpl2(const Tep2 ep[]){
        F64 rsearch_max_loc = std::numeric_limits<F64>::max() * -0.25;
        F64ort box_loc;
        box_loc.initNegativeVolume();
#pragma omp parallel
        {
            F64 rsearch_max_loc_tmp = std::numeric_limits<F64>::max() * -0.25;
            F64ort box_loc_tmp;
            box_loc_tmp.init();
#pragma omp for nowait
            for(S32 ip=0; ip<n_loc_tot_; ip++){
                rsearch_max_loc_tmp = (rsearch_max_loc_tmp > ep[ip].getRSearch()) ? rsearch_max_loc_tmp : ep[ip].getRSearch();
                box_loc_tmp.merge(ep[ip].getPos());
            }
#pragma omp critical
            {
                rsearch_max_loc = rsearch_max_loc > rsearch_max_loc_tmp ? rsearch_max_loc : rsearch_max_loc_tmp;
                box_loc.merge(box_loc_tmp);
            }
        }
        F64 rsearch_max_glb = Comm::getMaxValue(rsearch_max_loc);
        F64vec xlow_loc = box_loc.low_;
        F64vec xhigh_loc = box_loc.high_;
        F64vec xlow_glb = Comm::getMinValue(xlow_loc);
        F64vec xhigh_glb = Comm::getMaxValue(xhigh_loc);

        xlow_glb -= F64vec(rsearch_max_glb);
        xhigh_glb += F64vec(rsearch_max_glb);
        center_ = (xhigh_glb + xlow_glb) * 0.5;
        F64 tmp0 = (xlow_glb - center_).applyEach(Abs<F64>()).getMax();
        F64 tmp1 = (xhigh_glb - center_).applyEach(Abs<F64>()).getMax();
        length_ = std::max(tmp0, tmp1) * 2.0 * 2.0;
        pos_root_cell_.low_ = center_ - F64vec(length_*0.5);
        pos_root_cell_.high_ = center_ + F64vec(length_*0.5);
    }

    /////////////////
    // CALC FORCE ///
    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    template<class Tfunc_ep_ep>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcForceOnly(Tfunc_ep_ep pfunc_ep_ep,
                  const S32 adr_ipg,
                  const bool clear){
        const S32 offset = ipg_[adr_ipg].adr_ptcl_;
        const S32 n_epi = ipg_[adr_ipg].n_ptcl_;
        const S32 ith = Comm::getThreadNum();
        const S32 n_epj = epj_for_force_[ith].size();
        const S32 n_tail = offset + n_epi;
        //force_sorted_.resizeNoInitialize(n_tail);
        if(clear){
            for(S32 i=offset; i<n_tail; i++) force_sorted_[i].clear();
        }
        pfunc_ep_ep(epi_sorted_.getPointer(offset),     n_epi,
                    epj_for_force_[ith].getPointer(),   n_epj,
                    force_sorted_.getPointer(offset));
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    template<class Tfunc_ep_ep, class Tfunc_ep_sp>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcForceOnly(Tfunc_ep_ep pfunc_ep_ep,
                  Tfunc_ep_sp pfunc_ep_sp,
                  const S32 adr_ipg,
                  const bool clear){
        const S32 offset = ipg_[adr_ipg].adr_ptcl_;
        const S32 n_epi = ipg_[adr_ipg].n_ptcl_;
        const S32 ith = Comm::getThreadNum();
        const S32 n_epj = epj_for_force_[ith].size();
        const S32 n_spj = spj_for_force_[ith].size();
        const S32 n_tail = offset + n_epi;
        //force_sorted_.resizeNoInitialize(n_tail);
        if(clear){
            for(S32 i=offset; i<n_tail; i++) force_sorted_[i].clear();
        }
	
        pfunc_ep_ep(epi_sorted_.getPointer(offset),     n_epi,
                    epj_for_force_[ith].getPointer(),   n_epj,
                    force_sorted_.getPointer(offset));
        pfunc_ep_sp(epi_sorted_.getPointer(offset),     n_epi,
                    spj_for_force_[ith].getPointer(),   n_spj,
                    force_sorted_.getPointer(offset));
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    copyForceOriginalOrder(){
        force_org_.resizeNoInitialize(n_loc_tot_);
#pragma omp parallel for
        for(S32 i=0; i<n_loc_tot_; i++){
            const S32 adr = ClearMSB(tp_loc_[i].adr_ptcl_);
            force_org_[adr] = force_sorted_[i];
        }
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    template<class Tfunc_ep_ep>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcForce(Tfunc_ep_ep pfunc_ep_ep,
              const bool clear){
        force_sorted_.resizeNoInitialize(n_loc_tot_);
        force_org_.resizeNoInitialize(n_loc_tot_);
        const S32 n_ipg = ipg_.size();
        S32 ni_tmp = 0;
        S32 nj_tmp = 0;
        S64 n_interaction_tmp = 0;
        if(n_ipg > 0){
#pragma omp parallel for schedule(dynamic, 4) reduction(+ : ni_tmp, nj_tmp, n_interaction_tmp) 
            for(S32 i=0; i<n_ipg; i++){
                makeInteractionList(i);
                ni_tmp += ipg_[i].n_ptcl_;
                nj_tmp += epj_for_force_[Comm::getThreadNum()].size();
                n_interaction_tmp += ipg_[i].n_ptcl_ * epj_for_force_[Comm::getThreadNum()].size();
                calcForceOnly( pfunc_ep_ep, i, clear);
            }
            ni_ave_ = ni_tmp / n_ipg;
            nj_ave_ = nj_tmp / n_ipg;
            n_interaction_ = n_interaction_tmp;
        }
        else{
            ni_ave_ = nj_ave_ = n_interaction_ = 0;
        }
        copyForceOriginalOrder();
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"ipg_.size()="<<ipg_.size()<<std::endl;
        std::cout<<"force_sorted_.size()="<<force_sorted_.size()<<std::endl;
        std::cout<<"force_org_.size()="<<force_org_.size()<<std::endl;
#endif
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
             class Tmomloc, class Tmomglb, class Tspj>
    template<class Tfunc_ep_ep, class Tfunc_ep_sp>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcForce(Tfunc_ep_ep pfunc_ep_ep,
              Tfunc_ep_sp pfunc_ep_sp,
              const bool clear){
        force_sorted_.resizeNoInitialize(n_loc_tot_);
        force_org_.resizeNoInitialize(n_loc_tot_);
        const S32 n_ipg = ipg_.size();
        S32 ni_tmp = 0;
        S32 nj_tmp = 0;
        S64 n_interaction_tmp = 0;
        if(n_ipg > 0){
#pragma omp parallel for schedule(dynamic, 4) reduction(+ : ni_tmp, nj_tmp, n_interaction_tmp) 
            for(S32 i=0; i<n_ipg; i++){
                makeInteractionList(i);
                ni_tmp += ipg_[i].n_ptcl_;
                nj_tmp += epj_for_force_[Comm::getThreadNum()].size();
                nj_tmp += spj_for_force_[Comm::getThreadNum()].size();
                n_interaction_tmp += ipg_[i].n_ptcl_ * epj_for_force_[Comm::getThreadNum()].size();
                n_interaction_tmp += ipg_[i].n_ptcl_ * spj_for_force_[Comm::getThreadNum()].size();
                calcForceOnly( pfunc_ep_ep, pfunc_ep_sp, i, clear);
            }
            ni_ave_ = ni_tmp / n_ipg;
            nj_ave_ = nj_tmp / n_ipg;
            n_interaction_ = n_interaction_tmp;
        }
        else{
            ni_ave_ = nj_ave_ = n_interaction_ = 0;
        }
        copyForceOriginalOrder();
#ifdef PARTICLE_SIMULATOR_DEBUG_PRINT
        PARTICLE_SIMULATOR_PRINT_LINE_INFO();
        std::cout<<"ipg_.size()="<<ipg_.size()<<std::endl;
        std::cout<<"force_sorted_.size()="<<force_sorted_.size()<<std::endl;
        std::cout<<"force_org_.size()="<<force_org_.size()<<std::endl;
#endif
    }
    
    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    template<class Tfunc_ep_ep, class Tpsys>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcForceAndWriteBack(Tfunc_ep_ep pfunc_ep_ep,
                          Tpsys & psys,
                          const bool clear){
        calcForce(pfunc_ep_ep, clear);
        for(S32 i=0; i<n_loc_tot_; i++) psys[i].copyFromForce(force_org_[i]);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    template<class Tfunc_ep_ep, class Tfunc_ep_sp, class Tpsys>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcForceAndWriteBack(Tfunc_ep_ep pfunc_ep_ep,
			  Tfunc_ep_sp pfunc_ep_sp,
			  Tpsys & psys,
			  const bool clear){
        calcForce(pfunc_ep_ep, pfunc_ep_sp, clear);
        for(S32 i=0; i<n_loc_tot_; i++) psys[i].copyFromForce(force_org_[i]);
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    template<class Tfunc_ep_ep>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcForceDirect(Tfunc_ep_ep pfunc_ep_ep,
                    Tforce force[],
                    const DomainInfo & dinfo,
                    const bool clear){
        if(clear){
            for(S32 i=0; i<n_loc_tot_; i++)force[i].clear();
        }
        Tepj * epj_tmp;
        S32 n_epj_tmp;
        bool pa[DIMENSION];
        dinfo.getPeriodicAxis(pa);
        AllGatherParticle(epj_tmp, n_epj_tmp, epj_org_.getPointer(), n_loc_tot_, dinfo.getPosRootDomain().getFullLength(), pos_root_cell_, pa);
        pfunc_ep_ep(epi_org_.getPointer(), n_loc_tot_, epj_tmp, n_epj_tmp, force);
        delete [] epj_tmp;
    }

    template<class TSM, class Tforce, class Tepi, class Tepj,
	     class Tmomloc, class Tmomglb, class Tspj>
    template<class Tfunc_ep_ep>
    void TreeForForce<TSM, Tforce, Tepi, Tepj, Tmomloc, Tmomglb, Tspj>::
    calcForceDirectAndWriteBack(Tfunc_ep_ep pfunc_ep_ep,
                                const DomainInfo & dinfo,
                                const bool clear){
        force_org_.resizeNoInitialize(n_loc_tot_);
        if(clear){
            for(S32 i=0; i<n_loc_tot_; i++)force_org_[i].clear();
        }
        Tepj * epj_tmp;
        S32 n_epj_tmp;
        bool pa[DIMENSION];
        dinfo.getPeriodicAxis(pa);
        AllGatherParticle(epj_tmp, n_epj_tmp, epj_org_.getPointer(), n_loc_tot_, dinfo.getPosRootDomain().getFullLength(), pos_root_cell_, pa);
        pfunc_ep_ep(epi_org_.getPointer(), n_loc_tot_, epj_tmp, n_epj_tmp, force_org_.getPointer());
        delete [] epj_tmp;
    }
}
