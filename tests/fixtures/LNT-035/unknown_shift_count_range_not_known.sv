// sverity-args: --top top
// The count is a 128-bit value, wider than the range model reads: whether the shift can push bits out of the byte is not decided.
// expect-unresolved: LNT-035 5
module top(input logic [7:0] a, input logic [127:0] n, output logic [31:0] y);
  assign y = {24'd0, a << n};
endmodule
