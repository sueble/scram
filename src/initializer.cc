/// @file initializer.cc
/// Implementation of input file processing into analysis constructs.
#include "initializer.h"

#include <fstream>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/assign.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/pointer_cast.hpp>
#include <boost/unordered_map.hpp>

#include "ccf_group.h"
#include "cycle.h"
#include "element.h"
#include "env.h"
#include "error.h"
#include "expression.h"
#include "fault_tree.h"
#include "logger.h"
#include "xml_parser.h"

namespace fs = boost::filesystem;

namespace scram {

const std::map<std::string, Units> Initializer::units_ =
    boost::assign::map_list_of ("bool", kBool) ("int", kInt) ("float", kFloat)
                               ("hours", kHours) ("hours-1", kInverseHours)
                               ("years", kYears) ("years-1", kInverseYears)
                               ("fit", kFit) ("demands", kDemands);

const char* const Initializer::unit_to_string_[] = {"unitless", "bool", "int",
                                                     "float", "hours",
                                                     "hours-1", "years",
                                                     "years-1", "fit",
                                                     "demands"};

Initializer::Initializer() {
  // Initialize the mission time with any value.
  mission_time_ = boost::shared_ptr<MissionTime>(new MissionTime());
}

void Initializer::ProcessInput(std::string xml_file) {
  std::vector<std::string> single;
  single.push_back(xml_file);
  Initializer::ProcessInputFiles(single);
}

void Initializer::ProcessInputFiles(
    const std::vector<std::string>& xml_files) {
  // Assume that settings are configured.
  mission_time_->mission_time(settings_.mission_time_);

  CLOCK(input_time);
  LOG(DEBUG1) << "Processing input files";
  std::vector<std::string>::const_iterator it;
  for (it = xml_files.begin(); it != xml_files.end(); ++it) {
    try {
      Initializer::ProcessInputFile(*it);
    } catch (ValidationError& err) {
      err.msg("In file '" + *it + "', " + err.msg());
      throw err;
    }
  }
  CLOCK(def_time);
  Initializer::ProcessTbdElements();
  LOG(DEBUG2) << "Element definition time " << DUR(def_time);
  LOG(DEBUG1) << "Input files are processed in " << DUR(input_time);

  CLOCK(valid_time);
  LOG(DEBUG1) << "Validating the input files";
  // Check if the initialization is successful.
  Initializer::ValidateInitialization();
  LOG(DEBUG1) << "Validation is finished in " << DUR(valid_time);

  CLOCK(setup_time);
  LOG(DEBUG1) << "Setting up for the analysis";
  // Perform setup for analysis using configurations from the input files.
  Initializer::SetupForAnalysis();
  LOG(DEBUG1) << "Setup time " << DUR(setup_time);
}

void Initializer::ProcessInputFile(std::string xml_file) {
  std::ifstream file_stream(xml_file.c_str());
  if (!file_stream) {
    throw IOError("File '" + xml_file + "' could not be loaded.");
  }
  fs::path file_path = fs::canonical(xml_file);
  if (input_path_.count(file_path.native())) {
    throw ValidationError("Trying to pass the same file twice: " +
                          file_path.native());
  }
  input_path_.insert(file_path.native());

  std::stringstream stream;
  stream << file_stream.rdbuf();
  file_stream.close();

  boost::shared_ptr<XMLParser> parser(new XMLParser());
  parser->Init(stream);

  std::stringstream schema;
  std::string schema_path = Env::input_schema();
  std::ifstream schema_stream(schema_path.c_str());
  schema << schema_stream.rdbuf();
  schema_stream.close();

  parser->Validate(schema);
  parsers_.push_back(parser);

  const xmlpp::Document* doc = parser->Document();
  const xmlpp::Node* root = doc->get_root_node();
  assert(root->get_name() == "opsa-mef");
  if (!model_) {
    xmlpp::NodeSet name_attr = root->find("./@name");
    std::string model_name = "";
    if (!name_attr.empty()) {
      assert(name_attr.size() == 1);
      const xmlpp::Attribute* attr =
          dynamic_cast<const xmlpp::Attribute*>(name_attr[0]);
      model_name = attr->get_value();
    }
    model_ = ModelPtr(new Model(model_name));
    Initializer::AttachLabelAndAttributes(
        dynamic_cast<const xmlpp::Element*>(root),
        model_);
  }

  xmlpp::NodeSet::iterator it_ch;  // Iterator for all children.

  xmlpp::NodeSet fault_trees = root->find("./define-fault-tree");
  for (it_ch = fault_trees.begin(); it_ch != fault_trees.end(); ++it_ch) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it_ch);
    assert(element);
    Initializer::DefineFaultTree(element);
  }

  xmlpp::NodeSet ccf_groups = root->find("./define-CCF-group");
  for (it_ch = ccf_groups.begin(); it_ch != ccf_groups.end(); ++it_ch) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it_ch);
    assert(element);
    Initializer::RegisterCcfGroup(element);
  }

  xmlpp::NodeSet model_data = root->find("./model-data");
  for (it_ch = model_data.begin(); it_ch != model_data.end(); ++it_ch) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it_ch);
    assert(element);
    Initializer::ProcessModelData(element);
  }
}

