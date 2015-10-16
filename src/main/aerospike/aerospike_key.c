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
#include <aerospike/as_bin.h>
#include <aerospike/as_buffer.h>
#include <aerospike/as_command.h>
#include <aerospike/as_error.h>
#include <aerospike/as_event.h>
#include <aerospike/as_key.h>
#include <aerospike/as_list.h>
#include <aerospike/as_log.h>
#include <aerospike/as_msgpack.h>
#include <aerospike/as_operations.h>
#include <aerospike/as_policy.h>
#include <aerospike/as_record.h>
#include <aerospike/as_serializer.h>
#include <aerospike/as_status.h>
#include <citrusleaf/cf_clock.h>

static const char* CLUSTER_EMPTY = "Cluster is empty";

/******************************************************************************
 * FUNCTIONS
 *****************************************************************************/

static inline void
as_command_node_init(as_command_node* cn, const char* ns, const uint8_t* digest,
	as_policy_replica replica, bool write)
{
	cn->node = 0;
	cn->ns = ns;
	cn->digest = digest;
	cn->replica = replica;
	cn->write = write;
}

static inline as_node*
as_async_command_node_init(aerospike* as, as_error* err, const as_key* key, as_policy_replica replica, bool write)
{
	if (as_key_set_digest(err, (as_key*)key) != AEROSPIKE_OK) {
		return 0;
	}
	
	as_node* node = as_node_get(as->cluster, key->ns, key->digest.value, write, replica);
	
	if (! node) {
		as_error_set_message(err, AEROSPIKE_ERR_CLIENT, CLUSTER_EMPTY);
		return 0;
	}
	return node;
}

as_status
aerospike_key_get(aerospike* as, as_error* err, const as_policy_read* policy, const as_key* key,
	as_record** rec)
{
	as_error_reset(err);
	
	if (! policy) {
		policy = &as->config.policies.read;
	}

	int status = as_key_set_digest(err, (as_key*)key);
	
	if (status != AEROSPIKE_OK) {
		return status;
	}
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
		
	uint8_t* cmd = as_command_init(size);
	uint8_t* p = as_command_write_header_read(cmd, AS_MSG_INFO1_READ | AS_MSG_INFO1_GET_ALL, policy->consistency_level, policy->timeout, n_fields, 0);
	p = as_command_write_key(p, policy->key, key);
	size = as_command_write_end(cmd, p);
	
	as_command_node cn;
	as_command_node_init(&cn, key->ns, key->digest.value, policy->replica, false);
	
	as_command_parse_result_data data;
	data.record = rec;
	data.deserialize = policy->deserialize;
	
	status = as_command_execute(as->cluster, err, &cn, cmd, size, policy->timeout, policy->retry, as_command_parse_result, &data);
	
	as_command_free(cmd, size);
	return status;
}

void
aerospike_key_get_async(aerospike* as, const as_policy_read* policy, const as_key* key,
	as_event_loop* event_loop, bool pipeline, as_async_callback_fn ucb, void* udata)
{
	if (! policy) {
		policy = &as->config.policies.read;
	}
	
	as_error err;
	as_node* node = as_async_command_node_init(as, &err, key, policy->replica, false);
	if (! node) {
		ucb(&err, 0, udata, event_loop);
		return;
	}
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	
	as_async_command* cmd = as_async_record_command_create(size, as->cluster, node, policy->timeout, policy->deserialize, event_loop, pipeline, ucb, udata, as_async_command_parse_result);

	uint8_t* p = as_command_write_header_read(cmd->buf, AS_MSG_INFO1_READ | AS_MSG_INFO1_GET_ALL, policy->consistency_level, policy->timeout, n_fields, 0);
	p = as_command_write_key(p, policy->key, key);
	size = as_command_write_end(cmd->buf, p);
	as_async_command_assign(cmd, size);
}

