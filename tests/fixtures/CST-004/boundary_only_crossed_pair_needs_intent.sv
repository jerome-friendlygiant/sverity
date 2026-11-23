// sverity-args: --top top --sdc @DIR@/plain.sdc
// expect: CST-004 WARNING
// expect-unresolved: CST-004
// expect-coverage: clock-relationships INCOMPLETE
// revert: plain.sdc => declared_pair.sdc
// revert-expect-none: CST-004
module top(input logic clka, clkb, d, output logic q, same_clock_q);
  logic launch, meta;
  always_ff @(posedge clka) begin launch <= d; same_clock_q <= launch; end
  always_ff @(posedge clkb) begin meta <= launch; q <= meta; end
endmodule
