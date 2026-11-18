// sverity-args: --top top
// A constant one above the byte is not a zero extension: the value is 256..511, outside the signed 9-bit range.
// expect: LNT-005 WARNING 6
module top(input logic signed [7:0] temp, input logic [7:0] limit, output logic hot);
  logic signed [8:0] limit_s;
  assign limit_s = {1'b1, limit};
  assign hot = temp > limit_s;
endmodule