as_status
aerospike_key_select(aerospike* as, as_error* err, const as_policy_read* policy, const as_key* key,
	const char* bins[], as_record** rec)
{
	as_error_reset(err);
	
	if (! policy) {
		policy = &as->config.policies.read;
	}
	
	int status = as_key_set_digest(err, (as_key*)key);
	
	if (status != AEROSPIKE_OK) {
		return status;
	}
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	int nvalues = 0;
	
	for (nvalues = 0; bins[nvalues] != NULL && bins[nvalues][0] != '\0'; nvalues++) {
		status = as_command_bin_name_size(err, bins[nvalues], &size);
		
		if (status != AEROSPIKE_OK) {
			return status;
		}
	}
	
	uint8_t* cmd = as_command_init(size);
	uint8_t* p = as_command_write_header_read(cmd, AS_MSG_INFO1_READ, policy->consistency_level, policy->timeout, n_fields, nvalues);
	p = as_command_write_key(p, policy->key, key);
	
	for (int i = 0; i < nvalues; i++) {
		p = as_command_write_bin_name(p, bins[i]);
	}
	size = as_command_write_end(cmd, p);
	
	as_command_node cn;
	as_command_node_init(&cn, key->ns, key->digest.value, policy->replica, false);
	
	as_command_parse_result_data data;
	data.record = rec;
	data.deserialize = policy->deserialize;

	status = as_command_execute(as->cluster, err, &cn, cmd, size, policy->timeout, policy->retry, as_command_parse_result, &data);
	
	as_command_free(cmd, size);
	return status;
}

void
aerospike_key_select_async(aerospike* as, const as_policy_read* policy, const as_key* key,
	const char* bins[], as_event_loop* event_loop, bool pipeline, as_async_callback_fn ucb,
	void* udata)
{
	if (! policy) {
		policy = &as->config.policies.read;
	}
	
	as_error err;
	as_node* node = as_async_command_node_init(as, &err, key, policy->replica, false);
	if (! node) {
		ucb(&err, 0, udata, event_loop);
		return;
	}
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	int nvalues = 0;
	
	for (nvalues = 0; bins[nvalues] != NULL && bins[nvalues][0] != '\0'; nvalues++) {
		as_status status = as_command_bin_name_size(&err, bins[nvalues], &size);
		
		if (status != AEROSPIKE_OK) {
			ucb(&err, 0, udata, event_loop);
			return;
		}
	}
	
	as_async_command* cmd = as_async_record_command_create(size, as->cluster, node, policy->timeout, policy->deserialize, event_loop, pipeline, ucb, udata, as_async_command_parse_result);

	uint8_t* p = as_command_write_header_read(cmd->buf, AS_MSG_INFO1_READ, policy->consistency_level, policy->timeout, n_fields, nvalues);
	p = as_command_write_key(p, policy->key, key);
	
	for (int i = 0; i < nvalues; i++) {
		p = as_command_write_bin_name(p, bins[i]);
	}
	size = as_command_write_end(cmd->buf, p);
	as_async_command_assign(cmd, size);
}

as_status
aerospike_key_exists(aerospike* as, as_error* err, const as_policy_read* policy, const as_key* key,
	as_record** rec)
{
	as_error_reset(err);
	
	if (! policy) {
		policy = &as->config.policies.read;
	}
	
	as_status status = as_key_set_digest(err, (as_key*)key);
	
	if (status != AEROSPIKE_OK) {
		return status;
	}

	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	
	uint8_t* cmd = as_command_init(size);
	uint8_t* p = as_command_write_header_read(cmd, AS_MSG_INFO1_READ | AS_MSG_INFO1_GET_NOBINDATA, policy->consistency_level, policy->timeout, n_fields, 0);
	p = as_command_write_key(p, policy->key, key);
	size = as_command_write_end(cmd, p);
	
	as_command_node cn;
	as_command_node_init(&cn, key->ns, key->digest.value, policy->replica, false);
	
	as_proto_msg msg;
	status = as_command_execute(as->cluster, err, &cn, cmd, size, policy->timeout, policy->retry, as_command_parse_header, &msg);
	
	as_command_free(cmd, size);

	if (rec) {
		if (status == AEROSPIKE_OK) {
			as_record* r = *rec;
			
			if (r == 0) {
				r = as_record_new(0);
				*rec = r;
			}
			r->gen = (uint16_t)msg.m.generation;
			r->ttl = cf_server_void_time_to_ttl(msg.m.record_ttl);
		}
		else {
			*rec = 0;
		}
	}
	return status;
}

