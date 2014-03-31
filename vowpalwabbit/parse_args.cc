/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD (revised)
license as described in the file LICENSE.
 */
#include <stdio.h>
#include <float.h>
#include <sstream>

#include "cache.h"
#include "io_buf.h"
#include "parse_regressor.h"
#include "parser.h"
#include "parse_args.h"
#include "sender.h"
#include "network.h"
#include "global_data.h"
#include "nn.h"
#include "cbify.h"
#include "oaa.h"
#include "rand48.h"
#include "bs.h"
#include "topk.h"
#include "ect.h"
#include "csoaa.h"
#include "wap.h"
#include "cb.h"
#include "cb_algs.h"
#include "scorer.h"
#include "searn.h"
#include "bfgs.h"
#include "lda_core.h"
#include "noop.h"
#include "print.h"
#include "gd_mf.h"
#include "mf.h"
#include "vw.h"
#include "rand48.h"
#include "parse_args.h"
#include "binary.h"
#include "lrq.h"
#include "autolink.h"

using namespace std;
//
// Does string end with a certain substring?
//
bool ends_with(string const &fullString, string const &ending)
{
    if (fullString.length() > ending.length()) {
        return (fullString.compare(fullString.length() - ending.length(), ending.length(), ending) == 0);
    } else {
        return false;
    }
}

bool valid_ns(char c)
{
    if (c=='|'||c==':')
        return false;
    return true;
}


void parse_affix_argument(vw&all, string str) {
  if (str.length() == 0) return;
  char* cstr = (char*)calloc(str.length()+1, sizeof(char));
  strcpy(cstr, str.c_str());

  char*p = strtok(cstr, ",");
  while (p != 0) {
    char*q = p;
    uint16_t prefix = 1;
    if (q[0] == '+') { q++; }
    else if (q[0] == '-') { prefix = 0; q++; }
    if ((q[0] < '1') || (q[0] > '7')) {
      cerr << "malformed affix argument (length must be 1..7): " << p << endl;
      throw exception();
    }
    uint16_t len = (uint16_t)(q[0] - '0');
    uint16_t ns = (uint16_t)' ';  // default namespace
    if (q[1] != 0) {
      if (valid_ns(q[1]))
        ns = (uint16_t)q[1];
      else {
        cerr << "malformed affix argument (invalid namespace): " << p << endl;
        throw exception();
      }
      if (q[2] != 0) {
        cerr << "malformed affix argument (too long): " << p << endl;
        throw exception();
      }
    }

    uint16_t afx = (len << 1) | (prefix & 0x1);
    all.affix_features[ns] <<= 4;
    all.affix_features[ns] |=  afx;

    p = strtok(NULL, ",");
  }

  free(cstr);
}

