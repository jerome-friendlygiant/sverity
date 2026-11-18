// sverity-args: --top top
// The rounding average (a + b + 1) >> 1 shifts a sum that has already lost its carry.
// expect: LNT-036 WARNING 5
module top(input logic [7:0] a, b, output logic [7:0] avg);
  assign avg = (a + b + 8'd1) >> 1;
endmodule
