// sverity-args: --top top
// Every path reads `a` before any write and the write is computed from that read: a certain loop, not a statement-order question.
// expect: LNT-012 ERROR
module top(input logic x, output logic y);
  logic a;
  always_comb a = a ^ x;
  assign y = a;
endmodule
