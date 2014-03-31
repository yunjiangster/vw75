#include <float.h>
#include "reductions.h"
#include "multiclass.h"
#include "cost_sensitive.h"
#include "cb.h"
#include "cb_algs.h"
#include "rand48.h"
#include "bs.h"

using namespace LEARNER;

namespace CBIFY {

  struct cbify {
    size_t k;
    
    size_t tau;

    float epsilon;

    size_t counter;

    size_t bags;
    v_array<float> count;
    v_array<uint32_t> predictions;
    
    CB::label cb_label;
    COST_SENSITIVE::label cs_label;
    COST_SENSITIVE::label second_cs_label;

    learner* cs;
    vw* all;
  };
  
  uint32_t do_uniform(cbify& data)
  {  //Draw an action
    return (uint32_t)ceil(frand48() * data.k);
  }

  uint32_t choose_bag(cbify& data)
  {  //Draw an action
    return (uint32_t)floor(frand48() * data.bags);
  }

  float loss(uint32_t label, float final_prediction)
  {
    if (label != final_prediction)
      return 1.;
    else
      return 0.;
  }

  template <bool is_learn>
  void predict_or_learn_first(cbify& data, learner& base, example& ec)
  {//Explore tau times, then act according to optimal.
    MULTICLASS::mc_label* ld = (MULTICLASS::mc_label*)ec.ld;
    //Use CB to find current prediction for remaining rounds.
    if (data.tau && is_learn)
      {
	ec.final_prediction = (float)do_uniform(data);
	ec.loss = loss(ld->label, ec.final_prediction);
	data.tau--;
	uint32_t action = (uint32_t)ec.final_prediction;
	CB::cb_class l = {ec.loss, action, 1.f / data.k};
	data.cb_label.costs.erase();
	data.cb_label.costs.push_back(l);
	ec.ld = &(data.cb_label);
	base.learn(ec);
	ec.final_prediction = (float)action;
	ec.loss = l.cost;
      }
    else
      {
	data.cb_label.costs.erase();
	ec.ld = &(data.cb_label);
	base.predict(ec);
	ec.loss = loss(ld->label, ec.final_prediction);
      }
    ec.ld = ld;
  }
  
  template <bool is_learn>
  void predict_or_learn_greedy(cbify& data, learner& base, example& ec)
  {//Explore uniform random an epsilon fraction of the time.
    MULTICLASS::mc_label* ld = (MULTICLASS::mc_label*)ec.ld;
    ec.ld = &(data.cb_label);
    data.cb_label.costs.erase();
    
    base.predict(ec);
    uint32_t action = (uint32_t)ec.final_prediction;

    float base_prob = data.epsilon / data.k;
    if (frand48() < 1. - data.epsilon)
      {
	CB::cb_class l = {loss(ld->label, ec.final_prediction), 
			  action, 1.f - data.epsilon + base_prob};
	data.cb_label.costs.push_back(l);
      }
    else
      {
	action = do_uniform(data);
	CB::cb_class l = {loss(ld->label, (float)action), 
			  action, base_prob};
	if (action == ec.final_prediction)
	  l.probability = 1.f - data.epsilon + base_prob;
	data.cb_label.costs.push_back(l);
      }

    if (is_learn)
      base.learn(ec);
    
    ec.final_prediction = (float)action;
    ec.loss = loss(ld->label, ec.final_prediction);
    ec.ld = ld;
  }

  template <bool is_learn>
  void predict_or_learn_bag(cbify& data, learner& base, example& ec)
  {//Randomize over predictions from a base set of predictors
    //Use CB to find current predictions.
    MULTICLASS::mc_label* ld = (MULTICLASS::mc_label*)ec.ld;
    ec.ld = &(data.cb_label);
    data.cb_label.costs.erase();

    for (size_t j = 1; j <= data.bags; j++)
       data.count[j] = 0;
	 
    size_t bag = choose_bag(data);
    uint32_t action = 0;
    for (size_t i = 0; i < data.bags; i++)
      {
	base.predict(ec,i);
	data.count[(uint32_t)ec.final_prediction]++;
	if (i == bag)
	  action = (uint32_t)ec.final_prediction;
      }
    assert(action != 0);
    if (is_learn)
      {
	float probability = (float)data.count[action] / (float)data.bags;
	CB::cb_class l = {loss(ld->label, (float)action), 
			  action, probability};
	data.cb_label.costs.push_back(l);
	for (size_t i = 0; i < data.bags; i++)
	  {
	    uint32_t count = BS::weight_gen();
	    for (uint32_t j = 0; j < count; j++)
	      base.learn(ec,i);
	  }
      }
    ec.ld = ld;
    ec.final_prediction = (float)action;
  }
  
