// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-026 ERROR
// expect: CDC-040 WARNING
// expect-none: CDC-040 INFO
(* sverity_sync *) module sync2(input logic clkb, d, output logic q);
  logic stage1;
  always_ff @(posedge clkb) stage1 <= d;
  always_ff @(negedge clkb) q <= stage1;
endmodule
module top(input logic clka, clkb, d, output logic q);
  logic launch;
  always_ff @(posedge clka) launch <= d;
  sync2 u_sync(.clkb(clkb), .d(launch), .q(q));
endmodule
