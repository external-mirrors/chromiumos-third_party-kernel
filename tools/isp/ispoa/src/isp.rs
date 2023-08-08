// SPDX-License-Identifier: MIT
use std::collections::{BTreeMap, BTreeSet};
use std::fmt;
use std::io;
use std::num;
use std::str::FromStr;

pub type IdType = u64;
const NO_ID: u64 = u64::MAX;

type TimestampType = f64;

#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
struct ObjID(pub IdType);

impl FromStr for ObjID {
    type Err = num::ParseIntError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let id = IdType::from_str_radix(s.trim_start_matches("0x"), 16)?;
        Ok(ObjID(id))
    }
}

impl fmt::Display for ObjID {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
struct PipelineID(pub IdType);
impl FromStr for PipelineID {
    type Err = num::ParseIntError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let id = IdType::from_str_radix(s.trim_start_matches("0x"), 16)?;
        Ok(PipelineID(id))
    }
}

impl fmt::Display for PipelineID {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
struct OpID(pub IdType);
impl FromStr for OpID {
    type Err = num::ParseIntError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let id = IdType::from_str_radix(s.trim_start_matches("0x"), 16)?;
        Ok(OpID(id))
    }
}

impl fmt::Display for OpID {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
struct InstanceID(pub IdType);
impl FromStr for InstanceID {
    type Err = num::ParseIntError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let id = IdType::from_str_radix(s.trim_start_matches("0x"), 16)?;
        Ok(InstanceID(id))
    }
}

impl fmt::Display for InstanceID {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

#[derive(PartialEq, Debug)]
struct OpEvent {
    event_type: OpEventType,
    ts: TimestampType,
    id: OpID,
    state: String,
    delay_ns: i32,
    num_blockers: i32,
    instance_id: InstanceID,
    pipeline_id: PipelineID,
}

#[derive(PartialEq, Debug)]
enum OpEventType {
    SetState,
    Add,
}

impl OpEvent {
    fn from_tokens(tokens: &[&str]) -> Self {
        OpEvent {
            event_type: match tokens[3] {
                "isp_operation_set_state:" => OpEventType::SetState,
                "isp_operation_add:" => OpEventType::Add,
                &_ => unimplemented!("unknown operation event type: {}", tokens[3]),
            },
            ts: tokens[2]
                .strip_suffix(':')
                .unwrap()
                .parse::<TimestampType>()
                .unwrap(),
            id: tokens[6]
                .strip_suffix(',')
                .unwrap()
                .parse::<OpID>()
                .unwrap(),
            state: tokens[9].strip_suffix(',').unwrap().to_string(),
            delay_ns: tokens[12]
                .strip_suffix(',')
                .unwrap()
                .parse::<i32>()
                .unwrap(),
            num_blockers: tokens[15]
                .strip_suffix(',')
                .unwrap()
                .parse::<i32>()
                .unwrap(),
            instance_id: tokens[18]
                .strip_suffix(',')
                .unwrap()
                .parse::<InstanceID>()
                .unwrap(),
            pipeline_id: tokens[21].parse::<PipelineID>().unwrap(),
        }
    }
}

#[derive(PartialEq, Debug)]
struct SignalEvent {
    event_type: SignalEventType,
    ts: TimestampType,
    source_id: ObjID,
    source_type: String,
    target_id: OpID,
    instance_id: InstanceID,
    pipeline_id: PipelineID,
}

#[derive(PartialEq, Debug)]
enum SignalEventType {
    AddPending,
    AddActive,
    FireActive,
}

impl SignalEvent {
    fn from_tokens(tokens: &[&str]) -> Self {
        SignalEvent {
            event_type: match tokens[3] {
                "isp_signal_add_pending:" => SignalEventType::AddPending,
                "isp_signal_add_active:" => SignalEventType::AddActive,
                "isp_signal_fire_active:" => SignalEventType::FireActive,
                &_ => unimplemented!("unknown signal event type: {}", tokens[3]),
            },
            ts: tokens[2]
                .strip_suffix(':')
                .unwrap()
                .parse::<TimestampType>()
                .unwrap(),
            source_id: tokens[6]
                .strip_suffix(',')
                .unwrap()
                .parse::<ObjID>()
                .unwrap(),
            source_type: tokens[9].strip_suffix(',').unwrap().to_string(),
            target_id: tokens[12]
                .strip_suffix(',')
                .unwrap()
                .parse::<OpID>()
                .unwrap(),
            instance_id: tokens[15]
                .strip_suffix(',')
                .unwrap()
                .parse::<InstanceID>()
                .unwrap(),
            pipeline_id: tokens[18].parse::<PipelineID>().unwrap(),
        }
    }
}

#[derive(PartialEq, Debug)]
enum EventEntry {
    OpEntry(OpEvent),
    SignalEntry(SignalEvent),
}

impl EventEntry {
    fn op_id(&self) -> OpID {
        match self {
            EventEntry::OpEntry(o) => o.id,
            EventEntry::SignalEntry(s) => s.target_id,
        }
    }

