#include "expression.h"
#include <parse_cog/expression.h>
#include <interpret_arithmetic/export.h>

namespace chp {

string emit_composition(const arithmetic::Action &expr, ucs::ConstNetlist nets) {
	return arithmetic::export_assignment<parse_cog::assignment>(expr, nets).to_string();
}

string emit_composition(const arithmetic::Parallel &expr, ucs::ConstNetlist nets) {
	return arithmetic::export_composition<parse_cog::simple_composition>(expr, nets).to_string();
}

string emit_composition(const arithmetic::Choice &expr, ucs::ConstNetlist nets) {
	return arithmetic::export_composition<parse_cog::simple_composition>(expr, nets).to_string();
}

string emit_composition(const arithmetic::State &expr, ucs::ConstNetlist nets) {
	return arithmetic::export_composition<parse_cog::simple_composition>(expr, nets).to_string();
}

string emit_composition(const arithmetic::Region &expr, ucs::ConstNetlist nets) {
	return arithmetic::export_composition<parse_cog::simple_composition>(expr, nets).to_string();
}

string emit_expression(const arithmetic::Expression &expr, ucs::ConstNetlist nets) {
	return arithmetic::export_expression<parse_cog::expression>(expr, nets).to_string();
}

string emit_expression(const arithmetic::State &expr, ucs::ConstNetlist nets) {
	return arithmetic::export_expression<parse_cog::expression>(expr, nets).to_string();
}

}