void
aerospike_key_exists_async(aerospike* as, const as_policy_read* policy, const as_key* key,
	as_event_loop* event_loop, bool pipeline, as_async_callback_fn ucb, void* udata)
{
	if (! policy) {
		policy = &as->config.policies.read;
	}
	
	as_error err;
	as_node* node = as_async_command_node_init(as, &err, key, policy->replica, false);
	if (! node) {
		ucb(&err, 0, udata, event_loop);
		return;
	}
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	
	as_async_command* cmd = as_async_record_command_create(size, as->cluster, node, policy->timeout, false, event_loop, pipeline, ucb, udata, as_async_command_parse_result);

	uint8_t* p = as_command_write_header_read(cmd->buf, AS_MSG_INFO1_READ | AS_MSG_INFO1_GET_NOBINDATA, policy->consistency_level, policy->timeout, n_fields, 0);
	p = as_command_write_key(p, policy->key, key);
	size = as_command_write_end(cmd->buf, p);
	as_async_command_assign(cmd, size);
}

as_status
aerospike_key_put(aerospike* as, as_error* err, const as_policy_write* policy, const as_key* key,
	as_record* rec)
{
	as_error_reset(err);
	
	if (! policy) {
		policy = &as->config.policies.write;
	}
	
	as_status status = as_key_set_digest(err, (as_key*)key);
	
	if (status != AEROSPIKE_OK) {
		return status;
	}
	
	as_bin* bins = rec->bins.entries;
	uint32_t n_bins = rec->bins.size;
	as_buffer* buffers = (as_buffer*)alloca(sizeof(as_buffer) * n_bins);
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	memset(buffers, 0, sizeof(as_buffer) * n_bins);
	
	for (uint32_t i = 0; i < n_bins; i++) {
		size += as_command_bin_size(&bins[i], &buffers[i]);
	}
		
	uint8_t* cmd = as_command_init(size);
	uint8_t* p = as_command_write_header(cmd, 0, AS_MSG_INFO2_WRITE, policy->commit_level, 0,
					policy->exists, policy->gen, rec->gen, rec->ttl, policy->timeout, n_fields,
					n_bins);
		
	p = as_command_write_key(p, policy->key, key);
	
	for (uint32_t i = 0; i < n_bins; i++) {
		p = as_command_write_bin(p, AS_OPERATOR_WRITE, &bins[i], &buffers[i]);
	}
	size = as_command_write_end(cmd, p);

	as_command_node cn;
	as_command_node_init(&cn, key->ns, key->digest.value, AS_POLICY_REPLICA_MASTER, true);
	as_proto_msg msg;
	
	if (policy->compression_threshold == 0 || (size <= policy->compression_threshold)) {
		// Send uncompressed command.
		status = as_command_execute(as->cluster, err, &cn, cmd, size, policy->timeout,
					policy->retry, as_command_parse_header, &msg);
	}
	else {
		// Send compressed command.
		size_t comp_size = as_command_compress_max_size(size);
		uint8_t* comp_cmd = as_command_init(comp_size);
		status = as_command_compress(err, cmd, size, comp_cmd, &comp_size);
		
		if (status == AEROSPIKE_OK) {
			status = as_command_execute(as->cluster, err, &cn, comp_cmd, comp_size, policy->timeout,
										policy->retry, as_command_parse_header, &msg);
		}
		as_command_free(comp_cmd, comp_size);
	}
	as_command_free(cmd, size);
	return status;
}