    fn pipeline_id(&self) -> PipelineID {
        match self {
            EventEntry::OpEntry(o) => o.pipeline_id,
            EventEntry::SignalEntry(s) => s.pipeline_id,
        }
    }
}

impl EventEntry {
    /*
     * example for operation class logs:
     * CameraDeviceOps-6415  [005]    46.236774: isp_operation_set_state: id = 0x1, state = SLEEP, delay_ns = 0, num_blockers = 0, instance_id = 0xffffffffffffffff, pipeline_id = 0x2
     *
     * example for signal class logs:
     * ipu6_lb_video-6547  [000]    47.605062: isp_signal_add_pending: source_id = 0x2, source_type = EVENT, target_id = 0x29, instance_id = 0x20001, pipeline_id = 0x2
     *
     * example for event class logs (ignored):
     * psys_sched_cmd-3457  [006]    47.605625: isp_event_trigger:    entity_id = 0x1, instance_id = 0x20001, entity_name = PSYS0, event_id = 0x2, event_name = PSYS0
     *
     */
    fn parse_line(line: &str) -> Option<EventEntry> {
        let token: Vec<&str> = line.split_ascii_whitespace().collect();

        // skip anything that doesn't look like a isp ftace event log
        if token.len() < 4 || !token[3].starts_with("isp_") {
            return None;
        }

        if token[3].starts_with("isp_operation_") {
            Some(EventEntry::OpEntry(OpEvent::from_tokens(&token)))
        } else if token[3].starts_with("isp_signal_") {
            Some(EventEntry::SignalEntry(SignalEvent::from_tokens(&token)))
        } else {
            None
        }
    }
}

struct EventList {
    list: Vec<EventEntry>,
}

impl EventList {
    fn parse_op_log<R: io::BufRead>(reader: R) -> Self {
        EventList {
            list: reader
                .lines()
                .filter_map(|l| l.ok())
                .filter_map(|l| EventEntry::parse_line(&l))
                .collect(),
        }
    }
}

#[derive(Debug)]
pub struct OpList {
    map: BTreeMap<PipelineID, BTreeMap<OpID, Vec<Op>>>,
}

impl OpList {
    fn new() -> Self {
        OpList {
            map: BTreeMap::new(),
        }
    }

    fn from_event_list(events: &EventList) -> OpList {
        let mut list = OpList::new();
        for e in events.list.iter() {
            let v: &mut Vec<Op> = list
                .map
                .entry(e.pipeline_id())
                .or_default()
                .entry(e.op_id())
                .or_default();

            /* A signal event may be generated after the target operation was finished,
             * so we always associate a signal event to the latest operation */
            match v.last_mut() {
                None => v.push(Op::from_event(e)),
                Some(latest) => match e {
                    EventEntry::OpEntry(oe) => {
                        if latest.is_finished() {
                            v.push(Op::from_event(e));
                        } else {
                            latest.add_op_event(oe);
                        }
                    }
                    EventEntry::SignalEntry(se) => latest.add_signal_event(se),
                },
            }
        }
        list
    }

    pub fn output_summary<W: io::Write>(&self, mut writer: W) -> io::Result<()> {
        let indent = "    ";

        writeln!(writer, "{indent}# of pipelines: {}", self.map.len())?;

        let op_num: usize = self.map.values().map(|ops| ops.len()).sum();
        writeln!(writer, "{indent}# of operations: {}", op_num)?;

        let completed = OpRefList::ops_completed(self);
        let inflight = OpRefList::ops_inflight(self);
        writeln!(
            writer,
            "{indent}# of completed operations: {}",
            completed.len()
        )?;
        writeln!(
            writer,
            "{indent}# of in-flight operations: {}",
            inflight.len()
        )?;

        writeln!(writer, "{indent}[completed stats]")?;
        completed.output_timings(&mut writer)?;

        writeln!(writer, "{indent}[in-flight ops]")?;
        inflight.output_states(&mut writer)?;

        Ok(())
    }

    pub fn output_pipeline_numbers<W: io::Write>(&self, mut writer: W) -> io::Result<()> {
        for pipeline in self.map.keys() {
            write!(writer, " {}", pipeline)?;
        }
        writeln!(writer)
    }

    pub fn output_operation_numbers<W: io::Write>(
        &self,
        mut writer: W,
        pipeline_id: IdType,
    ) -> io::Result<()> {
        let pipeline = PipelineID(pipeline_id);
        if let Some(ops) = self.map.get(&pipeline) {
            writeln!(writer, "# of operations = {}", ops.keys().len())?;

            let mut s = None;
            let mut e = None;

            for op in ops.keys() {
                if s.is_none() && e.is_none() {
                    s = Some(*op);
                    e = Some(*op);
                    continue;
                }

                if *op == OpID(e.unwrap().0 + 1) {
                    e = Some(*op);
                    continue;
                }

                if s == e {
                    write!(writer, " {}", s.unwrap())?;
                } else {
                    write!(writer, " {}-{}", s.unwrap(), e.unwrap())?;
                }
                s = Some(*op);
                e = Some(*op);
            }

            if s.is_some() {
                if s == e {
                    write!(writer, " {}", s.unwrap())?;
                } else {
                    write!(writer, " {}-{}", s.unwrap(), e.unwrap())?;
                }
            }
            writeln!(writer)?
        }
        Ok(())
    }

    pub fn output_operation<W: io::Write>(
        &self,
        mut writer: W,
        pipeline_id: IdType,
        op_id: IdType,
    ) -> io::Result<()> {
        let pipeline = PipelineID(pipeline_id);
        let operation = OpID(op_id);
        if let Some(pipeline) = self.map.get(&pipeline) {
            if let Some(ops) = pipeline.get(&operation) {
                for op in ops.iter() {
                    op.output_details(&mut writer)?;
                }
            }
        }
        Ok(())
    }
}

struct OpRefList<'a> {
    list: Vec<&'a Op>,
}

