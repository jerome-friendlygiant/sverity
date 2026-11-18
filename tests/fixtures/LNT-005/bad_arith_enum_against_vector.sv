// sverity-args: --top top
// An enum and a plain vector of the same width: a type mismatch, not a width one.
// expect: LNT-005 WARNING 6
typedef enum logic [3:0] {A = 4'd1, B = 4'd2} e_t;
module top(input e_t e, input logic [3:0] v, output logic [3:0] y);
  assign y = e + v;
endmodule
