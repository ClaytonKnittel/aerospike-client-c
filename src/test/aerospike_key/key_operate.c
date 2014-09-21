/*
 * Copyright 2008-2014 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */
#include <aerospike/aerospike.h>
#include <aerospike/aerospike_key.h>

#include <aerospike/as_error.h>
#include <aerospike/as_status.h>

#include <aerospike/as_record.h>
#include <aerospike/as_integer.h>
#include <aerospike/as_string.h>
#include <aerospike/as_list.h>
#include <aerospike/as_arraylist.h>
#include <aerospike/as_map.h>
#include <aerospike/as_hashmap.h>
#include <aerospike/as_val.h>

#include "../test.h"

/******************************************************************************
 * GLOBAL VARS
 *****************************************************************************/

extern aerospike * as;

/******************************************************************************
 * TYPES
 *****************************************************************************/


/******************************************************************************
 * STATIC FUNCTIONS
 *****************************************************************************/


/******************************************************************************
 * TEST CASES
 *****************************************************************************/

TEST( key_operate_touchget , "operate: (test,test,key2) = {touch, get}" ) {

	as_error err;
	as_error_reset(&err);

	as_arraylist list;
	as_arraylist_init(&list, 3, 0);
	as_arraylist_append_int64(&list, 1);
	as_arraylist_append_int64(&list, 2);
	as_arraylist_append_int64(&list, 3);

	as_record r, * rec = &r;
	as_record_init(rec, 3);
	as_record_set_int64(rec, "a", 123);
	as_record_set_str(rec, "b", "abc");
	as_record_set_list(rec, "e", (as_list *) &list);

	as_key key;
	as_key_init(&key, "test", "operate", "key2");

	as_status rc = aerospike_key_remove(as, &err, NULL, &key);

	rc = aerospike_key_put(as, &err, NULL, &key, rec);
	assert_int_eq( rc, AEROSPIKE_OK );

	as_record_destroy(rec);
	as_record_init(rec, 1);

	as_operations ops;
	as_operations_inita(&ops, 2);
	as_operations_add_touch(&ops);
	as_operations_add_read(&ops, "e");
	ops.ttl = 120;

	// Apply the operation.
	rc = aerospike_key_operate(as, &err, NULL, &key, &ops, &rec);
	assert_int_eq( rc, AEROSPIKE_OK );

	as_list * rlist = as_record_get_list(rec, "e");
	assert_not_null( rlist );
	assert_int_eq( as_list_size(rlist), 3 );

	as_record_destroy(rec);

}

/******************************************************************************
 * TEST SUITE
 *****************************************************************************/

SUITE( key_operate, "aerospike_key_operate tests" ) {
	suite_add( key_operate_touchget );
}
