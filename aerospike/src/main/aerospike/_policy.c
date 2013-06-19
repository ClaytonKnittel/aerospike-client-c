
#include <aerospike/as_policy.h>

#include <stdbool.h>
#include <stdint.h>

#include "_policy.h"

/******************************************************************************
 *	MACROS
 *****************************************************************************/

#define as_policy_resolve(__field, __global, __local, __default) \
 	(__local && __local->__field ? \
 		__local->__field : \
		(__global.__field ? __global.__field : __default))

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/** 
 *	Resolve policy values from global and local policy.
 */
as_policy_read * as_policy_read_resolve(as_policy_read * p, const as_policies * global, const as_policy_read * local)
{
	p->timeout		= as_policy_resolve(timeout, global->read, local, global->timeout);
	p->key			= as_policy_resolve(key, global->read, local, global->key);
	return p;
}

/** 
 *	Resolve policy values from global and local policy.
 */
as_policy_write * as_policy_write_resolve(as_policy_write * p, const as_policies * global, const as_policy_write * local)
{
	p->timeout	= as_policy_resolve(timeout, global->write, local, global->timeout);
	p->mode		= as_policy_resolve(mode, global->write, local, global->mode);
	p->key		= as_policy_resolve(key, global->write, local, global->key);
	p->gen		= as_policy_resolve(gen, global->write, local, global->gen);
	return p;
}

/** 
 *	Resolve policy values from global and local policy.
 */
as_policy_operate * as_policy_operate_resolve(as_policy_operate * p, const as_policies * global, const as_policy_operate * local)
{
	p->timeout		= as_policy_resolve(timeout, global->operate, local, global->timeout);
	p->generation	= local ? local->generation : 0;
	p->mode			= as_policy_resolve(mode, global->operate, local, global->mode);
	p->key			= as_policy_resolve(key, global->operate, local, global->key);
	p->gen			= as_policy_resolve(gen, global->operate, local, global->gen);
	return p;
}

/** 
 *	Resolve policy values from global and local policy.
 */
as_policy_scan * as_policy_scan_resolve(as_policy_scan * p, const as_policies * global, const as_policy_scan * local)
{
	p->timeout					= as_policy_resolve(timeout, global->scan, local, global->timeout);
	p->fail_on_cluster_change	= as_policy_resolve(fail_on_cluster_change, global->scan, local, true);
	return p;
}

/** 
 *	Resolve policy values from global and local policy.
 */
as_policy_query * as_policy_query_resolve(as_policy_query * p, const as_policies * global, const as_policy_query * local)
{
	p->timeout	= as_policy_resolve(timeout, global->query, local, global->timeout);
	return p;
}

/** 
 *	Resolve policy values from global and local policy.
 */
as_policy_info * as_policy_info_resolve(as_policy_info * p, const as_policies * global, const as_policy_info * local)
{
	p->timeout		= as_policy_resolve(timeout, global->info, local, global->timeout);
	p->send_as_is	= as_policy_resolve(send_as_is, global->info, local, true);
	p->check_bounds	= as_policy_resolve(check_bounds, global->info, local, true);
	return p;
}