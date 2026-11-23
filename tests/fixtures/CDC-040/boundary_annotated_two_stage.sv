// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The annotation wraps a recognised two-stage chain, the minimum accepted synchroniser depth.
// expect: CDC-040 INFO
// expect-confidence: CDC-040 heuristic
// expect-none: CDC-040 WARNING
(* sverity_sync *) module sync2(input logic clkb, d, output logic q);
  logic stage1;
  always_ff @(posedge clkb) begin
    stage1 <= d;
    q <= stage1;
  end
endmodule
module top(input logic clka, clkb, d, output logic q);
  logic launch;
  always_ff @(posedge clka) launch <= d;
  sync2 u_sync(.clkb(clkb), .d(launch), .q(q));
endmodule