void Initializer::ProcessTbdElements() {
  std::vector< std::pair<ElementPtr, const xmlpp::Element*> >::iterator it;
  for (it = tbd_elements_.begin(); it != tbd_elements_.end(); ++it) {
    if (boost::dynamic_pointer_cast<BasicEvent>(it->first)) {
      BasicEventPtr basic_event =
          boost::dynamic_pointer_cast<BasicEvent>(it->first);
      DefineBasicEvent(it->second, basic_event);
    } else if (boost::dynamic_pointer_cast<Gate>(it->first)) {
      GatePtr gate =
          boost::dynamic_pointer_cast<Gate>(it->first);
      DefineGate(it->second, gate);
    } else if (boost::dynamic_pointer_cast<CcfGroup>(it->first)) {
      CcfGroupPtr ccf_group = boost::dynamic_pointer_cast<CcfGroup>(it->first);
      DefineCcfGroup(it->second, ccf_group);
    } else if (boost::dynamic_pointer_cast<Parameter>(it->first)) {
      ParameterPtr param = boost::dynamic_pointer_cast<Parameter>(it->first);
      DefineParameter(it->second, param);
    }
  }
}

void Initializer::AttachLabelAndAttributes(
    const xmlpp::Element* element_node,
    const ElementPtr& element) {
  xmlpp::NodeSet labels = element_node->find("./*[name() = 'label']");
  if (!labels.empty()) {
    assert(labels.size() == 1);
    const xmlpp::Element* label =
        dynamic_cast<const xmlpp::Element*>(labels.front());
    assert(label);
    const xmlpp::TextNode* text = label->get_child_text();
    assert(text);
    element->label(text->get_content());
  }

  xmlpp::NodeSet attributes = element_node->find("./*[name() = 'attributes']");
  if (!attributes.empty()) {
    assert(attributes.size() == 1);  // Only one big element 'attributes'.
    const xmlpp::Element* attributes_element =
        dynamic_cast<const xmlpp::Element*>(attributes.front());
    xmlpp::NodeSet attribute_list =
        attributes_element->find("./*[name() = 'attribute']");
    xmlpp::NodeSet::iterator it;
    for (it = attribute_list.begin(); it != attribute_list.end(); ++it) {
      const xmlpp::Element* attribute =
          dynamic_cast<const xmlpp::Element*>(*it);
      Attribute attr;
      attr.name = attribute->get_attribute_value("name");
      attr.value = attribute->get_attribute_value("value");
      attr.type = attribute->get_attribute_value("type");
      element->AddAttribute(attr);
    }
  }
}

void Initializer::DefineFaultTree(const xmlpp::Element* ft_node) {
  std::string name = ft_node->get_attribute_value("name");
  assert(!name.empty());
  FaultTreePtr fault_tree = FaultTreePtr(new FaultTree(name));
  try {
    model_->AddFaultTree(fault_tree);
  } catch (ValidationError& err) {
    std::stringstream msg;
    msg << "Line " << ft_node->get_line() << ":\n";
    msg << err.msg();
    throw ValidationError(msg.str());
  }

  Initializer::AttachLabelAndAttributes(ft_node, fault_tree);

  xmlpp::NodeSet gates = ft_node->find("./define-gate");
  xmlpp::NodeSet ccf_groups = ft_node->find("./define-CCF-group");

  xmlpp::NodeSet::iterator it;
  CLOCK(gate_time);
  for (it = gates.begin(); it != gates.end(); ++it) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it);
    assert(element);
    Initializer::RegisterGate(element, fault_tree);
  }
  LOG(DEBUG2) << "Gate registration time " << DUR(gate_time);
  for (it = ccf_groups.begin(); it != ccf_groups.end(); ++it) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it);
    assert(element);
    Initializer::RegisterCcfGroup(element);
  }
  // Handle house events, basic events, and parameters.
  Initializer::ProcessModelData(ft_node);
}

void Initializer::ProcessModelData(const xmlpp::Element* model_data) {
  xmlpp::NodeSet house_events = model_data->find("./define-house-event");
  xmlpp::NodeSet basic_events = model_data->find("./define-basic-event");
  xmlpp::NodeSet parameters = model_data->find("./define-parameter");

  xmlpp::NodeSet::iterator it;

  for (it = house_events.begin(); it != house_events.end(); ++it) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it);
    assert(element);
    Initializer::DefineHouseEvent(element);
  }
  CLOCK(basic_time);
  for (it = basic_events.begin(); it != basic_events.end(); ++it) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it);
    assert(element);
    Initializer::RegisterBasicEvent(element);
  }
  LOG(DEBUG2) << "Basic event registration time " << DUR(basic_time);
  for (it = parameters.begin(); it != parameters.end(); ++it) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it);
    assert(element);
    Initializer::RegisterParameter(element);
  }
}

