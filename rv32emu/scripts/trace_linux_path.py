#!/usr/bin/env python3
"""Symbolize rv32emu trace logs and summarize Linux execution paths."""

from __future__ import annotations

import argparse
import bisect
import collections
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple

KV_RE = re.compile(r"([A-Za-z0-9_]+)=([^ \t\r\n]+)")

FOCUS_PROFILES: Dict[str, Tuple[str, ...]] = {
    "scheduler": (
        "__schedule",
        "schedule",
        "pick_next_task",
        "context_switch",
        "try_to_wake_up",
        "wake_up",
        "enqueue_task",
        "dequeue_task",
        "scheduler_tick",
        "load_balance",
        "check_preempt_curr",
        "ttwu",
        "resched",
    ),
    "network": (
        "net_",
        "sock_",
        "tcp_",
        "udp_",
        "ip_",
        "inet_",
        "napi",
        "skb",
        "xmit",
        "dev_queue_xmit",
        "netif_receive",
        "__netif_receive",
        "arp_",
        "neigh_",
        "eth_",
        "rtnl_",
    ),
}


def parse_int(value: Optional[str]) -> Optional[int]:
    if value is None:
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def parse_trace_line(line: str) -> Optional[Dict[str, str]]:
    line = line.strip()
    if not line or line.startswith("#"):
        return None

    parts = line.split(" ", 1)
    if not parts or not parts[0].isdigit():
        return None

    fields: Dict[str, str] = {"seq": parts[0]}
    if len(parts) == 1:
        return fields

    for match in KV_RE.finditer(parts[1]):
        fields[match.group(1)] = match.group(2)
    return fields


def resolve_existing_path(raw_path: Path) -> Tuple[Optional[Path], List[Path]]:
    """Resolve a possibly-relative input path against common workspace roots."""
    expanded = raw_path.expanduser()
    candidates: List[Path] = []
    seen = set()

    def add_candidate(path: Path) -> None:
        key = str(path)
        if key in seen:
            return
        seen.add(key)
        candidates.append(path)

    add_candidate(expanded)
    if not expanded.is_absolute():
        script_dir = Path(__file__).resolve().parent
        project_root = script_dir.parent
        workspace_root = project_root.parent
        add_candidate(project_root / expanded)
        add_candidate(workspace_root / expanded)

    for cand in candidates:
        if cand.exists():
            return cand, candidates
    return None, candidates


def symbol_base(symbol: str) -> str:
    if "+" not in symbol:
        return symbol
    return symbol.split("+", 1)[0]


def build_focus_filters(profile: Optional[str], extras: Optional[List[str]]) -> List[str]:
    filters: List[str] = []
    seen = set()

    def add_filter(raw: str) -> None:
        key = raw.strip().lower()
        if not key or key in seen:
            return
        seen.add(key)
        filters.append(key)

    if profile is not None:
        for item in FOCUS_PROFILES.get(profile, ()):
            add_filter(item)
    for item in extras or []:
        add_filter(item)
    return filters


def symbol_matches(symbol: str, filters: List[str]) -> bool:
    if not filters:
        return False
    base = symbol_base(symbol).lower()
    full = symbol.lower()
    for token in filters:
        if token in base or token in full:
            return True
    return False


@dataclass
class SymbolMap:
    addrs: List[int]
    names: List[str]

    @classmethod
    def load(cls, path: Path) -> "SymbolMap":
        addrs: List[int] = []
        names: List[str] = []

        with path.open("r", encoding="utf-8", errors="replace") as fp:
            for raw in fp:
                line = raw.strip()
                if not line:
                    continue
                parts = line.split()
                if len(parts) < 3:
                    continue
                try:
                    addr = int(parts[0], 16)
                except ValueError:
                    continue
                addrs.append(addr)
                names.append(parts[2])

        if not addrs:
            raise ValueError(f"no symbols loaded from {path}")

        sorted_pairs = sorted(zip(addrs, names), key=lambda item: item[0])
        return cls([a for a, _ in sorted_pairs], [n for _, n in sorted_pairs])

    def resolve(self, addr: Optional[int]) -> str:
        if addr is None:
            return "?"
        idx = bisect.bisect_right(self.addrs, addr) - 1
        if idx < 0:
            return f"0x{addr:08x}"
        base = self.addrs[idx]
        name = self.names[idx]
        off = addr - base
        if off == 0:
            return name
        return f"{name}+0x{off:x}"