void
aerospike_key_put_async(aerospike* as, const as_policy_write* policy, const as_key* key,
	as_record* rec, as_event_loop* event_loop, bool pipeline, as_async_callback_fn ucb, void* udata)
{
	if (! policy) {
		policy = &as->config.policies.write;
	}

	as_error err;
	as_node* node = as_async_command_node_init(as, &err, key, AS_POLICY_REPLICA_MASTER, true);
	if (! node) {
		ucb(&err, 0, udata, event_loop);
		return;
	}
		
	as_bin* bins = rec->bins.entries;
	uint32_t n_bins = rec->bins.size;
	as_buffer* buffers = (as_buffer*)alloca(sizeof(as_buffer) * n_bins);
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	memset(buffers, 0, sizeof(as_buffer) * n_bins);
	
	for (uint32_t i = 0; i < n_bins; i++) {
		size += as_command_bin_size(&bins[i], &buffers[i]);
	}
	
	if (policy->compression_threshold == 0 || (size <= policy->compression_threshold)) {
		// Send uncompressed command.
		as_async_command* cmd = as_async_record_command_create(size, as->cluster, node, policy->timeout, false,
				event_loop, pipeline, ucb, udata, as_async_command_parse_header);
		
		uint8_t* p = as_command_write_header(cmd->buf, 0, AS_MSG_INFO2_WRITE, policy->commit_level, 0,
				policy->exists, policy->gen, rec->gen, rec->ttl, policy->timeout, n_fields, n_bins);
		
		p = as_command_write_key(p, policy->key, key);
		
		for (uint32_t i = 0; i < n_bins; i++) {
			p = as_command_write_bin(p, AS_OPERATOR_WRITE, &bins[i], &buffers[i]);
		}
		size = as_command_write_end(cmd->buf, p);
		as_async_command_assign(cmd, size);
	}
	else {
		// Send compressed command.
		// First write uncompressed buffer.
		uint8_t* cmd = as_command_init(size);
		uint8_t* p = as_command_write_header(cmd, 0, AS_MSG_INFO2_WRITE, policy->commit_level, 0,
				policy->exists, policy->gen, rec->gen, rec->ttl, policy->timeout, n_fields, n_bins);
		
		p = as_command_write_key(p, policy->key, key);
		
		for (uint32_t i = 0; i < n_bins; i++) {
			p = as_command_write_bin(p, AS_OPERATOR_WRITE, &bins[i], &buffers[i]);
		}
		size = as_command_write_end(cmd, p);
		
		// Allocate command with compressed upper bound.
		size_t comp_size = as_command_compress_max_size(size);
		
		as_async_command* comp_cmd = as_async_record_command_create(comp_size, as->cluster, node,
			policy->timeout, false, event_loop, pipeline, ucb, udata, as_async_command_parse_header);

		// Compress buffer and execute.
		if (as_command_compress(&err, cmd, size, comp_cmd->buf, &comp_size) == AEROSPIKE_OK) {
			as_async_command_assign(comp_cmd, comp_size);
		}
		else {
			as_async_error(comp_cmd, &err);
		}
		as_command_free(cmd, size);
	}
}

as_status
aerospike_key_remove(aerospike* as, as_error* err, const as_policy_remove* policy, const as_key* key)
{
	as_error_reset(err);
	
	if (! policy) {
		policy = &as->config.policies.remove;
	}
	
	as_status status = as_key_set_digest(err, (as_key*)key);
	
	if (status != AEROSPIKE_OK) {
		return status;
	}

	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
		
	uint8_t* cmd = as_command_init(size);
	uint8_t* p = as_command_write_header(cmd, 0, AS_MSG_INFO2_WRITE | AS_MSG_INFO2_DELETE, policy->commit_level, 0, AS_POLICY_EXISTS_IGNORE, policy->gen, policy->generation, 0, policy->timeout, n_fields, 0);
	p = as_command_write_key(p, policy->key, key);
	size = as_command_write_end(cmd, p);
	
	as_command_node cn;
	as_command_node_init(&cn, key->ns, key->digest.value, AS_POLICY_REPLICA_MASTER, true);
	
	as_proto_msg msg;
	status = as_command_execute(as->cluster, err, &cn, cmd, size, policy->timeout, policy->retry, as_command_parse_header, &msg);
	
	as_command_free(cmd, size);
	return status;
}

void
aerospike_key_remove_async(aerospike* as, const as_policy_remove* policy, const as_key* key,
	as_event_loop* event_loop, bool pipeline, as_async_callback_fn ucb, void* udata)
{
	if (! policy) {
		policy = &as->config.policies.remove;
	}
	
	as_error err;
	as_node* node = as_async_command_node_init(as, &err, key, AS_POLICY_REPLICA_MASTER, true);
	if (! node) {
		ucb(&err, 0, udata, event_loop);
		return;
	}
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	
	as_async_command* cmd = as_async_record_command_create(size, as->cluster, node, policy->timeout, false, event_loop, pipeline, ucb, udata, as_async_command_parse_header);

	uint8_t* p = as_command_write_header(cmd->buf, 0, AS_MSG_INFO2_WRITE | AS_MSG_INFO2_DELETE, policy->commit_level, 0, AS_POLICY_EXISTS_IGNORE, policy->gen, policy->generation, 0, policy->timeout, n_fields, 0);
	p = as_command_write_key(p, policy->key, key);
	size = as_command_write_end(cmd->buf, p);
	as_async_command_assign(cmd, size);
}