void Initializer::RegisterGate(const xmlpp::Element* gate_node,
                                const FaultTreePtr& ft) {
  std::string name = gate_node->get_attribute_value("name");
  boost::trim(name);
  std::string id = name;
  boost::to_lower(id);
  GatePtr gate(new Gate(id));
  gate->name(name);

  try {
    model_->AddGate(gate);
  } catch (ValidationError& err) {
    std::stringstream msg;
    msg << "Line " << gate_node->get_line() << ":\n";
    msg << err.msg();
    throw ValidationError(msg.str());
  }

  tbd_elements_.push_back(std::make_pair(gate, gate_node));

  Initializer::AttachLabelAndAttributes(gate_node, gate);

  ft->AddGate(gate);
  gate->container(ft->name());
}

void Initializer::DefineGate(const xmlpp::Element* gate_node,
                              const GatePtr& gate) {
  xmlpp::NodeSet formulas =
      gate_node->find("./*[name() != 'attributes' and name() != 'label']");
  // Assumes that there are no attributes and labels.
  assert(formulas.size() == 1);
  const xmlpp::Element* formula_node =
      dynamic_cast<const xmlpp::Element*>(formulas.front());
  gate->formula(Initializer::GetFormula(formula_node));
  try {
    gate->Validate();
  } catch (ValidationError& err) {
    std::stringstream msg;
    msg << "Line " << gate_node->get_line() << ":\n";
    throw ValidationError(msg.str() + err.msg());
  }
}

boost::shared_ptr<Formula> Initializer::GetFormula(
    const xmlpp::Element* formula_node) {
  std::string type = formula_node->get_name();
  if (type == "event" || type == "basic-event" || type == "gate" ||
      type == "house-event") {
    type = "null";
  }
  FormulaPtr formula(new Formula(type));
  if (type == "atleast") {
    std::string min_num = formula_node->get_attribute_value("min");
    boost::trim(min_num);
    int vote_number = boost::lexical_cast<int>(min_num);
    formula->vote_number(vote_number);
  }
  // Process arguments of this formula.
  if (type == "null") {
    Initializer::ProcessFormula(formula_node->get_parent(), formula);
  } else {
    Initializer::ProcessFormula(formula_node, formula);
  }

  try {
    formula->Validate();
  } catch (ValidationError& err) {
    std::stringstream msg;
    msg << "Line " << formula_node->get_line() << ":\n";
    throw ValidationError(msg.str() + err.msg());
  }
  return formula;
}

void Initializer::ProcessFormula(const xmlpp::Element* formula_node,
                                  const FormulaPtr& formula) {
  xmlpp::NodeSet events = formula_node->find("./*[name() = 'event' or "
                                             "name() = 'gate' or "
                                             "name() = 'basic-event' or "
                                             "name() = 'house-event']");
  std::set<std::string> children_id;  // To detect repeated children.
  xmlpp::NodeSet::const_iterator it;
  for (it = events.begin(); it != events.end(); ++it) {
    const xmlpp::Element* event = dynamic_cast<const xmlpp::Element*>(*it);
    assert(event);
    std::string name = event->get_attribute_value("name");
    boost::trim(name);
    std::string id = name;
    boost::to_lower(id);

    if (children_id.count(id)) {
      std::stringstream msg;
      msg << "Line " << event->get_line() << ":\n";
      msg << "Detected a repeated child " << name;
      throw ValidationError(msg.str());
    } else {
      children_id.insert(id);
    }

    std::string element_type = event->get_name();
    // This is for a case "<event name="id" type="type"/>".
    std::string type = event->get_attribute_value("type");
    boost::trim(type);
    if (type != "") {
      assert(type == "gate" || type == "basic-event" ||
             type == "house-event");
      element_type = type;  // Event type is defined.
    }

    EventPtr child(new Event(id));
    child->name(name);
    if (element_type == "event") {  // Undefined type yet.
      Initializer::ProcessFormulaEvent(event, child);

    } else if (element_type == "gate") {
      Initializer::ProcessFormulaGate(event, child);

    } else if (element_type == "basic-event") {
      Initializer::ProcessFormulaBasicEvent(event, child);

    } else if (element_type == "house-event") {
      Initializer::ProcessFormulaHouseEvent(event, child);
    }

    formula->AddArgument(child);
    child->orphan(false);
  }

  xmlpp::NodeSet formulas = formula_node->find("./*[name() != 'event' and "
                                               "name() != 'gate' and "
                                               "name() != 'basic-event' and "
                                               "name() != 'house-event']");
  for (it = formulas.begin(); it != formulas.end(); ++it) {
    const xmlpp::Element* nested_formula =
        dynamic_cast<const xmlpp::Element*>(*it);
    assert(nested_formula);
    formula->AddArgument(Initializer::GetFormula(nested_formula));
  }
}

