// sverity-args: --top top
// A whole packed struct parameter is an assigned value even when its X is in a member the design never reads.
// expect: LNT-039 WARNING 7
module top(output logic [3:0] y);
  typedef struct packed { logic [1:0] f; logic [1:0] g; } s_t;
  localparam s_t P = '{f: 2'b01, g: 2'bx0};
  assign y = P;
endmodule