  uint32_t choose_action(v_array<float>& distribution)
  {
    float value = frand48();
    for (uint32_t i = 0; i < distribution.size();i++)
      {
	if (value <= distribution[i])
	  return i+1;	    
	else
	  value -= distribution[i];
      }
    //some rounding problem presumably.
    return 1;
  }
  
  void safety(v_array<float>& distribution, float min_prob)
  {
    float added_mass = 0.;
    for (uint32_t i = 0; i < distribution.size();i++)
      if (distribution[i] > 0 && distribution[i] <= min_prob)
	{
	  added_mass += min_prob - distribution[i];
	  distribution[i] = min_prob;
	}
    
    float ratio = 1.f / (1.f + added_mass);
    if (ratio < 0.999)
      {
	for (uint32_t i = 0; i < distribution.size(); i++)
	  if (distribution[i] > min_prob)
	    distribution[i] = distribution[i] * ratio; 
	safety(distribution, min_prob);
      }
  }

  void gen_cs_label(vw& all, CB::cb_class& known_cost, example& ec, COST_SENSITIVE::label& cs_ld, uint32_t label)
  {
    COST_SENSITIVE::wclass wc;
    
    //get cost prediction for this label
    wc.x = CB_ALGS::get_cost_pred<false>(all, &known_cost, ec, label, all.sd->k);
    wc.weight_index = label;
    wc.partial_prediction = 0.;
    wc.wap_value = 0.;
    
    //add correction if we observed cost for this action and regressor is wrong
    if( known_cost.action == label ) 
      wc.x += (known_cost.cost - wc.x) / known_cost.probability;
    
    cs_ld.costs.push_back( wc );
  }

  template <bool is_learn>
  void predict_or_learn_cover(cbify& data, learner& base, example& ec)
  {//Randomize over predictions from a base set of predictors
    //Use cost sensitive oracle to cover actions to form distribution.
    MULTICLASS::mc_label* ld = (MULTICLASS::mc_label*)ec.ld;
    data.counter++;

    data.count.erase();
    data.cs_label.costs.erase();
    for (uint32_t j = 0; j < data.k; j++)
      {
	data.count.push_back(0);

	COST_SENSITIVE::wclass wc;
	
	//get cost prediction for this label
	wc.x = FLT_MAX;
	wc.weight_index = j+1;
	wc.partial_prediction = 0.;
	wc.wap_value = 0.;
	data.cs_label.costs.push_back(wc);
      }

    float additive_probability = 1.f / (float)data.bags;

    ec.ld = &data.cs_label;
    for (size_t i = 0; i < data.bags; i++)
      { //get predicted cost-sensitive predictions
	if (i == 0)
	  data.cs->predict(ec, i);
	else
	  data.cs->predict(ec,i+1);
	data.count[(size_t)ec.final_prediction-1] += additive_probability;
	data.predictions[i] = (uint32_t)ec.final_prediction;
      }

    float min_prob = data.epsilon * min (1.f / data.k, 1.f / (float)sqrt(data.counter * data.k));
    
    safety(data.count, min_prob);
    
    //compute random action
    uint32_t action = choose_action(data.count);
    
    if (is_learn)
      {
	data.cb_label.costs.erase();
	float probability = (float)data.count[action-1];
	CB::cb_class l = {loss(ld->label, (float)action), 
			  action, probability};
	data.cb_label.costs.push_back(l);
	ec.ld = &(data.cb_label);
	base.learn(ec);

	//Now update oracles
	
	//1. Compute loss vector
	data.cs_label.costs.erase();
	float norm = min_prob * data.k;
	for (uint32_t j = 0; j < data.k; j++)
	  { //data.cs_label now contains an unbiased estimate of cost of each class.
	    gen_cs_label(*data.all, l, ec, data.cs_label, j+1);
	    data.count[j] = 0;
	  }
	
	ec.ld = &data.second_cs_label;
	//2. Update functions
	for (size_t i = 0; i < data.bags; i++)
	  { //get predicted cost-sensitive predictions
	    for (uint32_t j = 0; j < data.k; j++)
	      {
		float pseudo_cost = data.cs_label.costs[j].x - data.epsilon * min_prob / (max(data.count[j], min_prob) / norm) + 1;
		data.second_cs_label.costs[j].weight_index = j+1;
		data.second_cs_label.costs[j].x = pseudo_cost;
	      }
	    if (i != 0)
	      data.cs->learn(ec,i+1);
	    if (data.count[data.predictions[i]-1] < min_prob)
	      norm += max(0, additive_probability - (min_prob - data.count[data.predictions[i]-1]));
	    else
	      norm += additive_probability;
	    data.count[data.predictions[i]-1] += additive_probability;
	  }
      }

    ec.ld = ld;
    ec.final_prediction = (float)action;
  }
  