void Initializer::ProcessFormulaEvent(const xmlpp::Element* event,
                                       EventPtr& child) {
  std::string id = child->id();
  if (model_->basic_events().count(id)) {
    child = model_->basic_events().find(id)->second;

  } else if (model_->gates().count(id)) {
    child = model_->gates().find(id)->second;

  } else if (model_->house_events().count(id)) {
    child = model_->house_events().find(id)->second;

  } else {
    std::stringstream msg;
    msg << "Line " << event->get_line() << ":\n";
    msg << "Undefined event: " << child->name();
    throw ValidationError(msg.str());
  }
}

void Initializer::ProcessFormulaBasicEvent(const xmlpp::Element* event,
                                            EventPtr& child) {
  std::string id = child->id();
  std::string name = child->name();
  if (!model_->basic_events().count(id)) {
    std::stringstream msg;
    msg << "Line " << event->get_line() << ":\n";
    msg << "Undefined basic event: " << name;
    throw ValidationError(msg.str());
  }
  child = model_->basic_events().find(id)->second;
}

void Initializer::ProcessFormulaHouseEvent(const xmlpp::Element* event,
                                            EventPtr& child) {
  std::string id = child->id();
  if (model_->house_events().count(id)) {
    child = model_->house_events().find(id)->second;

  } else {
    std::stringstream msg;
    msg << "Line " << event->get_line() << ":\n";
    msg << "Undefined house event: " << child->name();
    throw ValidationError(msg.str());
  }
}

void Initializer::ProcessFormulaGate(const xmlpp::Element* event,
                                      EventPtr& child) {
  std::string id = child->id();
  std::string name = child->name();
  if (!model_->gates().count(id)) {
    std::stringstream msg;
    msg << "Line " << event->get_line() << ":\n";
    msg << "Undefined gate: " << name;
    throw ValidationError(msg.str());
  }
  child = model_->gates().find(id)->second;
}

void Initializer::RegisterBasicEvent(const xmlpp::Element* event_node) {
  std::string name = event_node->get_attribute_value("name");
  boost::trim(name);
  std::string id = name;
  boost::to_lower(id);
  BasicEventPtr basic_event = BasicEventPtr(new BasicEvent(id));
  basic_event->name(name);
  try {
    model_->AddBasicEvent(basic_event);
  } catch (ValidationError& err) {
    std::stringstream msg;
    msg << "Line " << event_node->get_line() << ":\n";
    msg << err.msg();
    throw ValidationError(msg.str());
  }
  tbd_elements_.push_back(std::make_pair(basic_event, event_node));
  Initializer::AttachLabelAndAttributes(event_node, basic_event);
}

void Initializer::DefineBasicEvent(const xmlpp::Element* event_node,
                                    const BasicEventPtr& basic_event) {
  xmlpp::NodeSet expressions =
     event_node->find("./*[name() != 'attributes' and name() != 'label']");

  if (!expressions.empty()) {
    const xmlpp::Element* expr_node =
        dynamic_cast<const xmlpp::Element*>(expressions.back());
    assert(expr_node);
    ExpressionPtr expression = Initializer::GetExpression(expr_node);
    basic_event->expression(expression);
  }
}

void Initializer::DefineHouseEvent(const xmlpp::Element* event_node) {
  std::string name = event_node->get_attribute_value("name");
  boost::trim(name);
  std::string id = name;
  boost::to_lower(id);
  HouseEventPtr house_event = HouseEventPtr(new HouseEvent(id));
  house_event->name(name);
  try {
    model_->AddHouseEvent(house_event);
  } catch (ValidationError& err) {
    std::stringstream msg;
    msg << "Line " << event_node->get_line() << ":\n";
    msg << err.msg();
    throw ValidationError(msg.str());
  }

  // Only Boolean constant.
  xmlpp::NodeSet expression = event_node->find("./*[name() = 'constant']");
  if (!expression.empty()) {
    assert(expression.size() == 1);
    const xmlpp::Element* constant =
        dynamic_cast<const xmlpp::Element*>(expression.front());
    assert(constant);

    std::string val = constant->get_attribute_value("value");
    boost::trim(val);
    assert(val == "true" || val == "false");
    bool state = (val == "true") ? true : false;
    house_event->state(state);
  }
  Initializer::AttachLabelAndAttributes(event_node, house_event);
}

