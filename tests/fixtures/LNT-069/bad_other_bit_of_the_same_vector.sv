// sverity-args: --top top
// The block tests r[1], not the control r[0]: no edge control is used, so it cannot be a flip-flop with an asynchronous control.
// expect: LNT-069 ERROR
module top(input logic clk, input logic [1:0] r, input logic d, output logic q);
  always_ff @(posedge clk or negedge r[0]) if (!r[1]) q <= 1'b0; else q <= d;
endmodule