vw* parse_args(int argc, char *argv[])
{
  po::options_description desc("VW options");

  vw* all = new vw();

  size_t random_seed = 0;
  all->program_name = argv[0];

  po::options_description in_opt("Input options");

  in_opt.add_options()
    ("data,d", po::value< string >(), "Example Set")
    ("ring_size", po::value<size_t>(&(all->p->ring_size)), "size of example ring")
    ("examples", po::value<size_t>(&(all->max_examples)), "number of examples to parse")
    ("testonly,t", "Ignore label information and just test")
    ("daemon", "persistent daemon mode on port 26542")
    ("port", po::value<size_t>(),"port to listen on")
    ("num_children", po::value<size_t>(&(all->num_children)), "number of children for persistent daemon mode")
    ("pid_file", po::value< string >(), "Write pid file in persistent daemon mode")
    ("passes", po::value<size_t>(&(all->numpasses)),"Number of Training Passes")
    ("cache,c", "Use a cache.  The default is <data>.cache")
    ("cache_file", po::value< vector<string> >(), "The location(s) of cache_file.")
    ("kill_cache,k", "do not reuse existing cache: create a new one always")
    ("compressed", "use gzip format whenever possible. If a cache file is being created, this option creates a compressed cache file. A mixture of raw-text & compressed inputs are supported with autodetection.")
    ("no_stdin", "do not default to reading from stdin")
    ("save_resume", "save extra state so learning can be resumed later with new data")
    ;

  po::options_description out_opt("Output options");

  out_opt.add_options()
    ("audit,a", "print weights of features")
    ("predictions,p", po::value< string >(), "File to output predictions to")
    ("raw_predictions,r", po::value< string >(), "File to output unnormalized predictions to")
    ("sendto", po::value< vector<string> >(), "send examples to <host>")
    ("quiet", "Don't output disgnostics and progress updates")
    ("progress,P", po::value< string >(), "Progress update frequency. int: additive, float: multiplicative")
    ("binary", "report loss as binary classification on -1,1")
    ("min_prediction", po::value<float>(&(all->sd->min_label)), "Smallest prediction to output")
    ("max_prediction", po::value<float>(&(all->sd->max_label)), "Largest prediction to output")
    ;

  po::options_description update_opt("Update options");

  update_opt.add_options()
    ("sgd", "use regular stochastic gradient descent update.")
    ("hessian_on", "use second derivative in line search")
    ("bfgs", "use bfgs optimization")
    ("mem", po::value<int>(&(all->m)), "memory in bfgs")
    ("termination", po::value<float>(&(all->rel_threshold)),"Termination threshold")
    ("adaptive", "use adaptive, individual learning rates.")
    ("invariant", "use safe/importance aware updates.")
    ("normalized", "use per feature normalized updates")
    ("exact_adaptive_norm", "use current default invariant normalized adaptive update rule")
    ("conjugate_gradient", "use conjugate gradient based optimization")
    ("l1", po::value<float>(&(all->l1_lambda)), "l_1 lambda")
    ("l2", po::value<float>(&(all->l2_lambda)), "l_2 lambda")
    ("learning_rate,l", po::value<float>(&(all->eta)), "Set Learning Rate")
    ("loss_function", po::value<string>()->default_value("squared"), "Specify the loss function to be used, uses squared by default. Currently available ones are squared, classic, hinge, logistic and quantile.")
    ("quantile_tau", po::value<float>()->default_value(0.5), "Parameter \\tau associated with Quantile loss. Defaults to 0.5")
    ("power_t", po::value<float>(&(all->power_t)), "t power value")
    ("decay_learning_rate",    po::value<float>(&(all->eta_decay_rate)),
     "Set Decay factor for learning_rate between passes")
    ("initial_pass_length", po::value<size_t>(&(all->pass_length)), "initial number of examples per pass")
    ("initial_t", po::value<double>(&((all->sd->t))), "initial t value")
    ("feature_mask", po::value< string >(), "Use existing regressor to determine which parameters may be updated.  If no initial_regressor given, also used for initial weights.")
    ;

  po::options_description weight_opt("Weight options");

  weight_opt.add_options()
    ("bit_precision,b", po::value<size_t>(), "number of bits in the feature table")
    ("initial_regressor,i", po::value< vector<string> >(), "Initial regressor(s)")
    ("final_regressor,f", po::value< string >(), "Final regressor")
    ("initial_weight", po::value<float>(&(all->initial_weight)), "Set all weights to an initial value of 1.")
    ("random_weights", po::value<bool>(&(all->random_weights)), "make initial weights random")
    ("readable_model", po::value< string >(), "Output human-readable final regressor with numeric features")
    ("invert_hash", po::value< string >(), "Output human-readable final regressor with feature names")
    ("save_per_pass", "Save the model after every pass over data")
    ("input_feature_regularizer", po::value< string >(&(all->per_feature_regularizer_input)), "Per feature regularization input file")
    ("output_feature_regularizer_binary", po::value< string >(&(all->per_feature_regularizer_output)), "Per feature regularization output file")
    ("output_feature_regularizer_text", po::value< string >(&(all->per_feature_regularizer_text)), "Per feature regularization output file, in text")
    ;

  po::options_description holdout_opt("Holdout options");
  holdout_opt.add_options()
    ("holdout_off", "no holdout data in multiple passes")
    ("holdout_period", po::value<uint32_t>(&(all->holdout_period)), "holdout period for test only, default 10")
    ("holdout_after", po::value<uint32_t>(&(all->holdout_after)), "holdout after n training examples, default off (disables holdout_period)")
    ("early_terminate", po::value<size_t>(), "Specify the number of passes tolerated when holdout loss doesn't decrease before early termination, default is 3")
    ;

  po::options_description namespace_opt("Feature namespace options");
  namespace_opt.add_options()
    ("hash", po::value< string > (), "how to hash the features. Available options: strings, all")
    ("ignore", po::value< vector<unsigned char> >(), "ignore namespaces beginning with character <arg>")
    ("keep", po::value< vector<unsigned char> >(), "keep namespaces beginning with character <arg>")
    ("noconstant", "Don't add a constant feature")
    ("constant,C", po::value<float>(&(all->initial_constant)), "Set initial value of constant")
    ("sort_features", "turn this on to disregard order in which features have been defined. This will lead to smaller cache sizes")
    ("ngram", po::value< vector<string> >(), "Generate N grams")
    ("skips", po::value< vector<string> >(), "Generate skips in N grams. This in conjunction with the ngram tag can be used to generate generalized n-skip-k-gram.")
    ("affix", po::value<string>(), "generate prefixes/suffixes of features; argument '+2a,-3b,+1' means generate 2-char prefixes for namespace a, 3-char suffixes for b and 1 char prefixes for default namespace")
    ("spelling", po::value< vector<string> >(), "compute spelling features for a give namespace (use '_' for default namespace)");
    ;

  po::options_description mf_opt("Matrix factorization options");
  mf_opt.add_options()
    ("quadratic,q", po::value< vector<string> > (),
     "Create and use quadratic features")
    ("q:", po::value< string >(), ": corresponds to a wildcard for all printable characters")
    ("cubic", po::value< vector<string> > (),
     "Create and use cubic features")
    ("rank", po::value<uint32_t>(&(all->rank)), "rank for matrix factorization.")
    ("new_mf", "use new, reduction-based matrix factorization")
    ;

  po::options_description lrq_opt("Low Rank Quadratic options");
  lrq_opt.add_options()
    ("lrq", po::value<vector<string> > (), "use low rank quadratic features")
    ("lrqdropout", "use dropout training for low rank quadratic features")
    ;

  po::options_description multiclass_opt("Multiclass options");
  multiclass_opt.add_options()
    ("oaa", po::value<size_t>(), "Use one-against-all multiclass learning with <k> labels")
    ("ect", po::value<size_t>(), "Use error correcting tournament with <k> labels")
    ("csoaa", po::value<size_t>(), "Use one-against-all multiclass learning with <k> costs")
    ("wap", po::value<size_t>(), "Use weighted all-pairs multiclass learning with <k> costs")
    ("csoaa_ldf", po::value<string>(), "Use one-against-all multiclass learning with label dependent features.  Specify singleline or multiline.")
    ("wap_ldf", po::value<string>(), "Use weighted all-pairs multiclass learning with label dependent features.  Specify singleline or multiline.")
    ;

  po::options_description active_opt("Active Learning options");
  active_opt.add_options()
    ("active_learning", "active learning mode")
    ("active_simulation", "active learning simulation mode")
    ("active_mellowness", po::value<float>(&(all->active_c0)), "active learning mellowness parameter c_0. Default 8")
    ;

  po::options_description cluster_opt("Parallelization options");
  cluster_opt.add_options()
    ("span_server", po::value<string>(&(all->span_server)), "Location of server for setting up spanning tree")
    ("unique_id", po::value<size_t>(&(all->unique_id)),"unique id used for cluster parallel jobs")
    ("total", po::value<size_t>(&(all->total)),"total number of nodes used in cluster parallel job")
    ("node", po::value<size_t>(&(all->node)),"node number in cluster parallel job")
    ;

  po::options_description other_opt("Other options");
  other_opt.add_options()
    ("bs", po::value<size_t>(), "bootstrap mode with k rounds by online importance resampling")
    ("top", po::value<size_t>(), "top k recommendation")
    ("bs_type", po::value<string>(), "bootstrap mode - currently 'mean' or 'vote'")
    ("autolink", po::value<size_t>(), "create link function with polynomial d")
    ("cb", po::value<size_t>(), "Use contextual bandit learning with <k> costs")
    ("lda", po::value<uint32_t>(&(all->lda)), "Run lda with <int> topics")
    ("nn", po::value<size_t>(), "Use sigmoidal feedforward network with <k> hidden units")
    ("cbify", po::value<size_t>(), "Convert multiclass on <k> classes into a contextual bandit problem and solve")
    ("searn", po::value<size_t>(), "use searn, argument=maximum action id or 0 for LDF")
    ;

  // Declare the supported options.
  desc.add_options()
    ("help,h","Look here: http://hunch.net/~vw/ and click on Tutorial.")
    ("version","Version information")
    ("random_seed", po::value<size_t>(&random_seed), "seed random number generator")
    ("noop","do no learning")
    ("print","print examples");

  //po::positional_options_description p;
  // Be friendly: if -d was left out, treat positional param as data file
  //p.add("data", -1);

  desc.add(in_opt)
    .add(out_opt)
    .add(update_opt)
    .add(weight_opt)
    .add(holdout_opt)
    .add(namespace_opt)
    .add(mf_opt)
    .add(lrq_opt)
    .add(multiclass_opt)
    .add(active_opt)
    .add(cluster_opt)
    .add(other_opt);

  po::variables_map vm = po::variables_map();
  po::variables_map vm_file = po::variables_map(); //separate variable map for storing flags in regressor file

  po::parsed_options parsed = po::command_line_parser(argc, argv).
    style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing).
    options(desc).allow_unregistered().run();   // got rid of ".positional(p)" because it doesn't work well with unrecognized options
  vector<string> to_pass_further = po::collect_unrecognized(parsed.options, po::include_positional);
  string last_unrec_arg =
    (to_pass_further.size() > 0)
    ? string(to_pass_further[to_pass_further.size()-1])  // we want to write this down in case it's a data argument ala the positional option we got rid of
    : "";

  po::store(parsed, vm);
  po::notify(vm);

  if(all->numpasses > 1)
      all->holdout_set_off = false;

  if(vm.count("holdout_off"))
      all->holdout_set_off = true;

  if(!all->holdout_set_off && (vm.count("output_feature_regularizer_binary") || vm.count("output_feature_regularizer_text")))
  {
      all->holdout_set_off = true;
      cerr<<"Making holdout_set_off=true since output regularizer specified\n";
  }

  all->data_filename = "";

  all->searn = false;
  all->searnstr = NULL;

  all->sd->weighted_unlabeled_examples = all->sd->t;
  all->initial_t = (float)all->sd->t;

  if(all->initial_t > 0)
  {
    all->normalized_sum_norm_x = all->initial_t;//for the normalized update: if initial_t is bigger than 1 we interpret this as if we had seen (all->initial_t) previous fake datapoints all with norm 1
  }

  if (vm.count("help") || argc == 1) {
    /* upon direct query for help -- spit it out to stdout */
    cout << "\n" << desc << "\n";
    exit(0);
  }

  if (vm.count("quiet")) {
    all->quiet = true;
    // --quiet wins over --progress
  } else {
    all->quiet = false;

    if (vm.count("progress")) {
      string progress_str = vm["progress"].as<string>();
      all->progress_arg = (float)::atof(progress_str.c_str());

      // --progress interval is dual: either integer or floating-point
      if (progress_str.find_first_of(".") == string::npos) {
        // No "." in arg: assume integer -> additive
        all->progress_add = true;
        if (all->progress_arg < 1) {
          cerr    << "warning: additive --progress <int>"
                  << " can't be < 1: forcing to 1\n";
          all->progress_arg = 1;

        }
        all->sd->dump_interval = all->progress_arg;

      } else {
        // A "." in arg: assume floating-point -> multiplicative
        all->progress_add = false;

        if (all->progress_arg <= 1.0) {
          cerr    << "warning: multiplicative --progress <float>: "
                  << vm["progress"].as<string>()
                  << " is <= 1.0: adding 1.0\n";
          all->progress_arg += 1.0;

        } else if (all->progress_arg > 9.0) {
          cerr    << "warning: multiplicative --progress <float>"
                  << " is > 9.0: you probably meant to use an integer\n";
        }
        all->sd->dump_interval = 1.0;
      }
    }
  }

  msrand48(random_seed);

  if (vm.count("active_simulation"))
      all->active_simulation = true;

  if (vm.count("active_learning") && !all->active_simulation)
    all->active = true;

  if (vm.count("no_stdin"))
    all->stdin_off = true;

  if (vm.count("testonly") || all->eta == 0.)
    {
      if (!all->quiet)
	cerr << "only testing" << endl;
      all->training = false;
      if (all->lda > 0)
        all->eta = 0;
    }
  else
    all->training = true;

  if ( (vm.count("total") || vm.count("node") || vm.count("unique_id")) && !(vm.count("total") && vm.count("node") && vm.count("unique_id")) )
    {
      cout << "you must specificy unique_id, total, and node if you specify any" << endl;
      throw exception();
    }

  all->reg.stride = 4; //use stride of 4 for default invariant normalized adaptive updates
  //if the user specified anything in sgd,adaptive,invariant,normalized, we turn off default update rules and use whatever user specified
  if( (all->rank > 0 && !vm.count("new_mf")) || !all->training || ( ( vm.count("sgd") || vm.count("adaptive") || vm.count("invariant") || vm.count("normalized") ) && !vm.count("exact_adaptive_norm")) )
  {
    all->adaptive = all->training && vm.count("adaptive") && (all->rank == 0 && !vm.count("new_mf"));
    all->invariant_updates = all->training && vm.count("invariant");
    all->normalized_updates = all->training && vm.count("normalized") && (all->rank == 0 && !vm.count("new_mf"));

    all->reg.stride = 1;

    if( all->adaptive ) all->reg.stride *= 2;
    else all->normalized_idx = 1; //store per feature norm at 1 index offset from weight value instead of 2

    if( all->normalized_updates ) all->reg.stride *= 2;

    if(!vm.count("learning_rate") && !vm.count("l") && !(all->adaptive && all->normalized_updates))
      if (all->lda == 0)
        all->eta = 10; //default learning rate to 10 for non default update rule

    //if not using normalized or adaptive, default initial_t to 1 instead of 0
    if(!all->adaptive && !all->normalized_updates && !vm.count("initial_t")) {
      all->sd->t = 1.f;
      all->sd->weighted_unlabeled_examples = 1.f;
      all->initial_t = 1.f;
    }
    if (vm.count("feature_mask")){
      if(all->reg.stride == 1){
        all->reg.stride *= 2;//if --sgd, stride->2 and use the second position as mask
        all->feature_mask_idx = 1;
      }
      else if(all->reg.stride == 2){
        all->reg.stride *= 2;//if either normalized or adaptive, stride->4, mask_idx is still 3
      }
    }
  }

  if (all->l1_lambda < 0.) {
    cerr << "l1_lambda should be nonnegative: resetting from " << all->l1_lambda << " to 0" << endl;
    all->l1_lambda = 0.;
  }
  if (all->l2_lambda < 0.) {
    cerr << "l2_lambda should be nonnegative: resetting from " << all->l2_lambda << " to 0" << endl;
    all->l2_lambda = 0.;
  }
  all->reg_mode += (all->l1_lambda > 0.) ? 1 : 0;
  all->reg_mode += (all->l2_lambda > 0.) ? 2 : 0;
  if (!all->quiet)
    {
      if (all->reg_mode %2 && !vm.count("bfgs"))
	cerr << "using l1 regularization = " << all->l1_lambda << endl;
      if (all->reg_mode > 1)
	cerr << "using l2 regularization = " << all->l2_lambda << endl;
    }

  all->l = GD::setup(*all, vm); //debug: GD setup
  all->scorer = all->l;

  if (vm.count("bfgs") || vm.count("conjugate_gradient"))
    all->l = BFGS::setup(*all, to_pass_further, vm, vm_file);

  if (vm.count("version") || argc == 1) {
    /* upon direct query for version -- spit it out to stdout */
    cout << version.to_string() << "\n";
    exit(0);
  }


  if(vm.count("ngram")){
    if(vm.count("sort_features"))
      {
	cerr << "ngram is incompatible with sort_features.  " << endl;
	throw exception();
      }

    all->ngram_strings = vm["ngram"].as< vector<string> >();
    compile_gram(all->ngram_strings, all->ngram, (char*)"grams", all->quiet);
  }

  if(vm.count("skips"))
    {
      if(!vm.count("ngram"))
	{
	  cout << "You can not skip unless ngram is > 1" << endl;
	  throw exception();
	}

      all->skip_strings = vm["skips"].as<vector<string> >();
      compile_gram(all->skip_strings, all->skips, (char*)"skips", all->quiet);
    }

  if (vm.count("spelling")) {
    vector<string> spelling_ns = vm["spelling"].as< vector<string> >();
    for (size_t id=0; id<spelling_ns.size(); id++)
      if (spelling_ns[id][0] == '_') all->spelling_features[(unsigned char)' '] = true;
      else all->spelling_features[(size_t)spelling_ns[id][0]] = true;
  }

  if (vm.count("bit_precision"))
    {
      all->default_bits = false;
      all->num_bits = (uint32_t)vm["bit_precision"].as< size_t>();
      if (all->num_bits > min(32, sizeof(size_t)*8 - 3))
	{
	  cout << "Only " << min(32, sizeof(size_t)*8 - 3) << " or fewer bits allowed.  If this is a serious limit, speak up." << endl;
	  throw exception();
	}
    }

  if (vm.count("daemon") || vm.count("pid_file") || (vm.count("port") && !all->active) ) {
    all->daemon = true;

    // allow each child to process up to 1e5 connections
    all->numpasses = (size_t) 1e5;
  }

  if (vm.count("compressed"))
      set_compressed(all->p);

  if (vm.count("data")) {
    all->data_filename = vm["data"].as<string>();
    if (ends_with(all->data_filename, ".gz"))
      set_compressed(all->p);
  } else {
    all->data_filename = "";
  }

  if(vm.count("sort_features"))
    all->p->sort_features = true;

  if (vm.count("quadratic"))
    {
      all->pairs = vm["quadratic"].as< vector<string> >();
      vector<string> newpairs;
      //string tmp;
      char printable_start = '!';
      char printable_end = '~';
      int valid_ns_size = printable_end - printable_start - 1; //will skip two characters

      if(!all->quiet)
        cerr<<"creating quadratic features for pairs: ";

      for (vector<string>::iterator i = all->pairs.begin(); i != all->pairs.end();i++){
        if(!all->quiet){
          cerr << *i << " ";
          if (i->length() > 2)
            cerr << endl << "warning, ignoring characters after the 2nd.\n";
          if (i->length() < 2) {
            cerr << endl << "error, quadratic features must involve two sets.\n";
            throw exception();
          }
        }
        //-q x:
        if((*i)[0]!=':'&&(*i)[1]==':'){
          newpairs.reserve(newpairs.size() + valid_ns_size);
          for (char j=printable_start; j<=printable_end; j++){
            if(valid_ns(j))
              newpairs.push_back(string(1,(*i)[0])+j);
          }
        }
        //-q :x
        else if((*i)[0]==':'&&(*i)[1]!=':'){
          newpairs.reserve(newpairs.size() + valid_ns_size);
          for (char j=printable_start; j<=printable_end; j++){
            if(valid_ns(j)){
	      stringstream ss;
	      ss << j << (*i)[1];
	      newpairs.push_back(ss.str());
	    }
          }
        }
        //-q ::
        else if((*i)[0]==':'&&(*i)[1]==':'){
          newpairs.reserve(newpairs.size() + valid_ns_size*valid_ns_size);
          for (char j=printable_start; j<=printable_end; j++){
            if(valid_ns(j)){
              for (char k=printable_start; k<=printable_end; k++){
                if(valid_ns(k)){
		  stringstream ss;
                  ss << j << k;
                  newpairs.push_back(ss.str());
		}
              }
            }
          }
        }
        else{
          newpairs.push_back(string(*i));
        }
      }
      newpairs.swap(all->pairs);
      if(!all->quiet)
        cerr<<endl;
    }

  if (vm.count("cubic"))
    {
      all->triples = vm["cubic"].as< vector<string> >();
      if (!all->quiet)
	{
	  cerr << "creating cubic features for triples: ";
	  for (vector<string>::iterator i = all->triples.begin(); i != all->triples.end();i++) {
	    cerr << *i << " ";
	    if (i->length() > 3)
	      cerr << endl << "warning, ignoring characters after the 3rd.\n";
	    if (i->length() < 3) {
	      cerr << endl << "error, cubic features must involve three sets.\n";
	      throw exception();
	    }
	  }
	  cerr << endl;
	}
    }

  io_buf io_temp;
  parse_regressor_args(*all, vm, io_temp);

  //parse flags from regressor file
  all->options_from_file_argv = VW::get_argv_from_string(all->options_from_file,all->options_from_file_argc);

  po::parsed_options parsed_file = po::command_line_parser(all->options_from_file_argc, all->options_from_file_argv).
    style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing).
    options(desc).allow_unregistered().run();

  po::store(parsed_file, vm_file);
  po::notify(vm_file);

  for (size_t i = 0; i < 256; i++)
    all->ignore[i] = false;
  all->ignore_some = false;
  
  if (vm.count("ignore"))
    {
      all->ignore_some = true;

      vector<unsigned char> ignore = vm["ignore"].as< vector<unsigned char> >();
      for (vector<unsigned char>::iterator i = ignore.begin(); i != ignore.end();i++)
	{
	  all->ignore[*i] = true;
	}
      if (!all->quiet)
	{
	  cerr << "ignoring namespaces beginning with: ";
	  for (vector<unsigned char>::iterator i = ignore.begin(); i != ignore.end();i++)
	    cerr << *i << " ";

	  cerr << endl;
	}
    }

  if (vm.count("keep"))
    {
      for (size_t i = 0; i < 256; i++)
        all->ignore[i] = true;

      all->ignore_some = true;

      vector<unsigned char> keep = vm["keep"].as< vector<unsigned char> >();
      for (vector<unsigned char>::iterator i = keep.begin(); i != keep.end();i++)
	{
	  all->ignore[*i] = false;
	}
      if (!all->quiet)
	{
	  cerr << "using namespaces beginning with: ";
	  for (vector<unsigned char>::iterator i = keep.begin(); i != keep.end();i++)
	    cerr << *i << " ";

	  cerr << endl;
	}
    }

  // (non-reduction) matrix factorization enabled
  if (!vm.count("new_mf") && all->rank > 0) {
    // store linear + 2*rank weights per index, round up to power of two
    float temp = ceilf(logf((float)(all->rank*2+1)) / logf (2.f));
    all->reg.stride = 1 << (int) temp;
    all->random_weights = true;

    if ( vm.count("adaptive") )
      {
	cerr << "adaptive is not implemented for matrix factorization" << endl;
        throw exception();
      }
    if ( vm.count("normalized") )
      {
	cerr << "normalized is not implemented for matrix factorization" << endl;
        throw exception();
      }
    if ( vm.count("exact_adaptive_norm") )
      {
	cerr << "normalized adaptive updates is not implemented for matrix factorization" << endl;
        throw exception();
      }
    if (vm.count("bfgs") || vm.count("conjugate_gradient"))
      {
	cerr << "bfgs is not implemented for matrix factorization" << endl;
	throw exception();
      }	

    //default initial_t to 1 instead of 0
    if(!vm.count("initial_t")) {
      all->sd->t = 1.f;
      all->sd->weighted_unlabeled_examples = 1.f;
      all->initial_t = 1.f;
    }
  }

  if (vm.count("noconstant"))
    all->add_constant = false;

  //if (vm.count("nonormalize"))
  //  all->nonormalize = true;

  if (vm.count("lda"))
    all->l = LDA::setup(*all, to_pass_further, vm);

  if (!vm.count("lda") && !all->adaptive && !all->normalized_updates)
    all->eta *= powf((float)(all->sd->t), all->power_t);

  if (vm.count("readable_model"))
    all->text_regressor_name = vm["readable_model"].as<string>();

  if (vm.count("invert_hash")){
    all->inv_hash_regressor_name = vm["invert_hash"].as<string>();

    all->hash_inv = true;
  }

  if (vm.count("save_per_pass"))
    all->save_per_pass = true;

  if (vm.count("save_resume"))
    all->save_resume = true;

  if (vm.count("min_prediction"))
    all->sd->min_label = vm["min_prediction"].as<float>();
  if (vm.count("max_prediction"))
    all->sd->max_label = vm["max_prediction"].as<float>();
  if (vm.count("min_prediction") || vm.count("max_prediction") || vm.count("testonly"))
    all->set_minmax = noop_mm;

  string loss_function;
  if(vm.count("loss_function"))
    loss_function = vm["loss_function"].as<string>();
  else
    loss_function = "squaredloss";
  float loss_parameter = 0.0;
  if(vm.count("quantile_tau"))
    loss_parameter = vm["quantile_tau"].as<float>();

  if (vm.count("noop"))
    all->l = NOOP::setup(*all);

  if (vm.count("print"))
    {
      all->l = PRINT::setup(*all);
      all->reg.stride = 1;
    }

  if (!vm.count("new_mf") && all->rank > 0)
    all->l = GDMF::setup(*all);

  all->loss = getLossFunction(all, loss_function, (float)loss_parameter);

  if (pow((double)all->eta_decay_rate, (double)all->numpasses) < 0.0001 )
    cerr << "Warning: the learning rate for the last pass is multiplied by: " << pow((double)all->eta_decay_rate, (double)all->numpasses)
	 << " adjust --decay_learning_rate larger to avoid this." << endl;

  if (!all->quiet)
    {
      cerr << "Num weight bits = " << all->num_bits << endl;
      cerr << "learning rate = " << all->eta << endl;
      cerr << "initial_t = " << all->sd->t << endl;
      cerr << "power_t = " << all->power_t << endl;
      if (all->numpasses > 1)
	cerr << "decay_learning_rate = " << all->eta_decay_rate << endl;
      if (all->rank > 0)
	cerr << "rank = " << all->rank << endl;
    }

  if (vm.count("predictions")) {
    if (!all->quiet)
      cerr << "predictions = " <<  vm["predictions"].as< string >() << endl;
    if (strcmp(vm["predictions"].as< string >().c_str(), "stdout") == 0)
      {
	all->final_prediction_sink.push_back((size_t) 1);//stdout
      }
    else
      {
	const char* fstr = (vm["predictions"].as< string >().c_str());
	int f;
#ifdef _WIN32
	_sopen_s(&f, fstr, _O_CREAT|_O_WRONLY|_O_BINARY|_O_TRUNC, _SH_DENYWR, _S_IREAD|_S_IWRITE);
#else
	f = open(fstr, O_CREAT|O_WRONLY|O_LARGEFILE|O_TRUNC,0666);
#endif
	if (f < 0)
	  cerr << "Error opening the predictions file: " << fstr << endl;
	all->final_prediction_sink.push_back((size_t) f);
      }
  }

  if (vm.count("raw_predictions")) {
    if (!all->quiet) {
      cerr << "raw predictions = " <<  vm["raw_predictions"].as< string >() << endl;
      if (vm.count("binary") || vm_file.count("binary"))
        cerr << "Warning: --raw has no defined value when --binary specified, expect no output" << endl;
    }
    if (strcmp(vm["raw_predictions"].as< string >().c_str(), "stdout") == 0)
      all->raw_prediction = 1;//stdout
    else
	{
	  const char* t = vm["raw_predictions"].as< string >().c_str();
	  int f;
#ifdef _WIN32
	  _sopen_s(&f, t, _O_CREAT|_O_WRONLY|_O_BINARY|_O_TRUNC, _SH_DENYWR, _S_IREAD|_S_IWRITE);
#else
	  f = open(t, O_CREAT|O_WRONLY|O_LARGEFILE|O_TRUNC,0666);
#endif
	  all->raw_prediction = f;
	}
  }

  if (vm.count("audit")){
    all->audit = true;
  }

  if (vm.count("sendto"))
    all->l = SENDER::setup(*all, vm, all->pairs);

  // Need to see if we have to load feature mask first or second.
  // -i and -mask are from same file, load -i file first so mask can use it
  if (vm.count("feature_mask") && vm.count("initial_regressor")
      && vm["feature_mask"].as<string>() == vm["initial_regressor"].as< vector<string> >()[0]) {
    // load rest of regressor
    all->l->save_load(io_temp, true, false);
    io_temp.close_file();

    // set the mask, which will reuse -i file we just loaded
    parse_mask_regressor_args(*all, vm);
  }
  else {
    // load mask first
    parse_mask_regressor_args(*all, vm);

    // load rest of regressor
    all->l->save_load(io_temp, true, false);
    io_temp.close_file();
  }

  bool got_mc = false;
  bool got_cs = false;
  bool got_cb = false;

  if(vm.count("nn") || vm_file.count("nn") )
    all->l = NN::setup(*all, to_pass_further, vm, vm_file);

  if (vm.count("new_mf") && all->rank > 0)
    all->l = MF::setup(*all, vm);

  if(vm.count("autolink") || vm_file.count("autolink") )
    all->l = ALINK::setup(*all, to_pass_further, vm, vm_file);

  if (vm.count("lrq") || vm_file.count("lrq"))
    all->l = LRQ::setup(*all, to_pass_further, vm, vm_file);

  all->l = Scorer::setup(*all, to_pass_further, vm, vm_file);

  if(vm.count("top") || vm_file.count("top") )
    all->l = TOPK::setup(*all, to_pass_further, vm, vm_file);

  if (vm.count("binary") || vm_file.count("binary"))
    all->l = BINARY::setup(*all, to_pass_further, vm, vm_file);

  if(vm.count("oaa") || vm_file.count("oaa") ) {
    if (got_mc) { cerr << "error: cannot specify multiple MC learners" << endl; throw exception(); }

    all->l = OAA::setup(*all, to_pass_further, vm, vm_file);
    got_mc = true;
  }

  if (vm.count("ect") || vm_file.count("ect") ) {
    if (got_mc) { cerr << "error: cannot specify multiple MC learners" << endl; throw exception(); }

    all->l = ECT::setup(*all, to_pass_further, vm, vm_file);
    got_mc = true;
  }

  if(vm.count("csoaa") || vm_file.count("csoaa") ) {
    if (got_cs) { cerr << "error: cannot specify multiple CS learners" << endl; throw exception(); }

    all->l = CSOAA::setup(*all, to_pass_further, vm, vm_file);
    all->cost_sensitive = all->l;
    got_cs = true;
  }

  if(vm.count("wap") || vm_file.count("wap") ) {
    if (got_cs) { cerr << "error: cannot specify multiple CS learners" << endl; throw exception(); }

    all->l = WAP::setup(*all, to_pass_further, vm, vm_file);
    all->cost_sensitive = all->l;
    got_cs = true;
  }

  if(vm.count("csoaa_ldf") || vm_file.count("csoaa_ldf")) {
    if (got_cs) { cerr << "error: cannot specify multiple CS learners" << endl; throw exception(); }

    all->l = CSOAA_AND_WAP_LDF::setup(*all, to_pass_further, vm, vm_file);
    all->cost_sensitive = all->l;
    got_cs = true;
  }

  if(vm.count("wap_ldf") || vm_file.count("wap_ldf") ) {
    if (got_cs) { cerr << "error: cannot specify multiple CS learners" << endl; throw exception(); }

    all->l = CSOAA_AND_WAP_LDF::setup(*all, to_pass_further, vm, vm_file);
    all->cost_sensitive = all->l;
    got_cs = true;
  }

  if( vm.count("cb") || vm_file.count("cb") )
  {
    if(!got_cs) {
      if( vm_file.count("cb") ) vm.insert(pair<string,po::variable_value>(string("csoaa"),vm_file["cb"]));
      else vm.insert(pair<string,po::variable_value>(string("csoaa"),vm["cb"]));

      all->l = CSOAA::setup(*all, to_pass_further, vm, vm_file);  // default to CSOAA unless wap is specified
      all->cost_sensitive = all->l;
      got_cs = true;
    }

    all->l = CB_ALGS::setup(*all, to_pass_further, vm, vm_file);
    got_cb = true;
  }

  if (vm.count("cbify") || vm_file.count("cbify"))
    {
      if(!got_cs) {
	if( vm_file.count("cbify") ) vm.insert(pair<string,po::variable_value>(string("csoaa"),vm_file["cbify"]));
	else vm.insert(pair<string,po::variable_value>(string("csoaa"),vm["cbify"]));
	
	all->l = CSOAA::setup(*all, to_pass_further, vm, vm_file);  // default to CSOAA unless wap is specified
	all->cost_sensitive = all->l;
	got_cs = true;
      }

      if (!got_cb) {
	if( vm_file.count("cbify") ) vm.insert(pair<string,po::variable_value>(string("cb"),vm_file["cbify"]));
	else vm.insert(pair<string,po::variable_value>(string("cb"),vm["cbify"]));
	all->l = CB_ALGS::setup(*all, to_pass_further, vm, vm_file);
	got_cb = true;
      }

      all->l = CBIFY::setup(*all, to_pass_further, vm, vm_file);
    }

  
  if (vm_file.count("affix") && vm.count("affix")) {
    cerr << "should not specify --affix when loading a model trained with affix features (they're turned on by default)" << endl;
    throw exception();
  }
  if (vm_file.count("affix"))
    parse_affix_argument(*all, vm_file["affix"].as<string>());
  if (vm.count("affix")) {
    parse_affix_argument(*all, vm["affix"].as<string>());
    stringstream ss;
    ss << " --affix " << vm["affix"].as<string>();
    all->options_from_file.append(ss.str());
  }

  if (vm.count("searn") || vm_file.count("searn") ) {
    if (!got_cs && !got_cb) {
      if( vm_file.count("searn") ) vm.insert(pair<string,po::variable_value>(string("csoaa"),vm_file["searn"]));
      else vm.insert(pair<string,po::variable_value>(string("csoaa"),vm["searn"]));

      all->l = CSOAA::setup(*all, to_pass_further, vm, vm_file);  // default to CSOAA unless others have been specified
      all->cost_sensitive = all->l;
      got_cs = true;
    }
    //all->searnstr = (Searn::searn*)calloc(1, sizeof(Searn::searn));
    all->l = Searn::setup(*all, to_pass_further, vm, vm_file);
  }

  if (got_cb && got_mc) {
    cerr << "error: doesn't make sense to do both MC learning and CB learning" << endl;
    throw exception();
  }

  if(vm.count("bs") || vm_file.count("bs") )
    all->l = BS::setup(*all, to_pass_further, vm, vm_file);

  if (to_pass_further.size() > 0) {
    bool is_actually_okay = false;

    // special case to try to emulate the missing -d
    if ((to_pass_further.size() == 1) &&
        (to_pass_further[to_pass_further.size()-1] == last_unrec_arg)) {

      int f = io_buf().open_file(last_unrec_arg.c_str(), all->stdin_off, io_buf::READ);
      if (f != -1) {
#ifdef _WIN32
		 _close(f);
#else
		  close(f);
#endif
        //cerr << "warning: final argument '" << last_unrec_arg << "' assumed to be input file; in the future, please use -d" << endl;
        all->data_filename = last_unrec_arg;
        if (ends_with(last_unrec_arg, ".gz"))
          set_compressed(all->p);
        is_actually_okay = true;
      }
    }

    if (!is_actually_okay) {
      cerr << "unrecognized options:";
      for (size_t i=0; i<to_pass_further.size(); i++)
        cerr << " " << to_pass_further[i];
      cerr << endl;
      throw exception();
    }
  }

  parse_source_args(*all, vm, all->quiet,all->numpasses);

  // force wpp to be a power of 2 to avoid 32-bit overflow
  uint32_t i = 0;
  size_t params_per_problem = all->l->increment;
  while (params_per_problem > (uint32_t)(1 << i))
    i++;
  all->wpp = (1 << i) / all->reg.stride;

  return all;
}

