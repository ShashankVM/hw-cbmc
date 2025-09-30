/*******************************************************************\

Module: Verilog Expression Type Checker

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_VERILOG_TYPECHEK_EXPR_H
#define CPROVER_VERILOG_TYPECHEK_EXPR_H

#include <util/bitvector_expr.h>
#include <util/mp_arith.h>
#include <util/namespace.h>
#include <util/std_expr.h>

#include "sva_expr.h"
#include "verilog_typecheck_base.h"

#include <stack>

class function_call_exprt;
class power_exprt;
class verilog_package_scope_typet;

class verilog_typecheck_exprt:public verilog_typecheck_baset
{
public:
  verilog_typecheck_exprt(
    verilog_standardt _standard,
    bool _warn_implicit_nets,
    const namespacet &_ns,
    message_handlert &_message_handler)
    : verilog_typecheck_baset(_standard, _ns, _message_handler),
      warn_implicit_nets(_warn_implicit_nets)
  { }

  verilog_typecheck_exprt(
    verilog_standardt _standard,
    bool _warn_implicit_nets,
    const namespacet &_ns,
    const std::string &_module_identifier,
    message_handlert &_message_handler)
    : verilog_typecheck_baset(_standard, _ns, _message_handler),
      module_identifier(_module_identifier),
      warn_implicit_nets(_warn_implicit_nets)
  { }

  virtual void convert_expr(exprt &expr)
  {
    expr = convert_expr_rec(std::move(expr));
  }

  mp_integer convert_integer_constant_expression(exprt);

  exprt elaborate_constant_system_function_call(function_call_exprt);

protected:
  irep_idt module_identifier;
  irep_idt function_or_task_name;

  // module_identifier.function.block.base_name
  // including the Verilog:: prefix.
  irep_idt hierarchical_identifier(irep_idt base_name) const;

  void make_boolean(exprt &expr);

  void propagate_type(exprt &expr, const typet &type);
  void downwards_type_propagation(exprt &, const typet &);

  [[nodiscard]] typet elaborate_type(const typet &);
  typet elaborate_package_scope_typedef(const verilog_package_scope_typet &);
  typet convert_enum(const class verilog_enum_typet &);
  array_typet convert_unpacked_array_type(const type_with_subtypet &);
  typet convert_packed_array_type(const type_with_subtypet &);

  struct ranget
  {
    // This is Verilog's [msb:lsb].
    ranget(mp_integer _msb, mp_integer _lsb)
      : msb(std::move(_msb)), lsb(std::move(_lsb))
    {
    }

    ranget() : msb(0), lsb(0)
    {
    }

    mp_integer msb, lsb;

    /// Return true iff the bit with the higest index
    /// is the most significant bit, i.e., the vector
    /// is indexed left-to-right with decreasing indices.
    bool decreasing() const
    {
      return msb >= lsb;
    }

    bool increasing() const
    {
      return !decreasing();
    }

    mp_integer length() const
    {
      if(msb >= lsb)
        return msb - lsb + 1;
      else // lsb > msb
        return lsb - msb + 1;
    }

    mp_integer smallest_index() const
    {
      return msb >= lsb ? lsb : msb;
    }
  };

  ranget convert_range(const exprt &range);

  // to be overridden
  virtual mp_integer genvar_value(const irep_idt &identifier)
  {
    PRECONDITION(false);
  }

  virtual void elaborate_symbol_rec(irep_idt)
  {
    PRECONDITION(false);
  }

  // to be overridden
  virtual exprt var_value(const irep_idt &identifier)
  {
    PRECONDITION(false);
  }

  bool warn_implicit_nets = false;

  virtual bool
  implicit_wire(const irep_idt &identifier, const symbolt *&, const typet &)
  {
    return true;
  }

  void typecheck() override
  {
  }

  static typet enum_decay(const typet &);
  void enum_decay(exprt &) const;
  void union_decay(exprt &) const;
  void struct_decay(exprt &) const;
  typet max_type(const typet &t1, const typet &t2);

  // named blocks
  typedef std::vector<std::string> named_blockst;
  named_blockst named_blocks;
  void enter_named_block(const irep_idt &);

  // elaboration (expansion and folding) of constant expressions and functions
  bool is_constant_expression(const exprt &, mp_integer &value);
  std::optional<mp_integer> is_constant_integer_post_convert(const exprt &);
  exprt elaborate_constant_expression_rec(exprt);
  exprt elaborate_constant_expression(exprt);
  exprt elaborate_constant_expression_check(exprt);
  mp_integer elaborate_constant_integer_expression(exprt);

  // To be overridden, requires a Verilog interpreter.
  virtual exprt elaborate_constant_function_call(const function_call_exprt &)
  {
    UNREACHABLE;
  }

protected:
  [[nodiscard]] exprt convert_expr_rec(exprt expr);
  [[nodiscard]] exprt convert_constant(constant_exprt);
  [[nodiscard]] exprt
  convert_symbol(symbol_exprt, const std::optional<typet> &implicit_net_type);
  [[nodiscard]] exprt
    convert_hierarchical_identifier(class hierarchical_identifier_exprt);
  [[nodiscard]] exprt convert_nullary_expr(nullary_exprt);
  [[nodiscard]] exprt convert_unary_expr(unary_exprt);
  [[nodiscard]] exprt convert_binary_expr(binary_exprt);
  [[nodiscard]] exprt convert_trinary_expr(ternary_exprt);
  [[nodiscard]] exprt convert_expr_concatenation(concatenation_exprt);
  [[nodiscard]] exprt convert_expr_function_call(function_call_exprt);
  [[nodiscard]] exprt
  convert_system_function(const irep_idt &identifier, function_call_exprt);
  [[nodiscard]] exprt convert_bit_select_expr(binary_exprt);
  [[nodiscard]] exprt convert_replication_expr(replication_exprt);
  [[nodiscard]] exprt convert_power_expr(power_exprt);
  [[nodiscard]] exprt convert_shl_expr(shl_exprt);
  void implicit_typecast(exprt &, const typet &type);
  void tc_binary_expr(binary_exprt &);
  void tc_binary_expr(const exprt &expr, exprt &op0, exprt &op1);
  void convert_relation(binary_exprt &);
  void no_bool_ops(exprt &);
  void must_be_integral(const exprt &);

  // SVA
  void convert_sva(exprt &expr)
  {
    expr = convert_sva_rec(std::move(expr));
  }

  void require_sva_sequence(exprt &);
  void require_sva_property(exprt &);

  [[nodiscard]] exprt convert_sva_rec(exprt);
  [[nodiscard]] exprt convert_unary_sva(unary_exprt);
  [[nodiscard]] exprt convert_binary_sva(binary_exprt);
  [[nodiscard]] exprt convert_ternary_sva(ternary_exprt);
  [[nodiscard]] exprt convert_other_sva(exprt);

  // system functions
  exprt bits(const exprt &);
  std::optional<mp_integer> bits_rec(const typet &) const;
  constant_exprt countones(const constant_exprt &);
  constant_exprt isunknown(const constant_exprt &);
  constant_exprt left(const exprt &);
  constant_exprt right(const exprt &);
  constant_exprt low(const exprt &);
  constant_exprt high(const exprt &);
  constant_exprt increment(const exprt &);
  exprt typename_string(const exprt &);
};

bool verilog_typecheck(
  exprt &,
  const std::string &module_identifier,
  verilog_standardt,
  message_handlert &,
  const namespacet &);

#endif
