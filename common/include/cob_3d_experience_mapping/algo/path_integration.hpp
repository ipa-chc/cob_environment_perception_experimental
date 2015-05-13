//path integration (input from odometry)

template<class TGraph, class TMapCells, class TMapTransformations, typename TState>
void insert_cell(TGraph &graph, TMapCells &cells, TMapTransformations &trans, TState &new_cell) {
	new_cell->set_node(graph.addNode());
	cells.set(new_cell->node(), new_cell);
}

template<class TTransformation, class TGraph, class TMapTransformations, typename TState>
void insert_transistion(TGraph &graph, TMapTransformations &trans, TState &new_cell, TTransformation &link) {
	ROS_ASSERT(link->src());
	trans.set(graph.addArc(new_cell->node(), link->src()->node()), link);
	//trans.set(graph.addEdge(new_cell->node(), link->src()->node()), link);
}

template<class TGraph, typename TState>
void remove_cell(TGraph &graph, TState &cell) {
	if(!cell) return;
	graph.erase(cell->node()); //deletes also arcs
}

template<class TTransformation, class TGraph, class TContext, class TMapCells, class TMapTransformations>
void init(TGraph &graph, TContext &ctxt, TMapCells &cells, TMapTransformations &trans)
{
	typedef typename TContext::TState TState;

	typename TContext::TState::TPtr cell(new TState);
	//cell->energy() = 1;
	ctxt.active_cells().push_back(cell);
	insert_cell(graph, cells, trans, cell);

	//ctxt.virtual_cell().reset(new TState);
	//insert_cell<TTransformation>(graph, cells, trans, ctxt.virtual_cell(), ctxt.current_active_cell());
}
		
template<typename TStatePtr, typename TEnergy, typename TContext, typename TGraph, typename TCells, typename TTrans, typename TAction>
TEnergy inflow(TStatePtr &th, const TEnergy &offset, const TContext &ctxt, const TGraph &graph, const TCells &cells, const TTrans &trans, const TAction &odom) /*const*/ {
	TEnergy I = 0;
	for(typename TStatePtr::element_type::TArcIterator ait(th->edge_begin(graph)); ait!=th->edge_end(graph); ++ait) {
		TStatePtr opposite = cells[th->opposite_node(graph, ait)];
		const typename TAction::TType trans_fact = trans[ait]->directed(th).transition_factor_dbg(odom, ctxt.param().prox_thr_);
		ROS_ASSERT_MSG(trans_fact==0 || (opposite->outflow()>=0 && opposite->outflow()<=1), "outflow %f out of bounds [0,1] (d=%f)", opposite->outflow(), trans_fact);
		
		I = std::max(I, trans_fact*opposite->outflow());
	}
	ROS_INFO("inflow %f", I);
	I-=offset;
	
	//TODO: add sensor input here
	//I += p*(1-energy());
	
	return I;
}

template<class TStatePtr>
bool energy_order(const TStatePtr &a, const TStatePtr &b) {
	return a->d2() < b->d2();
}

