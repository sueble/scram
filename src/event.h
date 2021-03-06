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
/// Contains event classes for fault trees.

#pragma once

#include <cstdint>

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <boost/noncopyable.hpp>

#include "element.h"
#include "expression.h"

namespace scram::mef {

/// Abstract base class for general fault tree events.
class Event : public Id, public Usage {
 public:
  using Id::Id;

  virtual ~Event() = 0;  ///< Abstract class.
};

/// Representation of a house event in a fault tree.
///
/// @note House Events with unset/uninitialized expressions default to False.
class HouseEvent : public Event {
 public:
  static HouseEvent kTrue;  ///< Literal True event.
  static HouseEvent kFalse;  ///< Literal False event.

  using Event::Event;

  HouseEvent(HouseEvent&&);  ///< For the (N)RVO only (undefined!).

  /// Sets the state for House event.
  ///
  /// @param[in] constant  False or True for the state of this house event.
  void state(bool constant) { state_ = constant; }

  /// @returns The true or false state of this house event.
  bool state() const { return state_; }

 private:
  /// Represents the state of the house event.
  /// Implies On or Off for True or False values of the probability.
  bool state_ = false;
};

class Gate;

/// Representation of a basic event in a fault tree.
class BasicEvent : public Event {
 public:
  using Event::Event;

  virtual ~BasicEvent() = default;

  /// @returns true if the probability expression is set.
  bool HasExpression() const { return expression_ != nullptr; }

  /// Sets the expression of this basic event.
  ///
  /// @param[in] expression  The expression to describe this event.
  ///                        nullptr to remove unset the expression.
  void expression(Expression* expression) { expression_ = expression; }

  /// @returns The previously set expression for analysis purposes.
  ///
  /// @pre The expression has been set.
  Expression& expression() const {
    assert(expression_ && "The basic event's expression is not set.");
    return *expression_;
  }

  /// @returns The mean probability of this basic event.
  ///
  /// @pre The expression has been set.
  ///
  /// @note The user of this function should make sure
  ///       that the returned value is acceptable for calculations.
  double p() const noexcept {
    assert(expression_ && "The basic event's expression is not set.");
    return expression_->value();
  }

  /// Validates the probability expressions for the primary event.
  ///
  /// @pre The probability expression is set.
  ///
  /// @throws DomainError  The expression for the basic event is invalid.
  void Validate() const;

  /// Indicates if this basic event has been set to be in a CCF group.
  ///
  /// @returns true if in a CCF group.
  bool HasCcf() const { return ccf_gate_ != nullptr; }

  /// @returns CCF group gate representing this basic event.
  const Gate& ccf_gate() const {
    assert(ccf_gate_);
    return *ccf_gate_;
  }

  /// Sets the common cause failure group gate
  /// that can represent this basic event
  /// in analysis with common cause information.
  /// This information is expected to be provided by
  /// CCF group application.
  ///
  /// @param[in] gate  CCF group gate.
  void ccf_gate(std::unique_ptr<Gate> gate) {
    assert(!ccf_gate_);
    ccf_gate_ = std::move(gate);
  }

 private:
  /// Expression that describes this basic event
  /// and provides numerical values for probability calculations.
  Expression* expression_ = nullptr;

  /// If this basic event is in a common cause group,
  /// CCF gate can serve as a replacement for the basic event
  /// for common cause analysis.
  std::unique_ptr<Gate> ccf_gate_;
};

/// Convenience aliases for smart pointers @{
using EventPtr = std::unique_ptr<Event>;
using HouseEventPtr = std::unique_ptr<HouseEvent>;
using BasicEventPtr = std::unique_ptr<BasicEvent>;
using GatePtr = std::unique_ptr<Gate>;
/// @}

class Formula;  // To describe a gate's formula.
using FormulaPtr = std::unique_ptr<Formula>;  ///< Non-shared gate formulas.

/// A representation of a gate in a fault tree.
class Gate : public Event, public NodeMark {
 public:
  using Event::Event;

  /// @returns true if the gate formula has been set.
  bool HasFormula() const { return formula_ != nullptr; }

