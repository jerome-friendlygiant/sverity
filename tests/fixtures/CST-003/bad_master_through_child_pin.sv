// sverity-args: --top top --sdc @DIR@/through_pin.sdc
// The generated clock's -source is the child pin that the master's port drives: the same signal, so the group separating
// them contradicts the derivation.
// expect: CST-003 WARNING
module div(input logic clk, output logic q);
  always_ff @(posedge clk) q <= ~q;
endmodule
module top(input logic a, output logic q);
  div u(.clk(a), .q(q));
endmodule
