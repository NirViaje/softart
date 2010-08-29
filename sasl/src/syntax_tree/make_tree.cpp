#include <sasl/include/syntax_tree/make_tree.h>

#include <sasl/enums/buildin_type_code.h>
#include <sasl/include/common/token_attr.h>
#include <sasl/include/syntax_tree/declaration.h>
#include <sasl/include/syntax_tree/expression.h>
#include <sasl/include/syntax_tree/program.h>
#include <boost/static_assert.hpp>
//#include <boost/test/unit_test.hpp>
#include <boost/type_traits/is_base_of.hpp>
#include <boost/type_traits/is_same.hpp>

#define DEFAULT_STATE_SCOPE() state_scope ss(this, e_other);

BEGIN_NS_SASL_SYNTAX_TREE();

using ::sasl::common::token_attr;

literal_constant_types typecode_map::type_codes[] =
{
	literal_constant_types::boolean,
	literal_constant_types::integer,
	literal_constant_types::integer,
	literal_constant_types::integer,
	literal_constant_types::integer,
	literal_constant_types::integer,
	literal_constant_types::integer,
	literal_constant_types::integer,
	literal_constant_types::integer,
	literal_constant_types::real,
	literal_constant_types::real
};

SASL_TYPED_NODE_ACCESSORS_IMPL( tree_combinator, node );

/////////////////////////////////
// program combinator
SASL_TYPED_NODE_ACCESSORS_IMPL( dprog_combinator, program );

dprog_combinator::dprog_combinator( const std::string& prog_name ):
	tree_combinator(NULL)
{
	typed_node( create_node<program>( prog_name ) );
}

tree_combinator& dprog_combinator::dvar( const std::string& var_name )
{
	DEFAULT_STATE_SCOPE();

	var_comb = boost::make_shared<dvar_combinator>(this);

	boost::shared_ptr<variable_declaration> vardecl = create_node<variable_declaration>( token_attr::null() );
	typed_node()->decls.push_back( vardecl );
	var_comb->typed_node( vardecl );
	var_comb->dname( var_name );

	return *var_comb;
}
/////////////////////////////////
// type combinator
SASL_TYPED_NODE_ACCESSORS_IMPL( dtype_combinator, type_specifier );

dtype_combinator::dtype_combinator( tree_combinator* parent )
: tree_combinator( parent )
{
}

tree_combinator& dtype_combinator::dbuildin( buildin_type_code btc )
{
	DEFAULT_STATE_SCOPE();

	if( cur_node ){
		return default_proc();
	}
	
	typed_node( create_node<buildin_type>(token_attr::null()) )->value_typecode = btc;
	return *this;
}

tree_combinator& dtype_combinator::dvec( buildin_type_code comp_btc, size_t size )
{
	DEFAULT_STATE_SCOPE();

	if ( cur_node ){
		return default_proc();
	}

	typed_node( create_node<buildin_type>(token_attr::null()) );
	typed_node()->value_typecode = btc_helper::vector_of( comp_btc, size );
	return *this;
}

tree_combinator& dtype_combinator::dmat( buildin_type_code comp_btc, size_t s0, size_t s1 )
{
	DEFAULT_STATE_SCOPE();

	if( cur_node ){
		return default_proc();
	}
	typed_node( create_node<buildin_type>(token_attr::null() ) );
	typed_node()->value_typecode = btc_helper::matrix_of(comp_btc, s0, s1);
	return *this;
}

tree_combinator& dtype_combinator::dalias( const std::string& alias )
{
	DEFAULT_STATE_SCOPE();

	if( cur_node ){
		return default_proc();
	}
	typed_node( create_node<struct_type>( token_attr::null() ) );
	typed_node2<struct_type>()->name = token_attr::from_string(alias);
	return *this;
}

tree_combinator& dtype_combinator::dtypequal( type_qualifiers qual )
{
	DEFAULT_STATE_SCOPE();

	if( !cur_node || typed_node()->qual != type_qualifiers::none )
	{
		return default_proc();
	}
	typed_node()->qual = qual;
	return *this;
}

tree_combinator& dtype_combinator::darray()
{
	if ( !cur_node ) { return default_proc(); }
	enter( e_array );
	expr_comb = boost::make_shared<dexpr_combinator>(this);
	return *expr_comb;
}

