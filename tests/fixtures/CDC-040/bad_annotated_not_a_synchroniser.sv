// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: (* sverity_sync *) module fake_sync => module fake_sync
// revert-expect-none: CDC-040 WARNING
// expect: CDC-040 WARNING
(* sverity_sync *) module fake_sync(input logic clk, d, output logic q);
  assign q = d;
endmodule
module top(input logic clka, clkb, d, output logic q);
  logic o;
  fake_sync u(.clk(clkb), .d(d), .q(o));
  assign q = o;
endmodule