void Initializer::RegisterParameter(const xmlpp::Element* param_node) {
  std::string name = param_node->get_attribute_value("name");
  boost::trim(name);
  ParameterPtr parameter = ParameterPtr(new Parameter(name));
  try {
    model_->AddParameter(parameter);
  } catch (ValidationError& err) {
    std::stringstream msg;
    msg << "Line " << param_node->get_line() << ":\n";
    msg << err.msg();
    throw ValidationError(msg.str());
  }
  tbd_elements_.push_back(std::make_pair(parameter, param_node));

  // Attach units.
  std::string unit = param_node->get_attribute_value("unit");
  boost::trim(unit);
  if (unit != "") {
    assert(units_.count(unit));
    parameter->unit(units_.find(unit)->second);
  }
  Initializer::AttachLabelAndAttributes(param_node, parameter);
}

void Initializer::DefineParameter(const xmlpp::Element* param_node,
                                   const ParameterPtr& parameter) {
  // Assuming that expression is the last child of the parameter definition.
  xmlpp::NodeSet expressions =
      param_node->find("./*[name() != 'attributes' and name() != 'label']");
  assert(expressions.size() == 1);
  const xmlpp::Element* expr_node =
      dynamic_cast<const xmlpp::Element*>(expressions.back());
  assert(expr_node);
  ExpressionPtr expression = Initializer::GetExpression(expr_node);

  parameter->expression(expression);
}

boost::shared_ptr<Expression> Initializer::GetExpression(
    const xmlpp::Element* expr_element) {
  using scram::Initializer;
  ExpressionPtr expression;
  bool not_parameter = true;  // Parameters are saved in a different container.
  if (GetConstantExpression(expr_element, expression)) {
  } else if (GetParameterExpression(expr_element, expression)) {
    not_parameter = false;
  } else {
    GetDeviateExpression(expr_element, expression);
  }
  assert(expression);
  if (not_parameter) expressions_.push_back(expression);
  return expression;
}

bool Initializer::GetConstantExpression(const xmlpp::Element* expr_element,
                                         ExpressionPtr& expression) {
  typedef boost::shared_ptr<ConstantExpression> ConstantExpressionPtr;
  assert(expr_element);
  std::string expr_name = expr_element->get_name();
  if (expr_name == "float" || expr_name == "int") {
    std::string val = expr_element->get_attribute_value("value");
    boost::trim(val);
    double num = boost::lexical_cast<double>(val);
    expression = ConstantExpressionPtr(new ConstantExpression(num));

  } else if (expr_name == "bool") {
    std::string val = expr_element->get_attribute_value("value");
    boost::trim(val);
    bool state = (val == "true") ? true : false;
    expression = ConstantExpressionPtr(new ConstantExpression(state));
  } else {
    return false;
  }
  return true;
}

bool Initializer::GetParameterExpression(const xmlpp::Element* expr_element,
                                          ExpressionPtr& expression) {
  assert(expr_element);
  std::string expr_name = expr_element->get_name();
  std::string param_unit = "";  // The expected unit.
  if (expr_name == "parameter") {
    std::string name = expr_element->get_attribute_value("name");
    boost::trim(name);
    std::string id = name;
    boost::to_lower(id);
    if (model_->parameters().count(id)) {
      ParameterPtr param = model_->parameters().find(id)->second;
      param->unused(false);
      param_unit = unit_to_string_[param->unit()];
      expression = param;
    } else {
      std::stringstream msg;
      msg << "Line " << expr_element->get_line() << ":\n";
      msg << "Undefined parameter: " << name;
      throw ValidationError(msg.str());
    }
  } else if (expr_name == "system-mission-time") {
    param_unit = unit_to_string_[mission_time_->unit()];
    expression = mission_time_;

  } else {
    return false;
  }
  // Check units.
  std::string unit = expr_element->get_attribute_value("unit");
  boost::trim(unit);
  if (!unit.empty() && unit != param_unit) {
    std::stringstream msg;
    msg << "Line " << expr_element->get_line() << ":\n";
    msg << "Parameter unit mismatch.\nExpected: " << param_unit
        << "\nGiven: " << unit;
    throw ValidationError(msg.str());
  }
  return true;
}

