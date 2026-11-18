// sverity-args: --top top
// Every piece of the replication is a value of the array's enum, laid end to end: each element is an enumerator, nothing is reinterpreted.
// expect: LNT-005 INFO 10
// expect-none: LNT-005 WARNING
module top(input logic [1:0] r, output logic y);
  typedef enum logic [1:0] {A, B, C, D} state_e;
  state_e [3:0] full;
  state_e cur;
  assign cur = state_e'(r);
  assign full = {4{cur}};
  assign y = (full[0] == A);
endmodule
