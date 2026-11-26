// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The reset is gated by d (known) and by an undriven net: the gate stays visible (Heuristic) and the incomplete fan-in is a second, Unresolved finding.
// expect-count: RDC-009 WARNING 2
// expect: RDC-009 WARNING
module top(input logic clka, rst_n, d, output logic q);
  wire helper;
  wire rr = rst_n & d & helper;
  always_ff @(posedge clka or negedge rr) if (!rr) q <= 1'b0; else q <= d;
endmodule