  /// @returns The formula of this gate.
  ///
  /// @pre The gate has its formula initialized.
  ///
  /// @{
  const Formula& formula() const {
    assert(formula_ && "Gate formula is not set.");
    return *formula_;
  }
  Formula& formula() {
    return const_cast<Formula&>(std::as_const(*this).formula());
  }
  /// @}

  /// Sets the formula of this gate.
  ///
  /// @param[in] formula  The new Boolean formula of this gate.
  ///
  /// @returns The old formula.
  FormulaPtr formula(FormulaPtr formula) {
    assert(formula && "Cannot unset formula.");
    formula_.swap(formula);
    return formula;
  }

  /// Checks if a gate is initialized correctly.
  ///
  /// @pre The gate formula is set.
  ///
  /// @throws ValidityError  Errors in the gate's logic or setup.
  void Validate() const;

 private:
  FormulaPtr formula_;  ///< Boolean formula of this gate.
};

/// Operators for formulas.
/// The ordering is the same as analysis operators in the PDAG.
enum Operator : std::uint8_t {
  kAnd = 0,
  kOr,
  kVote,  ///< Combination, K/N, atleast, or Vote gate representation.
  kXor,  ///< Exclusive OR gate with two inputs only.
  kNot,  ///< Boolean negation.
  kNand,  ///< Not AND.
  kNor,  ///< Not OR.
  kNull  ///< Single argument pass-through without logic.
};

/// The number of operators in the enum.
const int kNumOperators = 8;

/// String representations of the operators.
/// The ordering is the same as the Operator enum.
const char* const kOperatorToString[] = {"and", "or",   "atleast", "xor",
                                         "not", "nand", "nor",     "null"};

/// Boolean formula with operators and arguments.
/// Formulas are not expected to be shared.
class Formula : private boost::noncopyable {
 public:
  /// Event arguments of a formula.
  using EventArg = std::variant<Gate*, BasicEvent*, HouseEvent*>;

  /// Constructs a formula.
  ///
  /// @param[in] type  The logical operator for this Boolean formula.
  explicit Formula(Operator type);

  /// @returns The type of this formula.
  Operator type() const { return type_; }

  /// @returns The vote number if and only if the formula is "atleast".
  ///
  /// @throws LogicError  The vote number is not yet assigned.
  int vote_number() const;

  /// Sets the vote number only for an "atleast" formula.
  ///
  /// @param[in] number  The vote number.
  ///
  /// @throws ValidityError  The vote number is invalid.
  /// @throws LogicError  The vote number is assigned illegally.
  ///
  /// @note (Children number > vote number) should be checked
  ///       outside of this class.
  void vote_number(int number);

  /// @returns The arguments of this formula of specific type.
  /// @{
  const std::vector<EventArg>& event_args() const { return event_args_; }
  const std::vector<FormulaPtr>& formula_args() const { return formula_args_; }
  /// @}

  /// @returns The number of arguments.
  int num_args() const { return event_args_.size() + formula_args_.size(); }

  /// Adds an event into the arguments list.
  ///
  /// @param[in] event_arg  An argument event.
  ///
  /// @throws DuplicateArgumentError  The argument event is duplicate.
  void AddArgument(EventArg event_arg);

  /// Adds a formula into the arguments list.
  /// Formulas are unique.
  ///
  /// @param[in] formula  A pointer to an argument formula.
  void AddArgument(FormulaPtr formula) {
    formula_args_.emplace_back(std::move(formula));
  }

  /// Removes an event from the formula.
  ///
  /// @param[in] event_arg  The argument event of this formula.
  ///
  /// @throws LogicError  The argument does not belong to this formula.
  void RemoveArgument(EventArg event_arg);

  /// Checks if a formula is initialized correctly with the number of arguments.
  ///
  /// @throws ValidityError  Problems with the operator or arguments.
  void Validate() const;

 private:
  Operator type_;  ///< Logical operator.
  int vote_number_;  ///< Vote number for "atleast" operator.
  std::vector<EventArg> event_args_;  ///< All event arguments.
  std::vector<FormulaPtr> formula_args_;  ///< Nested formula arguments.
};

}  // namespace scram::mef
