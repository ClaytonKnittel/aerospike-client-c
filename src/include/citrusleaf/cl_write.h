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
 #pragma once

/******************************************************************************
 * CONSTANTS
 ******************************************************************************/

typedef enum cl_write_policy_e { 
    CL_WRITE_ASYNC, 
    CL_WRITE_ONESHOT, 
    CL_WRITE_RETRY, 
    CL_WRITE_ASSURED
} cl_write_policy;

/**
 * write info structure
 * There's a lot of info that can go into a write ---
 */
typedef struct cl_write_parameters_s {
    bool            unique;                 // write unique - means success if didn't exist before
    bool            unique_bin;             // write unique bin - means success if the bin didn't exist before
    bool            update_only;            // means success only if the record did exist before
    bool            create_or_replace;      // completely overwrite existing record if any, otherwise create
    bool            replace_only;           // completely overwrite existing record, do not create new record
    bool            bin_replace_only;       // replace existing bin, do not create new bin
    bool            use_generation;         // generation must be exact for write to succeed
    bool            use_generation_gt;      // generation must be less - good for backup & restore
    bool            use_generation_dup;     // on generation collision, create a duplicate
    uint32_t        generation;
    int             timeout_ms;
    uint32_t        record_ttl;             // seconds, from now, when the record would be auto-removed from the DBcd 
    cl_write_policy w_pol;
} cl_write_parameters;

/******************************************************************************
 * INLINE FUNCTIONS
 ******************************************************************************/

static inline void cl_write_parameters_set_default(cl_write_parameters *cl_w_p) {
    cl_w_p->unique = false;
    cl_w_p->unique_bin = false;
    cl_w_p->update_only = false;
    cl_w_p->create_or_replace = false;
    cl_w_p->replace_only = false;
    cl_w_p->bin_replace_only = false;
    cl_w_p->use_generation = false;
    cl_w_p->use_generation_gt = false;
    cl_w_p->use_generation_dup = false;
    cl_w_p->timeout_ms = 0;
    cl_w_p->record_ttl = 0;
    cl_w_p->w_pol = CL_WRITE_RETRY;
}

static inline void cl_write_parameters_set_generation( cl_write_parameters *cl_w_p, uint32_t generation) {
    cl_w_p->generation = generation;
    cl_w_p->use_generation = true;
}

static inline void cl_write_parameters_set_generation_gt( cl_write_parameters *cl_w_p, uint32_t generation) {
    cl_w_p->generation = generation;
    cl_w_p->use_generation_gt = true;
}

static inline void cl_write_parameters_set_generation_dup( cl_write_parameters *cl_w_p, uint32_t generation) {
    cl_w_p->generation = generation;
    cl_w_p->use_generation_dup = true;
}
