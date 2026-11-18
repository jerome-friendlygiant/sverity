// sverity-args: --top top
// A constant select through a nested aggregate that selects clean bits holds no X: the X in another element is not read.
// expect-none: LNT-039
typedef struct { logic [1:0] t [2]; logic [1:0] k; } s_t;
module top(output logic [1:0] y);
  localparam s_t P = '{t: '{2'b00, 2'bx0}, k: 2'b01};
  assign y = P.t[0];
endmodule