impl<'a> OpRefList<'a> {
    fn new() -> Self {
        OpRefList { list: Vec::new() }
    }

    fn len(&self) -> usize {
        self.list.len()
    }

    fn ops_completed(list: &'a OpList) -> Self {
        let mut completed = Self::new();

        for pipeline in list.map.values() {
            for ops in pipeline.values() {
                for op in ops {
                    if op.is_finished() {
                        completed.list.push(op);
                    }
                }
            }
        }

        completed
    }

    fn ops_inflight(list: &'a OpList) -> Self {
        let mut inflight = Self::new();

        for pipeline in list.map.values() {
            for ops in pipeline.values() {
                for op in ops {
                    if !op.is_finished() {
                        inflight.list.push(op);
                    }
                }
            }
        }

        inflight
    }

    fn get_timings(&self) -> TimeStats {
        let mut stats = TimeStats::new();
        for op in &self.list {
            let mut is_first = true;
            let mut prev_state = "";
            let mut prev_ts = 0.0;
            for h in &op.state_history {
                if is_first {
                    prev_state = &h.state;
                    prev_ts = h.ts;
                    is_first = false;
                    continue;
                }
                if prev_state == h.state {
                    continue;
                }

                stats.append(prev_state, &h.state, (h.ts - prev_ts) as f32);
                prev_state = &h.state;
                prev_ts = h.ts;
            }
        }
        stats
    }

    fn output_timings<W: io::Write>(&self, writer: &mut W) -> io::Result<()> {
        let stats = self.get_timings();
        for ((old_s, new_s), e) in stats.map {
            write!(
                writer,
                "    {:8} -> {:8}: min {:.6}, max {:.6}, avg {:.6}, count {}",
                old_s, new_s, e.min, e.max, e.avg, e.count
            )?;
            writeln!(writer)?;
        }

        Ok(())
    }

    fn output_states<W: io::Write>(&self, writer: &mut W) -> io::Result<()> {
        let mut sh = StateHistogram::new();

        for op in &self.list {
            sh.append(op);
            op.output_details(writer)?;
        }

        sh.output_histogram(writer)?;

        Ok(())
    }
}

struct TimeStats<'a> {
    map: BTreeMap<(&'a str, &'a str), TimeStatsEntry>,
}

impl<'a> TimeStats<'a> {
    fn new() -> Self {
        TimeStats {
            map: BTreeMap::new(),
        }
    }

    fn append(&mut self, old_state: &'a str, new_state: &'a str, length: f32) {
        let e = self.map.entry((old_state, new_state)).or_default();
        let mut sum: f64 = e.avg as f64 * e.count as f64;
        sum += length as f64;
        e.count += 1;
        if length < e.min {
            e.min = length;
        }
        if e.max < length {
            e.max = length;
        }
        e.avg = (sum / e.count as f64) as f32;
    }
}

#[derive(PartialEq, Debug)]
struct TimeStatsEntry {
    min: f32,
    max: f32,
    avg: f32,
    count: usize,
}

impl Default for TimeStatsEntry {
    fn default() -> Self {
        TimeStatsEntry {
            min: f32::MAX,
            max: 0.0,
            avg: 0.0,
            count: 0,
        }
    }
}

struct StateHistogram<'a> {
    map: BTreeMap<PipelineID, BTreeMap<&'a str, usize>>,
}

impl<'a> StateHistogram<'a> {
    fn new() -> Self {
        StateHistogram {
            map: BTreeMap::new(),
        }
    }

    fn append(&mut self, op: &'a Op) {
        let p_e = self.map.entry(op.pipeline_id).or_default();
        let last_state = &op.state_history.last().unwrap().state;
        let c_e = p_e.entry(last_state).or_insert(0);
        *c_e += 1;
    }

    fn output_histogram<W: io::Write>(&self, writer: &mut W) -> io::Result<()> {
        let indent = "    ";
        for (pipeline, stat) in self.map.iter() {
            writeln!(writer, "{indent}[pipeline {}]", pipeline)?;
            for (state, count) in stat.iter() {
                writeln!(writer, "{indent} # of ops in {}: {}", state, count)?;
            }
        }

        Ok(())
    }
}

#[derive(PartialEq, Debug)]
pub struct Op {
    id: OpID,
    delay_ns: i32,
    pipeline_id: PipelineID,
    state_history: Vec<OpState>,
}

impl Op {
    fn unknown() -> Self {
        Op {
            id: OpID(NO_ID),
            delay_ns: 0,
            pipeline_id: PipelineID(NO_ID),
            state_history: Vec::new(),
        }
    }

    fn from_event(e: &EventEntry) -> Self {
        match e {
            EventEntry::OpEntry(oe) => {
                let mut op = Op {
                    id: oe.id,
                    delay_ns: oe.delay_ns,
                    pipeline_id: oe.pipeline_id,
                    state_history: Vec::new(),
                };
                op.add_op_event(oe);
                op
            }
            EventEntry::SignalEntry(se) => {
                let mut op = Op::unknown();
                op.add_signal_event(se);
                op
            }
        }
    }

