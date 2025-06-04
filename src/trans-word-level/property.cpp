/*******************************************************************\

Module: Unwinding the Properties

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "property.h"

#include <util/arith_tools.h>
#include <util/ebmc_util.h>
#include <util/expr_iterator.h>
#include <util/expr_util.h>
#include <util/namespace.h>
#include <util/std_expr.h>
#include <util/symbol_table.h>

#include <ebmc/ebmc_error.h>
#include <temporal-logic/ctl.h>
#include <temporal-logic/ltl.h>
#include <temporal-logic/nnf.h>
#include <temporal-logic/temporal_logic.h>
#include <verilog/sva_expr.h>

#include "instantiate_word_level.h"
#include "obligations.h"
#include "sequence.h"

#include <cstdlib>

/*******************************************************************\

Function: bmc_supports_LTL_property

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool bmc_supports_LTL_property(const exprt &expr)
{
  return true;
}

/*******************************************************************\

Function: bmc_supports_CTL_property

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool bmc_supports_CTL_property(const exprt &expr)
{
  // We map a subset of ACTL to LTL, following
  // Monika Maidl. "The common fragment of CTL and LTL"
  // http://dx.doi.org/10.1109/SFCS.2000.892332
  //
  // Specificially, we allow
  // * state predicates
  // * conjunctions of allowed formulas
  // * AX φ, where φ is allowed
  // * AF φ, where φ is allowed
  // * AG φ, where φ is allowed
  if(!has_CTL_operator(expr))
  {
    return true;
  }
  else if(expr.id() == ID_and)
  {
    for(auto &op : expr.operands())
      if(!bmc_supports_CTL_property(op))
        return false;
    return true;
  }
  else if(expr.id() == ID_AX)
  {
    return bmc_supports_CTL_property(to_AX_expr(expr).op());
  }
  else if(expr.id() == ID_AF)
  {
    return bmc_supports_CTL_property(to_AF_expr(expr).op());
  }
  else if(expr.id() == ID_AG)
  {
    return bmc_supports_CTL_property(to_AG_expr(expr).op());
  }
  else
    return false;
}

/*******************************************************************\

Function: bmc_supports_SVA_property

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool bmc_supports_SVA_property(const exprt &expr)
{
  return true;
}

/*******************************************************************\

Function: bmc_supports_property

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool bmc_supports_property(const exprt &expr)
{
  if(is_LTL(expr))
    return bmc_supports_LTL_property(expr);
  else if(is_CTL(expr))
    return bmc_supports_CTL_property(expr);
  else if(is_SVA(expr))
    return bmc_supports_SVA_property(expr);
  else
    return false; // unknown category
}

/*******************************************************************\

Function: sva_sequence_semantics

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static sva_sequence_semanticst sva_sequence_semantics(irep_idt id)
{
  if(id == ID_sva_strong)
    return sva_sequence_semanticst::STRONG;
  else if(id == ID_sva_weak)
    return sva_sequence_semanticst::WEAK;
  else if(id == ID_sva_implicit_strong)
    return sva_sequence_semanticst::STRONG;
  else if(id == ID_sva_implicit_weak)
    return sva_sequence_semanticst::WEAK;
  else
    PRECONDITION(false);
}

/*******************************************************************\

Function: property_obligations_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static obligationst property_obligations_rec(
  const exprt &property_expr,
  const mp_integer &current,
  const mp_integer &no_timeframes)
{
  PRECONDITION(current >= 0 && current < no_timeframes);

  if(
    property_expr.id() == ID_AG || property_expr.id() == ID_G ||
    property_expr.id() == ID_sva_always)
  {
    // We want AG phi.
    auto &phi = [](const exprt &expr) -> const exprt & {
      if(expr.id() == ID_AG)
        return to_AG_expr(expr).op();
      else if(expr.id() == ID_G)
        return to_G_expr(expr).op();
      else if(expr.id() == ID_sva_always)
        return to_sva_always_expr(expr).op();
      else
        PRECONDITION(false);
    }(property_expr);

    obligationst obligations;

    for(mp_integer c = current; c < no_timeframes; ++c)
    {
      obligations.add(property_obligations_rec(phi, c, no_timeframes));
    }

    return obligations;
  }
  else if(property_expr.id() == ID_sva_eventually)
  {
    const auto &eventually_expr = to_sva_eventually_expr(property_expr);
    const auto &op = eventually_expr.op();

    mp_integer from = numeric_cast_v<mp_integer>(eventually_expr.from());

    mp_integer to;
    if(to_integer_non_constant(eventually_expr.to(), to))
      throw "failed to convert sva_eventually index";

    // We rely on NNF.
    if(current + from >= no_timeframes || current + to >= no_timeframes)
    {
      DATA_INVARIANT(no_timeframes != 0, "must have timeframe");
      return obligationst{no_timeframes - 1, true_exprt()};
    }

    exprt::operandst disjuncts = {};

    for(mp_integer u = current + from; u <= current + to; ++u)
    {
      auto obligations_rec = property_obligations_rec(op, u, no_timeframes);
      disjuncts.push_back(obligations_rec.conjunction().second);
    }

    DATA_INVARIANT(no_timeframes != 0, "must have timeframe");
    return obligationst{no_timeframes - 1, disjunction(disjuncts)};
  }
  else if(
    property_expr.id() == ID_AF || property_expr.id() == ID_F ||
    property_expr.id() == ID_sva_s_eventually)
  {
    const auto &phi = to_unary_expr(property_expr).op();

    obligationst obligations;

    // Traces with any φ state from "current" onwards satisfy Fφ
    exprt::operandst phi_disjuncts;

    phi_disjuncts.reserve(numeric_cast_v<std::size_t>(no_timeframes - current));

    for(mp_integer j = current; j < no_timeframes; ++j)
    {
      auto tmp = property_obligations_rec(phi, j, no_timeframes);
      phi_disjuncts.push_back(tmp.conjunction().second);
    }

    auto phi_disjunction = disjunction(phi_disjuncts);

    // Counterexamples to Fφ must have a loop.
    // We consider l-k loops with l<k.
    for(mp_integer k = current + 1; k < no_timeframes; ++k)
    {
      // The following needs to be satisfied for a counterexample
      // to Fφ that loops back in timeframe k:
      //
      // (1) There is a loop from timeframe k back to
      //     some earlier state l with current<=l<k.
      // (2) No state j with current<=j<no_timeframes satisfies 'φ'.
      //     The weaker alternative current<=j<=k yields counterexamples
      //     that exhibit a ¬φ loop, but are then followed by a φ state.
      for(mp_integer l = current; l < k; ++l)
      {
        auto tmp = or_exprt{not_exprt(lasso_symbol(l, k)), phi_disjunction};
        obligations.add(k, std::move(tmp));
      }
    }

    return obligations;
  }
  else if(property_expr.id() == ID_sva_ranged_s_eventually)
  {
    const auto &s_eventually = to_sva_ranged_s_eventually_expr(property_expr);
    auto from = numeric_cast_v<mp_integer>(s_eventually.from());

    if(from < 0)
      throw ebmc_errort() << "SVA s_eventually from index must not be negative";

    from = std::min(no_timeframes - 1, current + from);

    mp_integer to;

    if(s_eventually.is_unbounded())
    {
      throw ebmc_errort()
        << "failed to convert SVA s_eventually to index (infinity)";
    }
    else
    {
      auto to_opt = numeric_cast<mp_integer>(s_eventually.to());
      if(!to_opt.has_value())
        throw ebmc_errort() << "failed to convert SVA s_eventually to index";
      to = std::min(current + *to_opt, no_timeframes - 1);
    }

    exprt::operandst disjuncts;
    mp_integer time = 0;

    for(mp_integer c = from; c <= to; ++c)
    {
      auto tmp = property_obligations_rec(s_eventually.op(), c, no_timeframes)
                   .conjunction();
      time = std::max(time, tmp.first);
      disjuncts.push_back(tmp.second);
    }

    return obligationst{time, disjunction(disjuncts)};
  }
  else if(
    property_expr.id() == ID_sva_ranged_always ||
    property_expr.id() == ID_sva_s_always)
  {
    auto &phi = property_expr.id() == ID_sva_ranged_always
                  ? to_sva_ranged_always_expr(property_expr).op()
                  : to_sva_s_always_expr(property_expr).op();
    auto &from_expr = property_expr.id() == ID_sva_ranged_always
                        ? to_sva_ranged_always_expr(property_expr).from()
                        : to_sva_s_always_expr(property_expr).from();
    auto &to_expr = property_expr.id() == ID_sva_ranged_always
                      ? to_sva_ranged_always_expr(property_expr).to()
                      : to_sva_s_always_expr(property_expr).to();

    auto from_opt = numeric_cast<mp_integer>(from_expr);
    if(!from_opt.has_value())
      throw ebmc_errort() << "failed to convert SVA always from index";

    if(*from_opt < 0)
      throw ebmc_errort() << "SVA always from index must not be negative";

    auto from = current + *from_opt;

    mp_integer to;

    if(to_expr.id() == ID_infinity)
    {
      to = no_timeframes - 1;
    }
    else
    {
      auto to_opt = numeric_cast<mp_integer>(to_expr);
      if(!to_opt.has_value())
        throw ebmc_errort() << "failed to convert SVA always to index";
      to = std::min(current + *to_opt, no_timeframes - 1);
    }

    obligationst obligations;

    for(mp_integer c = from; c <= to; ++c)
    {
      obligations.add(property_obligations_rec(phi, c, no_timeframes));
    }

    return obligations;
  }
  else if(
    property_expr.id() == ID_X || property_expr.id() == ID_AX ||
    property_expr.id() == ID_sva_nexttime ||
    property_expr.id() == ID_sva_s_nexttime)
  {
    const auto next = current + 1;

    auto &phi = [](const exprt &expr) -> const exprt &
    {
      if(expr.id() == ID_X)
        return to_X_expr(expr).op();
      else if(expr.id() == ID_AX)
        return to_AX_expr(expr).op();
      else if(expr.id() == ID_sva_nexttime)
        return to_sva_nexttime_expr(expr).op();
      else if(expr.id() == ID_sva_s_nexttime)
        return to_sva_s_nexttime_expr(expr).op();
      else
        PRECONDITION(false);
    }(property_expr);

    if(next < no_timeframes)
    {
      return property_obligations_rec(phi, next, no_timeframes);
    }
    else
    {
      DATA_INVARIANT(no_timeframes != 0, "must have timeframe");
      return obligationst{no_timeframes - 1, true_exprt()}; // works on NNF only
    }
  }
  else if(property_expr.id() == ID_sva_s_until || property_expr.id() == ID_U)
  {
    auto &p = to_binary_expr(property_expr).lhs();
    auto &q = to_binary_expr(property_expr).rhs();

    // p U q ≡ Fq ∧ (p W q)
    exprt tmp = and_exprt{F_exprt{q}, weak_U_exprt{p, q}};

    return property_obligations_rec(tmp, current, no_timeframes);
  }
  else if(property_expr.id() == ID_sva_until || property_expr.id() == ID_weak_U)
  {
    // we expand: p W q ≡ q ∨ ( p ∧ X(p W q) )
    auto &p = to_binary_expr(property_expr).lhs();
    auto &q = to_binary_expr(property_expr).rhs();

    // Once we reach the end of the unwinding, replace X(p W q) by 'true'.
    auto tmp = or_exprt{
      q,
      (current + 1) < no_timeframes ? and_exprt{p, X_exprt{property_expr}} : p};

    return property_obligations_rec(tmp, current, no_timeframes);
  }
  else if(property_expr.id() == ID_R)
  {
    // we expand: p R q <=> q ∧ (p ∨ X(p R q))
    auto &R_expr = to_R_expr(property_expr);
    auto &p = R_expr.lhs();
    auto &q = R_expr.rhs();

    // Once we reach the end of the unwinding, we replace X(p R q) by
    // true, and hence the expansion becomes "q" only.
    exprt expansion = (current + 1) < no_timeframes
                        ? and_exprt{q, or_exprt{p, X_exprt{property_expr}}}
                        : q;

    return property_obligations_rec(expansion, current, no_timeframes);
  }
  else if(property_expr.id() == ID_strong_R)
  {
    auto &p = to_strong_R_expr(property_expr).lhs();
    auto &q = to_strong_R_expr(property_expr).rhs();

    // p strongR q ≡ Fp ∧ (p R q)
    exprt tmp = and_exprt{F_exprt{q}, weak_U_exprt{p, q}};

    return property_obligations_rec(tmp, current, no_timeframes);
  }
  else if(property_expr.id() == ID_sva_until_with)
  {
    // Rewrite to LTL (weak) R.
    // Note that lhs and rhs are flipped.
    auto &until_with = to_sva_until_with_expr(property_expr);
    auto R = R_exprt{until_with.rhs(), until_with.lhs()};
    return property_obligations_rec(R, current, no_timeframes);
  }
  else if(property_expr.id() == ID_sva_s_until_with)
  {
    // Rewrite to LTL (strong) R.
    // Note that lhs and rhs are flipped.
    auto &s_until_with = to_sva_s_until_with_expr(property_expr);
    auto strong_R = strong_R_exprt{s_until_with.rhs(), s_until_with.lhs()};
    return property_obligations_rec(strong_R, current, no_timeframes);
  }
  else if(property_expr.id() == ID_and)
  {
    // Generate seperate sets of obligations for each conjunct,
    // and then return the union.
    obligationst obligations;

    for(auto &op : to_and_expr(property_expr).operands())
    {
      obligations.add(property_obligations_rec(op, current, no_timeframes));
    }

    return obligations;
  }
  else if(property_expr.id() == ID_or)
  {
    // Generate seperate obligations for each disjunct,
    // and then 'or' these.
    mp_integer t = 0;
    exprt::operandst disjuncts;
    obligationst obligations;

    for(auto &op : to_or_expr(property_expr).operands())
    {
      auto obligations = property_obligations_rec(op, current, no_timeframes);
      auto conjunction = obligations.conjunction();
      t = std::max(t, conjunction.first);
      disjuncts.push_back(conjunction.second);
    }

    return obligationst{t, disjunction(disjuncts)};
  }
  else if(
    property_expr.id() == ID_equal &&
    to_equal_expr(property_expr).lhs().type().id() == ID_bool)
  {
    // we rely on NNF: a<=>b ---> a=>b && b=>a
    auto &equal_expr = to_equal_expr(property_expr);
    auto tmp = and_exprt{
      implies_exprt{equal_expr.lhs(), equal_expr.rhs()},
      implies_exprt{equal_expr.rhs(), equal_expr.lhs()}};
    return property_obligations_rec(tmp, current, no_timeframes);
  }
  else if(property_expr.id() == ID_implies)
  {
    // we rely on NNF
    auto &implies_expr = to_implies_expr(property_expr);
    auto tmp = or_exprt{not_exprt{implies_expr.lhs()}, implies_expr.rhs()};
    return property_obligations_rec(tmp, current, no_timeframes);
  }
  else if(property_expr.id() == ID_if)
  {
    // we rely on NNF
    auto &if_expr = to_if_expr(property_expr);
    auto cond = instantiate_property(if_expr.cond(), current, no_timeframes);
    auto obligations_true =
      property_obligations_rec(if_expr.true_case(), current, no_timeframes)
        .conjunction();
    auto obligations_false =
      property_obligations_rec(if_expr.false_case(), current, no_timeframes)
        .conjunction();
    return obligationst{
      std::max(obligations_true.first, obligations_false.first),
      if_exprt{cond, obligations_true.second, obligations_false.second}};
  }
  else if(
    property_expr.id() == ID_typecast &&
    to_typecast_expr(property_expr).op().type().id() == ID_bool)
  {
    // drop reduntant type casts
    return property_obligations_rec(
      to_typecast_expr(property_expr).op(), current, no_timeframes);
  }
  else if(property_expr.id() == ID_not)
  {
    // We need NNF, try to eliminate the negation.
    auto &op = to_not_expr(property_expr).op();

    auto op_negated_opt = negate_property_node(op);

    if(op_negated_opt.has_value())
    {
      return property_obligations_rec(
        op_negated_opt.value(), current, no_timeframes);
    }
    else if(
      op.id() == ID_sva_strong || op.id() == ID_sva_weak ||
      op.id() == ID_sva_implicit_strong || op.id() == ID_sva_implicit_weak)
    {
      auto &sequence = to_sva_sequence_property_expr_base(op).sequence();
      auto semantics = sva_sequence_semantics(op.id());

      const auto matches =
        instantiate_sequence(sequence, semantics, current, no_timeframes);

      obligationst obligations;

      for(auto &match : matches)
      {
        // The sequence must not match.
        if(!match.empty_match())
          obligations.add(match.end_time, not_exprt{match.condition});
      }

      return obligations;
    }
    else if(is_temporal_operator(op))
    {
      throw ebmc_errort() << "failed to make NNF for " << op.id();
    }
    else
    {
      // state formula
      return obligationst{
        current, instantiate_property(property_expr, current, no_timeframes)};
    }
  }
  else if(property_expr.id() == ID_sva_implies)
  {
    // We need NNF, hence we go via implies_exprt.
    // Note that this is not an SVA sequence operator.
    auto &sva_implies_expr = to_sva_implies_expr(property_expr);
    auto implies_expr =
      implies_exprt{sva_implies_expr.lhs(), sva_implies_expr.rhs()};
    return property_obligations_rec(implies_expr, current, no_timeframes);
  }
  else if(property_expr.id() == ID_sva_iff)
  {
    // We need NNF, hence we go via equal_exprt.
    // Note that this is not an SVA sequence operator.
    auto &sva_iff_expr = to_sva_iff_expr(property_expr);
    auto equal_expr = equal_exprt{sva_iff_expr.lhs(), sva_iff_expr.rhs()};
    return property_obligations_rec(equal_expr, current, no_timeframes);
  }
  else if(
    property_expr.id() == ID_sva_overlapped_implication ||
    property_expr.id() == ID_sva_non_overlapped_implication)
  {
    auto &implication = to_binary_expr(property_expr);

    // The LHS is a sequence, the RHS is a property.
    // The implication must hold for _all_ (strong) matches on the LHS,
    // i.e., each pair of LHS match and RHS obligation yields an obligation.
    const auto lhs_match_points = instantiate_sequence(
      implication.lhs(),
      sva_sequence_semanticst::STRONG,
      current,
      no_timeframes);

    obligationst result;

    for(auto &lhs_match_point : lhs_match_points)
    {
      // The RHS of the non-overlapped implication starts one timeframe later
      auto t_rhs = property_expr.id() == ID_sva_non_overlapped_implication
                     ? lhs_match_point.end_time + 1
                     : lhs_match_point.end_time;

      // Do we exceed the bound? Make it 'true'
      if(t_rhs >= no_timeframes)
      {
        DATA_INVARIANT(no_timeframes != 0, "must have timeframe");
        return obligationst{no_timeframes - 1, true_exprt()};
      }

      // Get obligations for RHS
      auto rhs_obligations_rec =
        property_obligations_rec(implication.rhs(), t_rhs, no_timeframes);

      for(auto &rhs_obligation : rhs_obligations_rec.map)
      {
        auto rhs_conjunction = conjunction(rhs_obligation.second);
        auto cond = implies_exprt{lhs_match_point.condition, rhs_conjunction};
        result.add(rhs_obligation.first, cond);
      }
    }

    return result;
  }
  else if(
    property_expr.id() == ID_sva_nonoverlapped_followed_by ||
    property_expr.id() == ID_sva_overlapped_followed_by)
  {
    // The LHS is a sequence, the RHS is a property expression,
    // the result is a property expression.
    auto &followed_by = to_sva_followed_by_expr(property_expr);

    // get (proper) match points for LHS sequence
    auto matches = instantiate_sequence(
      followed_by.antecedent(),
      sva_sequence_semanticst::STRONG,
      current,
      no_timeframes);

    exprt::operandst disjuncts;
    mp_integer t = current;

    for(auto &match : matches)
    {
      mp_integer property_start = match.end_time;

      // #=# advances the clock by one from the sequence match point
      if(property_expr.id() == ID_sva_nonoverlapped_followed_by)
        property_start += 1;

      // at the end?
      if(property_start >= no_timeframes)
      {
        // relies on NNF
        t = std::max(t, no_timeframes - 1);
        disjuncts.push_back(match.condition);
      }
      else
      {
        auto obligations_rec =
          property_obligations_rec(
            followed_by.consequent(), property_start, no_timeframes)
            .conjunction();

        disjuncts.push_back(and_exprt{match.condition, obligations_rec.second});
        t = std::max(t, obligations_rec.first);
      }
    }
    return obligationst{t, disjunction(disjuncts)};
  }
  else if(
    property_expr.id() == ID_sva_strong || property_expr.id() == ID_sva_weak ||
    property_expr.id() == ID_sva_implicit_strong ||
    property_expr.id() == ID_sva_implicit_weak)
  {
    // sequence expressions -- these may have multiple potential
    // match points, and evaluate to true if any of them matches
    auto &sequence =
      to_sva_sequence_property_expr_base(property_expr).sequence();
    auto semantics = sva_sequence_semantics(property_expr.id());

    const auto matches =
      instantiate_sequence(sequence, semantics, current, no_timeframes);
    exprt::operandst disjuncts;
    disjuncts.reserve(matches.size());
    mp_integer max = current;

    for(auto &match : matches)
    {
      // empty matches are not considered
      if(!match.empty_match())
      {
        disjuncts.push_back(match.condition);
        max = std::max(max, match.end_time);
      }
    }

    return obligationst{max, disjunction(disjuncts)};
  }
  else if(property_expr.id() == ID_sva_sequence_property)
  {
    // Should have been turned into sva_implict_weak or sva_implict_strong in the type checker.
    PRECONDITION(false);
  }
  else
  {
    return obligationst{
      current, instantiate_property(property_expr, current, no_timeframes)};
  }
}

/*******************************************************************\

Function: property_obligations

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

obligationst property_obligations(
  const exprt &property_expr,
  const mp_integer &t,
  const mp_integer &no_timeframes)
{
  return property_obligations_rec(property_expr, t, no_timeframes);
}

/*******************************************************************\

Function: property_obligations

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

obligationst property_obligations(
  const exprt &property_expr,
  const mp_integer &no_timeframes)
{
  return property_obligations_rec(property_expr, 0, no_timeframes);
}

/*******************************************************************\

Function: property

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt::operandst property(
  const exprt &property_expr,
  message_handlert &message_handler,
  decision_proceduret &solver,
  std::size_t no_timeframes,
  const namespacet &)
{
  // The first element of the pair is the length of the
  // counterexample, and the second is the condition that
  // must be valid for the property to hold.
  auto obligations = property_obligations(property_expr, no_timeframes);

  // Map obligations onto timeframes.
  exprt::operandst prop_handles{no_timeframes, true_exprt()};

  for(auto &obligation_it : obligations.map)
  {
    auto t = obligation_it.first;
    DATA_INVARIANT(
      t >= 0 && t < no_timeframes, "obligation must have valid timeframe");
    auto t_int = numeric_cast_v<std::size_t>(t);
    prop_handles[t_int] = solver.handle(conjunction(obligation_it.second));
  }

  return prop_handles;
}