as_status
aerospike_key_operate(aerospike* as, as_error* err, const as_policy_operate* policy,
	const as_key* key, const as_operations* ops, as_record** rec)
{
	as_error_reset(err);
	
	if (! policy) {
		policy = &as->config.policies.operate;
	}

	int status = as_key_set_digest(err, (as_key*)key);
	
	if (status != AEROSPIKE_OK) {
		return status;
	}

	uint32_t n_operations = ops->binops.size;
	as_buffer* buffers = (as_buffer*)alloca(sizeof(as_buffer) * n_operations);
	memset(buffers, 0, sizeof(as_buffer) * n_operations);
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	uint8_t read_attr = 0;
	uint8_t write_attr = 0;
	
	for (int i = 0; i < n_operations; i++) {
		as_binop* op = &ops->binops.entries[i];
		
		switch (op->op)
		{
			case AS_OPERATOR_READ:
				read_attr |= AS_MSG_INFO1_READ;
				break;

			default:
				write_attr |= AS_MSG_INFO2_WRITE;
				break;
		}
		size += as_command_bin_size(&op->bin, &buffers[i]);
	}

	uint8_t* cmd = as_command_init(size);
	uint8_t* p = as_command_write_header(cmd, read_attr, write_attr, policy->commit_level, policy->consistency_level,
				 AS_POLICY_EXISTS_IGNORE, policy->gen, ops->gen, ops->ttl, policy->timeout, n_fields, n_operations);
	p = as_command_write_key(p, policy->key, key);
	
	for (uint32_t i = 0; i < n_operations; i++) {
		as_binop* op = &ops->binops.entries[i];
		p = as_command_write_bin(p, op->op, &op->bin, &buffers[i]);
	}

	size = as_command_write_end(cmd, p);
	
	as_command_node cn;
	as_command_node_init(&cn, key->ns, key->digest.value, policy->replica, write_attr != 0);
	
	as_command_parse_result_data data;
	data.record = rec;
	data.deserialize = policy->deserialize;

	status = as_command_execute(as->cluster, err, &cn, cmd, size, policy->timeout, policy->retry, as_command_parse_result, &data);
	
	as_command_free(cmd, size);
	return status;
}

void
aerospike_key_operate_async(aerospike* as, const as_policy_operate* policy, const as_key* key,
	const as_operations* ops, as_event_loop* event_loop, bool pipeline, as_async_callback_fn ucb,
	void* udata)
{
	if (! policy) {
		policy = &as->config.policies.operate;
	}
	
	uint32_t n_operations = ops->binops.size;
	as_buffer* buffers = (as_buffer*)alloca(sizeof(as_buffer) * n_operations);
	memset(buffers, 0, sizeof(as_buffer) * n_operations);
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	uint8_t read_attr = 0;
	uint8_t write_attr = 0;
	
	for (int i = 0; i < n_operations; i++) {
		as_binop* op = &ops->binops.entries[i];
		
		switch (op->op)
		{
			case AS_OPERATOR_READ:
				read_attr |= AS_MSG_INFO1_READ;
				break;
				
			default:
				write_attr |= AS_MSG_INFO2_WRITE;
				break;
		}
		size += as_command_bin_size(&op->bin, &buffers[i]);
	}
	
	as_error err;
	as_node* node = as_async_command_node_init(as, &err, key, policy->replica, write_attr != 0);
	if (! node) {
		for (uint32_t i = 0; i < n_operations; i++) {
			as_buffer* buffer = &buffers[i];
			
			if (buffer->data) {
				cf_free(buffer->data);
			}
		}
		ucb(&err, 0, udata, event_loop);
		return;
	}
	
	as_async_command* cmd = as_async_record_command_create(size, as->cluster, node, policy->timeout, false, event_loop, pipeline, ucb, udata, as_async_command_parse_result);

	uint8_t* p = as_command_write_header(cmd->buf, read_attr, write_attr, policy->commit_level, policy->consistency_level,
										 AS_POLICY_EXISTS_IGNORE, policy->gen, ops->gen, ops->ttl, policy->timeout, n_fields, n_operations);
	p = as_command_write_key(p, policy->key, key);
	
	for (uint32_t i = 0; i < n_operations; i++) {
		as_binop* op = &ops->binops.entries[i];
		p = as_command_write_bin(p, op->op, &op->bin, &buffers[i]);
	}
	size = as_command_write_end(cmd->buf, p);
	as_async_command_assign(cmd, size);
}

