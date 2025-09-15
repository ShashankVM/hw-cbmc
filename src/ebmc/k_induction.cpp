/*******************************************************************\

Module: k Induction

Author: Daniel Kroening, daniel.kroening@inf.ethz.ch

\*******************************************************************/

#include "k_induction.h"

#include <util/string2int.h>

#include <temporal-logic/temporal_logic.h>
#include <trans-word-level/instantiate_word_level.h>
#include <trans-word-level/trans_trace_word_level.h>
#include <trans-word-level/unwind.h>

#include "bmc.h"
#include "ebmc_error.h"
#include "ebmc_solver_factory.h"
#include "instrument_past.h"
#include "liveness_to_safety.h"

#include <fstream>

/*******************************************************************\

   Class: k_inductiont

 Purpose:

\*******************************************************************/

class k_inductiont
{
public:
  k_inductiont(
    std::size_t _k,
    const transition_systemt &_transition_system,
    ebmc_propertiest &_properties,
    const ebmc_solver_factoryt &_solver_factory,
    message_handlert &_message_handler)
    : k(_k),
      transition_system(_transition_system),
      properties(_properties),
      solver_factory(_solver_factory),
      message(_message_handler)
  {
  }

  void operator()();

  static bool
  have_supported_property(const ebmc_propertiest::propertiest &properties)
  {
    for(auto &p : properties)
      if(supported(p))
        return true;
    return false;
  }

protected:
  const std::size_t k;
  const transition_systemt &transition_system;
  ebmc_propertiest &properties;
  const ebmc_solver_factoryt &solver_factory;
  messaget message;

  void induction_base();
  void induction_step();

  static bool supported(const ebmc_propertiest::propertyt &p)
  {
    auto &expr = p.normalized_expr;
    if(expr.id() == ID_sva_always || expr.id() == ID_AG || expr.id() == ID_G)
    {
      // Must be AG p or equivalent.
      auto &op = to_unary_expr(expr).op();
      return !has_temporal_operator(op);
    }
    else
      return false;
  }
};

