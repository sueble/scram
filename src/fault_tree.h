/*
 * Copyright (C) 2014-2018 Olzhas Rakhimov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/// @file
/// Fault Tree and Component containers.

#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "ccf_group.h"
#include "element.h"
#include "event.h"
#include "parameter.h"

namespace scram::mef {

/// Component is for logical grouping of events, gates, and other components.
class Component : public Element, public Role {
 public:
  /// Constructs a component assuming
  /// that it exists within some fault tree.
  /// The public or private role of a component is not
  /// for the components itself,
  /// but for the events and parameters of the component.
  /// Component name is not meant to be public;
  /// however, it must be unique within the parent fault tree or component.
  ///
  /// @param[in] name  The name identifier for the component.
  /// @param[in] base_path  The series of containers to get this container.
  /// @param[in] role  The default role for container members.
  ///
  /// @throws LogicError  The name is empty.
  /// @throws ValidityError  The name or reference paths are malformed.
  explicit Component(std::string name, std::string base_path = "",
                     RoleSpecifier role = RoleSpecifier::kPublic);

  virtual ~Component() = default;

  /// @returns The container of component constructs of specific kind
  ///          with construct original names as keys.
  /// @{
  const ElementTable<Gate*>& gates() const { return gates_; }
  const ElementTable<BasicEvent*>& basic_events() const {
    return basic_events_;
  }
  const ElementTable<HouseEvent*>& house_events() const {
    return house_events_;
  }
  const ElementTable<Parameter*>& parameters() const { return parameters_; }
  const ElementTable<CcfGroup*>& ccf_groups() const { return ccf_groups_; }
  const ElementTable<std::unique_ptr<Component>>& components() const {
    return components_;
  }
  /// @}

  /// Adds MEF constructs into this component container.
  ///
  /// @param[in] element  The element to be added to the container.
  ///
  /// @throws ValidityError  The element is already in this container.
  ///
  /// @{
  void Add(Gate* element);
  void Add(BasicEvent* element);
  void Add(HouseEvent* element);
  void Add(Parameter* element);
  void Add(CcfGroup* element);
  void Add(std::unique_ptr<Component> element);
  /// @}

  /// Removes Event from the component container.
  ///
  /// @param[in] element  An element defined in this model.
  ///
  /// @throws UndefinedElement  The element doesn't belong to this container.
  ///
  /// @{
  void Remove(HouseEvent* element);
  void Remove(BasicEvent* element);
  void Remove(Gate* element);
  /// @}

 protected:
  /// Recursively traverses components
  /// to gather gates relevant to the whole component.
  ///
  /// @param[out] gates  Gates belonging to this component
  ///                    and its subcomponents.
  void GatherGates(std::unordered_set<Gate*>* gates);

 private:
  /// Adds an event into this component container.
  ///
  /// @tparam T  The event type.
  /// @tparam Container  Map with the event's original name as the key.
  ///
  /// @param[in] event  The event to be added to this component.
  /// @param[in,out] container  The destination container.
  ///
  /// @throws ValidityError  The event is already in this container.
  template <class T, class Container>
  void AddEvent(T* event, Container* container);

  /// Container for component constructs with original names as keys.
  /// @{
  ElementTable<Gate*> gates_;
  ElementTable<BasicEvent*> basic_events_;
  ElementTable<HouseEvent*> house_events_;
  ElementTable<Parameter*> parameters_;
  ElementTable<CcfGroup*> ccf_groups_;
  ElementTable<std::unique_ptr<Component>> components_;
  /// @}
};

using ComponentPtr = std::unique_ptr<Component>;  ///< Unique system components.

/// Fault tree representation as a container of
/// gates, basic and house events, and other information.
/// Additional functionality of a fault tree includes
/// detection of top events.
class FaultTree : public Component {
 public:
  /// The main constructor of the Fault Tree.
  /// Fault trees are assumed to be public and belong to the root model.
  ///
  /// @param[in] name  The name identifier of this fault tree.
  explicit FaultTree(const std::string& name);

  /// @returns The collected top events of this fault tree.
  const std::vector<const Gate*>& top_events() const { return top_events_; }

  /// Collects top event gates in this fault tree with components.
  /// This function is essential to guess the analysis targets
  /// if the user does not supply any.
  /// If the structure of the fault tree changes,
  /// this function must be called again to update the top events.
  ///
  /// @pre Gate marks are clear.
  void CollectTopEvents();

 private:
  /// Recursively marks descendant gates
  /// with an unspecified-but-non-clear mark.
  /// These gates belong to this fault tree only.
  ///
  /// @param[in,out] gate  The ancestor gate.
  /// @param[in] gates  Gates belonging to the whole fault tree with components.
  void MarkNonTopGates(Gate* gate, const std::unordered_set<Gate*>& gates);

  /// Recursively marks descendant gates in formulas as "non-top"
  ///
  /// @param[in] formula  The formula of a gate or another formula.
  /// @param[in] gates  Gates belonging to the whole fault tree with components.
  void MarkNonTopGates(const Formula& formula,
                       const std::unordered_set<Gate*>& gates);

  std::vector<const Gate*> top_events_;  ///< Top events of this fault tree.
};

using FaultTreePtr = std::unique_ptr<FaultTree>;  ///< Unique trees in models.

}  // namespace scram::mef
