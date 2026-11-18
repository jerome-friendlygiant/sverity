// sverity-args: --top top
// {1'b1, a} forces the extension bit to one: positive a would be given a sign bit it does not have.
// expect: LNT-005 WARNING 5
module top(input logic signed [7:0] a, output logic signed [8:0] b);
  assign b = {1'b1, a};
endmodule