void dtype_combinator::child_ended()
{
	if( is_state( e_array ) ){
		if ( !typed_node() ){
			default_proc();
		}
		boost::shared_ptr<array_type> outter_type;
		if ( typed_node()->node_class() != syntax_node_types::array_type ){
			outter_type = create_node<array_type>( token_attr::null() );
			outter_type->elem_type = typed_node();
			typed_node( outter_type );
		} else {
			outter_type = typed_node2<array_type>();
		}
		
		outter_type->array_lens.push_back( expr_comb->typed_node() );
		leave();
	}
}

/////////////////////////////////////
// variable combinator
SASL_TYPED_NODE_ACCESSORS_IMPL( dvar_combinator, variable_declaration );

dvar_combinator::dvar_combinator( tree_combinator* parent )
: tree_combinator( parent )
{
}

tree_combinator& dvar_combinator::dname( const std::string& name )
{
	DEFAULT_STATE_SCOPE();

	typed_node()->name = token_attr::from_string(name);
	return *this;
}

tree_combinator& dvar_combinator::dtype()
{
	type_comb = boost::make_shared<dtype_combinator>(this);
	enter( e_type );
	return *type_comb;
}

void dvar_combinator::child_ended()
{
	switch( leave() )
	{
	case e_type:
		typed_node()->type_info = type_comb->typed_node();
		break;
	default:
		default_proc();
		break;
	}
}

/////////////////////////////////////////////////////////////////
// expression combinator

SASL_TYPED_NODE_ACCESSORS_IMPL( dexpr_combinator, expression );

dexpr_combinator::dexpr_combinator( tree_combinator* parent )
: tree_combinator( parent )
{
}

tree_combinator& dexpr_combinator::dconstant( literal_constant_types lct, const std::string& v )
{
	DEFAULT_STATE_SCOPE();

	boost::shared_ptr< constant_expression > ret
		= create_node<constant_expression>( token_attr::null() );
	ret->value_tok = token_attr::from_string( v );
	ret->ctype = lct;

	typed_node( ret );
	return *this;
}

tree_combinator& dexpr_combinator::dvarexpr( const std::string& v)
{
	DEFAULT_STATE_SCOPE();

	boost::shared_ptr< variable_expression > ret = create_node<variable_expression>( token_attr::null() );
	ret->var_name = token_attr::from_string( v );
	
	typed_node( ret );
	return *this;
}

tree_combinator& dexpr_combinator::dpre( operators op )
{
	assert( operators_helper::instance().is_prefix(op) );
	assert( !typed_node() );

	enter(e_preexpr);

	boost::shared_ptr< unary_expression > ret = create_node<unary_expression>( token_attr::null() );
	ret->op = op;
	typed_node( ret );
	expr_comb = boost::make_shared<dexpr_combinator>( this );

	return *expr_comb;
}

tree_combinator& dexpr_combinator::dcast()
{
	assert( !typed_node() );
	enter( e_cast );
	cast_comb = boost::make_shared<dcast_combinator>( this );
	return *cast_comb;
}

void dexpr_combinator::child_ended()
{
	switch( leave() ){
		case e_preexpr:
			typed_node2<unary_expression>()->expr = expr_comb->typed_node();
			return;

		case e_cast:
			assert( cast_comb->typed_node() );
			typed_node( cast_comb->typed_node() );
			return;

		default:
			assert( !"unknown state." );
			return;
	}
}

//////////////////////////////////////////////////////////////////////////
// cast combinator

SASL_TYPED_NODE_ACCESSORS_IMPL( dcast_combinator, cast_expression );

dcast_combinator::dcast_combinator( tree_combinator* parent )
: tree_combinator( parent )
{
	typed_node( create_node<cast_expression>( token_attr::null() ) );
}

tree_combinator& dcast_combinator::dtype()
{
	assert( !type_comb );
	enter( e_type );
	type_comb = boost::make_shared<dtype_combinator>(this);
	return *type_comb;
}

tree_combinator& dcast_combinator::dexpr()
{
	assert( !expr_comb );
	enter( e_expr );
	expr_comb = boost::make_shared<dexpr_combinator>(this);
	return *expr_comb;
}

void dcast_combinator::child_ended()
{
	switch ( leave() ){
		case e_type:
			assert( type_comb->typed_node() );
			typed_node()->casted_type = type_comb->typed_node();
			return;
		case e_expr:
			assert( expr_comb->typed_node() );
			typed_node()->expr = expr_comb->typed_node();
			return;
		default:
			assert( !"invalid state." );
			return;
	}
}



END_NS_SASL_SYNTAX_TREE();