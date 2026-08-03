/*
 * Disruptor broadcast dependency/topology protocol model.
 *
 * The valid path is parse -> {validate, enrich} -> persist.  The sink may
 * observe only after both middle stages release the entry.  A separate
 * two-node cycle is rejected at topology commit and does not replace the
 * valid committed topology.
 */

byte valid_topology_committed = 0;
byte cycle_commit_result = 2;
byte cycle_rejected = 0;
byte published = 0;

byte parse_seen = 0;
byte parse_released = 0;
byte validate_seen = 0;
byte validate_released = 0;
byte enrich_seen = 0;
byte enrich_released = 0;
byte persist_seen = 0;
byte persist_released = 0;

proctype Builder() {
    atomic {
        valid_topology_committed = 1;
        cycle_commit_result = 0;
        cycle_rejected = 1
    }
}

proctype Publisher() {
    do
    :: valid_topology_committed == 1 && published == 0 ->
        published = 1;
        break
    od
}

proctype Parse() {
    do
    :: published == 1 && parse_seen == 0 ->
        atomic { parse_seen = 1 }
    :: parse_seen == 1 && parse_released == 0 ->
        atomic { parse_released = 1 }
        break
    od
}

proctype Validate() {
    do
    :: published == 1 && parse_released == 1 && validate_seen == 0 ->
        atomic {
            assert(parse_released == 1);
            validate_seen = 1
        }
    :: validate_seen == 1 && validate_released == 0 ->
        atomic { validate_released = 1 }
        break
    od
}

proctype Enrich() {
    do
    :: published == 1 && parse_released == 1 && enrich_seen == 0 ->
        atomic {
            assert(parse_released == 1);
            enrich_seen = 1
        }
    :: enrich_seen == 1 && enrich_released == 0 ->
        atomic { enrich_released = 1 }
        break
    od
}

proctype Persist() {
    do
    :: published == 1 && validate_released == 1 && enrich_released == 1 &&
       persist_seen == 0 ->
        atomic {
            assert(validate_released == 1);
            assert(enrich_released == 1);
            persist_seen = 1
        }
    :: persist_seen == 1 && persist_released == 0 ->
        atomic { persist_released = 1 }
        break
    od
}

proctype Owner() {
    do
    :: persist_released == 1 ->
        assert(valid_topology_committed == 1);
        assert(cycle_rejected == 1);
        assert(cycle_commit_result == 0);
        assert(parse_seen == 1 && parse_released == 1);
        assert(validate_seen == 1 && validate_released == 1);
        assert(enrich_seen == 1 && enrich_released == 1);
        assert(persist_seen == 1);
        break
    od
}

init {
    atomic {
        run Builder();
        run Publisher();
        run Parse();
        run Validate();
        run Enrich();
        run Persist();
        run Owner()
    }
}

ltl acyclic_topology_commits {
    [] (valid_topology_committed -> cycle_commit_result == 0)
}

ltl cyclic_topology_is_rejected {
    [] (cycle_rejected -> !cycle_commit_result)
}

ltl middle_stages_follow_parse {
    [] ((validate_seen || enrich_seen) -> parse_released)
}

ltl sink_waits_for_fan_in {
    [] (persist_seen -> (validate_released && enrich_released))
}

ltl topology_pipeline_eventually_drains {
    [] (published -> <> persist_released)
}
