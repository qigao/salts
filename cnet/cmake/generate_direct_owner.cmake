if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "generate_direct_owner requires INPUT and OUTPUT")
endif()

file(READ "${INPUT}" content)

function(replace_required old new label)
  string(FIND "${content}" "${old}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "direct-owner transform missing ${label}")
  endif()
  string(REPLACE "${old}" "${new}" content "${content}")
  set(content "${content}" PARENT_SCOPE)
endfunction()

set(old_request_fields [=[  native_io_coroutine_task coroutine;
  native_io_operation operation;]=])
set(new_request_fields [=[  native_io_coroutine_task coroutine;
  native_io_request direct_request;
  native_io_operation operation;]=])
replace_required("${old_request_fields}" "${new_request_fields}" "request handle fields")

set(old_cancel [=[status = native_io_backend_cancel_coroutine(&impl->backend, request->coroutine);]=])
set(new_cancel [=[status = native_io_backend_cancel(&impl->backend, request->direct_request);]=])
replace_required("${old_cancel}" "${new_cancel}" "request cancellation")

set(old_start [=[#if defined(CNET_INTERNAL_PROFILING)
  {
    const uint64_t profile_started = cnet_owner_profile_start(impl);
    status = native_io_backend_spawn_coroutine(&impl->backend, cnet_owner_coroutine_entry, request,
                                               &request->coroutine);
    cnet_owner_profile_finish(impl, profile_started, &impl->profile.request_start_ns,
                              &impl->profile.request_start_calls);
  }
#else
  status = native_io_backend_spawn_coroutine(&impl->backend, cnet_owner_coroutine_entry, request,
                                             &request->coroutine);
#endif
  if (status != SALTS_OK) {
    result = request->active ? cnet_owner_fail_started_request(request, status) : status;
    goto finish;
  }
  if (!request->active) {
    result = cnet_owner_take_coroutine_status(impl);
    goto finish;
  }
  index = (size_t)(request - impl->request_records);]=])
set(new_start [=[  index = (size_t)(request - impl->request_records);
  request->operation.user_data = (uintptr_t)(index + 1u);
#if defined(CNET_INTERNAL_PROFILING)
  {
    const uint64_t profile_started = cnet_owner_profile_start(impl);
    status = native_io_backend_submit(&impl->backend, &request->operation, &request->direct_request);
    cnet_owner_profile_finish(impl, profile_started, &impl->profile.request_start_ns,
                              &impl->profile.request_start_calls);
  }
#else
  status = native_io_backend_submit(&impl->backend, &request->operation, &request->direct_request);
#endif
  if (status != SALTS_OK) {
    result = cnet_owner_fail_started_request(request, status);
    goto finish;
  }]=])
replace_required("${old_start}" "${new_start}" "request start lifecycle")

set(old_start_result [=[  result = cnet_owner_take_coroutine_status(impl);

finish:]=])
set(new_start_result [=[  result = SALTS_OK;

finish:]=])
replace_required("${old_start_result}" "${new_start_result}" "request start result")

set(coroutine_marker [=[static void cnet_owner_coroutine_entry(native_io_coroutine *coroutine, void *user_data) {]=])
set(direct_helper [=[static bool cnet_owner_direct_request_equal(native_io_request left, native_io_request right) {
  return left.slot == right.slot && left.generation == right.generation;
}

static int cnet_owner_complete_direct(cnet_owner_impl *impl,
                                      const native_io_completion *completion) {
  cnet_owner_request *request;
  native_io_completion terminal;
  size_t index;
  int result;
  int status;
#if defined(CNET_INTERNAL_PROFILING)
  uint64_t profile_started;
#endif

  if (completion == NULL || completion->user_data == 0u ||
      completion->user_data > impl->request_capacity)
    return SALTS_EPROTO;
  index = (size_t)completion->user_data - 1u;
  request = &impl->request_records[index];
  if (!request->active || request->owner != impl ||
      !native_io_request_valid(request->direct_request) ||
      !cnet_owner_direct_request_equal(request->direct_request, completion->request))
    return SALTS_EPROTO;

#if defined(CNET_INTERNAL_PROFILING)
  profile_started = cnet_owner_profile_start(impl);
#endif
  terminal = *completion;
  if ((request->role == CNET_OWNER_REQUEST_SEND ||
       request->role == CNET_OWNER_REQUEST_TLS_WRITE) &&
      completion->kind == NATIVE_IO_COMPLETION_OK) {
    if (completion->bytes == 0u || completion->bytes > request->operation.length) {
      result = cnet_owner_fail_started_request(request, SALTS_EIO);
      goto finish;
    }
    request->completed_size += completion->bytes;
    if (request->completed_size < request->requested_size) {
      if (request->operation.kind == NATIVE_IO_OPERATION_UDP_SEND_TO) {
        result = cnet_owner_fail_started_request(request, SALTS_EIO);
        goto finish;
      }
      request->operation.buffer = (unsigned char *)request->operation.buffer + completion->bytes;
      request->operation.length -= completion->bytes;
      status = native_io_backend_submit(&impl->backend, &request->operation,
                                        &request->direct_request);
      result = status == SALTS_OK ? SALTS_OK : cnet_owner_fail_started_request(request, status);
      goto finish;
    }
    if (request->completed_size != request->requested_size) {
      result = cnet_owner_fail_started_request(request, SALTS_EIO);
      goto finish;
    }
    terminal.bytes = request->completed_size;
  }

  result = cnet_owner_complete(impl, request, &terminal);

finish:
#if defined(CNET_INTERNAL_PROFILING)
  cnet_owner_profile_finish(impl, profile_started, &impl->profile.request_completion_ns,
                            &impl->profile.request_completion_calls);
#endif
  return result;
}

]=])
replace_required("${coroutine_marker}" "${direct_helper}${coroutine_marker}" "direct completion hook")

set(old_observe [=[    status = cnet_owner_take_coroutine_status(impl);
    if (status != SALTS_OK) return status;
    if (completion_count != 0u) return SALTS_EPROTO;]=])
set(new_observe [=[    for (size_t completion_index = 0u; completion_index < completion_count;
         ++completion_index) {
      status = cnet_owner_complete_direct(impl, &impl->completions[completion_index]);
      if (status != SALTS_OK) return status;
    }]=])
replace_required("${old_observe}" "${new_observe}" "completion observation")

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${OUTPUT}" "${content}")
