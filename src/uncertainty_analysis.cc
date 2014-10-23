/// @file uncertainty_analysis.cc
/// Implements the functionality to run Monte Carlo uncertainty analysis.
#include "uncertainty_analysis.h"

#include <ctime>
#include <cmath>

#include <boost/algorithm/string.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/accumulators/statistics/density.hpp>

#include "error.h"

namespace scram {

UncertaintyAnalysis::UncertaintyAnalysis(int nsums, double cut_off,
                                         int num_trials)
    : mean_(-1),
      sigma_(-1),
      warnings_(""),
      p_time_(-1) {
  // Check for right number of sums.
  if (nsums < 1) {
    std::string msg = "The number of sums in the probability calculation "
                      "cannot be less than one";
    throw InvalidArgument(msg);
  }

  // Check for valid cut-off probability.
  if (cut_off < 0 || cut_off > 1) {
    std::string msg = "The cut-off probability cannot be negative or"
                      " more than 1.";
    throw InvalidArgument(msg);
  }
  cut_off_ = cut_off;

  if (num_trials < 1) {
    std::string msg = "The number of trials for uncertainty analysis cannot"
                      " be fewer than 1.";
    throw InvalidArgument(msg);
  }
  num_trials_ = num_trials;

  nsums_ = nsums;
}

void UncertaintyAnalysis::UpdateDatabase(
    const boost::unordered_map<std::string, BasicEventPtr>& basic_events) {
  basic_events_ = basic_events;
  UncertaintyAnalysis::AssignIndices();
}

void UncertaintyAnalysis::Analyze(
    const std::set< std::set<std::string> >& min_cut_sets) {
  min_cut_sets_ = min_cut_sets;

  UncertaintyAnalysis::IndexMcs(min_cut_sets_);

  std::set< std::set<int> > iset;

  std::vector< std::set<int> >::const_iterator it_min;
  for (it_min = imcs_.begin(); it_min != imcs_.end(); ++it_min) {
    // Calculate a probability of a set with AND relationship.
    double p_sub_set = UncertaintyAnalysis::ProbAnd(*it_min);
    if (p_sub_set > cut_off_) {
      std::set<int> mcs(*it_min);
      iset.insert(mcs);
    }
  }

  // Timing Initialization
  std::clock_t start_time;
  start_time = std::clock();

  // Maximum number of sums in the series.
  if (nsums_ > iset.size()) nsums_ = iset.size();
  // Generate the equation.
  UncertaintyAnalysis::ProbOr(1, nsums_, &iset);
  // Sample probabilities and generate data.
  UncertaintyAnalysis::Sample();

  // Perform statistical analysis.
  UncertaintyAnalysis::CalculateStatistics();

  // Duration of the calculations.
  p_time_ = (std::clock() - start_time) / static_cast<double>(CLOCKS_PER_SEC);
}

void UncertaintyAnalysis::AssignIndices() {
  // Cleanup the previous information.
  int_to_basic_.clear();
  basic_to_int_.clear();
  // Indexation of events.
  int j = 1;
  boost::unordered_map<std::string, BasicEventPtr>::iterator itp;
  // Dummy basic event at index 0.
  int_to_basic_.push_back(BasicEventPtr(new BasicEvent("dummy")));
  iprobs_.push_back(0);
  for (itp = basic_events_.begin(); itp != basic_events_.end(); ++itp) {
    int_to_basic_.push_back(itp->second);
    basic_to_int_.insert(std::make_pair(itp->second->id(), j));
    iprobs_.push_back(itp->second->p());
    ++j;
  }
}

void UncertaintyAnalysis::IndexMcs(
    const std::set<std::set<std::string> >& min_cut_sets) {
  // Update databases of minimal cut sets with indexed events.
  std::set< std::set<std::string> >::const_iterator it;
  for (it = min_cut_sets.begin(); it != min_cut_sets.end(); ++it) {
    std::set<int> mcs_with_indices;  // Minimal cut set with indices.
    std::set<std::string>::const_iterator it_set;
    for (it_set = it->begin(); it_set != it->end(); ++it_set) {
      std::vector<std::string> names;
      boost::split(names, *it_set, boost::is_any_of(" "),
                   boost::token_compress_on);
      assert(names.size() == 1 || names.size() == 2);
      if (names.size() == 1) {
        assert(basic_to_int_.count(names[0]));
        mcs_with_indices.insert(basic_to_int_.find(names[0])->second);
      } else {
        // This must be a complement of an event.
        assert(names[0] == "not");
        assert(basic_to_int_.count(names[1]));
        mcs_with_indices.insert(-basic_to_int_.find(names[1])->second);
      }
    }
    imcs_.push_back(mcs_with_indices);
  }
}

void UncertaintyAnalysis::CombineElAndSet(
    const std::set<int>& el,
    const std::set< std::set<int> >& set,
    std::set< std::set<int> >* combo_set) {
  std::set< std::set<int> >::iterator it_set;
  for (it_set = set.begin(); it_set != set.end(); ++it_set) {
    bool include = true;  // Indicates that the resultant set is not null.
    std::set<int>::iterator it;
    for (it = el.begin(); it != el.end(); ++it) {
      if (it_set->count(-*it)) {
        include = false;
        break;  // A complement is found; the set is null.
      }
    }
    if (include) {
      std::set<int> member_set(*it_set);
      member_set.insert(el.begin(), el.end());
      combo_set->insert(combo_set->end(), member_set);
    }
  }
}

// Generation of the representation of the original equation.
void UncertaintyAnalysis::ProbOr(int sign, int nsums,
                                 std::set< std::set<int> >* min_cut_sets) {
  assert(sign != 0);
  assert(nsums >= 0);

  // Recursive implementation.
  if (min_cut_sets->empty()) return;

  if (nsums == 0) return;

  // Get one element.
  std::set< std::set<int> >::iterator it = min_cut_sets->begin();
  std::set<int> element_one = *it;

  // Delete element from the original set. WARNING: the iterator is invalidated.
  min_cut_sets->erase(it);

  // Put this element into the equation.
  if (sign > 0) {
    // This is a positive member.
    pos_terms_.push_back(element_one);
  } else {
    // This must be a negative member.
    neg_terms_.push_back(element_one);
  }

  std::set< std::set<int> > combo_sets;
  UncertaintyAnalysis::CombineElAndSet(element_one, *min_cut_sets, &combo_sets);
  UncertaintyAnalysis::ProbOr(sign, nsums, min_cut_sets);
  UncertaintyAnalysis::ProbOr(-sign, nsums - 1, &combo_sets);
}

void UncertaintyAnalysis::Sample() {
  for (int i = 0; i < num_trials_; ++i) {
    // Reset distributions.
    std::vector<BasicEventPtr>::iterator it_b = int_to_basic_.begin();
    // The first element is dummy.
    for (++it_b; it_b != int_to_basic_.end(); ++it_b) {
      (*it_b)->Reset();
    }
    // Sample all basic events.
    /// @todo Sample only events that are in the minimal cut sets.
    /// @todo Do not sample constant values(i.e. events without distributions.)
    it_b = int_to_basic_.begin();
    for (++it_b; it_b != int_to_basic_.end(); ++it_b) {
      double prob = (*it_b)->SampleProbability();
      if (prob < 0) {
        prob = 0;
        if (warnings_ == "")
          warnings_ = "Invalid probability was sampled but adjusted to"
              " proper boundaries of 0 and 1.";
      } else if (prob > 1) {
        prob = 1;
        if (warnings_ == "")
          warnings_ = "Invalid probability was sampled but adjusted to"
              " proper boundaries of 0 and 1.";
      }
      iprobs_[basic_to_int_.find((*it_b)->id())->second] = prob;
    }
    double pos = 0;
    std::vector< std::set<int> >::iterator it_s;
    for (it_s = pos_terms_.begin(); it_s != pos_terms_.end(); ++it_s) {
      pos += UncertaintyAnalysis::ProbAnd(*it_s);
    }
    double neg = 0;
    for (it_s = neg_terms_.begin(); it_s != neg_terms_.end(); ++it_s) {
      neg += UncertaintyAnalysis::ProbAnd(*it_s);
    }
    sampled_results_.push_back(pos - neg);
  }
}

double UncertaintyAnalysis::ProbAnd(const std::set<int>& min_cut_set) {
  // Test just in case the min cut set is empty.
  if (min_cut_set.empty()) return 0;

  double p_sub_set = 1;  // 1 is for multiplication.
  std::set<int>::iterator it_set;
  for (it_set = min_cut_set.begin(); it_set != min_cut_set.end(); ++it_set) {
    if (*it_set > 0) {
      p_sub_set *= iprobs_[*it_set];
    } else {
      p_sub_set *= 1 - iprobs_[std::abs(*it_set)];  // Never zero.
    }
  }
  return p_sub_set;
}

void UncertaintyAnalysis::CalculateStatistics() {
  using namespace boost;
  using namespace boost::accumulators;
  accumulator_set<double, stats<tag::mean, tag::variance, tag::density> >
      acc(tag::density::num_bins = 20, tag::density::cache_size = num_trials_);
  std::vector<double>::iterator it;
  for (it = sampled_results_.begin(); it != sampled_results_.end(); ++it) {
    acc(*it);
  }
  typedef iterator_range<std::vector<std::pair<double, double> >::iterator >
      histogram_type;
  histogram_type hist = density(acc);
  for (int i = 0; i < hist.size(); i++) {
    distribution_.push_back(hist[i]);
  }
  mean_ = boost::accumulators::mean(acc);
  sigma_ = std::sqrt(variance(acc));
  confidence_interval_.first = mean_ - sigma_ * 1.96 / std::sqrt(num_trials_);
  confidence_interval_.second = mean_ + sigma_ * 1.96 / std::sqrt(num_trials_);
}

}  // namespace scram
