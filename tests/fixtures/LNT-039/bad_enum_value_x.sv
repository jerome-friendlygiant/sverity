// sverity-args: --top top
// An enum value whose bits are X is a named constant: assigning it writes X into the value.
// expect: LNT-039 WARNING 6
module top(output logic [1:0] y);
  typedef enum logic [1:0] {A = 2'b00, DC = 2'bxx} e_t;
  assign y = DC;
endmodule
