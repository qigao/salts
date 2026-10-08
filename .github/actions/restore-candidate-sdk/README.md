# Candidate SDK consumption

This action downloads one Release SDK from a completed, successful manual
`qigao/salts` CI run. Select both the run ID and its full source SHA; the action
checks the producer workflow, run status and SDK manifest before exporting
`SALTS_CANDIDATE_ROOT`. Pin this action itself to a reviewed commit in consumers.

Consumers must explicitly use this root instead of resolving `Salts.Native`,
while continuing to resolve their other SDK dependencies normally. A missing
artifact, mismatched commit or failed producer run is an error, with no fallback.
The candidate workflow must skip publication. This action neither publishes a
package nor qualifies a branch artifact for the default-branch release process.

```yaml
- uses: qigao/salts/.github/actions/restore-candidate-sdk@<reviewed-action-sha>
  with:
    run-id: ${{ inputs.salts_candidate_run_id }}
    commit: ${{ inputs.salts_candidate_sha }}
    rid: linux-x64
    token: ${{ secrets.GITHUB_TOKEN }}
```