@dataclass
class TraceSummary:
    total_events: int
    event_counts: collections.Counter
    trap_counts: collections.Counter
    syscall_counts: collections.Counter
    mmio_counts: collections.Counter
    pc_symbols: collections.Counter
    transitions: collections.Counter
    timeline: List[str]
    phases: List["PhaseSummary"]
    focus: Optional["FocusSummary"]


@dataclass
class TraceEventPoint:
    seq: int
    event: str
    cause: str
    pc_symbol: str
    a7: str
    a6: str
    delegated: str


@dataclass
class PhaseSummary:
    index: int
    start_seq: int
    end_seq: int
    total_events: int
    label: str
    dominant_event: str
    top_pc: str
    top_trap: str
    top_syscall: str


@dataclass
class FocusSummary:
    profile: Optional[str]
    filters: List[str]
    total_events: int
    event_counts: collections.Counter
    pc_symbols: collections.Counter
    transitions: collections.Counter
    timeline: List[str]


def iter_trace(path: Path) -> Iterable[Dict[str, str]]:
    with path.open("r", encoding="utf-8", errors="replace") as fp:
        for raw in fp:
            fields = parse_trace_line(raw)
            if fields is not None:
                yield fields


def classify_phase(
    total: int,
    event_counts: collections.Counter,
    trap_counts: collections.Counter,
    syscall_counts: collections.Counter,
) -> str:
    if total <= 0:
        return "empty"

    trap_total = event_counts.get("exception", 0) + event_counts.get("interrupt", 0)
    syscall_total = event_counts.get("syscall", 0)
    mmio_total = sum(count for event, count in event_counts.items() if event.startswith("mmio_"))

    trap_rate = trap_total / total
    syscall_rate = syscall_total / total
    mmio_rate = mmio_total / total

    if trap_rate >= 0.65:
        if trap_counts:
            (event_name, cause, delegated), _ = trap_counts.most_common(1)[0]
            return f"trap_storm({event_name}:cause={cause},delegated={delegated})"
        return "trap_heavy"
    if syscall_rate >= 0.30:
        if syscall_counts:
            (a7, a6), _ = syscall_counts.most_common(1)[0]
            return f"syscall_hotpath(a7={a7},a6={a6})"
        return "syscall_heavy"
    if mmio_rate >= 0.20:
        return "mmio_probe"
    return "mixed_boot"