namespace VW {
  void cmd_string_replace_value( string& cmd, string flag_to_replace, string new_value )
  {
    flag_to_replace.append(" "); //add a space to make sure we obtain the right flag in case 2 flags start with the same set of characters
    size_t pos = cmd.find(flag_to_replace);
    if( pos == string::npos ) {
      //flag currently not present in command string, so just append it to command string
      cmd.append(" ");
      cmd.append(flag_to_replace);
      cmd.append(new_value);
    }
    else {
      //flag is present, need to replace old value with new value

      //compute position after flag_to_replace
      pos += flag_to_replace.size();

      //now pos is position where value starts
      //find position of next space
      size_t pos_after_value = cmd.find(" ",pos);
      if(pos_after_value == string::npos) {
        //we reach the end of the string, so replace the all characters after pos by new_value
        cmd.replace(pos,cmd.size()-pos,new_value);
      }
      else {
        //replace characters between pos and pos_after_value by new_value
        cmd.replace(pos,pos_after_value-pos,new_value);
      }
    }
  }

  char** get_argv_from_string(string s, int& argc)
  {
    char* c = (char*)calloc(s.length()+3, sizeof(char));
    c[0] = 'b';
    c[1] = ' ';
    strcpy(c+2, s.c_str());
    substring ss = {c, c+s.length()+2};
    v_array<substring> foo;
    foo.end_array = foo.begin = foo.end = NULL;
    tokenize(' ', ss, foo);

    char** argv = (char**)calloc(foo.size(), sizeof(char*));
    for (size_t i = 0; i < foo.size(); i++)
      {
	*(foo[i].end) = '\0';
	argv[i] = (char*)calloc(foo[i].end-foo[i].begin+1, sizeof(char));
        sprintf(argv[i],"%s",foo[i].begin);
      }

    argc = (int)foo.size();
    free(c);
    foo.delete_v();
    return argv;
  }

  vw* initialize(string s)
  {
    int argc = 0;
    s += " --no_stdin";
    char** argv = get_argv_from_string(s,argc);

    vw* all = parse_args(argc, argv);
    
    initialize_parser_datastructures(*all);

    for(int i = 0; i < argc; i++)
      free(argv[i]);
    free (argv);

    return all;
  }

  void finish(vw& all)
  {
    finalize_regressor(all, all.final_regressor_name);
    all.l->finish();
    delete all.l;
    if (all.reg.weight_vector != NULL)
      free(all.reg.weight_vector);
    free_parser(all);
    finalize_source(all.p);
    all.p->parse_name.erase();
    all.p->parse_name.delete_v();
    free(all.p);
    free(all.sd);
    for (int i = 0; i < all.options_from_file_argc; i++)
      free(all.options_from_file_argv[i]);
    free(all.options_from_file_argv);
    for (size_t i = 0; i < all.final_prediction_sink.size(); i++)
      if (all.final_prediction_sink[i] != 1)
	io_buf::close_file_or_socket(all.final_prediction_sink[i]);
    all.final_prediction_sink.delete_v();
    delete all.loss;
    delete &all;
  }
}
