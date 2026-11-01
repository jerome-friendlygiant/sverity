// sverity-args: --top top
// expect: LNT-005 WARNING
module top(input logic signed [3:0] s, input logic [3:0] u, output logic r);
  assign r = s < u;
endmodule
