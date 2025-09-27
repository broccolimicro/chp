#pragma once

#include <arithmetic/expression.h>
#include <arithmetic/action.h>
#include <common/net.h>

namespace chp {

string emit_composition(const arithmetic::Action &expr, ucs::ConstNetlist nets);
string emit_composition(const arithmetic::Parallel &expr, ucs::ConstNetlist nets);
string emit_composition(const arithmetic::Choice &expr, ucs::ConstNetlist nets);
string emit_composition(const arithmetic::State &expr, ucs::ConstNetlist nets);
string emit_composition(const arithmetic::Region &expr, ucs::ConstNetlist nets);
string emit_expression(const arithmetic::Expression &expr, ucs::ConstNetlist nets);
string emit_expression(const arithmetic::State &expr, ucs::ConstNetlist nets);
string emit_expression(const arithmetic::Region &expr, ucs::ConstNetlist nets);

}
