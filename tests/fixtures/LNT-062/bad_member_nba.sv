// expect: LNT-062 WARNING 4
module top(input logic d, output logic q);
  struct packed {logic a;} value;
  always_comb value.a <= d;
  assign q = value.a;
endmodule
