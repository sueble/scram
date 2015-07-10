/// @file mocus.cc
/// Implementation of the MOCUS algorithm.
/// It is assumed that the tree is layered with OR and AND gates on each
/// level. That is, one level contains only AND or OR gates.
/// The function assumes the tree contains only positive gates.
///
/// The description of the algorithm.
///
/// Turn all existing gates in the tree into simple gates
/// with pointers to the child gates but not modules.
/// Leave minimal cut set modules to the last moment till all the gates
/// are operated. Those modules' minimal cut sets can be joined without
/// additional check for minimality.
///
/// Operate on each module starting from the top gate.
/// For now, it is assumed that a module cannot be unity, this means that
/// a module will at least add a new event into a cut set, so size of
/// a cut set with modules is a minimum number of members in the set.
/// This will fail if there is unity case but will hold if the module is
/// null because the cut set will be deleted anyway.
///
/// Upon walking from top to children gates, there are two types: OR and AND.
/// The generated sets are passed to child gates, which use the passed set
/// to generate new sets. AND gate will simply add its basic events and
/// modules to the set and pass the resultant sets into its OR child, which
/// will generate a lot more sets. These generated sets are passed to the
/// next gate child to generate even more.
///
/// For OR gates, the passed set is checked to have basic events of the gate.
/// If so, this is a local minimum cut set, so generation of the sets stops
/// on this gate. No new sets should be generated in this case. This condition
/// is also applicable if the child AND gate keeps the input set as output and
/// generates only additional supersets.
///
/// The generated sets are kept unique by storing them in a set.
#include "mocus.h"

#include <utility>

#include "indexed_fault_tree.h"
#include "indexed_gate.h"
#include "logger.h"