bool Initializer::GetDeviateExpression(const xmlpp::Element* expr_element,
                                        ExpressionPtr& expression) {
  assert(expr_element);
  std::string expr_name = expr_element->get_name();
  xmlpp::NodeSet args = expr_element->find("./*");
  if (expr_name == "uniform-deviate") {
    assert(args.size() == 2);
    const xmlpp::Element* element =
        dynamic_cast<const xmlpp::Element*>(args[0]);
    assert(element);
    ExpressionPtr min = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[1]);
    assert(element);
    ExpressionPtr max = GetExpression(element);

    expression = boost::shared_ptr<UniformDeviate>(
        new UniformDeviate(min, max));

  } else if (expr_name == "normal-deviate") {
    assert(args.size() == 2);
    const xmlpp::Element* element =
        dynamic_cast<const xmlpp::Element*>(args[0]);
    assert(element);
    ExpressionPtr mean = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[1]);
    assert(element);
    ExpressionPtr sigma = GetExpression(element);

    expression = boost::shared_ptr<NormalDeviate>(
        new NormalDeviate(mean, sigma));

  } else if (expr_name == "lognormal-deviate") {
    assert(args.size() == 3);
    const xmlpp::Element* element =
        dynamic_cast<const xmlpp::Element*>(args[0]);
    assert(element);
    ExpressionPtr mean = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[1]);
    assert(element);
    ExpressionPtr ef = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[2]);
    assert(element);
    ExpressionPtr level = GetExpression(element);

    expression = boost::shared_ptr<LogNormalDeviate>(
        new LogNormalDeviate(mean, ef, level));

  } else if (expr_name == "gamma-deviate") {
    assert(args.size() == 2);
    const xmlpp::Element* element =
        dynamic_cast<const xmlpp::Element*>(args[0]);
    assert(element);
    ExpressionPtr k = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[1]);
    assert(element);
    ExpressionPtr theta = GetExpression(element);

    expression = boost::shared_ptr<GammaDeviate>(new GammaDeviate(k, theta));

  } else if (expr_name == "beta-deviate") {
    assert(args.size() == 2);
    const xmlpp::Element* element =
        dynamic_cast<const xmlpp::Element*>(args[0]);
    assert(element);
    ExpressionPtr alpha = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[1]);
    assert(element);
    ExpressionPtr beta = GetExpression(element);

    expression = boost::shared_ptr<BetaDeviate>(new BetaDeviate(alpha, beta));

  } else if (expr_name == "histogram") {
    std::vector<ExpressionPtr> boundaries;
    std::vector<ExpressionPtr> weights;
    xmlpp::NodeSet::iterator it;
    for (it = args.begin(); it != args.end(); ++it) {
      const xmlpp::Element* el = dynamic_cast<const xmlpp::Element*>(*it);
      xmlpp::NodeSet pair = el->find("./*");
      assert(pair.size() == 2);
      const xmlpp::Element* element =
          dynamic_cast<const xmlpp::Element*>(pair[0]);
      assert(element);
      ExpressionPtr bound = GetExpression(element);
      boundaries.push_back(bound);

      element = dynamic_cast<const xmlpp::Element*>(pair[1]);
      assert(element);
      ExpressionPtr weight = GetExpression(element);
      weights.push_back(weight);
    }
    expression = boost::shared_ptr<Histogram>(
        new Histogram(boundaries, weights));

  } else if (expr_name == "exponential") {
    assert(args.size() == 2);
    const xmlpp::Element* element =
        dynamic_cast<const xmlpp::Element*>(args[0]);
    assert(element);
    ExpressionPtr lambda = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[1]);
    assert(element);
    ExpressionPtr time = GetExpression(element);

    expression = boost::shared_ptr<ExponentialExpression>(
        new ExponentialExpression(lambda, time));

  } else if (expr_name == "GLM") {
    assert(args.size() == 4);
    const xmlpp::Element* element =
        dynamic_cast<const xmlpp::Element*>(args[0]);
    assert(element);
    ExpressionPtr gamma = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[1]);
    assert(element);
    ExpressionPtr lambda = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[2]);
    assert(element);
    ExpressionPtr mu = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[3]);
    assert(element);
    ExpressionPtr time = GetExpression(element);

    expression = boost::shared_ptr<GlmExpression>(
        new GlmExpression(gamma, lambda, mu, time));

  } else if (expr_name == "Weibull") {
    assert(args.size() == 4);
    const xmlpp::Element* element =
        dynamic_cast<const xmlpp::Element*>(args[0]);
    assert(element);
    ExpressionPtr alpha = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[1]);
    assert(element);
    ExpressionPtr beta = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[2]);
    assert(element);
    ExpressionPtr t0 = GetExpression(element);

    element = dynamic_cast<const xmlpp::Element*>(args[3]);
    assert(element);
    ExpressionPtr time = GetExpression(element);

    expression = boost::shared_ptr<WeibullExpression>(
        new WeibullExpression(alpha, beta, t0, time));
  } else {
    return false;
  }
  return true;
}

