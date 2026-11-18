// sverity-args: --top top
// Lookalikes of the zero-extended compare that a value range does not decide: a free top bit, a bound inside the range,
// a signed operand, and a mask wider than the constant.
// expect-none: LNT-038
module top(input logic [2:0] sel, input logic free, input logic signed [3:0] s, input logic [7:0] data, output logic [4:0] y);
  assign y[0] = ({free, sel} < 4'd8);
  assign y[1] = ({1'b0, sel} < 4'd7);
  assign y[2] = ({1'b0, s[2:0]} < 4'd5);
  assign y[3] = ((data & 8'h0f) > 8'd14);
  assign y[4] = ($signed({1'b0, sel}) > -4'sd1) ? 1'b1 : data[0];
endmodule