namespace scram {

int SimpleGate::limit_order_ = 20;

void SimpleGate::GenerateCutSets(const SetPtr& cut_set,
                                 std::set<SetPtr, SetPtrComp>* new_cut_sets) {
  assert(cut_set->size() <= limit_order_);
  if (type_ == kOrGate) {  // OR gate operations.
    SimpleGate::OrGateCutSets(cut_set, new_cut_sets);

  } else {  // AND gate operations.
    SimpleGate::AndGateCutSets(cut_set, new_cut_sets);
  }
}

void SimpleGate::AndGateCutSets(const SetPtr& cut_set,
                                std::set<SetPtr, SetPtrComp>* new_cut_sets) {
  assert(cut_set->size() <= limit_order_);
  // Check for null case.
  std::vector<int>::iterator it;
  for (it = basic_events_.begin(); it != basic_events_.end(); ++it) {
    if (cut_set->count(-*it)) return;
  }
  // Limit order checks before other expensive operations.
  int order = cut_set->size();
  for (it = basic_events_.begin(); it != basic_events_.end(); ++it) {
    if (!cut_set->count(*it)) ++order;
    if (order > limit_order_) return;
  }
  for (it = modules_.begin(); it != modules_.end(); ++it) {
    if (!cut_set->count(*it)) ++order;
    if (order > limit_order_) return;
  }
  SetPtr cut_set_copy(new std::set<int>(*cut_set));
  // Include all basic events and modules into the set.
  for (it = basic_events_.begin(); it != basic_events_.end(); ++it) {
    cut_set_copy->insert(*it);
  }
  for (it = modules_.begin(); it != modules_.end(); ++it) {
    cut_set_copy->insert(*it);
  }

  // Deal with many OR gate children.
  SetPtrComp comp;
  std::set<SetPtr, SetPtrComp> arguments;  // Input to OR gates.
  arguments.insert(cut_set_copy);
  std::vector<SimpleGatePtr>::iterator it_g;
  for (it_g = gates_.begin(); it_g != gates_.end(); ++it_g) {
    std::set<SetPtr, SetPtrComp>::iterator it_s;
    std::set<SetPtr, SetPtrComp> results(comp);
    for (it_s = arguments.begin(); it_s != arguments.end(); ++it_s) {
      (*it_g)->OrGateCutSets(*it_s, &results);
    }
    arguments = results;
  }
  if (!arguments.empty() &&
      (*arguments.begin())->size() == cut_set_copy->size()) {
    new_cut_sets->insert(cut_set_copy);
  } else {
    new_cut_sets->insert(arguments.begin(), arguments.end());
  }
}

void SimpleGate::OrGateCutSets(const SetPtr& cut_set,
                               std::set<SetPtr, SetPtrComp>* new_cut_sets) {
  assert(cut_set->size() <= limit_order_);
  // Check for local minimality.
  std::vector<int>::iterator it;
  for (it = basic_events_.begin(); it != basic_events_.end(); ++it) {
    if (cut_set->count(*it)) {
      new_cut_sets->insert(cut_set);
      return;
    }
  }
  for (it = modules_.begin(); it != modules_.end(); ++it) {
    if (cut_set->count(*it)) {
      new_cut_sets->insert(cut_set);
      return;
    }
  }
  // There is a guarantee of a size increase of a cut set.
  if (cut_set->size() < limit_order_) {
    // Create new cut sets from basic events and modules.
    for (it = basic_events_.begin(); it != basic_events_.end(); ++it) {
      if (!cut_set->count(-*it)) {
        SetPtr new_set(new std::set<int>(*cut_set));
        new_set->insert(*it);
        new_cut_sets->insert(new_set);
      }
    }
    for (it = modules_.begin(); it != modules_.end(); ++it) {
      // No check for complements. The modules are assumed to be positive.
      SetPtr new_set(new std::set<int>(*cut_set));
      new_set->insert(*it);
      new_cut_sets->insert(new_set);
    }
  }

  // Generate cut sets from child gates of AND type.
  std::vector<SimpleGatePtr>::iterator it_g;
  SetPtrComp comp;
  std::set<SetPtr, SetPtrComp> local_sets(comp);
  for (it_g = gates_.begin(); it_g != gates_.end(); ++it_g) {
    (*it_g)->AndGateCutSets(cut_set, &local_sets);
    if (!local_sets.empty() &&
        (*local_sets.begin())->size() == cut_set->size()) {
      new_cut_sets->insert(cut_set);
      return;
    }
  }
  new_cut_sets->insert(local_sets.begin(), local_sets.end());
}

Mocus::Mocus(const IndexedFaultTree* fault_tree, int limit_order)
      : fault_tree_(fault_tree),
        limit_order_(limit_order) {
  SimpleGate::limit_order(limit_order);
}

void Mocus::FindMcs() {
  CLOCK(mcs_time);
  LOG(DEBUG2) << "Start minimal cut set generation.";

  int top_index = fault_tree_->top_event_index_;
  // Special case of empty top gate.
  IndexedGatePtr top = fault_tree_->indexed_gates_.find(top_index)->second;
  if (top->children().empty()) {
    State state = top->state();
    assert(state == kNullState || state == kUnityState);
    assert(fault_tree_->top_event_sign_ > 0);
    if (state == kUnityState) {
      std::set<int> empty_set;
      imcs_.push_back(empty_set);  // Special indication of unity set.
    }  // Other cases are null.
    return;
  }

  // Create simple gates from indexed gates.
  std::map<int, SimpleGatePtr> simple_gates;
  Mocus::CreateSimpleTree(top_index, &simple_gates);

  LOG(DEBUG3) << "Finding MCS from top module: " << top_index;
  std::vector<std::set<int> > mcs;
  Mocus::FindMcsFromSimpleGate(simple_gates.find(top_index)->second, &mcs);

  LOG(DEBUG3) << "Top gate cut sets are generated.";

  // The next is to join all other modules.
  LOG(DEBUG3) << "Joining modules.";
  // Save minimal cut sets of analyzed modules.
  std::map<int, std::vector< std::set<int> > > module_mcs;
  std::vector< std::set<int> >::iterator it;
  while (!mcs.empty()) {
    std::set<int> member = mcs.back();
    mcs.pop_back();
    if (*member.rbegin() < fault_tree_->gate_index_) {
      imcs_.push_back(member);
    } else {
      std::set<int>::iterator it_s = member.end();
      --it_s;
      int module_index = *it_s;
      member.erase(it_s);
      std::vector< std::set<int> > sub_mcs;
      if (module_mcs.count(module_index)) {
        sub_mcs = module_mcs.find(module_index)->second;
      } else {
        LOG(DEBUG3) << "Finding MCS from module index: " << module_index;
        Mocus::FindMcsFromSimpleGate(simple_gates.find(module_index)->second,
                                     &sub_mcs);
        module_mcs.insert(std::make_pair(module_index, sub_mcs));
      }
      std::vector< std::set<int> >::iterator it;
      for (it = sub_mcs.begin(); it != sub_mcs.end(); ++it) {
        if (it->size() + member.size() <= limit_order_) {
          it->insert(member.begin(), member.end());
          mcs.push_back(*it);
        }
      }
    }
  }

  // Special case of unity with empty sets.
  /// @todo Detect unity in modules.
  State state = fault_tree_->indexed_gates_.find(top_index)->second->state();
  assert(state != kUnityState);
  LOG(DEBUG2) << "The number of MCS found: " << imcs_.size();
  LOG(DEBUG2) << "Minimal cut set finding time: " << DUR(mcs_time);
}

void Mocus::CreateSimpleTree(int gate_index,
                             std::map<int, SimpleGatePtr>* processed_gates) {
  assert(gate_index > 0);
  if (processed_gates->count(gate_index)) return;
  IndexedGatePtr gate =
      fault_tree_->indexed_gates_.find(gate_index)->second;
  GateType simple_type = gate->type() == 2 ? kAndGate : kOrGate;
  SimpleGatePtr simple_gate(new SimpleGate(simple_type));
  processed_gates->insert(std::make_pair(gate_index, simple_gate));

  std::set<int>::iterator it;
  for (it = gate->children().begin(); it != gate->children().end(); ++it) {
    if (*it > fault_tree_->gate_index_) {
      if (fault_tree_->modules_.count(*it)) {
        simple_gate->InitiateWithModule(*it);
        Mocus::CreateSimpleTree(*it, processed_gates);
      } else {
        Mocus::CreateSimpleTree(*it, processed_gates);
        simple_gate->AddChildGate(processed_gates->find(*it)->second);
      }
    } else {
      assert(std::abs(*it) < fault_tree_->gate_index_);  // No negative gates.
      simple_gate->InitiateWithBasic(*it);
    }
  }
}

void Mocus::FindMcsFromSimpleGate(const SimpleGatePtr& gate,
                                  std::vector< std::set<int> >* mcs) {
  CLOCK(gen_time);

  SetPtrComp comp;
  std::set<SetPtr, SetPtrComp> cut_sets(comp);
  SetPtr cut_set(new std::set<int>);  // Initial empty cut set.
  // Generate main minimal cut set gates from top module.
  gate->GenerateCutSets(cut_set, &cut_sets);

  LOG(DEBUG4) << "Unique cut sets generated: " << cut_sets.size();
  LOG(DEBUG4) << "Cut set generation time: " << DUR(gen_time);

  CLOCK(min_time);
  LOG(DEBUG4) << "Minimizing the cut sets.";

  std::vector<const std::set<int>* > cut_sets_vector;
  cut_sets_vector.reserve(cut_sets.size());
  std::set<SetPtr, SetPtrComp>::iterator it;
  for (it = cut_sets.begin(); it != cut_sets.end(); ++it) {
    assert(!(*it)->empty());
    if ((*it)->size() == 1) {
      mcs->push_back(**it);
    } else {
      cut_sets_vector.push_back(&**it);
    }
  }
  Mocus::MinimizeCutSets(cut_sets_vector, *mcs, 2, mcs);

  LOG(DEBUG4) << "The number of local MCS: " << mcs->size();
  LOG(DEBUG4) << "Cut set minimization time: " << DUR(min_time);
}

void Mocus::MinimizeCutSets(const std::vector<const std::set<int>* >& cut_sets,
                            const std::vector<std::set<int> >& mcs_lower_order,
                            int min_order,
                            std::vector<std::set<int> >* mcs) {
  if (cut_sets.empty()) return;

  std::vector<const std::set<int>* > temp_sets;  // For mcs of a level above.
  std::vector<std::set<int> > temp_min_sets;  // For mcs of this level.

  std::vector<const std::set<int>* >::const_iterator it_uniq;
  for (it_uniq = cut_sets.begin(); it_uniq != cut_sets.end(); ++it_uniq) {
    bool include = true;  // Determine to keep or not.

    std::vector<std::set<int> >::const_iterator it_min;
    for (it_min = mcs_lower_order.begin(); it_min != mcs_lower_order.end();
         ++it_min) {
      if (std::includes((*it_uniq)->begin(), (*it_uniq)->end(),
                        it_min->begin(), it_min->end())) {
        // Non-minimal cut set is detected.
        include = false;
        break;
      }
    }
    // After checking for non-minimal cut sets,
    // all minimum sized cut sets are guaranteed to be minimal.
    if (include) {
      if ((*it_uniq)->size() == min_order) {
        temp_min_sets.push_back(**it_uniq);
      } else {
        temp_sets.push_back(*it_uniq);
      }
    }
    // Ignore the cut set because include = false.
  }
  mcs->insert(mcs->end(), temp_min_sets.begin(), temp_min_sets.end());
  min_order++;
  Mocus::MinimizeCutSets(temp_sets, temp_min_sets, min_order, mcs);
}

}  // namespace scram