void Initializer::RegisterCcfGroup(const xmlpp::Element* ccf_node) {
  std::string name = ccf_node->get_attribute_value("name");
  boost::trim(name);
  std::string model = ccf_node->get_attribute_value("model");
  boost::trim(model);
  assert(model == "beta-factor" || model == "alpha-factor" ||
         model == "MGL" || model == "phi-factor");

  CcfGroupPtr ccf_group;
  if (model == "beta-factor") {
    ccf_group = CcfGroupPtr(new BetaFactorModel(name));

  } else if (model == "MGL") {
    ccf_group = CcfGroupPtr(new MglModel(name));

  } else if (model == "alpha-factor") {
    ccf_group = CcfGroupPtr(new AlphaFactorModel(name));

  } else if (model == "phi-factor") {
    ccf_group = CcfGroupPtr(new PhiFactorModel(name));
  }

  try {
    model_->AddCcfGroup(ccf_group);
  } catch (ValidationError& err) {
    std::stringstream msg;
    msg << "Line " << ccf_node->get_line() << ":\n";
    msg << err.msg();
    throw ValidationError(msg.str());
  }

  xmlpp::NodeSet members = ccf_node->find("./members");
  assert(members.size() == 1);
  const xmlpp::Element* element =
      dynamic_cast<const xmlpp::Element*>(members[0]);

  Initializer::ProcessCcfMembers(element, ccf_group);

  Initializer::AttachLabelAndAttributes(ccf_node, ccf_group);

  tbd_elements_.push_back(std::make_pair(ccf_group, ccf_node));
}

void Initializer::DefineCcfGroup(const xmlpp::Element* ccf_node,
                                  const CcfGroupPtr& ccf_group) {
  xmlpp::NodeSet children = ccf_node->find("./*");
  xmlpp::NodeSet::iterator it;
  for (it = children.begin(); it != children.end(); ++it) {
    const xmlpp::Element* element = dynamic_cast<const xmlpp::Element*>(*it);

    assert(element);
    std::string name = element->get_name();

    if (name == "distribution") {
      assert(element->find("./*").size() == 1);
      const xmlpp::Element* expr_node =
          dynamic_cast<const xmlpp::Element*>(element->find("./*")[0]);
      ExpressionPtr expression = Initializer::GetExpression(expr_node);
      ccf_group->AddDistribution(expression);

    } else if (name == "factor") {
      Initializer::DefineCcfFactor(element, ccf_group);

    } else if (name == "factors") {
      Initializer::ProcessCcfFactors(element, ccf_group);
    }
  }
}

void Initializer::ProcessCcfMembers(const xmlpp::Element* members_node,
                                     const CcfGroupPtr& ccf_group) {
  xmlpp::NodeSet children = members_node->find("./*");
  assert(!children.empty());
  std::set<std::string> member_ids;
  xmlpp::NodeSet::iterator it;
  for (it = children.begin(); it != children.end(); ++it) {
    const xmlpp::Element* event_node =
        dynamic_cast<const xmlpp::Element*>(*it);
    assert(event_node);
    assert("basic-event" == event_node->get_name());

    std::string name = event_node->get_attribute_value("name");
    boost::trim(name);
    std::string id = name;
    boost::to_lower(id);
    if (member_ids.count(id)) {
      std::stringstream msg;
      msg << "Line " << event_node->get_line() << ":\n";
      msg << name << " is already in CCF group " << ccf_group->name() << ".";
      throw ValidationError(msg.str());
    }
    member_ids.insert(id);
    BasicEventPtr basic_event = BasicEventPtr(new BasicEvent(id));
    basic_event->name(name);
    try {
      model_->AddBasicEvent(basic_event);
    } catch (ValidationError& err) {
      std::stringstream msg;
      msg << "Line " << event_node->get_line() << ":\n";
      msg << err.msg();
      throw ValidationError(msg.str());
    }

    ccf_group->AddMember(basic_event);
  }
}

void Initializer::ProcessCcfFactors(const xmlpp::Element* factors_node,
                                     const CcfGroupPtr& ccf_group) {
  xmlpp::NodeSet children = factors_node->find("./*");
  assert(!children.empty());
  // To keep track of CCF group factor levels.
  xmlpp::NodeSet::iterator it;
  for (it = children.begin(); it != children.end(); ++it) {
    const xmlpp::Element* factor_node =
        dynamic_cast<const xmlpp::Element*>(*it);
    assert(factor_node);
    Initializer::DefineCcfFactor(factor_node, ccf_group);
  }
}

void Initializer::DefineCcfFactor(const xmlpp::Element* factor_node,
                                   const CcfGroupPtr& ccf_group) {
  // Checking the level for one factor input.
  std::string level = factor_node->get_attribute_value("level");
  boost::trim(level);
  if (level.empty()) {
    std::stringstream msg;
    msg << "Line " << factor_node->get_line() << ":\n";
    msg << "CCF group factor level number is not provided.";
    throw ValidationError(msg.str());
  }
  int level_num = boost::lexical_cast<int>(level);
  assert(factor_node->find("./*").size() == 1);
  const xmlpp::Element* expr_node =
      dynamic_cast<const xmlpp::Element*>(factor_node->find("./*")[0]);
  ExpressionPtr expression = Initializer::GetExpression(expr_node);
  try {
    ccf_group->AddFactor(expression, level_num);
  } catch (ValidationError& err) {
    std::stringstream msg;
    msg << "Line " << factor_node->get_line() << ":\n";
    msg << err.msg();
    throw ValidationError(msg.str());
  }
}

