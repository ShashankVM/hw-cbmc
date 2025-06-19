/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_VERILOG_EXPR2VERILOG_H
#define CPROVER_VERILOG_EXPR2VERILOG_H

#include <util/bitvector_expr.h>
#include <util/std_expr.h>

class sva_abort_exprt;
class sva_case_exprt;
class sva_if_exprt;
class sva_ranged_predicate_exprt;
class sva_cycle_delay_exprt;
class sva_sequence_first_match_exprt;
class sva_sequence_repetition_exprt;

// Precedences (higher means binds more strongly).
// Follows Table 11-2 in IEEE 1800-2017.
// We deviate from the table for the precedence of concatenation
// and replication, which act like parentheses.
enum class verilog_precedencet
{
  MAX = 19,
  MEMBER = 18,   //  [ ]  bit-select  ( )  parenthesis  ::   .
  NOT = 17,      //  unary !  ~  &  |  ~&  ~|  ^  ~^  ^~  +  -
  POWER = 16,    //  **  power
  MULT = 15,     //  *  /  %
  ADD = 14,      //  + -
  SHIFT = 13,    //  <<  >>  <<<  >>>
  RELATION = 12, //  >  >=  <  <= inside dist
  EQUALITY = 11, //  ==  !=  ===  !==  ==?  !=?
  BITAND = 10,   //  &
  XOR = 9,       //  ^  ~^  ^~ (binary)
  BITOR = 8,     //  |
  AND = 7,       //  &&
  OR = 6,        //  ||
  IF = 5,        //  ?:
  IMPLIES = 4,   //  -> <->
  ASSIGN = 3,    //  = += -= etc.
  CONCAT = 18,   //  { } concatenation, {{ }} replication
  MIN = 0        //  anything even weaker, e.g., SVA
};

class expr2verilogt
{
public:
  explicit expr2verilogt(const namespacet &__ns) : ns(__ns)
  {
  }

  virtual ~expr2verilogt()
  {
  }

  virtual std::string convert(const typet &);
  virtual std::string convert(const exprt &);

protected:
  struct resultt
  {
    resultt(verilog_precedencet _p, std::string _s) : p(_p), s(std::move(_s))
    {
    }

    verilog_precedencet p;
    std::string s;
  };

  virtual resultt convert_rec(const exprt &src);

  resultt convert_array(const exprt &src, verilog_precedencet);

  resultt convert_binary(
    const multi_ary_exprt &,
    const std::string &symbol,
    verilog_precedencet);

  resultt convert_unary(
    const unary_exprt &,
    const std::string &symbol,
    verilog_precedencet);

  resultt convert_if(const if_exprt &, verilog_precedencet);

  resultt convert_index(const index_exprt &, verilog_precedencet);

  resultt convert_extractbit(const extractbit_exprt &, verilog_precedencet);

  resultt convert_member(const member_exprt &, verilog_precedencet);

  resultt convert_extractbits(const extractbits_exprt &, verilog_precedencet);

  resultt convert_symbol(const exprt &);

  resultt
  convert_hierarchical_identifier(const class hierarchical_identifier_exprt &);

  resultt convert_nondet_symbol(const exprt &);

  resultt convert_next_symbol(const exprt &);

  resultt convert_constant(const constant_exprt &);

  resultt
  convert_explicit_const_cast(const class verilog_explicit_const_cast_exprt &);

  resultt convert_explicit_signing_cast(
    const class verilog_explicit_signing_cast_exprt &);

  resultt
  convert_explicit_type_cast(const class verilog_explicit_type_cast_exprt &);

  resultt convert_typecast(const typecast_exprt &);

  resultt
  convert_explicit_size_cast(const class verilog_explicit_size_cast_exprt &);

  resultt
  convert_concatenation(const concatenation_exprt &, verilog_precedencet);

  resultt convert_function(const std::string &name, const exprt &src);

  resultt convert_sva_case(const sva_case_exprt &);

  resultt convert_sva_ranged_predicate(
    const std::string &name,
    const sva_ranged_predicate_exprt &);

  resultt convert_sva_unary(const std::string &name, const unary_exprt &);

  resultt convert_sva_unary(const unary_exprt &, const std::string &name);

  resultt convert_sva_binary(const std::string &name, const binary_exprt &);

  resultt
  convert_sva_cycle_delay(const std::string &symbol, const binary_exprt &);

  resultt convert_sva_sequence_repetition(
    const std::string &name,
    const sva_sequence_repetition_exprt &);

  resultt convert_sva_abort(const std::string &name, const sva_abort_exprt &);

  resultt
  convert_sva_indexed_binary(const std::string &name, const binary_exprt &);

  resultt convert_replication(const replication_exprt &, verilog_precedencet);

  resultt convert_norep(const exprt &);

  resultt convert_with(const with_exprt &, verilog_precedencet);

  resultt convert_sva_cycle_delay(const sva_cycle_delay_exprt &);

  resultt convert_sva_if(const sva_if_exprt &);

  resultt convert_sva_sequence_concatenation(const binary_exprt &);

  resultt
  convert_sva_sequence_first_match(const sva_sequence_first_match_exprt &);

  resultt convert_function_call(const class function_call_exprt &);

  resultt convert_non_indexed_part_select(
    const class verilog_non_indexed_part_select_exprt &,
    verilog_precedencet precedence);

  resultt convert_indexed_part_select(
    const class verilog_indexed_part_select_plus_or_minus_exprt &,
    verilog_precedencet precedence);

  resultt convert_streaming_concatenation(
    const std::string &name,
    const class verilog_streaming_concatenation_exprt &);

  resultt convert_inside(const class verilog_inside_exprt &);

  resultt convert_value_range(const class verilog_value_range_exprt &);

protected:
  const namespacet &ns;
};

#endif
