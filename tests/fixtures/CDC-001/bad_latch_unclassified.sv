// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_latch if (en) q = d; => always_ff @(posedge clka) q <= d;
// revert-expect-none: CDC-001 WARNING
// A level-sensitive element has no clock: it must be reported, never skipped.
// expect: CDC-001 WARNING
module top(input logic clka, en, d, output logic q);
  always_latch if (en) q = d;
endmodule
