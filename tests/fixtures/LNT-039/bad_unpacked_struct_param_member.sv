// sverity-args: --top top
// An unpacked struct parameter whose selected member has X bits is an assigned value.
// expect: LNT-039 WARNING 7
module top(output logic [1:0] y);
  typedef struct { logic [1:0] f; } s_t;
  localparam s_t P = '{f: 2'bx1};
  assign y = P.f;
endmodule
