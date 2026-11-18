// sverity-args: --top top
// A non-constant index into a nested aggregate may reach the element that holds X: the message says the index may reach it.
// expect: LNT-039 WARNING 7
typedef struct { logic [1:0] t [2]; logic [1:0] k; } s_t;
module top(input logic i, output logic [1:0] y);
  localparam s_t P = '{t: '{2'b00, 2'bx0}, k: 2'b01};
  assign y = P.t[i];
endmodule
