// sverity-args: --top top
// expect: LNT-057 WARNING 7
module top #(parameter W = 8) (output logic [3:0] a);
  typedef logic [W-1:0] wide_t;
  typedef wide_t alias_t;
  alias_t value;
  assign a = value;
endmodule