template<class TCellVector, class TEnergyFactor, class TGraph, class TContext, class TResultList, class TMapCells, class TMapTransformations, class TTransformation>
void path_integration(TCellVector &active_cells/*, const TEnergyFactor &weight*/, TGraph &graph, TContext &ctxt, TMapCells &cells, TMapTransformations &trans, const TTransformation &odom, TResultList &result)
{
	typedef typename TContext::TState TState;
	typedef typename TCellVector::iterator TIter;
	typedef typename TState::TArcOutIterator TArcIter_out;
	typedef typename TState::TArcInIterator TArcIter_in;
	
	boost::lock_guard<boost::mutex> guard(ctxt.get_mutex());	//TODO: reduce locking area
	
	{
		TIter begin = active_cells.begin();
		TIter end   = active_cells.end();
		std::sort(begin, end, energy_order<typename TCellVector::value_type>);
		for(TIter it=begin; it!=end; it++) {
			printf("cell %f %f\n", (*it)->dist_h(), (*it)->dist_o());
			if(it+1!=end)
				ROS_ASSERT( (*it)->d()<=(*(it+1))->d() );
		}
		ROS_ASSERT(ctxt.current_active_cell()==*begin);
	}
	
	ROS_ASSERT(ctxt.active_cells().size()>0);
	ROS_ASSERT(ctxt.current_active_cell());
	
	//if(ctxt.virtual_cell() && ctxt.current_active_cell()!=ctxt.virtual_cell())
	//	ctxt.virtual_cell()->dist_o() = ctxt.current_active_cell()->dist_o();

	if(!ctxt.virtual_cell() || (ctxt.last_active_cell()!=ctxt.current_active_cell() &&ctxt.current_active_cell()->dist_h()<=0) || (ctxt.current_active_cell()!=ctxt.virtual_cell() && ctxt.current_active_cell()->d2()<ctxt.last_dist_min() && ctxt.current_active_cell()->dist_h()<=0) || ctxt.virtual_cell()->dist_h()<=0) {
		ROS_INFO("resetting virtual cell %d (%f %f)",
			(int)(ctxt.last_active_cell()!=ctxt.current_active_cell()),
			ctxt.virtual_cell()?ctxt.virtual_cell()->dist_h():0., ctxt.virtual_cell()?ctxt.virtual_cell()->dist_o():0.
		);
		
		if(ctxt.current_active_cell()==ctxt.virtual_cell()) {
			ROS_INFO("virtual cell is inserted to map (action dist.: %f)", ctxt.virtual_transistion()->dist(ctxt.param().prox_thr_));
			ctxt.virtual_transistion()->dbg();
		}
		else {
			ROS_INFO("relocalized %d -> %d", ctxt.last_active_cell()?ctxt.last_active_cell()->dbg().id_:-1, ctxt.current_active_cell()?ctxt.current_active_cell()->dbg().id_:-1);
			
			for(TIter it=active_cells.begin(); it!=active_cells.end(); it++)
				if(*it == ctxt.virtual_cell()) {
					active_cells.erase(it);
					break;
				}

			remove_cell(graph, ctxt.virtual_cell());
		}
		
		ctxt.virtual_cell().reset(new TState);
		ctxt.virtual_cell()->dist_h() = 1;
		ctxt.virtual_cell()->dist_o() = ctxt.current_active_cell()->dist_o();
		
		insert_cell(graph, cells, trans, ctxt.virtual_cell());
		ctxt.virtual_transistion().reset(new TTransformation(ctxt.current_active_cell()));
		insert_transistion(graph, trans, ctxt.virtual_cell(), ctxt.virtual_transistion());

		ctxt.last_active_cell() = ctxt.current_active_cell();
		
		active_cells.push_back(ctxt.virtual_cell());
	}
	
	ctxt.last_dist_min() = ctxt.current_active_cell()->d2();
	
	ROS_ASSERT(ctxt.virtual_cell());
	ROS_ASSERT(ctxt.virtual_transistion());

	ctxt.virtual_transistion()->integrate(odom);
	
	{ //DEBUG
		ctxt.virtual_cell()->dbg().info_+="V ";
		ctxt.current_active_cell()->dbg().info_+="C ";
		
		ctxt.virtual_transistion()->dbg();
	} //DEBUG
	
	
	TIter begin = active_cells.begin();
	TIter end   = active_cells.end();
	
	//step 0: update feature prob.
	for(TIter it=begin; it!=end; it++) {
		if( (*it)->dbg().id_ >= ctxt.virtual_cell()->dbg().id_-ctxt.param().min_age_ )
			continue;
			
		typename TState::TEnergy ft_prob = (*it)->get_feature_prob(), ft_prob_max=0;
		typename TState::TEnergy prob_chang = ft_prob-(*it)->get_last_feature_prob(), ft_prob_ch_max=0;
		
		for(TArcIter_out ait((*it)->arc_out_begin(graph)); ait!=(*it)->arc_out_end(graph); ++ait) {
			std::cout<<"xo "<<(*it).get()<<" "<<((*it)->opposite_node(graph, ait)==lemon::INVALID)<<std::endl;
			typename TIter::value_type opposite = cells[(*it)->opposite_node(graph, ait)];
			ft_prob_max = std::max(ft_prob_max, opposite->get_feature_prob());
			ft_prob_ch_max = std::max(ft_prob_ch_max, trans[ait]->directed(*it).transition_factor_dbg(odom, ctxt.param().prox_thr_)*(prob_chang-(opposite->get_feature_prob()-opposite->get_last_feature_prob()))/2);
		}
		for(TArcIter_in ait((*it)->arc_in_begin(graph)); ait!=(*it)->arc_in_end(graph); ++ait) {
			std::cout<<"xi "<<(*it).get()<<" "<<((*it)->opposite_node(graph, ait)==lemon::INVALID)<<std::endl;
			typename TIter::value_type opposite = cells[(*it)->opposite_node(graph, ait)];
			ft_prob_max = std::max(ft_prob_max, opposite->get_feature_prob());
			ft_prob_ch_max = std::max(ft_prob_ch_max, trans[ait]->directed(*it).transition_factor_dbg(odom, ctxt.param().prox_thr_)*(prob_chang-(opposite->get_feature_prob()-opposite->get_last_feature_prob()))/2);
		}
		
		ft_prob = std::max((typename TState::TEnergy)0, ft_prob-ft_prob_max)*odom.dist(ctxt.param().prox_thr_);
	
		if(ft_prob+ft_prob_ch_max)
			ROS_INFO("%d: injecting energy %f (feature probability)    %f %f %f    %f %f", (*it)->dbg().id_, ft_prob+ft_prob_ch_max, ft_prob, (*it)->get_feature_prob(), ft_prob_max, prob_chang, ft_prob_ch_max);
			
		(*it)->dist_o() *= std::max((typename TState::TEnergy)0, 1-ft_prob-ft_prob_ch_max);
	}
	
	//step 1: set min. dist.
	for(TIter it=begin; it!=end; it++) {
		typename TState::TEnergy dh_max = 0;
		
		for(TArcIter_out ait((*it)->arc_out_begin(graph)); ait!=(*it)->arc_out_end(graph); ++ait) {
				typename TIter::value_type opposite = cells[(*it)->opposite_node(graph, ait)];
				if(opposite->dist_h()>0) continue;
				
				if( trans[ait]!=ctxt.virtual_transistion() && 
					std::pow(trans[ait]->dist(ctxt.param().prox_thr_),2) + std::pow(opposite->dist_o(),2)
					<
					(*it)->d2()
				) {
					ROS_INFO("setting dist from %f/%f to %f/%f",
						(*it)->dist_h(), (*it)->dist_o(),
						trans[ait]->dist(ctxt.param().prox_thr_), opposite->dist_o());
						
					(*it)->dist_h() = trans[ait]->dist(ctxt.param().prox_thr_);
					(*it)->dist_o() = opposite->dist_o();
				}
		}
	}
	
	//step 2: increase dist.
	for(TIter it=begin; it!=end; it++) {
		typename TState::TEnergy dh_max = 0;
		
		for(TArcIter_out ait((*it)->arc_out_begin(graph)); ait!=(*it)->arc_out_end(graph); ++ait)
			dh_max = std::max(dh_max, trans[ait]->directed(*it).transition_factor_dbg(odom, ctxt.param().prox_thr_) );
		
		const typename TState::TEnergy delta = std::min((*it)->dist_h(), dh_max);
		(*it)->dist_h() -= delta;
		(*it)->dist_o() += std::sqrt(std::pow(odom.dist(ctxt.param().prox_thr_),2)-delta*delta + 0.000001f);
		
		ROS_INFO("changing dist(%f/%f) by (%f/%f) %f %f", (*it)->dist_h(),(*it)->dist_o(), delta, std::sqrt(std::pow(odom.dist(ctxt.param().prox_thr_),2)-delta*delta), odom.dist(ctxt.param().prox_thr_), std::pow(odom.dist(ctxt.param().prox_thr_),2)-delta*delta);
		ROS_ASSERT((std::pow(odom.dist(ctxt.param().prox_thr_),2)-delta*delta + 0.000001f)>=0);
		ROS_ASSERT( (*it)->dist_h()==(*it)->dist_h() );
		ROS_ASSERT( (*it)->dist_o()==(*it)->dist_o() );
		
		(*it)->reset_feature();
	}
	
	
	//debug
	{
		int i=0;
		printf("distlist");
		for(TIter it=begin; it!=end; it++) {
			printf("\t %f:%d:%d", (*it)->d(), (*it)->dist_h()<=0, (*it)->dbg().id_);
			++i;
			if(i>3) break;
		}
		printf("\n");
	}
	
#if 0
	
	//...
	TIter begin = active_cells.begin();
	TIter end   = active_cells.end();
	/*for(TIter it=begin; it!=end; it++) {
		if(!ctxt.current_active_cell() || ctxt.current_active_cell()->energy()<(*it)->energy())
			ctxt.current_active_cell() = *it;
	}*/
	std::sort(begin, end, energy_order<typename TCellVector::value_type>);
	ctxt.set_energy_max( ctxt.current_active_cell()->energy() );
	//...
	
	ROS_ASSERT(ctxt.active_cells().size()>0);
	ROS_ASSERT(ctxt.current_active_cell());

	if(!ctxt.virtual_cell() || ctxt.last_active_cell()!=ctxt.current_active_cell() || ctxt.last_energy_max() <= ctxt.energy_max()) {
		ROS_INFO("resetting virtual cell %d %d (%f %f)",
			(int)(ctxt.last_active_cell()!=ctxt.current_active_cell()),
			(int)(ctxt.last_energy_max() <= ctxt.energy_max()),
			ctxt.last_energy_max(), ctxt.energy_max()
		);
		
		if(ctxt.current_active_cell()==ctxt.virtual_cell()) {
			ROS_INFO("virtual cell is inserted to map (action dist.: %f)", ctxt.virtual_transistion()->dist(ctxt.param().prox_thr_));
			ctxt.virtual_transistion()->dbg();
		}
		else {
			for(TIter it=active_cells.begin(); it!=active_cells.end(); it++)
				if(*it == ctxt.virtual_cell()) {
					active_cells.erase(it);
					break;
				}

			remove_cell(graph, ctxt.virtual_cell());
		}
		
		ctxt.virtual_cell().reset(new TState);
		insert_cell(graph, cells, trans, ctxt.virtual_cell());
		ctxt.virtual_transistion().reset(new TTransformation(ctxt.current_active_cell()));
		insert_transistion(graph, trans, ctxt.virtual_cell(), ctxt.virtual_transistion());

		ctxt.last_energy_max()  = ctxt.energy_max();
		ctxt.last_active_cell() = ctxt.current_active_cell();
		
		active_cells.push_back(ctxt.virtual_cell());
	}
	
	begin = active_cells.begin();
	end   = active_cells.end();
	
	ROS_ASSERT(ctxt.virtual_cell());
	ROS_ASSERT(ctxt.virtual_transistion());

	ctxt.virtual_transistion()->integrate(odom);
	
	ctxt.virtual_cell()->dbg().info_+="V ";
	ctxt.current_active_cell()->dbg().info_+="C ";
	
	ctxt.virtual_transistion()->dbg();
	ROS_INFO("virtual energy 1: %f", ctxt.virtual_cell()->energy());

	/*if(ctxt.virtual_transistion()->dist(ctxt.param().prox_thr_)>=1) {
		ROS_INFO("virtual cell is inserted to map (action dist.: %f)", ctxt.virtual_transistion()->dist(ctxt.param().prox_thr_));

		ctxt.virtual_cell().reset();
	}*/
	
	//-----------------------------------------------
	
	//calc. loss (depends only on action)
	ROS_ASSERT(odom.dist(ctxt.param().prox_thr_)>=0);
	
	size_t remaining = 0;
	for(TIter it=begin; it!=end; it++) {
		(*it)->loss() = (*it)->energy()*(1 - 1/std::pow(2, odom.dist(ctxt.param().prox_thr_)));
		(*it)->outflow() = -1; //not set yet!
		++remaining;
		
		ROS_INFO("loss %f (from %f, d=%f)\t\t%s %s", (*it)->loss(), (*it)->energy(), odom.dist(ctxt.param().prox_thr_), (*it)->dbg().name_.c_str(), (*it)->dbg().info_.c_str());
	}
		
	while(remaining>0) {
		TIter it_min=end;
		//expected max. outflow
		for(TIter it=begin; it!=end; it++) {
			if((*it)->outflow()!=-1) continue;	//already done
			
			ROS_INFO("<<<<<<<<<<<<<<");
			(*it)->outflow_em() = 0;
			for(TArcIter ait((*it)->edge_begin(graph)); ait!=(*it)->edge_end(graph); ++ait) {
				typename TIter::value_type opposite = cells[(*it)->opposite_node(graph, ait)];
				
				ROS_INFO("%f %f %f",
					opposite->outflow(),
					opposite->loss(),
					trans[ait]->directed(*it).transition_factor(odom, ctxt.param().prox_thr_));
				
				if(opposite->outflow()==-1)
					(*it)->outflow_em() = std::max((*it)->outflow_em(),
					trans[ait]->directed(*it).transition_factor(odom, ctxt.param().prox_thr_)
					*opposite->loss());
			}
			ROS_INFO(">>>>>>>>>>>>>>>");
			
			if((*it)->outflow_em()==0) {
				(*it)->outflow() = 0;
				it_min = it;
				break;
			}
			
			if(it_min==end || (*it_min)->outflow_em()>(*it)->outflow_em())
				it_min = it;
		}
		
		ROS_INFO("min. outflow em: %f", (*it_min)->outflow_em());
		
		//todo... improve speed by sorted map, now just proof of concept
		
		typename TState::TEnergy I = inflow(*it_min, (*it_min)->outflow_em(), ctxt, graph, cells, trans, odom);
		typename TState::TEnergy D = (*it_min)->loss();//-I;
		
		//calc. outflow
		(*it_min)->outflow() = std::max((typename TState::TEnergy)0, D);
		ROS_ASSERT( (*it_min)->outflow()>=0 && (*it_min)->outflow()<=1 );
		
		//calc. energy delta
		ROS_INFO("e=%f I=%f L=%f\t\t%s %s", (*it_min)->energy(), I, (*it_min)->loss(), (*it_min)->dbg().name_.c_str(), (*it_min)->dbg().info_.c_str());
		if((*it_min)->loss()>I)
			(*it_min)->energy() -= (*it_min)->loss();
		else
			(*it_min)->energy() += I;//-(*it_min)->outflow();
		
		/*if((*it_min)->loss()>2*I)
			(*it_min)->energy() -= (*it_min)->loss();
		else
			(*it_min)->energy() += I;*/
			
		ROS_ASSERT_MSG( (*it_min)->energy()>=0 && (*it_min)->energy()<=1, "energy %f is out of bound [0,1]", (*it_min)->energy() );
		
		--remaining;
	}

	ROS_INFO("summary:");
	double dbg_sum=0;
	for(TIter it=begin; it!=end; it++) {
		ROS_INFO("e=%f\t\t%s %s", (*it)->energy(), (*it)->dbg().name_.c_str(), (*it)->dbg().info_.c_str());
		dbg_sum += (*it)->energy();
	}
	ROS_INFO("sum e=%f", dbg_sum);
#endif
}