def summarize_phases(points: List[TraceEventPoint], phase_window: int) -> List[PhaseSummary]:
    phases: List[PhaseSummary] = []

    if phase_window <= 0 or not points:
        return phases

    for idx in range(0, len(points), phase_window):
        chunk = points[idx : idx + phase_window]
        event_counts: collections.Counter = collections.Counter()
        trap_counts: collections.Counter = collections.Counter()
        syscall_counts: collections.Counter = collections.Counter()
        pc_counts: collections.Counter = collections.Counter()

        for point in chunk:
            event_counts[point.event] += 1
            pc_counts[point.pc_symbol] += 1
            if point.event in ("exception", "interrupt"):
                trap_counts[(point.event, point.cause, point.delegated)] += 1
            if point.event == "syscall":
                syscall_counts[(point.a7, point.a6)] += 1

        dominant_event = event_counts.most_common(1)[0][0] if event_counts else "unknown"
        top_pc = pc_counts.most_common(1)[0][0] if pc_counts else "-"
        if trap_counts:
            (event_name, cause, delegated), _ = trap_counts.most_common(1)[0]
            top_trap = f"{event_name}:cause={cause},delegated={delegated}"
        else:
            top_trap = "-"
        if syscall_counts:
            (a7, a6), _ = syscall_counts.most_common(1)[0]
            top_syscall = f"a7={a7},a6={a6}"
        else:
            top_syscall = "-"

        phases.append(
            PhaseSummary(
                index=(idx // phase_window) + 1,
                start_seq=chunk[0].seq,
                end_seq=chunk[-1].seq,
                total_events=len(chunk),
                label=classify_phase(len(chunk), event_counts, trap_counts, syscall_counts),
                dominant_event=dominant_event,
                top_pc=top_pc,
                top_trap=top_trap,
                top_syscall=top_syscall,
            )
        )

    return phases


def summarize_trace(
    trace_path: Path,
    symbol_map: Optional[SymbolMap],
    symbol_bias: int,
    priv_filter: Set[str],
    hart_filter: Optional[int],
    timeline_limit: int,
    phase_window: int,
    focus_profile: Optional[str],
    focus_filters: List[str],
    focus_timeline_limit: int,
) -> TraceSummary:
    event_counts: collections.Counter = collections.Counter()
    trap_counts: collections.Counter = collections.Counter()
    syscall_counts: collections.Counter = collections.Counter()
    mmio_counts: collections.Counter = collections.Counter()
    pc_symbols: collections.Counter = collections.Counter()
    transitions: collections.Counter = collections.Counter()
    timeline: List[str] = []
    points: List[TraceEventPoint] = []
    focus_event_counts: collections.Counter = collections.Counter()
    focus_pc_symbols: collections.Counter = collections.Counter()
    focus_transitions: collections.Counter = collections.Counter()
    focus_timeline: List[str] = []
    focus_total = 0
    total = 0

    def sym(addr: Optional[int]) -> str:
        if symbol_map is None:
            if addr is None:
                return "?"
            return f"0x{addr:08x}"
        if addr is None:
            return "?"
        if symbol_bias != 0:
            addr = (addr + symbol_bias) & 0xFFFFFFFF
        return symbol_map.resolve(addr)

    for fields in iter_trace(trace_path):
        if priv_filter:
            priv = fields.get("priv", "?")
            if priv not in priv_filter:
                continue
        if hart_filter is not None:
            hart = parse_int(fields.get("hart"))
            if hart != hart_filter:
                continue

        total += 1
        event = fields.get("event", "unknown")
        event_counts[event] += 1

        seq = parse_int(fields.get("seq"))
        if seq is None:
            seq = total - 1
        pc = parse_int(fields.get("pc"))
        pc_symbol = sym(pc)
        focus_event = False
        if pc is not None:
            pc_symbols[pc_symbol] += 1
            if symbol_matches(pc_symbol, focus_filters):
                focus_event = True
                focus_pc_symbols[pc_symbol] += 1
        if phase_window > 0:
            points.append(
                TraceEventPoint(
                    seq=seq,
                    event=event,
                    cause=fields.get("cause", "?"),
                    pc_symbol=pc_symbol,
                    a7=fields.get("a7", "?"),
                    a6=fields.get("a6", "?"),
                    delegated=fields.get("delegated", "?"),
                )
            )

        if event in ("exception", "interrupt"):
            cause = fields.get("cause", "?")
            delegated = fields.get("delegated", "?")
            trap_counts[(event, cause, delegated)] += 1

            from_pc = parse_int(fields.get("from_pc"))
            target_pc = parse_int(fields.get("target_pc"))
            from_sym = sym(from_pc)
            target_sym = sym(target_pc)
            transitions[(from_sym, target_sym, event, cause)] += 1
            if symbol_matches(from_sym, focus_filters) or symbol_matches(target_sym, focus_filters):
                focus_event = True
                focus_transitions[(from_sym, target_sym, event, cause)] += 1

            if len(timeline) < timeline_limit:
                timeline.append(
                    f"{fields.get('seq', '?')}: {event} cause={cause} "
                    f"{from_sym} -> {target_sym} "
                    f"priv {fields.get('from_priv', '?')}->{fields.get('to_priv', '?')}"
                )
            if focus_event and len(focus_timeline) < focus_timeline_limit:
                focus_timeline.append(
                    f"{fields.get('seq', '?')}: {event} cause={cause} "
                    f"{from_sym} -> {target_sym} @pc={pc_symbol}"
                )
            if focus_event:
                focus_total += 1
                focus_event_counts[event] += 1
            continue

        if event == "syscall":
            key = (
                fields.get("priv", "?"),
                fields.get("cause", "?"),
                fields.get("a7", "?"),
                fields.get("a6", "?"),
                fields.get("handled", "?"),
            )
            syscall_counts[key] += 1
            if len(timeline) < timeline_limit:
                timeline.append(
                    f"{fields.get('seq', '?')}: syscall cause={fields.get('cause', '?')} "
                    f"a7={fields.get('a7', '?')} a6={fields.get('a6', '?')} "
                    f"handled={fields.get('handled', '?')} @ {sym(pc)}"
                )
            if focus_event and len(focus_timeline) < focus_timeline_limit:
                focus_timeline.append(
                    f"{fields.get('seq', '?')}: syscall a7={fields.get('a7', '?')} "
                    f"a6={fields.get('a6', '?')} handled={fields.get('handled', '?')} @ {pc_symbol}"
                )
            if focus_event:
                focus_total += 1
                focus_event_counts[event] += 1
            continue

        if event.startswith("mmio_"):
            key = (event, fields.get("addr", "?"), fields.get("len", "?"), fields.get("ok", "?"))
            mmio_counts[key] += 1
            if len(timeline) < timeline_limit:
                timeline.append(
                    f"{fields.get('seq', '?')}: {event} addr={fields.get('addr', '?')} "
                    f"len={fields.get('len', '?')} ok={fields.get('ok', '?')}"
                )
            if focus_event and len(focus_timeline) < focus_timeline_limit:
                focus_timeline.append(
                    f"{fields.get('seq', '?')}: {event} addr={fields.get('addr', '?')} "
                    f"len={fields.get('len', '?')} ok={fields.get('ok', '?')} @ {pc_symbol}"
                )
            if focus_event:
                focus_total += 1
                focus_event_counts[event] += 1
            continue

        if focus_event:
            focus_total += 1
            focus_event_counts[event] += 1
            if len(focus_timeline) < focus_timeline_limit:
                focus_timeline.append(
                    f"{fields.get('seq', '?')}: {event} @ {pc_symbol}"
                )

    focus: Optional[FocusSummary] = None
    if focus_filters:
        focus = FocusSummary(
            profile=focus_profile,
            filters=focus_filters,
            total_events=focus_total,
            event_counts=focus_event_counts,
            pc_symbols=focus_pc_symbols,
            transitions=focus_transitions,
            timeline=focus_timeline,
        )

    return TraceSummary(
        total_events=total,
        event_counts=event_counts,
        trap_counts=trap_counts,
        syscall_counts=syscall_counts,
        mmio_counts=mmio_counts,
        pc_symbols=pc_symbols,
        transitions=transitions,
        timeline=timeline,
        phases=summarize_phases(points, phase_window),
        focus=focus,
    )


def print_counter(
    title: str,
    counter: collections.Counter,
    formatter,
    limit: int,
) -> None:
    print(f"\n{title}")
    if not counter:
        print("  (none)")
        return
    for idx, (key, count) in enumerate(counter.most_common(limit), start=1):
        print(f"  {idx:>2}. {formatter(key)} -> {count}")


def to_jsonable(summary: TraceSummary, limit: int, symbol_bias: int) -> Dict[str, object]:
    payload: Dict[str, object] = {
        "total_events": summary.total_events,
        "symbol_bias": symbol_bias & 0xFFFFFFFF,
        "event_counts": dict(summary.event_counts.most_common(limit)),
        "trap_counts": [
            {
                "event": key[0],
                "cause": key[1],
                "delegated": key[2],
                "count": count,
            }
            for key, count in summary.trap_counts.most_common(limit)
        ],
        "syscall_counts": [
            {
                "priv": key[0],
                "cause": key[1],
                "a7": key[2],
                "a6": key[3],
                "handled": key[4],
                "count": count,
            }
            for key, count in summary.syscall_counts.most_common(limit)
        ],
        "mmio_counts": [
            {
                "event": key[0],
                "addr": key[1],
                "len": key[2],
                "ok": key[3],
                "count": count,
            }
            for key, count in summary.mmio_counts.most_common(limit)
        ],
        "pc_hotspots": [
            {"symbol": symbol, "count": count}
            for symbol, count in summary.pc_symbols.most_common(limit)
        ],
        "transitions": [
            {
                "from": key[0],
                "to": key[1],
                "event": key[2],
                "cause": key[3],
                "count": count,
            }
            for key, count in summary.transitions.most_common(limit)
        ],
        "timeline": summary.timeline,
        "phases": [
            {
                "index": phase.index,
                "start_seq": phase.start_seq,
                "end_seq": phase.end_seq,
                "total_events": phase.total_events,
                "label": phase.label,
                "dominant_event": phase.dominant_event,
                "top_pc": phase.top_pc,
                "top_trap": phase.top_trap,
                "top_syscall": phase.top_syscall,
            }
            for phase in summary.phases
        ],
    }
    if summary.focus is not None:
        payload["focus"] = {
            "profile": summary.focus.profile,
            "filters": summary.focus.filters,
            "total_events": summary.focus.total_events,
            "event_counts": dict(summary.focus.event_counts.most_common(limit)),
            "pc_hotspots": [
                {"symbol": symbol, "count": count}
                for symbol, count in summary.focus.pc_symbols.most_common(limit)
            ],
            "transitions": [
                {
                    "from": key[0],
                    "to": key[1],
                    "event": key[2],
                    "cause": key[3],
                    "count": count,
                }
                for key, count in summary.focus.transitions.most_common(limit)
            ],
            "timeline": summary.focus.timeline,
        }
    return payload


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Summarize rv32emu Linux trace logs with optional symbolization."
    )
    parser.add_argument("--trace", required=True, type=Path, help="trace log path")
    parser.add_argument("--system-map", type=Path, help="Linux System.map path")
    parser.add_argument(
        "--priv",
        action="append",
        choices=["M", "S", "U"],
        help="only include one privilege level (can repeat)",
    )
    parser.add_argument("--hart", type=int, help="only include one hart")
    parser.add_argument("--top", type=int, default=20, help="top-N rows for each section")
    parser.add_argument(
        "--timeline",
        type=int,
        default=40,
        help="number of first timeline events to print",
    )
    parser.add_argument(
        "--phase-window",
        type=int,
        default=0,
        help="events per phase chunk (0 disables phase report)",
    )
    parser.add_argument(
        "--phase-max",
        type=int,
        default=20,
        help="max number of phase rows to print",
    )
    parser.add_argument("--json-out", type=Path, help="optional JSON summary output file")
    parser.add_argument(
        "--focus-profile",
        choices=sorted(FOCUS_PROFILES.keys()),
        help="focus by subsystem profile (scheduler/network)",
    )
    parser.add_argument(
        "--focus-symbol",
        action="append",
        help="additional symbol substring filter (can repeat)",
    )
    parser.add_argument(
        "--focus-timeline",
        type=int,
        default=40,
        help="focused timeline row limit",
    )
    parser.add_argument(
        "--symbol-bias",
        type=lambda s: int(s, 0),
        default=0,
        help="bias added to trace addresses before System.map lookup (e.g. 0x40000000)",
    )
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    symbol_map: Optional[SymbolMap] = None
    trace_path, trace_candidates = resolve_existing_path(args.trace)
    system_map_path: Optional[Path] = None
    focus_filters = build_focus_filters(args.focus_profile, args.focus_symbol)
    priv_filter: Set[str] = set(args.priv or [])

    if trace_path is None:
        print(f"[ERR] trace file not found: {args.trace}", file=sys.stderr)
        print("[ERR] searched:", file=sys.stderr)
        for cand in trace_candidates:
            print(f"  - {cand}", file=sys.stderr)
        print("[ERR] hint: run rv32emu with --trace --trace-file <path> first.", file=sys.stderr)
        return 2
    if args.system_map is not None:
        system_map_path, map_candidates = resolve_existing_path(args.system_map)
        if system_map_path is None:
            print(f"[ERR] System.map not found: {args.system_map}", file=sys.stderr)
            print("[ERR] searched:", file=sys.stderr)
            for cand in map_candidates:
                print(f"  - {cand}", file=sys.stderr)
            return 2
        try:
            symbol_map = SymbolMap.load(system_map_path)
        except ValueError as err:
            print(f"[ERR] {err}", file=sys.stderr)
            return 2

    summary = summarize_trace(
        trace_path,
        symbol_map,
        args.symbol_bias,
        priv_filter,
        args.hart,
        args.timeline,
        args.phase_window,
        args.focus_profile,
        focus_filters,
        args.focus_timeline,
    )

    print("Trace Summary")
    print(f"  trace: {trace_path}")
    print(f"  total events: {summary.total_events}")
    if args.hart is not None:
        print(f"  hart filter: {args.hart}")
    if priv_filter:
        print(f"  priv filter: {','.join(sorted(priv_filter))}")
    if symbol_map is None:
        print("  symbols: raw addresses")
    else:
        print(f"  symbols: {system_map_path}")
        if args.symbol_bias != 0:
            print(f"  symbol bias: 0x{args.symbol_bias & 0xFFFFFFFF:08x}")

    print_counter("Event counts", summary.event_counts, lambda key: str(key), args.top)
    print_counter(
        "Trap/Interrupt causes",
        summary.trap_counts,
        lambda key: f"{key[0]} cause={key[1]} delegated={key[2]}",
        args.top,
    )
    print_counter(
        "Syscalls",
        summary.syscall_counts,
        lambda key: f"priv={key[0]} cause={key[1]} a7={key[2]} a6={key[3]} handled={key[4]}",
        args.top,
    )
    print_counter(
        "MMIO hotspots",
        summary.mmio_counts,
        lambda key: f"{key[0]} addr={key[1]} len={key[2]} ok={key[3]}",
        args.top,
    )
    print_counter("PC hotspots", summary.pc_symbols, lambda key: str(key), args.top)
    print_counter(
        "Path transitions",
        summary.transitions,
        lambda key: f"{key[0]} -> {key[1]} ({key[2]} cause={key[3]})",
        args.top,
    )

    print("\nTimeline")
    if not summary.timeline:
        print("  (none)")
    else:
        for item in summary.timeline:
            print(f"  {item}")

    print("\nPhases")
    if not summary.phases:
        print("  (disabled; use --phase-window > 0)")
    else:
        for phase in summary.phases[: args.phase_max]:
            print(
                f"  {phase.index:>2}. seq[{phase.start_seq}-{phase.end_seq}] "
                f"events={phase.total_events} label={phase.label} dominant={phase.dominant_event} "
                f"top_pc={phase.top_pc} top_trap={phase.top_trap} top_syscall={phase.top_syscall}"
            )

    print("\nFocus")
    if summary.focus is None:
        print("  (disabled; use --focus-profile/--focus-symbol)")
    else:
        profile_name = summary.focus.profile or "custom"
        print(f"  profile: {profile_name}")
        print(f"  filters: {', '.join(summary.focus.filters)}")
        print(f"  matched events: {summary.focus.total_events}")
        print_counter("Focus event counts", summary.focus.event_counts, lambda key: str(key), args.top)
        print_counter("Focus PC hotspots", summary.focus.pc_symbols, lambda key: str(key), args.top)
        print_counter(
            "Focus transitions",
            summary.focus.transitions,
            lambda key: f"{key[0]} -> {key[1]} ({key[2]} cause={key[3]})",
            args.top,
        )
        print("\nFocus timeline")
        if not summary.focus.timeline:
            print("  (none)")
        else:
            for item in summary.focus.timeline:
                print(f"  {item}")

    if args.json_out is not None:
        payload = to_jsonable(summary, args.top, args.symbol_bias)
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        with args.json_out.open("w", encoding="utf-8") as fp:
            json.dump(payload, fp, indent=2, ensure_ascii=True)
            fp.write("\n")
        print(f"\n[INFO] JSON written: {args.json_out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