/*******************************************************************\

Function: k_induction

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

property_checker_resultt k_induction(
  std::size_t k,
  const transition_systemt &transition_system,
  const ebmc_propertiest &properties,
  const ebmc_solver_factoryt &solver_factory,
  message_handlert &message_handler)
{
  // copy
  auto properties_copy = properties;

  // Are there any properties suitable for k-induction?
  // Fail early if not.
  if(!k_inductiont::have_supported_property(properties.properties))
  {
    for(auto &property : properties_copy.properties)
    {
      if(
        !property.is_assumed() && !property.is_disabled() &&
        !property.is_proved())
      {
        property.unsupported("unsupported by k-induction");
      }
    }
    return property_checker_resultt{properties_copy};
  }

  k_inductiont(
    k, transition_system, properties_copy, solver_factory, message_handler)();

  return property_checker_resultt{properties_copy};
}

/*******************************************************************\

Function: k_induction

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

property_checker_resultt k_induction(
  const cmdlinet &cmdline,
  const transition_systemt &transition_system,
  ebmc_propertiest &properties,
  message_handlert &message_handler)
{
  std::size_t k = [&cmdline, &message_handler]() -> std::size_t {
    if(!cmdline.isset("bound"))
    {
      messaget message(message_handler);
      message.warning() << "using 1-induction" << messaget::eom;
      return 1;
    }
    else
      return unsafe_string2unsigned(cmdline.get_value("bound"));
  }();

  if(properties.properties.empty())
    throw ebmc_errort() << "no properties";

  // Are there any properties suitable for k-induction?
  // Fail early if not.
  if(!k_inductiont::have_supported_property(properties.properties))
  {
    throw ebmc_errort() << "there is no property suitable for k-induction";
  }

  auto solver_factory = ebmc_solver_factory(cmdline);

  return k_induction(
    k, transition_system, properties, solver_factory, message_handler);
}

/*******************************************************************\

Function: k_inductiont::operator()

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void k_inductiont::operator()()
{
  // check that $past is not present
  PRECONDITION(!has_past(transition_system, properties));

  // Unsupported assumption? Mark as such.
  bool assumption_unsupported = false;
  for(auto &property : properties.properties)
  {
    if(!supported(property) && property.is_assumed())
    {
      assumption_unsupported = true;
      property.unsupported("unsupported by k-induction");
    }
  }

  // Fail unsupported properties that are not proved yet
  for(auto &property : properties.properties)
  {
    if(
      !supported(property) && !property.is_assumed() &&
      !property.is_disabled() && !property.is_proved())
    {
      property.unsupported("unsupported by k-induction");
    }
  }

  // do induction base
  induction_base();

  // do induction step
  induction_step();

  // Any refuted properties are really inconclusive if there are
  // unsupported assumptions, as the assumption might have
  // proven the property.
  if(assumption_unsupported)
  {
    for(auto &property : properties.properties)
    {
      if(property.is_refuted())
        property.inconclusive();
    }
  }
}

/*******************************************************************\

Function: k_inductiont::induction_base

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void k_inductiont::induction_base()
{
  message.status() << "Induction Base" << messaget::eom;

  auto result = bmc(
    k,
    false, // convert_only
    false, // bmc_with_assumptions
    transition_system,
    properties,
    solver_factory,
    message.get_message_handler());

  properties.properties = std::move(result.properties);
}

/*******************************************************************\

Function: k_inductiont::induction_step

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void k_inductiont::induction_step()
{
  message.status() << "Induction Step" << messaget::eom;

  const std::size_t no_timeframes = k + 1;
  const namespacet ns(transition_system.symbol_table);

  for(auto &p_it : properties.properties)
  {
    if(
      p_it.is_disabled() || p_it.is_failure() || p_it.is_assumed() ||
      p_it.is_unsupported() || p_it.is_proved())
    {
      continue;
    }

    // If it's not failed, then it's supported.
    DATA_INVARIANT(supported(p_it), "property must be supported");

    // Do not run the step case for properties that have
    // failed the base case already. Properties may pass the step
    // case, but are still false when the base case fails.
    if(p_it.is_refuted())
      continue;

    auto solver_wrapper = solver_factory(ns, message.get_message_handler());
    auto &solver = solver_wrapper.decision_procedure();

    // *no* initial state
    unwind(
      transition_system.trans_expr,
      message.get_message_handler(),
      solver,
      no_timeframes,
      ns,
      false);

    // add all assumptions for all time frames
    for(auto &property : properties.properties)
      if(property.is_assumed())
      {
        const exprt &p = to_unary_expr(property.normalized_expr).op();
        for(std::size_t c = 0; c < no_timeframes; c++)
        {
          exprt tmp = instantiate(p, c, no_timeframes);
          solver.set_to_true(tmp);
        }
      }

    const exprt property(p_it.normalized_expr);
    const exprt &p = to_unary_expr(property).op();

    // assumption: time frames 0,...,k-1
    for(std::size_t c = 0; c < no_timeframes - 1; c++)
    {
      exprt tmp = instantiate(p, c, no_timeframes - 1);
      solver.set_to_true(tmp);
    }
    
    // property: time frame k
    {
      exprt tmp = instantiate(p, no_timeframes - 1, no_timeframes);
      solver.set_to_false(tmp);
    }

    decision_proceduret::resultt dec_result = solver();

    switch(dec_result)
    {
    case decision_proceduret::resultt::D_SATISFIABLE:
      message.result()
        << "SAT: inductive proof failed, k-induction is inconclusive"
        << messaget::eom;
      p_it.inconclusive();
      break;

    case decision_proceduret::resultt::D_UNSATISFIABLE:
      message.result() << "UNSAT: inductive proof successful, property holds"
                       << messaget::eom;
      p_it.proved(std::to_string(no_timeframes - 1) + "-induction");
      break;

    case decision_proceduret::resultt::D_ERROR:
      throw ebmc_errort() << "Error from decision procedure";

    default:
      throw ebmc_errort() << "Unexpected result from decision procedure";
    }
  }
}
