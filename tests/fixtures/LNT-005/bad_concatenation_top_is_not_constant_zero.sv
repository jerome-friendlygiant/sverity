// sverity-args: --top top
// Lookalike of the zero extension: the bit above the byte is a signal, not a constant zero, so the value is 0..511 and a signed
// 9-bit value reads the upper half as negative.
// expect: LNT-005 WARNING 9
// revert: {x, limit} => {1'b0, limit}
// revert-expect-none: LNT-005 WARNING
module top(input logic signed [7:0] temp, input logic [7:0] limit, input logic x, output logic hot);
  logic signed [8:0] limit_s;
  assign limit_s = {x, limit};
  assign hot = temp > limit_s;
endmodule