as_status
aerospike_key_apply(aerospike* as, as_error* err, const as_policy_apply* policy, const as_key* key,
	const char* module, const char* function, as_list* arglist, as_val** result)
{
	as_error_reset(err);
	
	if (! policy) {
		policy = &as->config.policies.apply;
	}

	int status = as_key_set_digest(err, (as_key*)key);
	
	if (status != AEROSPIKE_OK) {
		return status;
	}
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	size += as_command_string_field_size(module);
	size += as_command_string_field_size(function);
	
	as_serializer ser;
	as_msgpack_init(&ser);
	as_buffer args;
	as_buffer_init(&args);
	as_serializer_serialize(&ser, (as_val*)arglist, &args);
	size += as_command_field_size(args.size);
	n_fields += 3;

	uint8_t* cmd = as_command_init(size);
	uint8_t* p = as_command_write_header(cmd, 0, AS_MSG_INFO2_WRITE, policy->commit_level, 0, 0, 0, 0, policy->ttl, policy->timeout, n_fields, 0);
	p = as_command_write_key(p, policy->key, key);
	p = as_command_write_field_string(p, AS_FIELD_UDF_PACKAGE_NAME, module);
	p = as_command_write_field_string(p, AS_FIELD_UDF_FUNCTION, function);
	p = as_command_write_field_buffer(p, AS_FIELD_UDF_ARGLIST, &args);
	size = as_command_write_end(cmd, p);
	
	as_command_node cn;
	as_command_node_init(&cn, key->ns, key->digest.value, AS_POLICY_REPLICA_MASTER, true);
	
	status = as_command_execute(as->cluster, err, &cn, cmd, size, policy->timeout, 0, as_command_parse_success_failure, result);
	
	as_command_free(cmd, size);
	as_buffer_destroy(&args);
	as_serializer_destroy(&ser);
	return status;
}

void
aerospike_key_apply_async(aerospike* as, const as_policy_apply* policy, const as_key* key,
	const char* module, const char* function, as_list* arglist,
	as_event_loop* event_loop, bool pipeline, as_async_callback_fn ucb, void* udata)
{
	if (! policy) {
		policy = &as->config.policies.apply;
	}
	
	as_error err;
	as_node* node = as_async_command_node_init(as, &err, key, AS_POLICY_REPLICA_MASTER, true);
	if (! node) {
		ucb(&err, 0, udata, event_loop);
		return;
	}
	
	uint16_t n_fields;
	size_t size = as_command_key_size(policy->key, key, &n_fields);
	size += as_command_string_field_size(module);
	size += as_command_string_field_size(function);
	
	as_serializer ser;
	as_msgpack_init(&ser);
	as_buffer args;
	as_buffer_init(&args);
	as_serializer_serialize(&ser, (as_val*)arglist, &args);
	size += as_command_field_size(args.size);
	n_fields += 3;
	
	as_async_command* cmd = as_async_record_command_create(size, as->cluster, node, policy->timeout, false, event_loop, pipeline, ucb, udata, as_async_command_parse_success_failure);
		
	uint8_t* p = as_command_write_header(cmd->buf, 0, AS_MSG_INFO2_WRITE, policy->commit_level, 0, 0, 0, 0, policy->ttl, policy->timeout, n_fields, 0);
	p = as_command_write_key(p, policy->key, key);
	p = as_command_write_field_string(p, AS_FIELD_UDF_PACKAGE_NAME, module);
	p = as_command_write_field_string(p, AS_FIELD_UDF_FUNCTION, function);
	p = as_command_write_field_buffer(p, AS_FIELD_UDF_ARGLIST, &args);
	size = as_command_write_end(cmd->buf, p);
	as_buffer_destroy(&args);
	as_serializer_destroy(&ser);
	as_async_command_assign(cmd, size);
}

bool
aerospike_has_double(aerospike* as)
{
	as_nodes* nodes = as_nodes_reserve(as->cluster);
	
	if (nodes->size == 0) {
		as_nodes_release(nodes);
		return false;
	}
	
	for (uint32_t i = 0; i < nodes->size; i++) {
		if (! nodes->array[i]->has_double) {
			as_nodes_release(nodes);
			return false;
		}
	}
	as_nodes_release(nodes);
	return true;
}

bool
aerospike_has_geo(aerospike* as)
{
	as_nodes* nodes = as_nodes_reserve(as->cluster);
	
	if (nodes->size == 0) {
		as_nodes_release(nodes);
		return false;
	}
	
	for (uint32_t i = 0; i < nodes->size; i++) {
		if (! nodes->array[i]->has_geo) {
			as_nodes_release(nodes);
			return false;
		}
	}
	as_nodes_release(nodes);
	return true;
}
