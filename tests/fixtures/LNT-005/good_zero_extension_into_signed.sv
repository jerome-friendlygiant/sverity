// sverity-args: --top top
// `{1'b0, limit}` is the unsigned byte with a constant zero above it: 0..255, which the signed 9-bit value holds unchanged.
// expect-none: LNT-005 WARNING
module top(input logic signed [7:0] temp, input logic [7:0] limit, output logic hot);
  logic signed [8:0] limit_s;
  assign limit_s = {1'b0, limit};
  assign hot = temp > limit_s;
endmodule
