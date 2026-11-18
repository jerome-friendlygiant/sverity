// sverity-args: --top top
// expect-none: LNT-057
module top (input logic [7:0] a, output logic [3:0] o);
  typedef logic [7:0] fixed_t;
  fixed_t value;
  assign value = a;
  assign o = value;
endmodule