void Initializer::ValidateInitialization() {
  // Validation of essential members of analysis in the first layer.
  Initializer::CheckFirstLayer();

  // Validation of fault trees.
  Initializer::CheckSecondLayer();
}

void Initializer::CheckFirstLayer() {
  // Check if all gates have no cycles.
  boost::unordered_map<std::string, GatePtr>::const_iterator it;
  for (it = model_->gates().begin(); it != model_->gates().end(); ++it) {
    std::vector<std::string> cycle;
    if (cycle::DetectCycle<Gate, Formula>(&*it->second, &cycle)) {
      std::string msg = "Detected a cycle in " + it->second->name() +
                        " gate:\n";
      msg += cycle::PrintCycle(cycle);
      throw ValidationError(msg);
    }
  }
  std::stringstream error_messages;
  // Check if all primary events have expressions for probability analysis.
  if (settings_.probability_analysis_) {
    std::string msg = "";
    boost::unordered_map<std::string, BasicEventPtr>::const_iterator it_b;
    for (it_b = model_->basic_events().begin();
         it_b != model_->basic_events().end(); ++it_b) {
      if (!it_b->second->has_expression()) msg += it_b->second->name() + "\n";
    }
    boost::unordered_map<std::string, HouseEventPtr>::const_iterator it_h;
    for (it_h = model_->house_events().begin();
         it_h != model_->house_events().end(); ++it_h) {
      if (!it_h->second->has_expression()) msg += it_h->second->name() + "\n";
    }
    if (!msg.empty()) {
      error_messages << "\nThese primary events do not have expressions:\n"
                     << msg;
    }
  }

  if (!error_messages.str().empty()) {
    throw ValidationError(error_messages.str()); }

  Initializer::ValidateExpressions();
}

void Initializer::CheckSecondLayer() {
  if (!model_->fault_trees().empty()) {
    std::map<std::string, FaultTreePtr>::const_iterator it;
    for (it = model_->fault_trees().begin(); it != model_->fault_trees().end();
         ++it) {
      it->second->Validate();
    }
  }

  if (!model_->ccf_groups().empty()) {
    std::map<std::string, CcfGroupPtr>::const_iterator it;
    for (it = model_->ccf_groups().begin(); it != model_->ccf_groups().end();
         ++it) {
      it->second->Validate();
    }
  }
}

void Initializer::ValidateExpressions() {
  // Check for cycles in parameters. This must be done before expressions.
  if (!model_->parameters().empty()) {
    boost::unordered_map<std::string, ParameterPtr>::const_iterator it;
    for (it = model_->parameters().begin(); it != model_->parameters().end();
         ++it) {
      std::vector<std::string> cycle;
      if (cycle::DetectCycle<Parameter, Expression>(&*it->second, &cycle)) {
        std::string msg = "Detected a cycle in " + it->second->name() +
                          " parameter:\n";
        msg += cycle::PrintCycle(cycle);
        throw ValidationError(msg);
      }
    }
  }

  // Validate expressions.
  if (!expressions_.empty()) {
    try {
      std::vector<ExpressionPtr>::iterator it;
      for (it = expressions_.begin(); it != expressions_.end(); ++it) {
        (*it)->Validate();
      }
    } catch (InvalidArgument& err) {
      throw ValidationError(err.msg());
    }
  }

  // Check probability values for primary events.
  if (settings_.probability_analysis_) {
    std::stringstream msg;
    msg << "";
    if (!model_->ccf_groups().empty()) {
      std::map<std::string, CcfGroupPtr>::const_iterator it;
      for (it = model_->ccf_groups().begin(); it != model_->ccf_groups().end();
           ++it) {
        try {
          it->second->ValidateDistribution();
        } catch (ValidationError& err) {
          msg << it->second->name() << " : " << err.msg() << "\n";
        }
      }
    }
    boost::unordered_map<std::string, BasicEventPtr>::const_iterator it;
    for (it = model_->basic_events().begin();
         it != model_->basic_events().end(); ++it) {
      try {
        it->second->Validate();
      } catch (ValidationError& err) {
        msg << it->second->name() << " : " << err.msg() << "\n";
      }
    }
    if (!msg.str().empty()) {
      std::string head = "Invalid probabilities detected:\n";
      throw ValidationError(head + msg.str());
    }
  }
}

void Initializer::SetupForAnalysis() {
  // CCF groups.
  if (!model_->ccf_groups().empty()) {
    std::map<std::string, CcfGroupPtr>::const_iterator it;
    for (it = model_->ccf_groups().begin(); it != model_->ccf_groups().end();
         ++it) {
      it->second->ApplyModel();
    }
  }
}

}  // namespace scram