  void init_driver(cbify&) {}

  void finish_example(vw& all, cbify&, example& ec)
  {
    MULTICLASS::output_example(all, ec);
    VW::finish_example(all, &ec);
  }

  learner* setup(vw& all, std::vector<std::string>&opts, po::variables_map& vm, po::variables_map& vm_file)
  {//parse and set arguments
    cbify* data = (cbify*)calloc(1, sizeof(cbify));

    data->epsilon = 0.05f;
    data->counter = 0;
    data->tau = 1000;
    data->all = &all;
    po::options_description desc("CBIFY options");
    desc.add_options()
      ("first", po::value<size_t>(), "tau-first exploration")
      ("epsilon",po::value<float>() ,"epsilon-greedy exploration")
      ("bag",po::value<size_t>() ,"bagging-based exploration")
      ("cover",po::value<size_t>() ,"bagging-based exploration");
    
    po::parsed_options parsed = po::command_line_parser(opts).
      style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing).
      options(desc).allow_unregistered().run();
    opts = po::collect_unrecognized(parsed.options, po::include_positional);
    po::store(parsed, vm);
    po::notify(vm);
    
    po::parsed_options parsed_file = po::command_line_parser(all.options_from_file_argc,all.options_from_file_argv).
      style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing).
      options(desc).allow_unregistered().run();
    po::store(parsed_file, vm_file);
    po::notify(vm_file);
    
    if( vm_file.count("cbify") ) {
      data->k = (uint32_t)vm_file["cbify"].as<size_t>();
      if( vm.count("cbify") && (uint32_t)vm["cbify"].as<size_t>() != data->k )
        std::cerr << "warning: you specified a different number of actions through --cbify than the one loaded from predictor. Pursuing with loaded value of: " << data->k << endl;
    }
    else {
      data->k = (uint32_t)vm["cbify"].as<size_t>();
      
      //appends nb_actions to options_from_file so it is saved to regressor later
      std::stringstream ss;
      ss << " --cbify " << data->k;
      all.options_from_file.append(ss.str());
    }

    all.p->lp = MULTICLASS::mc_label_parser;
    learner* l;
    if (vm.count("cover"))
      {
	data->bags = (uint32_t)vm["cover"].as<size_t>();
	data->cs = all.cost_sensitive;
	data->count.resize(data->k);
	data->predictions.resize(data->bags);
	data->second_cs_label.costs.resize(data->k);
	data->second_cs_label.costs.end = data->second_cs_label.costs.begin+data->k;
	if ( vm.count("epsilon") ) 
	  data->epsilon = vm["epsilon"].as<float>();
	l = new learner(data, all.l, data->bags + 1);
	l->set_learn<cbify, predict_or_learn_cover<true> >();
	l->set_predict<cbify, predict_or_learn_cover<false> >();
      }
    else if (vm.count("bag"))
      {
	data->bags = (uint32_t)vm["bag"].as<size_t>();
	data->count.resize(data->bags+1);
	l = new learner(data, all.l, data->bags);
	l->set_learn<cbify, predict_or_learn_bag<true> >();
	l->set_predict<cbify, predict_or_learn_bag<false> >();
      }
    else if (vm.count("first") )
      {
	data->tau = (uint32_t)vm["first"].as<size_t>();
	l = new learner(data, all.l, 1);
	l->set_learn<cbify, predict_or_learn_first<true> >();
	l->set_predict<cbify, predict_or_learn_first<false> >();
      }
    else
      {
	if ( vm.count("epsilon") ) 
	  data->epsilon = vm["epsilon"].as<float>();
	l = new learner(data, all.l, 1);
	l->set_learn<cbify, predict_or_learn_greedy<true> >();
	l->set_predict<cbify, predict_or_learn_greedy<false> >();
      }

    l->set_finish_example<cbify,finish_example>();
    l->set_init_driver<cbify,init_driver>();
    
    return l;
  }
}