    fn add_op_event(&mut self, oe: &OpEvent) {
        let maybe_prev = self.state_history.last();
        if let Some(state) = OpState::from_op_event(oe, maybe_prev) {
            self.state_history.push(state);
        }
    }

    fn add_signal_event(&mut self, se: &SignalEvent) {
        if self.state_history.is_empty() {
            self.state_history.push(OpState::unknown());
        }

        let prev = self.state_history.last().unwrap();
        if let Some(state) = OpState::from_signal_event(se, prev) {
            self.state_history.push(state);
        }
    }

    fn is_finished(&self) -> bool {
        let last_state = &self.state_history.last().unwrap().state;
        last_state == "EXECUTED" || last_state == "DELETED"
    }

    fn output_details<W: io::Write>(&self, writer: &mut W) -> io::Result<()> {
        writeln!(
            writer,
            "    id {}, delay_ns {}, pipeline_id {}",
            self.id, self.delay_ns, self.pipeline_id
        )?;

        let mut prev: Option<&OpState> = None;

        writeln!(writer, "    state_history:")?;
        for s in &self.state_history {
            match prev {
                None => {
                    writeln!(
                        writer,
                        "      [ts {:.6}, state {}, added {}, num_blockers {}, blockers {:?}]",
                        s.ts, s.state, s.added, s.num_blockers, s.blockers
                    )?;
                }
                Some(prev_state) => {
                    write!(writer, "      [ts {:.6}", s.ts)?;
                    if prev_state.state != s.state {
                        write!(writer, ", state -> {}", s.state)?;
                    }
                    if prev_state.added != s.added {
                        write!(writer, ", added -> {}", s.added)?;
                    }
                    if prev_state.num_blockers != s.num_blockers {
                        write!(
                            writer,
                            ", num_blockers -> {}, blockers {:?}",
                            s.num_blockers, s.blockers
                        )?;
                    }
                    writeln!(writer, "]")?;
                }
            }
            prev = Some(s)
        }

        Ok(())
    }
}

#[derive(PartialEq, Debug)]
pub struct OpState {
    ts: TimestampType,
    state: String,
    added: bool,
    num_blockers: i32,
    blockers: BTreeSet<BlockerInfo>,
}

impl OpState {
    fn unknown() -> Self {
        OpState {
            ts: -1.0,
            state: "".to_string(),
            added: false,
            num_blockers: 0,
            blockers: BTreeSet::new(),
        }
    }

    fn from_op_event(oe: &OpEvent, maybe_prev: Option<&OpState>) -> Option<Self> {
        Some(OpState {
            ts: oe.ts,
            state: oe.state.clone(),
            added: match oe.event_type {
                OpEventType::Add => true,
                _ => {
                    if let Some(prev) = maybe_prev {
                        prev.added
                    } else {
                        false
                    }
                }
            },
            num_blockers: oe.num_blockers,
            blockers: BTreeSet::new(),
        })
    }

    fn from_signal_event(se: &SignalEvent, prev: &OpState) -> Option<Self> {
        let mut blockers = prev.blockers.clone();

        let num_blockers = match se.event_type {
            SignalEventType::AddPending => {
                blockers.insert(BlockerInfo::from_signal_event(se));
                prev.num_blockers + 1
            }
            SignalEventType::AddActive => prev.num_blockers,
            SignalEventType::FireActive => {
                blockers.remove(&BlockerInfo::from_signal_event(se));
                prev.num_blockers - 1
            }
        };

        let op_state = OpState {
            ts: se.ts,
            state: prev.state.clone(),
            added: prev.added,
            num_blockers,
            blockers,
        };
        if !op_state.is_in_same(prev) {
            Some(op_state)
        } else {
            None
        }
    }

    fn is_in_same(&self, other: &OpState) -> bool {
        self.state == other.state
            && self.added == other.added
            && self.num_blockers == other.num_blockers
            && self.blockers == other.blockers
    }
}

#[derive(Eq, Ord, PartialOrd, PartialEq, Clone)]
struct BlockerInfo {
    obj_type: String,
    id: ObjID,
    instance_id: InstanceID,
}

impl BlockerInfo {
    fn from_signal_event(se: &SignalEvent) -> Self {
        BlockerInfo {
            obj_type: se.source_type.clone(),
            id: se.source_id,
            instance_id: se.instance_id,
        }
    }
}

impl fmt::Debug for BlockerInfo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} {} {}", self.obj_type, self.id, self.instance_id)
    }
}

