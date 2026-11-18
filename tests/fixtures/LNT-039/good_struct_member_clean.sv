// sverity-args: --top top
// A member select of a struct parameter is judged by the member it selects: the X in member g is not read.
// expect-none: LNT-039
module top(output logic [1:0] y);
  typedef struct packed { logic [1:0] f; logic [1:0] g; } s_t;
  localparam s_t P = '{f: 2'b01, g: 2'bx0};
  assign y = P.f;
endmodule