/* parse + extract */
pub fn get_op_list<R: io::BufRead>(reader: R) -> io::Result<OpList> {
    let event_list = EventList::parse_op_log(reader);
    let op_list = OpList::from_event_list(&event_list);
    Ok(op_list)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fmt::Write;

    impl BlockerInfo {
        fn test_new(obj_type: &str, id: u64, instance_id: u64) -> Self {
            BlockerInfo {
                obj_type: obj_type.to_string(),
                id: ObjID(id),
                instance_id: InstanceID(instance_id),
            }
        }
    }

    #[test]
    fn parse_line_for_state_event() {
        let log = "ipu6_bb_video-7908  [010]   995.744359: isp_operation_set_state: id = 0x79605, state = SLEEP, delay_ns = 2, num_blockers = 4, instance_id = 0x70, pipeline_id = 0x30";

        let entry = EventEntry::parse_line(&log);
        assert!(entry.is_some());

        let expected = EventEntry::OpEntry(OpEvent {
            event_type: OpEventType::SetState,
            ts: 995.744359,
            id: OpID(0x79605),
            state: "SLEEP".to_string(),
            delay_ns: 2,
            num_blockers: 4,
            instance_id: InstanceID(0x70),
            pipeline_id: PipelineID(0x30),
        });
        assert_eq!(entry, Some(expected));
    }

    #[test]
    fn parse_line_for_state_add() {
        let log = "visptest-265   [001]    17.088879: isp_operation_add:    id = 0, state = SLEEP, delay_ns = 0, num_blockers = 1, instance_id = 0x30, pipeline_id = 0x20";

        let entry = EventEntry::parse_line(&log);
        assert!(entry.is_some());

        let expected = EventEntry::OpEntry(OpEvent {
            event_type: OpEventType::Add,
            ts: 17.088879,
            id: OpID(0),
            state: "SLEEP".to_string(),
            delay_ns: 0,
            num_blockers: 1,
            instance_id: InstanceID(0x30),
            pipeline_id: PipelineID(0x20),
        });
        assert_eq!(entry, Some(expected));
    }

    #[test]
    fn parse_line_for_signal_event() {
        let log = "visptest-270   [000]    34.317839: isp_signal_add_pending: source_id = 0x80, source_type = EVENT, target_id = 0x50, instance_id = 0x30, pipeline_id = 0x20";

        let entry = EventEntry::parse_line(&log);
        assert!(entry.is_some());

        let expected = EventEntry::SignalEntry(SignalEvent {
            event_type: SignalEventType::AddPending,
            ts: 34.317839,
            source_id: ObjID(0x80),
            source_type: "EVENT".to_string(),
            target_id: OpID(0x50),
            instance_id: InstanceID(0x30),
            pipeline_id: PipelineID(0x20),
        });
        assert_eq!(entry, Some(expected));
    }

    #[test]
    fn parse_line_for_event_trigger() {
        let log = "irq/16-intel-ip-2807  [010]   995.813770: isp_event_trigger:    entity_id = 0x100, instance_id = 0x100, entity_name = PSYS, event_id = 0x200, event_name = event-189";

        let entry = EventEntry::parse_line(&log);
        assert!(entry.is_none());
    }

    #[test]
    fn parse_line_for_log_headers() {
        let log = "version = 7";
        let log2 = "cpus=12";

        assert!(EventEntry::parse_line(&log).is_none());
        assert!(EventEntry::parse_line(&log2).is_none());
    }

    #[test]
    fn extract_ops_single() {
        let events = EventList {
            list: vec![EventEntry::OpEntry(OpEvent {
                event_type: OpEventType::SetState,
                ts: 1.234,
                id: OpID(5),
                state: "QUEUED".to_string(),
                delay_ns: 3,
                num_blockers: 4,
                instance_id: InstanceID(1),
                pipeline_id: PipelineID(5),
            })],
        };

        let op_list = OpList::from_event_list(&events);
        assert_eq!(op_list.map.len(), 1);
    }

    #[test]
    fn extract_ops_two_different_id() {
        let events = EventList {
            list: vec![
                EventEntry::OpEntry(OpEvent {
                    event_type: OpEventType::SetState,
                    ts: 1.234,
                    id: OpID(5),
                    state: "QUEUED".to_string(),
                    delay_ns: 3,
                    num_blockers: 4,
                    instance_id: InstanceID(1),
                    pipeline_id: PipelineID(5),
                }),
                EventEntry::OpEntry(OpEvent {
                    event_type: OpEventType::SetState,
                    ts: 1.234,
                    id: OpID(6),
                    state: "QUEUED".to_string(),
                    delay_ns: 3,
                    num_blockers: 4,
                    instance_id: InstanceID(1),
                    pipeline_id: PipelineID(5),
                }),
            ],
        };

        let op_list = OpList::from_event_list(&events);
        assert_eq!(op_list.map.len(), 1);
        assert_eq!(op_list.map[&PipelineID(5)].len(), 2);
    }

    #[test]
    fn extract_ops_two_different_pipeline_id() {
        let events = EventList {
            list: vec![
                EventEntry::OpEntry(OpEvent {
                    event_type: OpEventType::SetState,
                    ts: 1.234,
                    id: OpID(5),
                    state: "QUEUED".to_string(),
                    delay_ns: 3,
                    num_blockers: 4,
                    instance_id: InstanceID(1),
                    pipeline_id: PipelineID(5),
                }),
                EventEntry::OpEntry(OpEvent {
                    event_type: OpEventType::SetState,
                    ts: 1.234,
                    id: OpID(5),
                    state: "QUEUED".to_string(),
                    delay_ns: 3,
                    num_blockers: 4,
                    instance_id: InstanceID(1),
                    pipeline_id: PipelineID(7),
                }),
            ],
        };

        let op_list = OpList::from_event_list(&events);
        assert_eq!(op_list.map.len(), 2);
        assert_eq!(op_list.map[&PipelineID(5)].len(), 1);
        assert_eq!(op_list.map[&PipelineID(7)].len(), 1);
    }

    #[test]
    fn extract_signal_single() {
        let events = EventList {
            list: vec![EventEntry::SignalEntry(SignalEvent {
                event_type: SignalEventType::AddPending,
                ts: 34.317839,
                source_id: ObjID(8),
                source_type: "EVENT".to_string(),
                target_id: OpID(0),
                instance_id: InstanceID(1),
                pipeline_id: PipelineID(2),
            })],
        };

        let op_list = OpList::from_event_list(&events);
        assert_eq!(op_list.map.len(), 1);
    }

    #[test]
    fn extract_signal_two_different_target_id() {
        let events = EventList {
            list: vec![
                EventEntry::SignalEntry(SignalEvent {
                    event_type: SignalEventType::AddPending,
                    ts: 34.317839,
                    source_id: ObjID(8),
                    source_type: "EVENT".to_string(),
                    target_id: OpID(0),
                    instance_id: InstanceID(1),
                    pipeline_id: PipelineID(2),
                }),
                EventEntry::SignalEntry(SignalEvent {
                    event_type: SignalEventType::AddPending,
                    ts: 34.317839,
                    source_id: ObjID(8),
                    source_type: "EVENT".to_string(),
                    target_id: OpID(1),
                    instance_id: InstanceID(1),
                    pipeline_id: PipelineID(2),
                }),
            ],
        };

        let op_list = OpList::from_event_list(&events);
        assert_eq!(op_list.map.len(), 1);
        assert_eq!(op_list.map[&PipelineID(2)].len(), 2);
    }

    #[test]
    fn extract_signal_two_different_pipeline_id() {
        let events = EventList {
            list: vec![
                EventEntry::SignalEntry(SignalEvent {
                    event_type: SignalEventType::AddPending,
                    ts: 34.317839,
                    source_id: ObjID(8),
                    source_type: "EVENT".to_string(),
                    target_id: OpID(0),
                    instance_id: InstanceID(1),
                    pipeline_id: PipelineID(1),
                }),
                EventEntry::SignalEntry(SignalEvent {
                    event_type: SignalEventType::FireActive,
                    ts: 34.317839,
                    source_id: ObjID(8),
                    source_type: "EVENT".to_string(),
                    target_id: OpID(1),
                    instance_id: InstanceID(1),
                    pipeline_id: PipelineID(2),
                }),
            ],
        };

        let op_list = OpList::from_event_list(&events);
        assert_eq!(op_list.map.len(), 2);
        assert_eq!(op_list.map[&PipelineID(1)].len(), 1);
        assert_eq!(op_list.map[&PipelineID(2)].len(), 1);
    }

    #[test]
    fn create_op_from_op_event() {
        let event = EventEntry::OpEntry(OpEvent {
            event_type: OpEventType::SetState,
            ts: 1.234,
            id: OpID(5),
            state: "QUEUED".to_string(),
            delay_ns: 3,
            num_blockers: 4,
            instance_id: InstanceID(1),
            pipeline_id: PipelineID(5),
        });

        let op = Op::from_event(&event);
        let expected = Op {
            id: OpID(5),
            delay_ns: 3,
            pipeline_id: PipelineID(5),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };
        assert_eq!(op, expected);
    }

    #[test]
    fn update_op_with_signal_event() {
        let mut op = Op {
            id: OpID(5),
            delay_ns: 3,
            pipeline_id: PipelineID(5),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 0,
                blockers: BTreeSet::new(),
            }],
        };

        let se_1 = SignalEvent {
            event_type: SignalEventType::AddPending,
            ts: 34.317839,
            source_id: ObjID(8),
            source_type: "EVENT".to_string(),
            target_id: OpID(5),
            instance_id: InstanceID(1),
            pipeline_id: PipelineID(1),
        };

        let se_2 = SignalEvent {
            event_type: SignalEventType::FireActive,
            ts: 35.317839,
            source_id: ObjID(8),
            source_type: "EVENT".to_string(),
            target_id: OpID(5),
            instance_id: InstanceID(1),
            pipeline_id: PipelineID(1),
        };

        op.add_signal_event(&se_1);
        op.add_signal_event(&se_2);

        let expected = Op {
            id: OpID(5),
            delay_ns: 3,
            pipeline_id: PipelineID(5),
            state_history: vec![
                OpState {
                    ts: 1.234,
                    state: "QUEUED".to_string(),
                    added: false,
                    num_blockers: 0,
                    blockers: BTreeSet::new(),
                },
                OpState {
                    ts: 34.317839,
                    state: "QUEUED".to_string(),
                    added: false,
                    num_blockers: 1,
                    blockers: BTreeSet::from([BlockerInfo::test_new("EVENT", 8, 1)]),
                },
                OpState {
                    ts: 35.317839,
                    state: "QUEUED".to_string(),
                    added: false,
                    num_blockers: 0,
                    blockers: BTreeSet::new(),
                },
            ],
        };

        assert_eq!(op, expected);
    }

    #[test]
    fn get_refs_for_completed_and_inflight() {
        let mut op_list = OpList::new();
        let pipeline1_id = 1;
        let pipeline2_id = 2;

        let pipeline1_op1 = Op {
            id: OpID(5),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };
        let pipeline1_op2 = Op {
            id: OpID(6),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![
                OpState {
                    ts: 1.234,
                    state: "SLEEP".to_string(),
                    added: false,
                    num_blockers: 0,
                    blockers: BTreeSet::new(),
                },
                OpState {
                    ts: 1.234,
                    state: "QUEUED".to_string(),
                    added: false,
                    num_blockers: 0,
                    blockers: BTreeSet::new(),
                },
                OpState {
                    ts: 1.234,
                    state: "RUNNING".to_string(),
                    added: false,
                    num_blockers: 0,
                    blockers: BTreeSet::new(),
                },
                OpState {
                    ts: 1.234,
                    state: "EXECUTED".to_string(),
                    added: false,
                    num_blockers: 0,
                    blockers: BTreeSet::new(),
                },
            ],
        };

        let pipeline2_op1 = Op {
            id: OpID(7),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline2_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "DELETED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_ops = vec![
            (pipeline1_op1.id, vec![pipeline1_op1]),
            (pipeline1_op2.id, vec![pipeline1_op2]),
        ];

        let pipeline2_ops = vec![(pipeline2_op1.id, vec![pipeline2_op1])];

        op_list.map.insert(
            PipelineID(pipeline1_id),
            BTreeMap::from_iter(pipeline1_ops.into_iter()),
        );
        op_list.map.insert(
            PipelineID(pipeline2_id),
            BTreeMap::from_iter(pipeline2_ops.into_iter()),
        );

        let completed = OpRefList::ops_completed(&op_list);
        assert_eq!(completed.list.len(), 2);

        let inflight = OpRefList::ops_inflight(&op_list);
        assert_eq!(inflight.list.len(), 1);
    }

    #[test]
    fn output_oplist_with_single_op() {
        let mut op_list = OpList::new();
        let pipeline1_id = 1;

        let pipeline1_op1 = Op {
            id: OpID(5),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_ops = vec![(pipeline1_op1.id, vec![pipeline1_op1])];

        op_list.map.insert(
            PipelineID(pipeline1_id),
            BTreeMap::from_iter(pipeline1_ops.into_iter()),
        );

        let mut expected = String::new();
        writeln!(expected, "# of operations = 1").unwrap();
        writeln!(expected, " 5").unwrap();

        let mut buf = Vec::new();
        op_list
            .output_operation_numbers(&mut buf, pipeline1_id)
            .unwrap();
        let s = std::str::from_utf8(&buf).unwrap();

        assert_eq!(s, &expected);
    }

    #[test]
    fn output_oplist_with_two_consecutive_ops() {
        let mut op_list = OpList::new();
        let pipeline1_id = 1;

        let pipeline1_op1 = Op {
            id: OpID(5),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_op2 = Op {
            id: OpID(6),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_ops = vec![
            (pipeline1_op1.id, vec![pipeline1_op1]),
            (pipeline1_op2.id, vec![pipeline1_op2]),
        ];

        op_list.map.insert(
            PipelineID(pipeline1_id),
            BTreeMap::from_iter(pipeline1_ops.into_iter()),
        );

        let mut expected = String::new();
        writeln!(expected, "# of operations = 2").unwrap();
        writeln!(expected, " 5-6").unwrap();

        let mut buf = Vec::new();
        op_list
            .output_operation_numbers(&mut buf, pipeline1_id)
            .unwrap();
        let s = std::str::from_utf8(&buf).unwrap();

        assert_eq!(s, &expected);
    }

    #[test]
    fn output_oplist_with_non_consecutive_ops() {
        let mut op_list = OpList::new();
        let pipeline1_id = 1;

        let pipeline1_op1 = Op {
            id: OpID(5),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_op2 = Op {
            id: OpID(6),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_op3 = Op {
            id: OpID(8),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_ops = vec![
            (pipeline1_op1.id, vec![pipeline1_op1]),
            (pipeline1_op2.id, vec![pipeline1_op2]),
            (pipeline1_op3.id, vec![pipeline1_op3]),
        ];

        op_list.map.insert(
            PipelineID(pipeline1_id),
            BTreeMap::from_iter(pipeline1_ops.into_iter()),
        );

        let mut expected = String::new();
        writeln!(expected, "# of operations = 3").unwrap();
        writeln!(expected, " 5-6 8").unwrap();

        let mut buf = Vec::new();
        op_list
            .output_operation_numbers(&mut buf, pipeline1_id)
            .unwrap();
        let s = std::str::from_utf8(&buf).unwrap();

        assert_eq!(s, &expected);
    }

    #[test]
    fn output_oplist_with_non_consecutive_ops_complex() {
        let mut op_list = OpList::new();
        let pipeline1_id = 1;

        let pipeline1_op1 = Op {
            id: OpID(5),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_op2 = Op {
            id: OpID(6),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_op3 = Op {
            id: OpID(8),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_op4 = Op {
            id: OpID(10),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_op5 = Op {
            id: OpID(11),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_op6 = Op {
            id: OpID(12),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: false,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        let pipeline1_ops = vec![
            (pipeline1_op1.id, vec![pipeline1_op1]),
            (pipeline1_op2.id, vec![pipeline1_op2]),
            (pipeline1_op3.id, vec![pipeline1_op3]),
            (pipeline1_op4.id, vec![pipeline1_op4]),
            (pipeline1_op5.id, vec![pipeline1_op5]),
            (pipeline1_op6.id, vec![pipeline1_op6]),
        ];

        op_list.map.insert(
            PipelineID(pipeline1_id),
            BTreeMap::from_iter(pipeline1_ops.into_iter()),
        );

        let mut expected = String::new();
        writeln!(expected, "# of operations = 6").unwrap();
        writeln!(expected, " 5-6 8 10-12").unwrap();

        let mut buf = Vec::new();
        op_list
            .output_operation_numbers(&mut buf, pipeline1_id)
            .unwrap();
        let s = std::str::from_utf8(&buf).unwrap();

        assert_eq!(s, &expected);
    }

    #[test]
    fn calc_time_statistics() {
        let key_pair_1 = ("SLEEP", "QUEUED");
        let key_pair_2 = ("QUEUED", "RUNNING");

        let mut ts = TimeStats::new();

        ts.append(key_pair_1.0, key_pair_1.1, 10.0);
        assert_eq!(
            ts.map.get(&key_pair_1).unwrap(),
            &TimeStatsEntry {
                min: 10.0,
                max: 10.0,
                avg: 10.0,
                count: 1
            }
        );

        /* (10.0 + 18.0) / 2 = 14.0 */
        ts.append(key_pair_1.0, key_pair_1.1, 18.0);
        assert_eq!(
            ts.map.get(&key_pair_1).unwrap(),
            &TimeStatsEntry {
                min: 10.0,
                max: 18.0,
                avg: 14.0,
                count: 2
            }
        );

        /* (10.0 + 18.0 + 8.3) / 3 = 12.1 */
        ts.append(key_pair_1.0, key_pair_1.1, 8.3);
        assert_eq!(
            ts.map.get(&key_pair_1).unwrap(),
            &TimeStatsEntry {
                min: 8.3,
                max: 18.0,
                avg: 12.1,
                count: 3
            }
        );

        /* (10.0 + 18.0 + 8.3 + 4.1) / 4 = 10.1 */
        ts.append(key_pair_1.0, key_pair_1.1, 4.1);
        assert_eq!(
            ts.map.get(&key_pair_1).unwrap(),
            &TimeStatsEntry {
                min: 4.1,
                max: 18.0,
                avg: 10.1,
                count: 4
            }
        );

        ts.append(key_pair_2.0, key_pair_2.1, 0.1);
        assert_eq!(
            ts.map.get(&key_pair_1).unwrap(),
            &TimeStatsEntry {
                min: 4.1,
                max: 18.0,
                avg: 10.1,
                count: 4
            }
        );
        assert_eq!(
            ts.map.get(&key_pair_2).unwrap(),
            &TimeStatsEntry {
                min: 0.1,
                max: 0.1,
                avg: 0.1,
                count: 1
            }
        );
    }

    #[test]
    fn calc_state_histogram() {
        let pipeline1_id = 1;
        let pipeline2_id = 2;

        let mut sh = StateHistogram::new();

        let pipeline1_op1 = Op {
            id: OpID(5),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "QUEUED".to_string(),
                added: true,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };
        let pipeline1_op2 = Op {
            id: OpID(6),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline1_id),
            state_history: vec![
                OpState {
                    ts: 1.234,
                    state: "SLEEP".to_string(),
                    added: true,
                    num_blockers: 0,
                    blockers: BTreeSet::new(),
                },
                OpState {
                    ts: 1.234,
                    state: "QUEUED".to_string(),
                    added: true,
                    num_blockers: 0,
                    blockers: BTreeSet::new(),
                },
                OpState {
                    ts: 1.234,
                    state: "RUNNING".to_string(),
                    added: true,
                    num_blockers: 0,
                    blockers: BTreeSet::new(),
                },
                OpState {
                    ts: 1.234,
                    state: "EXECUTED".to_string(),
                    added: true,
                    num_blockers: 0,
                    blockers: BTreeSet::new(),
                },
            ],
        };

        let pipeline2_op1 = Op {
            id: OpID(7),
            delay_ns: 3,
            pipeline_id: PipelineID(pipeline2_id),
            state_history: vec![OpState {
                ts: 1.234,
                state: "DELETED".to_string(),
                added: true,
                num_blockers: 4,
                blockers: BTreeSet::new(),
            }],
        };

        sh.append(&pipeline1_op1);
        sh.append(&pipeline1_op2);
        sh.append(&pipeline2_op1);

        assert_eq!(
            sh.map.get(&PipelineID(pipeline1_id)).unwrap().get("SLEEP"),
            None
        );
        assert_eq!(
            sh.map.get(&PipelineID(pipeline1_id)).unwrap().get("QUEUED"),
            Some(&1)
        );
        assert_eq!(
            sh.map
                .get(&PipelineID(pipeline1_id))
                .unwrap()
                .get("RUNNING"),
            None
        );
        assert_eq!(
            sh.map
                .get(&PipelineID(pipeline1_id))
                .unwrap()
                .get("EXECUTED"),
            Some(&1)
        );
        assert_eq!(
            sh.map
                .get(&PipelineID(pipeline1_id))
                .unwrap()
                .get("DELETED"),
            None
        );

        assert_eq!(
            sh.map
                .get(&PipelineID(pipeline2_id))
                .unwrap()
                .get("DELETED"),
            Some(&1)
        );

        /* illegal states */
        assert_eq!(
            sh.map.get(&PipelineID(pipeline1_id)).unwrap().get("SLEPT"),
            None
        );
        assert_eq!(
            sh.map
                .get(&PipelineID(pipeline2_id))
                .unwrap()
                .get("ABORTED"),
            None
        );
    }
}
